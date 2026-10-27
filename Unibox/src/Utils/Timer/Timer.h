#pragma once

class Timer
{
private:
	float m_flLast = 0.f;

public:
	Timer();
	bool Check(float flS) const;
	bool Run(float flS);
	void Update();
	float GetLastUpdate() const { return m_flLast; }

	inline void operator-=(float flS)
    {
		m_flLast -= flS;
    }

	inline void operator+=(float flS)
    {
        m_flLast += flS;
    }
};