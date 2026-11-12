#include "AudioStream.h"

#include <algorithm>
#include <cstring>
#include <memory>

#include <WinHttp.h>

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_PULLDATA_API
#include "Vendor/stb_vorbis.c"

#pragma comment(lib, "winhttp.lib")

static constexpr uint32_t StreamSeconds = 5;
static constexpr uint32_t StreamSlackFrames = 4096;
static constexpr DWORD StreamFullWait = 20;
static constexpr size_t VorbisMemory = 4 * 1024 * 1024;
static constexpr size_t OggPageHeader = 27;
static constexpr uint64_t StreamStallDelay = 2000;
static constexpr uint64_t StreamStartTimeout = 10000;
static constexpr int StreamConnectTimeout = 3000;
static constexpr int StreamReceiveTimeout = 2000;

static bool ContentIs(const wchar_t* sType, const wchar_t* sPrefix)
{
	return _wcsicmp(sType, sPrefix) == 0;
}

static bool ContentIsMp3(const wchar_t* sType)
{
	return ContentIs(sType, L"audio/mpeg") || ContentIs(sType, L"audio/mp3") || ContentIs(sType, L"audio/x-mpeg");
}

static bool ContentIsOgg(const wchar_t* sType)
{
	return ContentIs(sType, L"application/ogg") || ContentIs(sType, L"audio/ogg");
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
	ResetCodec();
	Close();
	free(m_pRing);
	free(m_pVorbisMemory);
}

CAudioStream* CAudioStream::Open(const char* sUrl, AudioStreamError_t& iError, const std::atomic<bool>* pCancel)
{
	auto pStream = std::make_unique<CAudioStream>();
	pStream->m_pCancel = pCancel;
	iError = pStream->Connect(sUrl);
	const uint64_t uDeadline = GetTickCount64() + StreamStartTimeout;

	while (iError == AudioStreamError_t::None && (!pStream->m_pRing || !pStream->m_uWritten.load()))
	{
		if (pStream->Cancelled() || GetTickCount64() >= uDeadline)
		{
			iError = AudioStreamError_t::Connect;
			break;
		}
		iError = pStream->Receive();
	}

	if (iError != AudioStreamError_t::None)
		return nullptr;

	if (pStream->Cancelled())
		return nullptr;

	const uint32_t uRate = pStream->m_uRate.load();
	if (pStream->m_uWritten.load() < uRate / 10)
	{
		const uint64_t uDeadline = GetTickCount64() + 1000;
		while (!pStream->Cancelled() && pStream->m_uWritten.load() < uRate / 10 && GetTickCount64() < uDeadline)
			if (const AudioStreamError_t iError = pStream->ReadAudioChunk(); iError != AudioStreamError_t::None)
				break;
	}

	if (pStream->Cancelled())
		return nullptr;

	try
	{
		CAudioStream* pRawStream = pStream.get();
		pStream->m_tReader = std::thread(&CAudioStream::Reader, pRawStream);
	}
	catch (...)
	{
		iError = AudioStreamError_t::Thread;
		return nullptr;
	}

	return pStream.release();
}

void CAudioStream::Release()
{
	if (m_bReleased.exchange(true))
		return;

	m_bClosing.store(true);
	m_bEnded.store(true);
	m_uRead.store(m_uWritten.load());

	const size_t uVoice = m_uVoice.load();
	if (uVoice != AudioNoVoice)
	{
		U::Audio.CloseVoice(uVoice);
		m_uVoice.store(AudioNoVoice);
	}

	if (m_tReader.joinable())
	{
		CancelSynchronousIo(m_tReader.native_handle());
		m_tReader.join();
	}

	Close();
	delete this;
}

void CAudioStream::Play()
{
	ConnectVoice();
}

AudioStreamError_t CAudioStream::ConnectVoice()
{
	if (m_uVoice.load() != AudioNoVoice)
		return AudioStreamError_t::None;

	m_uVoice.store(U::Audio.OpenVoice(m_uRate.load(), RenderVoice, this, m_flVolume.load()));
	return m_uVoice.load() == AudioNoVoice ? AudioStreamError_t::Thread : AudioStreamError_t::None;
}

