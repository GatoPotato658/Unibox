#include "AudioDevice.h"

#include <algorithm>
#include <chrono>

#define FAR
#define NEAR
#include <audioclient.h>
#include <mmdeviceapi.h>
#undef FAR
#undef NEAR

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

static constexpr REFERENCE_TIME AudioBufferDuration = 400000;
static constexpr DWORD AudioWait = 10;
static constexpr uint64_t AudioRetryDelay = 1000;
static constexpr uint32_t AudioMinRamp = 10;

bool CAudioDevice::Startup()
{
	if (m_bStarted.exchange(true))
		return m_iReady.load() > 0;

	m_bStop.store(false);
	m_iReady.store(0);

	for (Voice_t& tVoice : m_aVoices)
	{
		tVoice = {};
		tVoice.m_hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!tVoice.m_hEvent)
		{
			m_bStarted.store(false);
			return false;
		}
	}

	m_tThread = std::thread(&CAudioDevice::Run, this);
	while (!m_iReady.load() && !m_bStop.load())
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

	if (m_iReady.load() > 0)
		return true;

	m_bStop.store(true);
	if (m_tThread.joinable())
		m_tThread.join();
	m_bStarted.store(false);
	return false;
}

void CAudioDevice::Run()
{
	if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) ||
		FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&m_pDevices))))
	{
		m_iReady.store(-1);
		return;
	}

	m_iReady.store(1);

	while (!m_bStop.load())
	{
		HANDLE aEvents[AudioVoiceCount] = {};
		DWORD uCount = 0;

		for (Voice_t& tVoice : m_aVoices)
		{
			if (tVoice.m_bRunning)
				aEvents[uCount++] = tVoice.m_hEvent;
		}

		if (uCount)
			WaitForMultipleObjects(uCount, aEvents, FALSE, AudioWait);
		else
			Sleep(AudioWait);

		for (Voice_t& tVoice : m_aVoices)
			Service(tVoice);
	}

	{
		std::lock_guard tLock(m_tMutex);
		for (Voice_t& tVoice : m_aVoices)
		{
			ReleaseClient(tVoice);
			if (tVoice.m_hEvent)
			{
				CloseHandle(tVoice.m_hEvent);
				tVoice.m_hEvent = nullptr;
			}
		}

		if (m_pDevices)
		{
			m_pDevices->Release();
			m_pDevices = nullptr;
		}
	}

	CoUninitialize();
}

void CAudioDevice::Shutdown()
{
	if (!m_bStarted.exchange(false))
		return;

	m_bStop.store(true);
	if (m_tThread.joinable())
		m_tThread.join();
	m_iReady.store(0);
}

size_t CAudioDevice::OpenVoice(uint32_t uSampleRate, AudioRenderFunction fRender, void* pUser, float flVolume)
{
	std::lock_guard tLock(m_tMutex);
	return OpenLocked(uSampleRate, fRender, pUser, flVolume);
}

size_t CAudioDevice::OpenLocked(uint32_t uSampleRate, AudioRenderFunction fRender, void* pUser, float flVolume)
{
	for (size_t i = 0; i < AudioVoiceCount; i++)
	{
		Voice_t& tVoice = m_aVoices[i];
		if (tVoice.m_iSlot != VoiceSlot_t::Free)
			continue;

		tVoice.m_iSlot = VoiceSlot_t::Open;
		tVoice.m_fRender = fRender;
		tVoice.m_pUser = pUser;
		tVoice.m_uRate = uSampleRate;
		tVoice.m_bPaused = false;
		tVoice.m_flTarget = flVolume;
		tVoice.m_uRamp = 0;
		++tVoice.m_uSerial;
		return i;
	}
	return AudioNoVoice;
}

void CAudioDevice::CloseVoice(size_t uVoice)
{
	if (uVoice >= AudioVoiceCount)
		return;

	std::unique_lock tLock(m_tMutex);
	Voice_t& tVoice = m_aVoices[uVoice];
	if (tVoice.m_iSlot == VoiceSlot_t::Free)
		return;

	tVoice.m_iSlot = VoiceSlot_t::Closing;
	m_tClosed.wait_for(tLock, std::chrono::seconds(2), [&] { return tVoice.m_iSlot == VoiceSlot_t::Free; });
}

void CAudioDevice::PauseVoice(size_t uVoice, bool bPaused)
{
	if (uVoice >= AudioVoiceCount)
		return;

	std::lock_guard tLock(m_tMutex);
	m_aVoices[uVoice].m_bPaused = bPaused;
}

void CAudioDevice::SlideVoice(size_t uVoice, float flVolume, uint32_t uMilliseconds)
{
	if (uVoice >= AudioVoiceCount)
		return;

	std::lock_guard tLock(m_tMutex);
	Voice_t& tVoice = m_aVoices[uVoice];
	tVoice.m_flTarget = flVolume;
	tVoice.m_uRamp = uMilliseconds;
	++tVoice.m_uSerial;
}

