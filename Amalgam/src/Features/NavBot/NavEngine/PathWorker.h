#pragma once
#include "NavEngine.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace PathWorker
{
	struct CancellationToken
	{
		uint64_t m_uId = 0;
		std::shared_ptr<std::atomic_bool> m_pCancelled{};

		bool IsCancelled() const
		{
			return m_pCancelled && m_pCancelled->load(std::memory_order_relaxed);
		}
		void Cancel() const
		{
			if (m_pCancelled) m_pCancelled->store(true, std::memory_order_relaxed);
		}
	};

	struct PathRequest
	{
		uint64_t m_uRequestId = 0;
		uint64_t m_uWorldGeneration = 0;
		uint64_t m_uHazardGeneration = 0;
		CNavArea* m_pStartArea = nullptr;
		CNavArea* m_pDestArea = nullptr;
		Vector m_vStart{};
		Vector m_vDestination{};
		PriorityListEnum::PriorityListEnum m_ePriority = PriorityListEnum::None;
		bool m_bIgnoreTraces = false;
		bool m_bNavToLocal = true;
		SolveContext m_tCtx{};
		CancellationToken m_tToken{};
	};

	struct PathResult
	{
		uint64_t m_uRequestId = 0;
		uint64_t m_uWorldGeneration = 0;
		uint64_t m_uHazardGeneration = 0;
		Vector m_vDestination{};
		PriorityListEnum::PriorityListEnum m_ePriority = PriorityListEnum::None;
		bool m_bIgnoreTraces = false;
		bool m_bNavToLocal = true;
		int m_iSolveResult = -1;
		bool m_bCancelled = false;
		std::vector<CachedPathCrumb_t> m_vCrumbs;
	};

	class CPathWorker
	{
	public:
		~CPathWorker();
		void Start(CMap* pMap);
		void Stop();

		// Submitting a new request cancels any older pending one — only the newest is honored.
		CancellationToken Submit(PathRequest tRequest);

		void CancelAll();

		std::optional<PathResult> Poll();

	private:
		void WorkerMain();

		CMap* m_pMap = nullptr;
		std::atomic_bool m_bRunning{ false };
		std::thread m_tWorker;

		std::mutex m_mPending;
		std::condition_variable m_cvPending;
		std::optional<PathRequest> m_oPending;

		std::mutex m_mCompleted;
		std::vector<PathResult> m_vCompleted;
	};
} // namespace PathWorker
