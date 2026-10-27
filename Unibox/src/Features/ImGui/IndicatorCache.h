#pragma once
#include <mutex>

template <class T>
class CIndicatorCache
{
public:
	void Set(T tData)
	{
		std::lock_guard<std::mutex> tLock(m_tMutex);
		m_tData = std::move(tData);
	}

	T Get()
	{
		std::lock_guard<std::mutex> tLock(m_tMutex);
		return m_tData;
	}

private:
	T m_tData = {};
	std::mutex m_tMutex = {};
};
