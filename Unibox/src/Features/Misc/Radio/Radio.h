#pragma once

#include "../../../SDK/SDK.h"
#include "../../../Utils/Audio/AudioStream.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

Enum(RadioPhase, Off, Connecting, Buffering, Playing, Paused, Failed)
Enum(RadioStation, DnB, Jungle, LiquidDnB, Hardcore, Techno, ClassicRap, Vaporwave, NonStopFm, Tekk, Underground, TheBeat, Count)

const char* RadioStationName(size_t uStation);
const char* const* RadioStationNames();

class CRadio
{
private:
	enum BindSlot_t
	{
		BindToggle,
		BindNext,
		BindPrevious,
		BindLouder,
		BindQuieter,
		BindCount
	};

	void SetPhase(int iPhase);
	void StartConnect(size_t uStation);
	void Connect(int iGeneration, size_t uStation);
	bool PollConnect();
	void Silence();
	void FailStream();
	void CancelConnect();
	void Pump();
	void ApplyVolume();
	void PauseOutput(bool bPaused);
	void RetireFade(bool bImmediately);
	void PollTitle();
	void SetTitle(const char* sTitle);
	bool Pressed(int iSlot, int iKey);

	float WantedVolume() const;
	bool Ducking() const;
	bool Unfocused() const;

	CAudioStream* m_pStream = nullptr;
	CAudioStream* m_pFading = nullptr;
	uint64_t m_uFadeDeadline = 0;
	uint64_t m_uFadeInUntil = 0;
	uint64_t m_uRetryAt = 0;
	size_t m_uAttempted = RadioStationEnum::Count;

	std::mutex m_tConnectMutex;
	int m_iConnectGeneration = 0;
	int m_iConnectReady = 0;
	CAudioStream* m_pConnectResult = nullptr;
	size_t m_uConnectStation = RadioStationEnum::Count;
	bool m_bConnectPending = false;
	bool m_bPlaying = false;
	bool m_bStarted = false;
	bool m_bOutputPaused = false;

	ConVar* m_pGameVolume = nullptr;

	std::atomic<int> m_iPhase = int(RadioPhaseEnum::Off);
	std::atomic<int> m_iBuffer = 0;
	std::atomic<size_t> m_uStation = RadioStationEnum::Count;
	std::atomic<void*> m_pConnectThread = nullptr;
	std::atomic<bool> m_bSelfTalking = false;
	std::atomic<bool> m_bOthersTalking = false;
	std::atomic<uint64_t> m_uSpeaking = 0;
	std::atomic<bool> m_bPaused = false;
	std::atomic<bool> m_bStop = false;

	mutable std::mutex m_tTitleMutex;
	char m_sTitle[AudioTitleCapacity] = {};

	bool m_aBindHeld[BindCount] = {};
	KeyStorage m_aBindStorage[BindCount] = {};
	std::thread m_tThread;
	std::thread m_tConnectThread;

public:
	bool Startup();
	void Shutdown();
	void Run();
	void OnFrame();
	void OnVoiceStatus(int iEntity, bool bTalking);
	void OnLevelShutdown();
#ifndef TEXTMODE
	void Draw();
#endif

	void StepStation(int iDelta);
	void TogglePaused();
	bool Paused() const;
	int Phase() const;
	int BufferPercent() const;
	const char* NowPlaying() const;
	void Title(char* sOut, size_t uCapacity) const;
	std::string Status() const;
};

ADD_FEATURE(CRadio, Radio);
