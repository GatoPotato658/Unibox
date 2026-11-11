#include "Radio.h"

#include <algorithm>
#include <chrono>

static constexpr int RadioVolumeStep = 5;
static constexpr uint64_t RadioRetryDelay = 3000;
static constexpr int RadioBufferReady = 75;
static constexpr int RadioVoiceMaxPlayers = 64;

static const char* RadioStationUrl(size_t uStation)
{
	switch (uStation)
	{
	case RadioStationEnum::DnB: return "https://sc2.dubplate.fm/radio/8030/dnb/uhifi";
	case RadioStationEnum::Jungle: return "https://energy1058.radioca.st/stream";
	case RadioStationEnum::LiquidDnB: return "https://radiorecord.hostingradio.ru/liquidfunk96.aacp";
	case RadioStationEnum::Hardcore: return "https://stream.hardcoreradio.nl:9000/hcr.ogg";
	case RadioStationEnum::Techno: return "https://schranz.in/schranz";
	case RadioStationEnum::ClassicRap: return "https://radiorecord.hostingradio.ru/rapclassics96.aacp";
	case RadioStationEnum::Vaporwave: return "https://radio.plaza.one/ogg";
	case RadioStationEnum::NonStopFm: return "https://stream.zeno.fm/hace1hc4vwzuv";
	case RadioStationEnum::Tekk: return "https://stream.zeno.fm/oj65ts503spvv";
	case RadioStationEnum::Underground: return "https://54house.fm:9013/stream";
	case RadioStationEnum::TheBeat: return "https://audio.gtaradio.net/4/beat_95";
	default: return nullptr;
	}
}

const char* RadioStationName(size_t uStation)
{
	switch (uStation)
	{
	case RadioStationEnum::DnB: return "Drum and bass";
	case RadioStationEnum::Jungle: return "Jungle";
	case RadioStationEnum::LiquidDnB: return "Liquid DnB";
	case RadioStationEnum::Hardcore: return "Hardcore";
	case RadioStationEnum::Techno: return "Techno";
	case RadioStationEnum::ClassicRap: return "Classic rap";
	case RadioStationEnum::Vaporwave: return "Vaporwave";
	case RadioStationEnum::NonStopFm: return "NonStop FM";
	case RadioStationEnum::Tekk: return "Tekk";
	case RadioStationEnum::Underground: return "Underground";
	case RadioStationEnum::TheBeat: return "The Beat 102.7";
	default: return "No station tuned";
	}
}

const char* const* RadioStationNames()
{
	static const char* aNames[RadioStationEnum::Count] = {};
	if (!aNames[0])
	{
		for (size_t i = 0; i < RadioStationEnum::Count; i++)
			aNames[i] = RadioStationName(i);
	}
	return aNames;
}

bool CRadio::Startup()
{
	if (m_bStarted)
		return true;

	if (!U::Audio.Startup())
		return false;

	m_pGameVolume = H::ConVars.FindVar("volume");
	m_bStop.store(false);
	m_bStarted = true;
	m_tThread = std::thread(&CRadio::Run, this);
	return true;
}

void CRadio::Shutdown()
{
	if (!m_bStarted)
		return;

	m_bStop.store(true);
	if (m_tThread.joinable())
		m_tThread.join();

	if (m_tConnectThread.joinable())
		m_tConnectThread.join();

	m_bStarted = false;
	U::Audio.Shutdown();
}

void CRadio::SetPhase(int iPhase)
{
	if (m_iPhase.load() != iPhase)
		m_iPhase.store(iPhase);
}

