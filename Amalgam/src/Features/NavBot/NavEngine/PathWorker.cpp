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
		m_cvPending.notify_all();
		if (m_tWorker.joinable())
			m_tWorker.join();

		{
			std::lock_guard lock(m_mPending);
			if (m_oPending) m_oPending->m_tToken.Cancel();
			m_oPending.reset();
		}
		{
			std::lock_guard lock(m_mCompleted);
			m_vCompleted.clear();
		}
		m_pMap = nullptr;
	}

	CancellationToken CPathWorker::Submit(PathRequest tRequest)
	{
		tRequest.m_tToken.m_uId = tRequest.m_uRequestId;
		tRequest.m_tToken.m_pCancelled = std::make_shared<std::atomic_bool>(false);
		CancellationToken tToken = tRequest.m_tToken;

		{
			std::lock_guard lock(m_mPending);
			if (m_oPending) m_oPending->m_tToken.Cancel();
			m_oPending = std::move(tRequest);
		}
		m_cvPending.notify_one();
		return tToken;
	}

	void CPathWorker::CancelAll()
	{
		std::lock_guard lock(m_mPending);
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
			}

			if (tRequest.m_tToken.IsCancelled() || !m_pMap)
			{
				PathResult tResult{};
				tResult.m_uRequestId        = tRequest.m_uRequestId;
				tResult.m_uWorldGeneration  = tRequest.m_uWorldGeneration;
				tResult.m_uHazardGeneration = tRequest.m_uHazardGeneration;
				tResult.m_vDestination      = tRequest.m_vDestination;
				tResult.m_ePriority         = tRequest.m_ePriority;
				tResult.m_bIgnoreTraces     = tRequest.m_bIgnoreTraces;
				tResult.m_bNavToLocal       = tRequest.m_bNavToLocal;
				tResult.m_iSolveResult      = -1;
				tResult.m_bCancelled        = true;

				std::lock_guard lock(m_mCompleted);
				m_vCompleted.push_back(std::move(tResult));
				continue;
			}

			std::vector<CNavArea*> vPath;
			int iResult = -1;
			{
				std::lock_guard lock(m_pMap->m_mutex);
				if (!tRequest.m_tToken.IsCancelled())
					iResult = m_pMap->Solve(tRequest.m_pStartArea, tRequest.m_pDestArea, tRequest.m_tCtx, vPath, nullptr);
			}

			PathResult tResult{};
			tResult.m_uRequestId        = tRequest.m_uRequestId;
			tResult.m_uWorldGeneration  = tRequest.m_uWorldGeneration;
			tResult.m_uHazardGeneration = tRequest.m_uHazardGeneration;
			tResult.m_vDestination      = tRequest.m_vDestination;
			tResult.m_ePriority         = tRequest.m_ePriority;
			tResult.m_bIgnoreTraces     = tRequest.m_bIgnoreTraces;
			tResult.m_bNavToLocal       = tRequest.m_bNavToLocal;
			tResult.m_iSolveResult      = iResult;
			tResult.m_bCancelled        = tRequest.m_tToken.IsCancelled();
			tResult.m_vPath             = std::move(vPath);

			std::lock_guard lock(m_mCompleted);
			while (m_vCompleted.size() >= 4)
				m_vCompleted.erase(m_vCompleted.begin());
			m_vCompleted.push_back(std::move(tResult));
		}
	}
} // namespace PathWorker
