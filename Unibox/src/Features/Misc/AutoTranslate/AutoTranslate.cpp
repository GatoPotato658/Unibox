#include "AutoTranslate.h"

#include <WinHttp.h>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <format>
#include <iterator>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

static constexpr size_t kMaxPendingJobs = 16;
static constexpr size_t kMaxCacheEntries = 128;
static constexpr size_t kMaxResponseBytes = 64 * 1024;
static constexpr int kConnectTimeoutMs = 3000;
static constexpr int kTransferTimeoutMs = 4000;

namespace
{
	struct ScopedWinHttp
	{
		HINTERNET m_hHandle = nullptr;
		~ScopedWinHttp()
		{
			if (m_hHandle)
				WinHttpCloseHandle(m_hHandle);
		}
		operator HINTERNET() const { return m_hHandle; }
	};

	std::string Trim(const std::string& sInput)
	{
		size_t iStart = 0;
		while (iStart < sInput.size() && static_cast<unsigned char>(sInput[iStart]) <= ' ')
			++iStart;
		size_t iEnd = sInput.size();
		while (iEnd > iStart && static_cast<unsigned char>(sInput[iEnd - 1]) <= ' ')
			--iEnd;
		return sInput.substr(iStart, iEnd - iStart);
	}

	std::string CleanChatText(const std::string& sInput)
	{
		std::string sOut;
		sOut.reserve(sInput.size());

		for (size_t i = 0; i < sInput.size(); ++i)
		{
			const unsigned char c = static_cast<unsigned char>(sInput[i]);
			if (c == 0x07 && i + 6 < sInput.size())
			{
				i += 6;
				continue;
			}
			if (c == 0x08 && i + 8 < sInput.size())
			{
				i += 8;
				continue;
			}
			if (c < 0x20)
			{
				if (c == '\t' || c == '\n' || c == '\r')
					sOut.push_back(' ');
				continue;
			}
			sOut.push_back(static_cast<char>(c));
		}

		return Trim(sOut);
	}

	bool HasTranslatableText(const std::string& sText)
	{
		for (unsigned char c : sText)
		{
			if (c >= 0x80 || std::isalpha(c))
				return true;
		}
		return false;
	}

	std::string ToLowerAscii(std::string sText)
	{
		for (char& c : sText)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return sText;
	}

	bool IsSameLanguage(const std::string& sSource, const std::string& sTarget)
	{
		if (sSource.empty() || sTarget.empty())
			return false;

		const std::string sA = ToLowerAscii(sSource);
		const std::string sB = ToLowerAscii(sTarget);
		if (sA == sB)
			return true;

		const size_t iDashA = sA.find('-');
		const size_t iDashB = sB.find('-');
		const std::string sBaseA = iDashA == std::string::npos ? sA : sA.substr(0, iDashA);
		const std::string sBaseB = iDashB == std::string::npos ? sB : sB.substr(0, iDashB);
		if (sBaseA == "zh" && sBaseB == "zh")
			return false;
		return sBaseA == sBaseB;
	}

	bool EqualsIgnoreCaseTrimmed(const std::string& sA, const std::string& sB)
	{
		return ToLowerAscii(Trim(sA)) == ToLowerAscii(Trim(sB));
	}

	std::string UrlEncode(const std::string& sInput)
	{
		static constexpr char kHex[] = "0123456789ABCDEF";
		std::string sOut;
		sOut.reserve(sInput.size() * 3);

		for (unsigned char c : sInput)
		{
			if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
				|| c == '-' || c == '_' || c == '.' || c == '~')
			{
				sOut.push_back(static_cast<char>(c));
			}
			else
			{
				sOut.push_back('%');
				sOut.push_back(kHex[(c >> 4) & 0x0F]);
				sOut.push_back(kHex[c & 0x0F]);
			}
		}