void CRadio::Run()
{
	while (!m_bStop.load())
	{
		if (!Vars::Misc::Radio::Enabled.Value)
			Silence();
		else if (m_bPaused.load())
		{
			if (!m_bOutputPaused)
			{
				PauseOutput(true);
				m_bOutputPaused = true;
			}
			SetPhase(RadioPhaseEnum::Paused);
		}
		else
		{
			if (m_bOutputPaused)
			{
				PauseOutput(false);
				m_bOutputPaused = false;
			}

			const size_t uWanted = std::clamp<size_t>(Vars::Misc::Radio::Station.Value, 0, RadioStationEnum::Count - 1);
			if (uWanted != m_uAttempted)
			{
				m_uAttempted = uWanted;
				m_uRetryAt = 0;
			}

			PollConnect();
			const bool bSuperseded = m_bConnectPending && m_uConnectStation != uWanted;
			if (m_uStation != uWanted && (!m_bConnectPending || bSuperseded) && GetTickCount64() >= m_uRetryAt)
				StartConnect(uWanted);

			ApplyVolume();
			Pump();
		}

		RetireFade(false);
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	Silence();
}

void CRadio::StartConnect(size_t uStation)
{
	if (m_tConnectThread.joinable())
		m_tConnectThread.join();

	int iGeneration = 0;
	{
		std::lock_guard tLock(m_tConnectMutex);
		iGeneration = ++m_iConnectGeneration;
		m_iConnectReady = 0;
		if (m_pConnectResult)
		{
			m_pConnectResult->Release();
			m_pConnectResult = nullptr;
		}
	}

	m_bConnectPending = true;
	m_uConnectStation = uStation;
	SetPhase(RadioPhaseEnum::Connecting);
	m_iBuffer.store(0);

	try
	{
		m_tConnectThread = std::thread(&CRadio::Connect, this, iGeneration, uStation);
	}
	catch (...)
	{
		m_bConnectPending = false;
		m_uRetryAt = GetTickCount64() + RadioRetryDelay;
		SetPhase(RadioPhaseEnum::Failed);
	}
}

void CRadio::Connect(int iGeneration, size_t uStation)
{
	const char* sUrl = RadioStationUrl(uStation);

	AudioStreamError_t iError = AudioStreamError_t::Url;
	CAudioStream* pStream = sUrl ? CAudioStream::Open(sUrl, iError) : nullptr;

	std::lock_guard tLock(m_tConnectMutex);
	if (m_iConnectGeneration != iGeneration)
	{
		if (pStream)
			pStream->Release();
		return;
	}

	m_pConnectResult = pStream;
	m_iConnectReady = iGeneration;
}

bool CRadio::PollConnect()
{
	CAudioStream* pStream = nullptr;

	{
		std::lock_guard tLock(m_tConnectMutex);
		if (!m_iConnectReady || m_iConnectReady != m_iConnectGeneration)
			return false;

		pStream = m_pConnectResult;
		m_iConnectReady = 0;
		m_pConnectResult = nullptr;
	}

	m_bConnectPending = false;
	if (!pStream)
	{
		m_uRetryAt = GetTickCount64() + RadioRetryDelay;
		SetPhase(RadioPhaseEnum::Failed);
		return true;
	}

	const int iCrossfade = Vars::Misc::Radio::Crossfade.Value;
	if (m_pStream && iCrossfade > 0)
	{
		RetireFade(true);
		m_pFading = m_pStream;
		m_uFadeDeadline = GetTickCount64() + iCrossfade;
		m_pFading->Slide(0.f, iCrossfade);
	}
	else if (m_pStream)
		m_pStream->Release();

	m_pStream = pStream;
	m_uStation = m_uConnectStation;
	m_bPlaying = false;
	SetTitle("");
	m_pStream->Slide(iCrossfade > 0 ? 0.f : WantedVolume(), 0);
	return true;
}

void CRadio::Silence()
{
	{
		std::lock_guard tLock(m_tConnectMutex);
		++m_iConnectGeneration;
		m_iConnectReady = 0;
		if (m_pConnectResult)
		{
			m_pConnectResult->Release();
			m_pConnectResult = nullptr;
		}
	}

	m_bConnectPending = false;
	RetireFade(true);
	SetPhase(RadioPhaseEnum::Off);
	m_iBuffer.store(0);
	m_uRetryAt = 0;
	m_uAttempted = RadioStationEnum::Count;

	if (!m_pStream)
		return;

	m_pStream->Release();
	m_pStream = nullptr;
	m_uStation = RadioStationEnum::Count;
	m_bPlaying = false;
	SetTitle("");
}

void CRadio::Pump()
{
	if (!m_pStream || m_bConnectPending)
		return;

	if (m_bPlaying)
	{
		const AudioStreamState_t iState = m_pStream->State();
		if (iState == AudioStreamState_t::Stalled)
		{
			SetPhase(RadioPhaseEnum::Buffering);
			m_iBuffer.store(std::clamp(m_pStream->BufferPercent(), 0, 100));
			return;
		}

		if (iState == AudioStreamState_t::Stopped)
		{
			m_pStream->Release();
			m_pStream = nullptr;
			m_uStation = RadioStationEnum::Count;
			m_bPlaying = false;
			m_iBuffer.store(0);
			SetTitle("");
			SetPhase(RadioPhaseEnum::Connecting);
			return;
		}

		SetPhase(RadioPhaseEnum::Playing);
		m_iBuffer.store(100);
		PollTitle();
		return;
	}

	const int iFilled = m_pStream->BufferPercent();
	if (iFilled <= RadioBufferReady && m_pStream->Connected())
	{
		SetPhase(RadioPhaseEnum::Buffering);
		m_iBuffer.store(iFilled);
		return;
	}

	m_pStream->Play();
	m_bPlaying = true;
	if (const int iCrossfade = Vars::Misc::Radio::Crossfade.Value; iCrossfade > 0)
	{
		m_pStream->Slide(WantedVolume(), iCrossfade);
		m_uFadeInUntil = GetTickCount64() + iCrossfade;
	}

	SetPhase(RadioPhaseEnum::Playing);
	m_iBuffer.store(100);
}

void CRadio::ApplyVolume()
{
	if (!m_pStream || !m_bPlaying || GetTickCount64() < m_uFadeInUntil)
		return;

	m_pStream->Slide(WantedVolume(), 0);
}

void CRadio::PauseOutput(bool bPaused)
{
	if (m_pStream)
		m_pStream->Pause(bPaused);
	if (m_pFading)
		m_pFading->Pause(bPaused);
}

void CRadio::RetireFade(bool bImmediately)
{
	if (!m_pFading || (!bImmediately && GetTickCount64() < m_uFadeDeadline))
		return;

	m_pFading->Release();
	m_pFading = nullptr;
}

void CRadio::SetTitle(const char* sTitle)
{
	std::lock_guard tLock(m_tTitleMutex);
	strcpy_s(m_sTitle, sTitle);
}

void CRadio::PollTitle()
{
	char sTitle[AudioTitleCapacity] = {};
	m_pStream->Title(sTitle, sizeof(sTitle));
	SetTitle(sTitle);
}

float CRadio::WantedVolume() const
{
	if (Unfocused())
		return 0.f;

	float flVolume = float(Vars::Misc::Radio::Volume.Value) / 100.f;
	if (Ducking())
		flVolume *= float(Vars::Misc::Radio::DuckLevel.Value) / 100.f;

	if (!Vars::Misc::Radio::ScaleToGameVolume.Value || !m_pGameVolume)
		return flVolume;

	return std::clamp(flVolume * std::clamp(m_pGameVolume->GetFloat(), 0.f, 1.f), 0.f, 1.f);
}

bool CRadio::Ducking() const
{
	if (!Vars::Misc::Radio::DuckOnVoice.Value)
		return false;

	return Vars::Misc::Radio::DuckOnSelf.Value && m_bSelfTalking.load() ||
		Vars::Misc::Radio::DuckOnOthers.Value && m_bOthersTalking.load();
}

bool CRadio::Unfocused() const
{
	return Vars::Misc::Radio::MuteUnfocused.Value && !SDK::IsGameWindowInFocus();
}

void CRadio::OnVoiceStatus(int iEntity, bool bTalking)
{
	if (iEntity == -1)
	{
		m_bSelfTalking.store(bTalking);
		return;
	}

	if (iEntity < 1 || iEntity > RadioVoiceMaxPlayers)
		return;

	const uint64_t uBit = 1ull << (iEntity - 1);
	if (bTalking)
		m_uSpeaking.fetch_or(uBit);
	else
		m_uSpeaking.fetch_and(~uBit);

	m_bOthersTalking.store(m_uSpeaking.load() != 0);
}

void CRadio::OnLevelShutdown()
{
	m_uSpeaking.store(0);
	m_bSelfTalking.store(false);
	m_bOthersTalking.store(false);
}

void CRadio::OnFrame()
{
	if (Pressed(BindToggle, Vars::Misc::Radio::Key.Value))
		Vars::Misc::Radio::Enabled.Value = !Vars::Misc::Radio::Enabled.Value;
	if (Pressed(BindNext, Vars::Misc::Radio::NextKey.Value))
		StepStation(1);
	if (Pressed(BindPrevious, Vars::Misc::Radio::PreviousKey.Value))
		StepStation(-1);
	if (Pressed(BindLouder, Vars::Misc::Radio::LouderKey.Value))
		Vars::Misc::Radio::Volume.Value = std::min(Vars::Misc::Radio::Volume.Value + RadioVolumeStep, 100);
	if (Pressed(BindQuieter, Vars::Misc::Radio::QuieterKey.Value))
		Vars::Misc::Radio::Volume.Value = std::max(Vars::Misc::Radio::Volume.Value - RadioVolumeStep, 0);
}

bool CRadio::Pressed(int iSlot, int iKey)
{
	if (iSlot < 0 || iSlot >= BindCount)
		return false;

	const bool bActive = U::KeyHandler.Pressed(byte(iKey), false, &m_aBindStorage[iSlot]);
	const bool bEdge = bActive && !m_aBindHeld[iSlot];
	m_aBindHeld[iSlot] = bActive;
	return bEdge;
}

void CRadio::StepStation(int iDelta)
{
	int iNext = int(Vars::Misc::Radio::Station.Value) + iDelta;
	while (iNext < 0)
		iNext += RadioStationEnum::Count;
	Vars::Misc::Radio::Station.Value = iNext % RadioStationEnum::Count;
}

void CRadio::TogglePaused()
{
	m_bPaused.store(!m_bPaused.load());
}

bool CRadio::Paused() const
{
	return m_bPaused.load();
}

int CRadio::Phase() const
{
	return m_iPhase.load();
}

int CRadio::BufferPercent() const
{
	return m_iBuffer.load();
}

const char* CRadio::NowPlaying() const
{
	if (Phase() == RadioPhaseEnum::Off)
		return RadioStationName(RadioStationEnum::Count);
	return RadioStationName(size_t(Vars::Misc::Radio::Station.Value));
}

void CRadio::Title(char* sOut, size_t uCapacity) const
{
	std::lock_guard tLock(m_tTitleMutex);
	strcpy_s(sOut, uCapacity, m_sTitle);
}

std::string CRadio::Status() const
{
	switch (Phase())
	{
	case RadioPhaseEnum::Playing:
		return std::format("Playing at {}%", Vars::Misc::Radio::Volume.Value);
	case RadioPhaseEnum::Buffering:
		return std::format("Buffering at {}%", BufferPercent());
	case RadioPhaseEnum::Paused:
		return "Paused";
	case RadioPhaseEnum::Failed:
		return "Stream unavailable, retrying";
	case RadioPhaseEnum::Connecting:
		return "Connecting";
	default:
		return "Off";
	}
}
