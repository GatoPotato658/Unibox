#pragma once
#include "../../SDK/SDK.h"
#include "IndicatorCache.h"
#include <atomic>
#include <functional>

class CRenderSync
{
private:
	class CQueuedCall
	{
	public:
		explicit CQueuedCall(std::function<void()> fCall) : m_fCall(std::move(fCall)) { s_iPending++; }

		virtual int AddRef() { return ++m_iRefs; }
		virtual int Release()
		{
			const int iRefs = --m_iRefs;
			if (!iRefs)
				delete this;
			return iRefs;
		}
		virtual ~CQueuedCall() { s_iPending--; }
		virtual void operator()()
		{
			if (!G::Unload)
				m_fCall();
		}

		static inline std::atomic<int> s_iPending = 0;

	private:
		std::function<void()> m_fCall;
		std::atomic<int> m_iRefs = 0;
	};

	static constexpr size_t GetCallQueueIndex = 147;
	static constexpr size_t QueueFunctorInternalIndex = 0;

public:
	void Queue(std::function<void()> fCall)
	{
		IMatRenderContext* pContext = I::MaterialSystem ? I::MaterialSystem->GetRenderContext() : nullptr;
		void* pQueue = pContext ? U::Memory.CallVirtual<GetCallQueueIndex, void*>(pContext) : nullptr;
		if (!pQueue)
		{
			if (pContext)
				pContext->Release();
			fCall();
			return;
		}

		auto pCall = new CQueuedCall(std::move(fCall));
		pCall->AddRef();
		U::Memory.CallVirtual<QueueFunctorInternalIndex, void>(pQueue, static_cast<void*>(pCall));
		pContext->Release();
	}

	template <class T>
	void Set(CIndicatorCache<T>& tCache, T tData)
	{
		Queue([&tCache, tData = std::move(tData)]() mutable { tCache.Set(std::move(tData)); });
	}

	void WaitIdle(int iTimeoutMs)
	{
		for (int i = 0; CQueuedCall::s_iPending > 0 && i < iTimeoutMs; i += 5)
			Sleep(5);
	}
};

ADD_FEATURE(CRenderSync, RenderSync);