bool CAudioStream::Playing() const
{
	const size_t uVoice = m_uVoice.load();
	return uVoice != AudioNoVoice && U::Audio.VoiceReady(uVoice);
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

	if (!m_bStalled.load() && GetTickCount64() < m_uLastDataTick.load() + StreamStallDelay)
		return AudioStreamState_t::Playing;

	return AudioStreamState_t::Stalled;
}

int CAudioStream::BufferPercent() const
{
	const uint32_t uCapacity = m_uCapacity.load(std::memory_order_acquire);
	if (!uCapacity)
		return 0;

	const uint32_t uRead = m_uRead.load();
	const uint32_t uWritten = m_uWritten.load();
	const uint32_t uUsed = uWritten - uRead;
	if (!uUsed || uUsed > uCapacity)
		return 0;

	return int(uUsed * 100u / uCapacity);
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
	return OpenConnection(sUrl);
}

AudioStreamError_t CAudioStream::OpenConnection(const char* sUrl)
{
	Close();
	ResetCodec();
	if (!sUrl || !*sUrl)
		return AudioStreamError_t::Url;
	if (Cancelled())
		return AudioStreamError_t::Connect;
	const int iLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, sUrl, -1, nullptr, 0);
	if (iLength <= 0)
		return AudioStreamError_t::Url;

	std::wstring sWide(iLength, L'\0');
	if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, sUrl, -1, sWide.data(), iLength))
		return AudioStreamError_t::Url;
	sWide.resize(iLength - 1);

	URL_COMPONENTS tParts = {};
	tParts.dwStructSize = sizeof(tParts);
	tParts.dwHostNameLength = DWORD(-1);
	tParts.dwUrlPathLength = DWORD(-1);
	tParts.dwExtraInfoLength = DWORD(-1);

	if (!WinHttpCrackUrl(sWide.c_str(), 0, 0, &tParts))
		return AudioStreamError_t::Url;
	if (Cancelled())
		return AudioStreamError_t::Connect;

	wchar_t sHost[256] = {};
	const size_t uHostLength = tParts.dwHostNameLength;
	if (!uHostLength || uHostLength >= std::size(sHost) || (!tParts.lpszUrlPath && !tParts.lpszExtraInfo) ||
		(tParts.nScheme != INTERNET_SCHEME_HTTPS && tParts.nScheme != INTERNET_SCHEME_HTTP))
		return AudioStreamError_t::Url;

	memcpy(sHost, tParts.lpszHostName, uHostLength * sizeof(wchar_t));

	m_pSession = WinHttpOpen(L"VLC/3.0.20 LibVLC/3.0.20", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!m_pSession || Cancelled())
		return AudioStreamError_t::Connect;

	WinHttpSetTimeouts(m_pSession, StreamConnectTimeout, StreamConnectTimeout, StreamReceiveTimeout, StreamReceiveTimeout);

	m_pConnection = WinHttpConnect(m_pSession, sHost, tParts.nPort, 0);
	if (!m_pConnection || Cancelled())
		return AudioStreamError_t::Connect;

	std::wstring sTarget = tParts.lpszUrlPath ? std::wstring(tParts.lpszUrlPath, tParts.dwUrlPathLength) : L"/";
	if (tParts.lpszExtraInfo)
		sTarget.append(tParts.lpszExtraInfo, tParts.dwExtraInfoLength);

	m_pRequest = WinHttpOpenRequest(m_pConnection, L"GET", sTarget.c_str(), nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, tParts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
	if (!m_pRequest || Cancelled())
		return AudioStreamError_t::Connect;

	if (!WinHttpSendRequest(m_pRequest, L"Accept: */*\r\nIcy-MetaData: 1\r\n", DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
		!WinHttpReceiveResponse(m_pRequest, nullptr) || Cancelled())
		return AudioStreamError_t::Connect;

	DWORD uStatus = 0, uSize = sizeof(uStatus);
	if (!WinHttpQueryHeaders(m_pRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &uStatus, &uSize, WINHTTP_NO_HEADER_INDEX) || uStatus != 200)
		return AudioStreamError_t::Status;

	wchar_t sType[128] = {};
	uSize = sizeof(sType);
	if (!WinHttpQueryHeaders(m_pRequest, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, sType, &uSize, WINHTTP_NO_HEADER_INDEX))
		return AudioStreamError_t::Format;
	wchar_t* sSemicolon = wcschr(sType, L';');
	if (sSemicolon)
		*sSemicolon = L'\0';

	DWORD uMetaint = 0;
	uSize = sizeof(uMetaint);
	const bool bMetadata = WinHttpQueryHeaders(m_pRequest, WINHTTP_QUERY_CUSTOM | WINHTTP_QUERY_FLAG_NUMBER,
		L"icy-metaint", &uMetaint, &uSize, WINHTTP_NO_HEADER_INDEX);
	if (!bMetadata)
		uMetaint = 0;
	std::wstring sTypeText(sType);
	sTypeText.erase(std::remove_if(sTypeText.begin(), sTypeText.end(), iswspace), sTypeText.end());
	if (ContentIsMp3(sTypeText.c_str()))
	{
		m_iCodec = Codec_t::Mp3;
		mp3dec_init(&m_tMp3);
	}
	else if (ContentIsOgg(sTypeText.c_str()))
	{
		m_iCodec = Codec_t::Vorbis;
		if (!m_pVorbisMemory)
			m_pVorbisMemory = static_cast<char*>(malloc(VorbisMemory));
		if (!m_pVorbisMemory)
			return AudioStreamError_t::Format;
	}
	else
		return AudioStreamError_t::Unsupported;
	m_pVorbis = nullptr;
	m_uInputSize = 0;
	m_uInputUsed = 0;
	m_uMetaint = uMetaint;
	m_uAudioLeft = uMetaint;

	m_uMetaLeft = 0;
	m_uMetaSize = 0;
	m_sMeta[0] = '\0';
	m_bStalled.store(false);
	return AudioStreamError_t::None;
}

bool CAudioStream::Cancelled() const
{
	return m_bClosing.load() || m_pCancel && m_pCancel->load();
}

void CAudioStream::ResetCodec()
{
	if (m_pVorbis)
	{
		stb_vorbis_close(m_pVorbis);
		m_pVorbis = nullptr;
	}

	m_uInputSize = 0;
	m_uInputUsed = 0;
	m_uMetaLeft = 0;
	m_uMetaSize = 0;
	m_sMeta[0] = '\0';
	m_bStalled.store(false);
}

AudioStreamError_t CAudioStream::Receive()
{
	const AudioStreamError_t iError = ReadAudioChunk();
	if (iError != AudioStreamError_t::None)
		return iError;

	m_uLastDataTick.store(GetTickCount64());
	return AudioStreamError_t::None;
}

AudioStreamError_t CAudioStream::ReadAudioChunk()
{
	uint8_t aChunk[4096] = {};
	DWORD uAvailable = 0, uReceived = 0;

	if (Cancelled() || !m_pRequest ||
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

		const AudioStreamError_t iError = DrainInput();
		if (iError != AudioStreamError_t::None)
			return iError;
	}

	return AudioStreamError_t::None;
}

AudioStreamError_t CAudioStream::DrainInput()
{
	return m_iCodec == Codec_t::Mp3 ? DecodeMp3() :
		DecodeVorbis();
}

AudioStreamError_t CAudioStream::DecodeMp3()
{
	float aPcm[MINIMP3_MAX_SAMPLES_PER_FRAME] = {};
	uint32_t uInvalid = 0;

	while (m_uInputSize != m_uInputUsed)
	{
		mp3dec_frame_info_t tInfo = {};
		const int iSamples = mp3dec_decode_frame(&m_tMp3, m_aInput + m_uInputUsed,
			int(m_uInputSize - m_uInputUsed), aPcm, &tInfo);
		if (!tInfo.frame_bytes)
			return AudioStreamError_t::None;

		uInvalid = tInfo.frame_bytes == 1 || tInfo.frame_bytes == 2 ? uInvalid + 1 : 0;
		if (uInvalid > 8)
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
		if (!uPending && m_pVorbis)
			return AudioStreamError_t::None;

		if (m_pVorbis && StartsOggStream(pData, uPending))
		{
			stb_vorbis_close(m_pVorbis);
			m_pVorbis = nullptr;
		}

		if (!m_pVorbis)
		{
			if (!uPending)
				return AudioStreamError_t::None;
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

		if (iUsed < 0)
		{
			stb_vorbis_close(m_pVorbis);
			m_pVorbis = nullptr;
			m_uInputUsed += uLimit;
			continue;
		}

		if (!iUsed && !iSamples)
		{
			if (uLimit == uPending)
				return AudioStreamError_t::None;

			m_uInputUsed += uLimit;
			continue;
		}

		m_uInputUsed += size_t(iUsed);
		if (iSamples && pOutput && !Push(pOutput[0], pOutput[iChannels > 1 ? 1 : 0], 1, uint32_t(iSamples),
			stb_vorbis_get_info(m_pVorbis).sample_rate))
			return AudioStreamError_t::Connect;
	}
}

bool CAudioStream::Push(const float* pLeft, const float* pRight, size_t uStride, uint32_t uFrames, uint32_t uRate)
{
	if (m_pRing && uRate != m_uRate.load())
		return false;
	if (!m_pRing && StartOutput(uRate) != AudioStreamError_t::None)
		return false;

	const uint32_t uCapacity = m_uCapacity.load(std::memory_order_relaxed);
	uint32_t uWritten = m_uWritten.load();
	while (uWritten + uFrames - m_uRead.load() > uCapacity - StreamSlackFrames)
	{
		if (Cancelled())
			return false;

		Sleep(StreamFullWait);
		uWritten = m_uWritten.load();
	}

	for (uint32_t i = 0; i < uFrames; i++)
	{
		float* pSlot = m_pRing + ((uWritten + i) & (uCapacity - 1)) * AudioChannels;
		pSlot[0] = pLeft[i * uStride];
		pSlot[1] = pRight[i * uStride];
	}

	m_uWritten.store(uWritten + uFrames);
	return true;
}

AudioStreamError_t CAudioStream::StartOutput(uint32_t uRate)
{
	const uint32_t uCurrentRate = m_uRate.load(std::memory_order_relaxed);
	if (m_pRing)
		return uRate == uCurrentRate ? AudioStreamError_t::None : AudioStreamError_t::Format;
	if (uRate < 8000 || uRate > 192000)
		return AudioStreamError_t::Format;

	const uint64_t uTarget = uint64_t(uRate) * StreamSeconds + StreamSlackFrames;
	uint32_t uCapacity = 1;
	while (uCapacity < uTarget)
		uCapacity <<= 1;

	float* pRing = static_cast<float*>(malloc(uCapacity * AudioChannels * sizeof(float)));
	if (!pRing)
		return AudioStreamError_t::Format;
	m_pRing = pRing;
	m_uRate.store(uRate, std::memory_order_relaxed);
	m_uCapacity.store(uCapacity, std::memory_order_release);
	return AudioStreamError_t::None;
}

void CAudioStream::Render(float* pOut, uint32_t uFrames)
{
	const uint32_t uRate = m_uRate.load(std::memory_order_relaxed);
	const uint32_t uCapacity = m_uCapacity.load(std::memory_order_acquire);
	const uint32_t uReadValue = m_uRead.load();
	uint32_t uAvailable = m_uWritten.load() - uReadValue;

	if (m_bStalled.load())
	{
		if (uAvailable < uRate && !m_bEnded.load())
			uAvailable = 0;
		else
			m_bStalled.store(false);
	}

	const uint32_t uCount = (std::min)(uAvailable, uFrames);
	for (uint32_t i = 0; i < uCount; i++)
	{
		const float* pSlot = m_pRing + ((uReadValue + i) & (uCapacity - 1)) * AudioChannels;
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
	m_uLastDataTick.store(GetTickCount64());
	while (!Cancelled() && Receive() == AudioStreamError_t::None)
	{
	}

	Close();
	m_bEnded.store(true);
}

void CAudioStream::RenderVoice(void* pUser, float* pOut, uint32_t uFrames)
{
	static_cast<CAudioStream*>(pUser)->Render(pOut, uFrames);
}