		return sOut;
	}

	std::wstring Utf8ToWide(const std::string& sInput)
	{
		if (sInput.empty())
			return {};

		const int iLen = MultiByteToWideChar(CP_UTF8, 0, sInput.data(), static_cast<int>(sInput.size()), nullptr, 0);
		if (iLen <= 0)
			return {};

		std::wstring wsOut(static_cast<size_t>(iLen), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, sInput.data(), static_cast<int>(sInput.size()), wsOut.data(), iLen);
		return wsOut;
	}

	bool HttpRequest(const wchar_t* sHost, const std::wstring& wsPath, bool bPost, const std::string& sBody, std::string& sResponseOut)
	{
		sResponseOut.clear();

		ScopedWinHttp hSession{ WinHttpOpen(L"Unibox/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
		if (!hSession)
			return false;

		WinHttpSetTimeouts(hSession, kConnectTimeoutMs, kConnectTimeoutMs, kTransferTimeoutMs, kTransferTimeoutMs);

		ScopedWinHttp hConnect{ WinHttpConnect(hSession, sHost, INTERNET_DEFAULT_HTTPS_PORT, 0) };
		if (!hConnect)
			return false;

		ScopedWinHttp hRequest{ WinHttpOpenRequest(hConnect, bPost ? L"POST" : L"GET", wsPath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) };
		if (!hRequest)
			return false;

		const wchar_t* sHeaders = bPost ? L"Content-Type: application/x-www-form-urlencoded\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS;
		const DWORD dwHeadersLen = bPost ? static_cast<DWORD>(-1) : 0;
		void* pBody = bPost && !sBody.empty() ? const_cast<char*>(sBody.data()) : WINHTTP_NO_REQUEST_DATA;
		const DWORD dwBodyLen = bPost ? static_cast<DWORD>(sBody.size()) : 0;

		if (!WinHttpSendRequest(hRequest, sHeaders, dwHeadersLen, pBody, dwBodyLen, dwBodyLen, 0))
			return false;

		if (!WinHttpReceiveResponse(hRequest, nullptr))
			return false;

		DWORD dwStatusCode = 0;
		DWORD dwStatusSize = sizeof(dwStatusCode);
		if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwStatusSize, WINHTTP_NO_HEADER_INDEX))
			return false;
		if (dwStatusCode != 200)
			return false;

		DWORD dwAvailable = 0;
		while (WinHttpQueryDataAvailable(hRequest, &dwAvailable) && dwAvailable > 0)
		{
			if (sResponseOut.size() >= kMaxResponseBytes)
				break;

			const size_t uToRead = (std::min)(static_cast<size_t>(dwAvailable), kMaxResponseBytes - sResponseOut.size());
			const size_t uOffset = sResponseOut.size();
			sResponseOut.resize(uOffset + uToRead);

			DWORD dwRead = 0;
			if (!WinHttpReadData(hRequest, sResponseOut.data() + uOffset, static_cast<DWORD>(uToRead), &dwRead))
				return false;

			sResponseOut.resize(uOffset + dwRead);
		}

		return !sResponseOut.empty();
	}

	bool ParseGtxResponse(const std::string& sJson, std::string& sTranslatedOut, std::string& sSourceLangOut)
	{
		try
		{
			std::istringstream tStream(sJson);
			boost::property_tree::ptree tRoot;
			boost::property_tree::read_json(tStream, tRoot);

			if (tRoot.empty())
				return false;

			const auto& tSegments = tRoot.begin()->second;
			std::string sCombined;
			for (const auto& [sKey, tSegment] : tSegments)
			{
				if (!tSegment.empty())
					sCombined += tSegment.begin()->second.get_value<std::string>("");
			}

			sTranslatedOut = Trim(sCombined);
			if (sTranslatedOut.empty())
				return false;

			if (tRoot.size() > 2)
			{
				const auto itLang = std::next(tRoot.begin(), 2);
				sSourceLangOut = itLang->second.get_value<std::string>("");
				if (sSourceLangOut == "null")
					sSourceLangOut.clear();
			}
			else
			{
				sSourceLangOut.clear();
			}

			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool ParseCloudV2Response(const std::string& sJson, std::string& sTranslatedOut, std::string& sSourceLangOut)
	{
		try
		{
			std::istringstream tStream(sJson);
			boost::property_tree::ptree tRoot;
			boost::property_tree::read_json(tStream, tRoot);

			const auto& tTranslations = tRoot.get_child("data.translations");
			if (tTranslations.empty())
				return false;

			const auto& tFirst = tTranslations.begin()->second;
			sTranslatedOut = Trim(tFirst.get<std::string>("translatedText", ""));
			sSourceLangOut = tFirst.get<std::string>("detectedSourceLanguage", "");
			return !sTranslatedOut.empty();
		}
		catch (...)
		{
			return false;
		}
	}

	bool TranslateText(const std::string& sText, const std::string& sTargetLang, const std::string& sApiKey, std::string& sTranslatedOut, std::string& sSourceLangOut)
	{
		const std::string sTarget = Trim(sTargetLang).empty() ? std::string("en") : Trim(sTargetLang);
		const std::string sKey = Trim(sApiKey);
		std::string sResponse;

		if (!sKey.empty())
		{
			const std::wstring wsPath = Utf8ToWide("/language/translate/v2?key=" + UrlEncode(sKey));
			const std::string sBody = "q=" + UrlEncode(sText) + "&target=" + UrlEncode(sTarget) + "&format=text";
			if (!HttpRequest(L"translation.googleapis.com", wsPath, true, sBody, sResponse))
				return false;
			return ParseCloudV2Response(sResponse, sTranslatedOut, sSourceLangOut);
		}

		const std::wstring wsPath = Utf8ToWide("/translate_a/single?client=gtx&sl=auto&tl=" + UrlEncode(sTarget) + "&dt=t&q=" + UrlEncode(sText));
		if (!HttpRequest(L"translate.googleapis.com", wsPath, false, {}, sResponse))
			return false;
		return ParseGtxResponse(sResponse, sTranslatedOut, sSourceLangOut);
	}
}

void CAutoTranslate::OnChatMessage(int iEntIndex, const std::string& sName, const std::string& sMsg)
{
	if (!Vars::Misc::Automation::AutoTranslate::Enable.Value)
		return;

	if (Vars::Misc::Automation::AutoTranslate::IgnoreLocal.Value && I::EngineClient && iEntIndex == I::EngineClient->GetLocalPlayer())
		return;

	const std::string sCleanMsg = CleanChatText(sMsg);
	if (sCleanMsg.empty() || !HasTranslatableText(sCleanMsg))
		return;

	const std::string sTargetLang = Trim(Vars::Misc::Automation::AutoTranslate::TargetLanguage.Value).empty()
		? std::string("en")
		: Trim(Vars::Misc::Automation::AutoTranslate::TargetLanguage.Value);
	const std::string sApiKey = Trim(Vars::Misc::Automation::AutoTranslate::ApiKey.Value);
	const std::string sCacheKey = sTargetLang + '\0' + sCleanMsg;

	{
		std::lock_guard tLock(m_tCacheMutex);
		if (const auto it = m_mCache.find(sCacheKey); it != m_mCache.end())
		{
			if (!IsSameLanguage(it->second.m_sSourceLang, sTargetLang)
				&& !EqualsIgnoreCaseTrimmed(it->second.m_sTranslated, sCleanMsg))
			{
				PushResult(iEntIndex, sName, it->second.m_sTranslated, it->second.m_sSourceLang);
			}
			return;
		}
	}

	EnsureWorker();

	{
		std::lock_guard tLock(m_tJobMutex);
		if (m_vPendingJobs.size() >= kMaxPendingJobs)
			m_vPendingJobs.pop_front();
		m_vPendingJobs.push_back({ iEntIndex, sName, sCleanMsg, sTargetLang, sApiKey });
	}
	m_tJobCv.notify_one();
}

void CAutoTranslate::Run()
{
	std::vector<TranslateResult_t> vResults;
	{
		std::lock_guard tLock(m_tResultMutex);
		if (m_vCompletedResults.empty())
			return;
		vResults.swap(m_vCompletedResults);
	}

	if (!I::EngineClient || !I::EngineClient->IsInGame() || !I::ClientModeShared || !I::ClientModeShared->m_pChatElement)
		return;

	const std::string sAccent = Vars::Menu::Theme::Accent.Value.ToHex();
	for (const auto& tResult : vResults)
	{
		const std::string sTag = tResult.m_sSourceLang.empty() ? std::string("TR") : tResult.m_sSourceLang;
		std::string sLine;
		if (!tResult.m_sName.empty())
			sLine = std::format("{}[{}] \x3{}\x1: {}", sAccent, sTag, tResult.m_sName, tResult.m_sTranslated);
		else
			sLine = std::format("{}[{}] \x1{}", sAccent, sTag, tResult.m_sTranslated);

		I::ClientModeShared->m_pChatElement->ChatPrintf(tResult.m_iEntIndex, "%s", sLine.c_str());
	}
}

void CAutoTranslate::OnLevelShutdown()
{
	{
		std::lock_guard tLock(m_tJobMutex);
		m_vPendingJobs.clear();
	}
	{
		std::lock_guard tLock(m_tResultMutex);
		m_vCompletedResults.clear();
	}
}

void CAutoTranslate::Shutdown()
{
	m_bStop = true;
	m_tJobCv.notify_all();
	if (m_tWorker.joinable())
		m_tWorker.join();
	m_bWorkerStarted = false;
	OnLevelShutdown();
}

void CAutoTranslate::EnsureWorker()
{
	if (m_bWorkerStarted)
		return;

	m_bStop = false;
	m_tWorker = std::thread(&CAutoTranslate::WorkerLoop, this);
	m_bWorkerStarted = true;
}

void CAutoTranslate::PushResult(int iEntIndex, const std::string& sName, const std::string& sTranslated, const std::string& sSourceLang)
{
	std::lock_guard tLock(m_tResultMutex);
	m_vCompletedResults.push_back({ iEntIndex, sName, sTranslated, sSourceLang });
}

void CAutoTranslate::WorkerLoop()
{
	while (true)
	{
		TranslateJob_t tJob;
		{
			std::unique_lock tLock(m_tJobMutex);
			m_tJobCv.wait(tLock, [this]() { return m_bStop.load() || !m_vPendingJobs.empty(); });
			if (m_bStop.load())
				break;

			tJob = std::move(m_vPendingJobs.front());
			m_vPendingJobs.pop_front();
		}

		std::string sTranslated;
		std::string sSourceLang;
		if (!TranslateText(tJob.m_sMsg, tJob.m_sTargetLang, tJob.m_sApiKey, sTranslated, sSourceLang))
			continue;

		const std::string sCacheKey = tJob.m_sTargetLang + '\0' + tJob.m_sMsg;
		{
			std::lock_guard tLock(m_tCacheMutex);
			if (m_mCache.find(sCacheKey) == m_mCache.end())
			{
				if (m_vCacheOrder.size() >= kMaxCacheEntries)
				{
					m_mCache.erase(m_vCacheOrder.front());
					m_vCacheOrder.pop_front();
				}
				m_vCacheOrder.push_back(sCacheKey);
			}
			m_mCache[sCacheKey] = { sTranslated, sSourceLang };
		}

		if (IsSameLanguage(sSourceLang, tJob.m_sTargetLang))
			continue;
		if (EqualsIgnoreCaseTrimmed(sTranslated, tJob.m_sMsg))
			continue;

		PushResult(tJob.m_iEntIndex, tJob.m_sName, sTranslated, sSourceLang);
	}
}
