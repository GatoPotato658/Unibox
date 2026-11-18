#pragma once

#include <atomic>
#include <new>
#include <type_traits>
#include <utility>

#ifdef _WIN32
#include "../../../SDK/Definitions/Interfaces/IMaterialSystem.h"

class CFunctor : public IRefCounted
{
public:
	virtual ~CFunctor() = default;
	virtual void operator()() = 0;

	unsigned m_nUserID = 0;
};

class ICallQueue
{
public:
	void QueueFunctor(CFunctor* pFunctor)
	{
		pFunctor->AddRef();
		QueueFunctorInternal(pFunctor);
	}

private:
	virtual void QueueFunctorInternal(CFunctor* pFunctor) = 0;
};

namespace RenderQueue
{
	template <typename F>
	class CRenderCall final : public CFunctor
	{
	public:
		explicit CRenderCall(F&& call) : m_Call(std::move(call)) {}

		int AddRef() override
		{
			return m_nReferences.fetch_add(1, std::memory_order_relaxed) + 1;
		}

		int Release() override
		{
			const int nReferences = m_nReferences.fetch_sub(1, std::memory_order_acq_rel) - 1;
			if (!nReferences)
				delete this;
			return nReferences;
		}

		void operator()() override
		{
			m_Call();
		}

	private:
		std::atomic<int> m_nReferences = 1;
		F m_Call;
	};
}

template <typename F>
bool QueueRenderCall(IMatRenderContext* pContext, F call)
{
	if (!pContext)
		return false;

	if (auto pQueue = pContext->GetCallQueue())
	{
		auto pFunctor = new (std::nothrow) RenderQueue::CRenderCall<std::decay_t<F>>(std::move(call));
		if (!pFunctor)
			return false;
		pQueue->QueueFunctor(pFunctor);
		pFunctor->Release();
	}
	else
		call();

	return true;
}
#else
class IMatRenderContext;

template <typename F>
bool QueueRenderCall(IMatRenderContext*, F)
{
	return false;
}
#endif
