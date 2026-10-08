#include "PathWorker.h"

namespace PathWorker
{
	CPathWorker::~CPathWorker()
	{
		Stop();
	}

	void CPathWorker::Start(CMap* pMap)
	{
		Stop();

		m_pMap = pMap;
		m_bRunning.store(true, std::memory_order_release);
		m_tWorker = std::thread(&CPathWorker::WorkerMain, this);
	}

	void CPathWorker::Stop()
	{
		m_bRunning.store(false, std::memory_order_release);
		{
			std::lock_guard lock(m_mPending);
			if (m_oPending) m_oPending->m_tToken.Cancel();
			if (m_pActiveCancellation) m_pActiveCancellation->store(true, std::memory_order_relaxed);
		}
		m_cvPending.notify_all();
		if (m_tWorker.joinable())
			m_tWorker.join();

		{
			std::lock_guard lock(m_mPending);
			m_oPending.reset();
			m_pActiveCancellation.reset();
		}
		{
			std::lock_guard lock(m_mCompleted);
			m_vCompleted.clear();
		}
		m_pMap = nullptr;
	}

	void CPathWorker::Submit(PathRequest tRequest)
	{
		tRequest.m_tToken.m_pCancelled = std::make_shared<std::atomic_bool>(false);

		{
			std::lock_guard lock(m_mPending);
			if (m_pActiveCancellation) m_pActiveCancellation->store(true, std::memory_order_relaxed);
			if (m_oPending) m_oPending->m_tToken.Cancel();
			m_oPending = std::move(tRequest);
		}
		m_cvPending.notify_one();
	}

	void CPathWorker::CancelAll()
	{
		std::lock_guard lock(m_mPending);
		if (m_pActiveCancellation) m_pActiveCancellation->store(true, std::memory_order_relaxed);
		if (m_oPending) m_oPending->m_tToken.Cancel();
		m_oPending.reset();
	}

	std::optional<PathResult> CPathWorker::Poll()
	{
		std::lock_guard lock(m_mCompleted);
		if (m_vCompleted.empty()) return std::nullopt;
		PathResult tResult = std::move(m_vCompleted.front());
		m_vCompleted.erase(m_vCompleted.begin());
		return tResult;
	}

	void CPathWorker::WorkerMain()
	{
		while (m_bRunning.load(std::memory_order_acquire))
		{
			PathRequest tRequest;
			{
				std::unique_lock lock(m_mPending);
				m_cvPending.wait(lock, [this]
					{ return !m_bRunning.load(std::memory_order_acquire) || m_oPending.has_value(); });
				if (!m_bRunning.load(std::memory_order_acquire)) return;
				if (!m_oPending) continue;
				tRequest = std::move(*m_oPending);
				m_oPending.reset();
				m_pActiveCancellation = tRequest.m_tToken.m_pCancelled;
			}

			PathResult tResult{};
			tResult.m_uRequestId = tRequest.m_uRequestId;
			tResult.m_uWorldGeneration = tRequest.m_uWorldGeneration;
			tResult.m_ePriority = tRequest.m_ePriority;

			if (m_pMap && !tRequest.m_tToken.IsCancelled())
			{
				tRequest.m_tCtx.m_pCancel = tRequest.m_tToken.m_pCancelled.get();
				std::lock_guard lock(m_pMap->m_mutex);
				if (!tRequest.m_tToken.IsCancelled())
					tResult.m_iSolveResult = m_pMap->SolveCrumbs(tRequest.m_vStart, tRequest.m_pStartArea, tRequest.m_vDestination,
						tRequest.m_pDestArea, tRequest.m_tCtx, tResult.m_vCrumbs, nullptr);
			}
			tResult.m_bCancelled = !m_pMap || tRequest.m_tToken.IsCancelled();

			{
				std::lock_guard lock(m_mPending);
				m_pActiveCancellation.reset();
			}
			std::lock_guard lock(m_mCompleted);
			while (m_vCompleted.size() >= 4)
				m_vCompleted.erase(m_vCompleted.begin());
			m_vCompleted.push_back(std::move(tResult));
		}
	}
}
