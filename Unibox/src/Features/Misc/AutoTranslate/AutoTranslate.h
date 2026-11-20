#pragma once
#include "../../../SDK/SDK.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class CAutoTranslate
{
public:
	void OnChatMessage(int iEntIndex, const std::string& sName, const std::string& sMsg);
	void Run();
	void OnLevelShutdown();
	void Shutdown();

private:
	struct TranslateJob_t
	{
		int m_iEntIndex = 0;
		std::string m_sName;
		std::string m_sMsg;
		std::string m_sTargetLang;
		std::string m_sApiKey;
	};

	struct TranslateResult_t
	{
		int m_iEntIndex = 0;
		std::string m_sName;
		std::string m_sTranslated;
		std::string m_sSourceLang;
	};

	struct CachedTranslation_t
	{
		std::string m_sTranslated;
		std::string m_sSourceLang;
	};

	void EnsureWorker();
	void WorkerLoop();
	void PushResult(int iEntIndex, const std::string& sName, const std::string& sTranslated, const std::string& sSourceLang);

	std::mutex m_tJobMutex;
	std::condition_variable m_tJobCv;
	std::deque<TranslateJob_t> m_vPendingJobs;

	std::mutex m_tResultMutex;
	std::vector<TranslateResult_t> m_vCompletedResults;

	std::mutex m_tCacheMutex;
	std::unordered_map<std::string, CachedTranslation_t> m_mCache;
	std::deque<std::string> m_vCacheOrder;

	std::thread m_tWorker;
	std::atomic<bool> m_bStop = false;
	bool m_bWorkerStarted = false;
};

ADD_FEATURE(CAutoTranslate, AutoTranslate);
