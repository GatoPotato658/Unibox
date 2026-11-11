#pragma once
#include "../Macros/Macros.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>

using ThreadJob_t = void(*)(void* pContext, int iIndex, int iWorker);

class CThreadPool
{
public:
	static constexpr int MaxWorkers = 4;
	static constexpr int MaxSlots = MaxWorkers + 1;

	bool Startup(int iWorkers = 0);
	void Shutdown();

	bool Run(ThreadJob_t fJob, void* pContext, int iCount);

	template <class F>
	bool Run(int iCount, F&& fJob)
	{
		using Job_t = std::remove_reference_t<F>;
		return Run([](void* pContext, int iIndex, int iWorker) { (*static_cast<Job_t*>(pContext))(iIndex, iWorker); },
			const_cast<void*>(static_cast<const void*>(std::addressof(fJob))), iCount);
	}

	int Concurrency();

private:
	struct Run_t
	{
		ThreadJob_t m_fJob = nullptr;
		void* m_pContext = nullptr;
		int m_iCount = 0;
		std::atomic<int> m_iNext = 0;
		std::atomic<int> m_iRemaining = 0;
	};

	void WorkerLoop(int iWorker);
	void Serve(Run_t& tRun, int iWorker);
	void WaitZero(std::atomic<int>& iValue);
	void NotifyDone();

	Run_t m_tRun = {};
	std::atomic<Run_t*> m_pCurrent = nullptr;
	std::atomic<int> m_iActive = 0;
	std::atomic<bool> m_bBusy = false;
	std::atomic<bool> m_bStop = false;
	std::atomic<bool> m_bStarted = false;
	std::atomic<int> m_iWorkers = 0;
	std::atomic<unsigned> m_uGeneration = 0;

	std::mutex m_tMutex;
	std::condition_variable m_tWake;
	std::condition_variable m_tDone;
	std::thread m_aThreads[MaxWorkers];
};

ADD_FEATURE_CUSTOM(CThreadPool, ThreadPool, U);
