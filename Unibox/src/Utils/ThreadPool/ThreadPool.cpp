#include "ThreadPool.h"

#include <algorithm>
#include <system_error>

static constexpr int SpinBeforePark = 128;

bool CThreadPool::Startup(int iWorkers)
{
	std::lock_guard tLifecycleLock(m_tLifecycleMutex);
	if (m_bStarted.exchange(true, std::memory_order_acq_rel))
		return m_iWorkers.load(std::memory_order_acquire) > 0;

	m_bStop.store(false, std::memory_order_release);
	if (iWorkers <= 0)
		iWorkers = std::clamp(int(std::thread::hardware_concurrency()) / 2 - 1, 1, MaxWorkers);
	iWorkers = std::min(iWorkers, MaxWorkers);

	for (int i = 0; i < iWorkers; i++)
	{
		try
		{
			m_aThreads[i] = std::thread(&CThreadPool::WorkerLoop, this, i + 1);
		}
		catch (const std::system_error&)
		{
			break;
		}
		m_iWorkers.store(i + 1, std::memory_order_release);
	}

	if (m_iWorkers.load(std::memory_order_acquire) > 0)
		return true;

	m_bStarted.store(false, std::memory_order_release);
	return false;
}

void CThreadPool::Shutdown()
{
	std::lock_guard tLifecycleLock(m_tLifecycleMutex);
	if (!m_bStarted.exchange(false, std::memory_order_acq_rel))
		return;

	{
		std::lock_guard tLock(m_tMutex);
		m_bStop.store(true, std::memory_order_release);
	}
	m_tWake.notify_all();

	for (auto& tThread : m_aThreads)
	{
		if (tThread.joinable())
			tThread.join();
	}
	m_iWorkers.store(0, std::memory_order_release);
}

int CThreadPool::Concurrency()
{
	if (m_bStop.load(std::memory_order_acquire))
		return 1;
	if (!m_bStarted.load(std::memory_order_acquire))
		Startup();
	return m_iWorkers.load(std::memory_order_acquire) + 1;
}

bool CThreadPool::Run(ThreadJob_t fJob, void* pContext, int iCount)
{
	if (iCount <= 0)
		return true;
	if (!fJob)
		return false;

	if (iCount == 1 || Concurrency() < 2 || m_bBusy.exchange(true, std::memory_order_acq_rel))
	{
		for (int i = 0; i < iCount; i++)
			fJob(pContext, i, 0);
		return true;
	}

	WaitZero(m_iActive);

	m_tRun.m_fJob = fJob;
	m_tRun.m_pContext = pContext;
	m_tRun.m_iCount = iCount;
	m_tRun.m_iNext.store(0, std::memory_order_relaxed);
	m_tRun.m_iRemaining.store(iCount, std::memory_order_relaxed);
	m_pCurrent.store(&m_tRun, std::memory_order_release);

	{
		std::lock_guard tLock(m_tMutex);
		m_uGeneration.fetch_add(1, std::memory_order_acq_rel);
	}
	m_tWake.notify_all();

	Serve(m_tRun, 0);
	WaitZero(m_tRun.m_iRemaining);

	m_pCurrent.store(nullptr, std::memory_order_release);
	m_bBusy.store(false, std::memory_order_release);
	return true;
}

void CThreadPool::Serve(Run_t& tRun, int iWorker)
{
	for (;;)
	{
		const int iIndex = tRun.m_iNext.fetch_add(1, std::memory_order_acq_rel);
		if (iIndex >= tRun.m_iCount)
			return;

		tRun.m_fJob(tRun.m_pContext, iIndex, iWorker);
		if (tRun.m_iRemaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
			NotifyDone();
	}
}

void CThreadPool::WorkerLoop(int iWorker)
{
	unsigned uSeen = 0;
	for (;;)
	{
		int iSpins = 0;
		while (m_uGeneration.load(std::memory_order_acquire) == uSeen && !m_bStop.load(std::memory_order_acquire))
		{
			if (iSpins++ < SpinBeforePark)
			{
				std::this_thread::yield();
				continue;
			}

			std::unique_lock tLock(m_tMutex);
			m_tWake.wait(tLock, [&]() { return m_uGeneration.load(std::memory_order_acquire) != uSeen || m_bStop.load(std::memory_order_acquire); });
		}

		if (m_bStop.load(std::memory_order_acquire))
			return;

		uSeen = m_uGeneration.load(std::memory_order_acquire);

		m_iActive.fetch_add(1, std::memory_order_acq_rel);
		if (auto pRun = m_pCurrent.load(std::memory_order_acquire))
			Serve(*pRun, iWorker);
		if (m_iActive.fetch_sub(1, std::memory_order_acq_rel) == 1)
			NotifyDone();
	}
}

void CThreadPool::WaitZero(std::atomic<int>& iValue)
{
	for (int iSpins = 0; iValue.load(std::memory_order_acquire); iSpins++)
	{
		if (iSpins < SpinBeforePark)
		{
			std::this_thread::yield();
			continue;
		}

		std::unique_lock tLock(m_tMutex);
		m_tDone.wait(tLock, [&]() { return !iValue.load(std::memory_order_acquire); });
	}
}

void CThreadPool::NotifyDone()
{
	{
		std::lock_guard tLock(m_tMutex);
	}
	m_tDone.notify_all();
}
