#pragma once

#include "../Macros/Macros.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <Windows.h>

struct IAudioClient;
struct IAudioRenderClient;
struct IMMDeviceEnumerator;

inline constexpr uint32_t AudioChannels = 2;
inline constexpr size_t AudioVoiceCount = 4;
inline constexpr size_t AudioNoVoice = AudioVoiceCount;

using AudioRenderFunction = void(*)(void*, float*, uint32_t);

class CAudioDevice
{
private:
	enum class VoiceSlot_t : uint8_t
	{
		Free,
		Open,
		Closing
	};

	struct Voice_t
	{
		VoiceSlot_t m_iSlot = VoiceSlot_t::Free;
		AudioRenderFunction m_fRender = nullptr;
		void* m_pUser = nullptr;
		uint32_t m_uRate = 0;
		bool m_bPaused = false;
		float m_flTarget = 0.f;
		uint32_t m_uRamp = 0;
		uint32_t m_uSerial = 0;
		IAudioClient* m_pClient = nullptr;
		IAudioRenderClient* m_pOutput = nullptr;
		HANDLE m_hEvent = nullptr;
		uint32_t m_uBufferFrames = 0;
		bool m_bRunning = false;
		uint64_t m_uRetryAt = 0;
		uint32_t m_uApplied = 0;
		float m_flGain = 0.f;
		float m_flGoal = 0.f;
		float m_flStep = 0.f;
		uint32_t m_uStepsLeft = 0;
	};

	size_t OpenLocked(uint32_t uSampleRate, AudioRenderFunction fRender, void* pUser, float flVolume);
	bool OpenClient(Voice_t& tVoice);
	void ReleaseClient(Voice_t& tVoice);
	void Service(Voice_t& tVoice);
	void Ramp(Voice_t& tVoice, float* pOut, uint32_t uFrames);

	IMMDeviceEnumerator* m_pDevices = nullptr;
	std::atomic<int> m_iReady = 0;
	std::mutex m_tMutex;
	std::condition_variable m_tClosed;
	Voice_t m_aVoices[AudioVoiceCount] = {};
	std::atomic<bool> m_aVoiceReady[AudioVoiceCount] = {};
	std::atomic<bool> m_bStop = false;
	std::atomic<bool> m_bStarted = false;
	std::thread m_tThread;

public:
	bool Startup();
	void Run();
	void Shutdown();

	size_t OpenVoice(uint32_t uSampleRate, AudioRenderFunction fRender, void* pUser, float flVolume);
	bool VoiceReady(size_t uVoice) const;
	void CloseVoice(size_t uVoice);
	void PauseVoice(size_t uVoice, bool bPaused);
	void SlideVoice(size_t uVoice, float flVolume, uint32_t uMilliseconds);
};

ADD_FEATURE_CUSTOM(CAudioDevice, Audio, U);
