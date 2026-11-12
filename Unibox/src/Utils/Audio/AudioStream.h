#pragma once

#include "AudioDevice.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#define MINIMP3_FLOAT_OUTPUT
#include "Vendor/minimp3.h"

struct stb_vorbis;

inline constexpr size_t AudioTitleCapacity = 96;

enum class AudioStreamState_t : uint8_t
{
	Playing,
	Stalled,
	Stopped
};

enum class AudioStreamError_t : uint8_t
{
	None,
	Url,
	Connect,
	Status,
	Format,
	Unsupported,
	Thread
};

class CAudioStream
{
private:
	enum class Codec_t : uint8_t
	{
		Mp3,
		Vorbis
	};

	static constexpr size_t IcyMetaCapacity = 255 * 16;

	AudioStreamError_t Connect(const char* sUrl);
	AudioStreamError_t Receive();
	AudioStreamError_t Filter(const uint8_t* pData, uint32_t uSize);
	AudioStreamError_t Decode(const uint8_t* pData, uint32_t uSize);
	AudioStreamError_t DecodeMp3();
	AudioStreamError_t DecodeVorbis();

	bool Push(const float* pLeft, const float* pRight, size_t uStride, uint32_t uFrames, uint32_t uRate);
	void Render(float* pOut, uint32_t uFrames);
	void ReadIcyTitle();
	void ReadVorbisTitle();
	void SetTitle(const char* sTitle);
	void Close();
	void Reader();
	AudioStreamError_t OpenConnection(const char* sUrl);
	bool Cancelled() const;
	void ResetCodec();
	AudioStreamError_t StartOutput(uint32_t uRate);
	AudioStreamError_t ReadAudioChunk();
	AudioStreamError_t DrainInput();
	AudioStreamError_t ConnectVoice();
	static void RenderVoice(void* pUser, float* pOut, uint32_t uFrames);

	std::atomic<bool> m_bClosing = false;
	std::atomic<bool> m_bEnded = false;
	std::atomic<bool> m_bStalled = false;
	std::atomic<bool> m_bReleased = false;
	std::atomic<uint64_t> m_uLastDataTick = 0;
	const std::atomic<bool>* m_pCancel = nullptr;

	void* m_pSession = nullptr;
	void* m_pConnection = nullptr;
	void* m_pRequest = nullptr;
	Codec_t m_iCodec = Codec_t::Mp3;

	uint32_t m_uMetaint = 0;
	uint32_t m_uAudioLeft = 0;
	uint32_t m_uMetaLeft = 0;
	uint32_t m_uMetaSize = 0;
	char m_sMeta[IcyMetaCapacity + 1] = {};

	uint8_t m_aInput[64 * 1024] = {};
	size_t m_uInputSize = 0;
	size_t m_uInputUsed = 0;

	mp3dec_t m_tMp3 = {};
	stb_vorbis* m_pVorbis = nullptr;
	char* m_pVorbisMemory = nullptr;

	float* m_pRing = nullptr;
	std::atomic<uint32_t> m_uRate = 0;
	std::atomic<uint32_t> m_uCapacity = 0;
	std::atomic<uint32_t> m_uWritten = 0;
	std::atomic<uint32_t> m_uRead = 0;

	std::atomic<size_t> m_uVoice = AudioNoVoice;
	std::atomic<float> m_flVolume = 0.f;

	mutable std::mutex m_tTitleMutex;
	char m_sTitle[AudioTitleCapacity] = {};

	std::thread m_tReader;

public:
	~CAudioStream();

	static CAudioStream* Open(const char* sUrl, AudioStreamError_t& iError, const std::atomic<bool>* pCancel = nullptr);
	void Release();

	void Play();
	bool Playing() const;
	void Pause(bool bPaused);
	void Slide(float flVolume, uint32_t uMilliseconds);

	AudioStreamState_t State() const;
	int BufferPercent() const;
	bool Connected() const;
	void Title(char* sOut, size_t uCapacity) const;
};
