#include "AudioStream.h"

#include <algorithm>
#include <cstring>

#include <WinHttp.h>

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_PULLDATA_API
#include "Vendor/stb_vorbis.c"

#pragma comment(lib, "winhttp.lib")

static constexpr uint32_t StreamSeconds = 5;
static constexpr uint32_t StreamStartSeconds = 1;
static constexpr DWORD StreamFullWait = 20;
static constexpr size_t Mp3Window = 16 * 1024;
static constexpr size_t VorbisMemory = 512 * 1024;
static constexpr size_t OggPageHeader = 27;

static bool ContentIs(const wchar_t* sType, const wchar_t* sPrefix)
{
	const size_t uPrefix = wcslen(sPrefix);
	return wcslen(sType) >= uPrefix && _wcsnicmp(sType, sPrefix, uPrefix) == 0;
}

static bool StartsOggStream(const uint8_t* pData, size_t uSize)
{
	return uSize >= OggPageHeader && pData[0] == 'O' && pData[1] == 'g' && pData[2] == 'g' && pData[3] == 'S' &&
		pData[4] == 0 && (pData[5] & 2) != 0;
}

static size_t NextOggStream(const uint8_t* pData, size_t uSize)
{
	for (size_t i = 1; i < uSize; i++)
	{
		if (StartsOggStream(pData + i, uSize - i))
			return i;
	}
	return uSize;
}

static void CopyString(char* sOut, size_t uCapacity, const char* sIn, size_t uLength)
{
	if (!uCapacity)
		return;

	uLength = (std::min)(uLength, uCapacity - 1);
	memcpy(sOut, sIn, uLength);
	sOut[uLength] = '\0';
}

CAudioStream::~CAudioStream()
{
	Close();
	free(m_pRing);
	free(m_pVorbisMemory);
}

CAudioStream* CAudioStream::Open(const char* sUrl, AudioStreamError_t& iError)
{
	CAudioStream* pStream = new CAudioStream();
	iError = pStream->Connect(sUrl);

	while (iError == AudioStreamError_t::None && (!pStream->m_pRing || !pStream->m_uWritten.load()))
		iError = pStream->Receive();

	if (iError != AudioStreamError_t::None)
	{
		delete pStream;
		return nullptr;
	}

	try
	{
		pStream->m_tReader = std::thread(&CAudioStream::Reader, pStream);
	}
	catch (...)
	{
		iError = AudioStreamError_t::Thread;
		delete pStream;
		return nullptr;
	}

	return pStream;
}

void CAudioStream::Release()
{
	if (m_bReleased.exchange(true))
		return;

	m_bClosing.store(true);
	Close();

	const size_t uVoice = m_uVoice.load();
	if (uVoice != AudioNoVoice)
	{
		U::Audio.CloseVoice(uVoice);
		m_uVoice.store(AudioNoVoice);
	}

	if (m_tReader.joinable())
		m_tReader.join();

	delete this;
}

void CAudioStream::Play()
{
	m_uVoice.store(U::Audio.OpenVoice(m_uRate, RenderVoice, this, m_flVolume.load()));
}

void CAudioStream::Pause(bool bPaused)
{
	const size_t uVoice = m_uVoice.load();
	if (uVoice != AudioNoVoice)
		U::Audio.PauseVoice(uVoice, bPaused);
}

void CAudioStream::Slide(float flVolume, uint32_t uMilliseconds)
{
	m_flVolume.store(flVolume);

	const size_t uVoice = m_uVoice.load();
	if (uVoice != AudioNoVoice)
		U::Audio.SlideVoice(uVoice, flVolume, uMilliseconds);
}

AudioStreamState_t CAudioStream::State() const
{
	if (m_bEnded.load() && m_uWritten.load() == m_uRead.load())
		return AudioStreamState_t::Stopped;

	return m_bStalled.load() ? AudioStreamState_t::Stalled : AudioStreamState_t::Playing;
}

int CAudioStream::BufferPercent() const
{
	if (!m_uCapacity)
		return 0;

	return int((m_uWritten.load() - m_uRead.load()) * 100u / m_uCapacity);
}

bool CAudioStream::Connected() const
{
	return !m_bEnded.load();
}

void CAudioStream::Title(char* sOut, size_t uCapacity) const
{
	std::lock_guard tLock(m_tTitleMutex);
	CopyString(sOut, uCapacity, m_sTitle, strlen(m_sTitle));
}

