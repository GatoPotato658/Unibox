#pragma once

// I dont remember why exactly i added this
#define NTAB 32
class CValve_Random final
{
public:
	void SetSeed(const int iSeed);
	float RandomFloat(const float flMinVal = -1.f, const float flMaxVal = 1.f);
	float RandomFloatExp(const float flMinVal = 0.f, const float flMaxVal = 1.f, const float flExponent = 1.f);
	int RandomInt(const int iMinVal = 0, const int iMaxVal = 0x7FFF);
	//Vec3 RandomAngle(const float flMinVal = -1.f, const float flMaxVal = 1.f);
private:
	int	GenerateRandomNumber();

	int m_idum;
	int m_iy;
	int m_iv[NTAB];
};