#pragma once
#include <memory>
#include <mutex>

template <class T>
class CIndicatorCache
{
public:
	using DataPtr = std::shared_ptr<const T>;

	CIndicatorCache() : m_pData(std::make_shared<T>()) {}

	void Set(T tData)
	{
		DataPtr pData = std::make_shared<T>(std::move(tData));
		std::lock_guard<std::mutex> tLock(m_tMutex);
		m_pData = std::move(pData);
	}

	DataPtr Get() const
	{
		std::lock_guard<std::mutex> tLock(m_tMutex);
		return m_pData;
	}

private:
	DataPtr m_pData;
	mutable std::mutex m_tMutex = {};
};