AudioStreamError_t CAudioStream::Connect(const char* sUrl)
{
	wchar_t sWide[512] = {};
	const size_t uLength = strlen(sUrl);
	if (uLength >= sizeof(sWide) / sizeof(sWide[0]))
		return AudioStreamError_t::Url;

	for (size_t i = 0; i < uLength; i++)
		sWide[i] = wchar_t(uint8_t(sUrl[i]));

	URL_COMPONENTS tParts = {};
	tParts.dwStructSize = sizeof(tParts);
	tParts.dwHostNameLength = DWORD(-1);
	tParts.dwUrlPathLength = DWORD(-1);
	tParts.dwExtraInfoLength = DWORD(-1);

	if (!WinHttpCrackUrl(sWide, 0, 0, &tParts))
		return AudioStreamError_t::Url;

	wchar_t sHost[256] = {};
	const size_t uHostLength = tParts.dwHostNameLength;
	if (!uHostLength || uHostLength >= sizeof(sHost) / sizeof(sHost[0]))
		return AudioStreamError_t::Url;

	memcpy(sHost, tParts.lpszHostName, uHostLength * sizeof(wchar_t));

	m_pSession = WinHttpOpen(L"VLC/3.0.20 LibVLC/3.0.20", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!m_pSession)
		return AudioStreamError_t::Connect;

	WinHttpSetTimeouts(m_pSession, 5000, 5000, 5000, 10000);

	m_pConnection = WinHttpConnect(m_pSession, sHost, tParts.nPort, 0);
	if (!m_pConnection)
		return AudioStreamError_t::Connect;

	m_pRequest = WinHttpOpenRequest(m_pConnection, L"GET", tParts.lpszUrlPath, nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, tParts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
	if (!m_pRequest ||
		!WinHttpSendRequest(m_pRequest, L"Icy-MetaData: 1", DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
		!WinHttpReceiveResponse(m_pRequest, nullptr))
		return AudioStreamError_t::Connect;

	DWORD uStatus = 0, uSize = sizeof(uStatus);
	if (!WinHttpQueryHeaders(m_pRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &uStatus, &uSize, WINHTTP_NO_HEADER_INDEX) || uStatus != 200)
		return AudioStreamError_t::Status;

	wchar_t sType[64] = {};
	uSize = sizeof(sType) - sizeof(sType[0]);
	WinHttpQueryHeaders(m_pRequest, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, sType, &uSize, WINHTTP_NO_HEADER_INDEX);

	if (ContentIs(sType, L"audio/mpeg"))
	{
		m_iCodec = Codec_t::Mp3;
		mp3dec_init(&m_tMp3);
	}
	else if (ContentIs(sType, L"application/ogg") || ContentIs(sType, L"audio/ogg"))
	{
		m_iCodec = Codec_t::Vorbis;
		m_pVorbisMemory = static_cast<char*>(malloc(VorbisMemory));
		if (!m_pVorbisMemory)
			return AudioStreamError_t::Format;
	}
	else
		return AudioStreamError_t::Format;

	DWORD uMetaint = 0;
	uSize = sizeof(uMetaint);
	if (WinHttpQueryHeaders(m_pRequest, WINHTTP_QUERY_CUSTOM | WINHTTP_QUERY_FLAG_NUMBER,
		L"icy-metaint", &uMetaint, &uSize, WINHTTP_NO_HEADER_INDEX))
	{
		m_uMetaint = uMetaint;
		m_uAudioLeft = uMetaint;
	}

	return AudioStreamError_t::None;
}

AudioStreamError_t CAudioStream::Receive()
{
	uint8_t aChunk[4096] = {};
	DWORD uAvailable = 0, uReceived = 0;

	if (m_bClosing.load() ||
		!WinHttpQueryDataAvailable(m_pRequest, &uAvailable) || !uAvailable ||
		!WinHttpReadData(m_pRequest, aChunk, std::min<DWORD>(uAvailable, sizeof(aChunk)), &uReceived) || !uReceived)
		return AudioStreamError_t::Connect;

	return Filter(aChunk, uReceived);
}

AudioStreamError_t CAudioStream::Filter(const uint8_t* pData, uint32_t uSize)
{
	if (!m_uMetaint)
		return Decode(pData, uSize);

	while (uSize)
	{
		if (m_uAudioLeft)
		{
			const uint32_t uTake = (std::min)(uSize, m_uAudioLeft);
			if (const AudioStreamError_t iError = Decode(pData, uTake); iError != AudioStreamError_t::None)
				return iError;

			m_uAudioLeft -= uTake;
			pData += uTake;
			uSize -= uTake;
			continue;
		}

		if (!m_uMetaLeft)
		{
			m_uMetaLeft = uint32_t(*pData) * 16;
			m_uMetaSize = 0;
			++pData;
			--uSize;
			if (!m_uMetaLeft)
				m_uAudioLeft = m_uMetaint;
			continue;
		}

		const uint32_t uTake = (std::min)(uSize, m_uMetaLeft);
		memcpy(m_sMeta + m_uMetaSize, pData, uTake);
		m_uMetaSize += uTake;
		m_uMetaLeft -= uTake;
		pData += uTake;
		uSize -= uTake;

		if (!m_uMetaLeft)
		{
			m_sMeta[m_uMetaSize] = '\0';
			m_uAudioLeft = m_uMetaint;
			ReadIcyTitle();
		}
	}

	return AudioStreamError_t::None;
}

AudioStreamError_t CAudioStream::Decode(const uint8_t* pData, uint32_t uSize)
{
	while (uSize)
	{
		if (m_uInputUsed)
		{
			m_uInputSize -= m_uInputUsed;
			memmove(m_aInput, m_aInput + m_uInputUsed, m_uInputSize);
			m_uInputUsed = 0;
		}

		const size_t uTake = std::min<size_t>(uSize, sizeof(m_aInput) - m_uInputSize);
		if (!uTake)
			return AudioStreamError_t::Format;

		memcpy(m_aInput + m_uInputSize, pData, uTake);
		m_uInputSize += uTake;
		pData += uTake;
		uSize -= uint32_t(uTake);

		const AudioStreamError_t iError = m_iCodec == Codec_t::Mp3 ? DecodeMp3() : DecodeVorbis();
		if (iError != AudioStreamError_t::None)
			return iError;
	}

	return AudioStreamError_t::None;
}

AudioStreamError_t CAudioStream::DecodeMp3()
{
	float aPcm[MINIMP3_MAX_SAMPLES_PER_FRAME] = {};

	while (m_uInputSize - m_uInputUsed >= Mp3Window)
	{
		mp3dec_frame_info_t tInfo = {};
		const int iSamples = mp3dec_decode_frame(&m_tMp3, m_aInput + m_uInputUsed,
			int(m_uInputSize - m_uInputUsed), aPcm, &tInfo);
		if (!tInfo.frame_bytes)
			return AudioStreamError_t::Format;

		m_uInputUsed += size_t(tInfo.frame_bytes);
		if (iSamples && !Push(aPcm, aPcm + (tInfo.channels > 1 ? 1 : 0), size_t(tInfo.channels),
			uint32_t(iSamples), uint32_t(tInfo.hz)))
			return AudioStreamError_t::Connect;
	}

	return AudioStreamError_t::None;
}

AudioStreamError_t CAudioStream::DecodeVorbis()
{
	for (;;)
	{
		const uint8_t* pData = m_aInput + m_uInputUsed;
		const size_t uPending = m_uInputSize - m_uInputUsed;

		if (m_pVorbis && StartsOggStream(pData, uPending))
			m_pVorbis = nullptr;

		if (!m_pVorbis)
		{
			const stb_vorbis_alloc tMemory = { m_pVorbisMemory, int(VorbisMemory) };
			int iUsed = 0, iError = 0;
			m_pVorbis = stb_vorbis_open_pushdata(pData, int(uPending), &iUsed, &iError, &tMemory);
			if (!m_pVorbis)
			{
				return iError == VORBIS_need_more_data && m_uInputSize < sizeof(m_aInput)
					? AudioStreamError_t::None
					: AudioStreamError_t::Format;
			}

			m_uInputUsed += size_t(iUsed);
			ReadVorbisTitle();
			continue;
		}

		const size_t uLimit = NextOggStream(pData, uPending);
		int iChannels = 0, iSamples = 0;
		float** pOutput = nullptr;
		const int iUsed = stb_vorbis_decode_frame_pushdata(m_pVorbis, pData, int(uLimit), &iChannels, &pOutput, &iSamples);

		if (!iUsed && !iSamples)
		{
			if (uLimit == uPending)
				return AudioStreamError_t::None;

			m_uInputUsed += uLimit;
			continue;
		}

		m_uInputUsed += size_t(iUsed);
		if (iSamples && !Push(pOutput[0], pOutput[iChannels > 1 ? 1 : 0], 1, uint32_t(iSamples),
			stb_vorbis_get_info(m_pVorbis).sample_rate))
			return AudioStreamError_t::Connect;
	}
}

bool CAudioStream::Push(const float* pLeft, const float* pRight, size_t uStride, uint32_t uFrames, uint32_t uRate)
{
	if (!m_pRing)
	{
		uint32_t uCapacity = 1;
		while (uCapacity < uRate * StreamSeconds)
			uCapacity <<= 1;

		m_pRing = static_cast<float*>(malloc(uCapacity * AudioChannels * sizeof(float)));
		if (!m_pRing)
			return false;

		uint32_t uStart = 1;
		while (uStart < uRate * StreamStartSeconds)
			uStart <<= 1;

		m_uRate = uRate;
		m_uCapacity = uCapacity;
		m_uStartFill = uStart;
	}

	const uint32_t uWritten = m_uWritten.load();
	const uint32_t uLimit = m_uVoice.load() == AudioNoVoice ? m_uStartFill : m_uCapacity;

	while (uWritten + uFrames - m_uRead.load() > uLimit)
	{
		if (m_bClosing.load())
			return false;

		if (m_uVoice.load() == AudioNoVoice)
		{
			m_uRead.store(uWritten + uFrames - uLimit);
			break;
		}

		Sleep(StreamFullWait);
	}

	for (uint32_t i = 0; i < uFrames; i++)
	{
		float* pSlot = m_pRing + ((uWritten + i) & (m_uCapacity - 1)) * AudioChannels;
		pSlot[0] = pLeft[i * uStride];
		pSlot[1] = pRight[i * uStride];
	}

	m_uWritten.store(uWritten + uFrames);
	return true;
}

void CAudioStream::Render(float* pOut, uint32_t uFrames)
{
	const uint32_t uReadValue = m_uRead.load();
	uint32_t uAvailable = m_uWritten.load() - uReadValue;

	if (m_bStalled.load())
	{
		if (uAvailable < m_uRate && !m_bEnded.load())
			uAvailable = 0;
		else
			m_bStalled.store(false);
	}

	const uint32_t uCount = (std::min)(uAvailable, uFrames);
	for (uint32_t i = 0; i < uCount; i++)
	{
		const float* pSlot = m_pRing + ((uReadValue + i) & (m_uCapacity - 1)) * AudioChannels;
		pOut[i * AudioChannels] = pSlot[0];
		pOut[i * AudioChannels + 1] = pSlot[1];
	}

	memset(pOut + uCount * AudioChannels, 0, (uFrames - uCount) * AudioChannels * sizeof(float));
	m_uRead.store(uReadValue + uCount);

	if (uCount < uFrames && !m_bEnded.load())
		m_bStalled.store(true);
}

void CAudioStream::ReadIcyTitle()
{
	constexpr const char* sKey = "StreamTitle='";
	constexpr const char* sEnd = "';";
	const char* sStart = strstr(m_sMeta, sKey);
	if (!sStart)
	{
		SetTitle("");
		return;
	}

	sStart += strlen(sKey);
	const char* sFinish = strstr(sStart, sEnd);
	char sTitle[AudioTitleCapacity] = {};
	CopyString(sTitle, sizeof(sTitle), sStart, sFinish ? size_t(sFinish - sStart) : strlen(sStart));
	SetTitle(sTitle);
}

void CAudioStream::ReadVorbisTitle()
{
	const stb_vorbis_comment tComments = stb_vorbis_get_comment(m_pVorbis);
	constexpr const char* sArtistKey = "ARTIST=";
	constexpr const char* sTitleKey = "TITLE=";
	const size_t uArtistKey = strlen(sArtistKey), uTitleKey = strlen(sTitleKey);

	char sArtist[AudioTitleCapacity] = {};
	char sTitle[AudioTitleCapacity] = {};

	for (int i = 0; i < tComments.comment_list_length; i++)
	{
		const char* sComment = tComments.comment_list[i];
		const size_t uSize = strlen(sComment);
		if (uSize > uArtistKey && _strnicmp(sComment, sArtistKey, uArtistKey) == 0)
			CopyString(sArtist, sizeof(sArtist), sComment + uArtistKey, uSize - uArtistKey);
		else if (uSize > uTitleKey && _strnicmp(sComment, sTitleKey, uTitleKey) == 0)
			CopyString(sTitle, sizeof(sTitle), sComment + uTitleKey, uSize - uTitleKey);
	}

	if (!sTitle[0])
	{
		SetTitle(sArtist);
		return;
	}

	if (!sArtist[0])
	{
		SetTitle(sTitle);
		return;
	}

	char sLine[AudioTitleCapacity] = {};
	snprintf(sLine, sizeof(sLine), "%s - %s", sArtist, sTitle);
	SetTitle(sLine);
}

void CAudioStream::SetTitle(const char* sTitle)
{
	std::lock_guard tLock(m_tTitleMutex);
	CopyString(m_sTitle, sizeof(m_sTitle), sTitle, strlen(sTitle));
}

void CAudioStream::Close()
{
	std::lock_guard tLock(m_tRequestMutex);
	void** aHandles[] = { &m_pRequest, &m_pConnection, &m_pSession };
	for (void** pHandle : aHandles)
	{
		if (*pHandle)
		{
			WinHttpCloseHandle(*pHandle);
			*pHandle = nullptr;
		}
	}
}

void CAudioStream::Reader()
{
	while (!m_bClosing.load() && Receive() == AudioStreamError_t::None)
	{
	}

	Close();
	m_bEnded.store(true);
}

void CAudioStream::RenderVoice(void* pUser, float* pOut, uint32_t uFrames)
{
	static_cast<CAudioStream*>(pUser)->Render(pOut, uFrames);
}