bool CAudioDevice::OpenClient(Voice_t& tVoice)
{
	IMMDevice* pDevice = nullptr;
	if (FAILED(m_pDevices->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice)))
		return false;

	const HRESULT iActivate = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&tVoice.m_pClient));
	pDevice->Release();

	if (FAILED(iActivate))
	{
		tVoice.m_pClient = nullptr;
		return false;
	}

	WAVEFORMATEX tFormat = {};
	tFormat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
	tFormat.nChannels = AudioChannels;
	tFormat.nSamplesPerSec = tVoice.m_uRate;
	tFormat.wBitsPerSample = sizeof(float) * 8;
	tFormat.nBlockAlign = AudioChannels * sizeof(float);
	tFormat.nAvgBytesPerSec = tVoice.m_uRate * tFormat.nBlockAlign;

	constexpr DWORD iFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
	if (FAILED(tVoice.m_pClient->Initialize(AUDCLNT_SHAREMODE_SHARED, iFlags, AudioBufferDuration, 0, &tFormat, nullptr)) ||
		FAILED(tVoice.m_pClient->SetEventHandle(tVoice.m_hEvent)) ||
		FAILED(tVoice.m_pClient->GetBufferSize(&tVoice.m_uBufferFrames)) ||
		FAILED(tVoice.m_pClient->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&tVoice.m_pOutput))))
	{
		ReleaseClient(tVoice);
		return false;
	}

	return true;
}

void CAudioDevice::ReleaseClient(Voice_t& tVoice)
{
	if (tVoice.m_pOutput)
	{
		tVoice.m_pOutput->Release();
		tVoice.m_pOutput = nullptr;
	}

	if (tVoice.m_pClient)
	{
		tVoice.m_pClient->Stop();
		tVoice.m_pClient->Release();
		tVoice.m_pClient = nullptr;
	}

	tVoice.m_bRunning = false;
}

void CAudioDevice::Service(Voice_t& tVoice)
{
	VoiceSlot_t iSlot = VoiceSlot_t::Free;
	bool bPaused = false;

	{
		std::lock_guard tLock(m_tMutex);
		iSlot = tVoice.m_iSlot;
		bPaused = tVoice.m_bPaused;
	}

	if (iSlot == VoiceSlot_t::Free)
		return;

	if (iSlot == VoiceSlot_t::Closing)
	{
		ReleaseClient(tVoice);
		tVoice.m_flGain = 0.f;
		tVoice.m_uStepsLeft = 0;

		std::lock_guard tLock(m_tMutex);
		tVoice.m_iSlot = VoiceSlot_t::Free;
		m_tClosed.notify_all();
		return;
	}

	if (!tVoice.m_pClient)
	{
		const uint64_t uNow = GetTickCount64();
		if (uNow < tVoice.m_uRetryAt)
			return;

		if (!OpenClient(tVoice))
		{
			tVoice.m_uRetryAt = uNow + AudioRetryDelay;
			return;
		}
	}

	if (bPaused)
	{
		if (tVoice.m_bRunning)
		{
			tVoice.m_pClient->Stop();
			tVoice.m_bRunning = false;
		}
		return;
	}

	uint32_t uPadding = 0;
	if (FAILED(tVoice.m_pClient->GetCurrentPadding(&uPadding)))
	{
		ReleaseClient(tVoice);
		return;
	}

	if (const uint32_t uFrames = tVoice.m_uBufferFrames - uPadding)
	{
		BYTE* pBuffer = nullptr;
		if (FAILED(tVoice.m_pOutput->GetBuffer(uFrames, &pBuffer)))
		{
			ReleaseClient(tVoice);
			return;
		}

		float* pOut = reinterpret_cast<float*>(pBuffer);
		{
			std::lock_guard tLock(m_tMutex);
			if (tVoice.m_iSlot != VoiceSlot_t::Open)
				memset(pOut, 0, uFrames * AudioChannels * sizeof(float));
			else
				tVoice.m_fRender(tVoice.m_pUser, pOut, uFrames);

			if (tVoice.m_uSerial != tVoice.m_uApplied)
			{
				tVoice.m_uApplied = tVoice.m_uSerial;
				tVoice.m_uStepsLeft = std::max(tVoice.m_uRamp, AudioMinRamp) * tVoice.m_uRate / 1000;
				tVoice.m_flStep = (tVoice.m_flTarget - tVoice.m_flGain) / float(tVoice.m_uStepsLeft);
				tVoice.m_flGoal = tVoice.m_flTarget;
			}
		}

		Ramp(tVoice, pOut, uFrames);
		tVoice.m_pOutput->ReleaseBuffer(uFrames, 0);
	}

	if (!tVoice.m_bRunning)
	{
		tVoice.m_pClient->Start();
		tVoice.m_bRunning = true;
	}
}

void CAudioDevice::Ramp(Voice_t& tVoice, float* pOut, uint32_t uFrames)
{
	for (uint32_t i = 0; i < uFrames; i++)
	{
		if (tVoice.m_uStepsLeft)
			tVoice.m_flGain = --tVoice.m_uStepsLeft ? tVoice.m_flGain + tVoice.m_flStep : tVoice.m_flGoal;

		pOut[i * AudioChannels] *= tVoice.m_flGain;
		pOut[i * AudioChannels + 1] *= tVoice.m_flGain;
	}
}
