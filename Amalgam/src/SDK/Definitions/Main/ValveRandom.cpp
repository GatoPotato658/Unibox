#pragma fp_contract(off)
//========= Copyright Valve Corporation, All rights reserved. ============//
//fuck you
// Purpose: Random number generator
//
// $Workfile: $
// $NoKeywords: $
//===========================================================================//


#include "ValveRandom.h"
#include <cmath>

#define IA 16807
#define IM 2147483647
#define IQ 127773
#define IR 2836
#define NDIV (1+(IM-1)/NTAB)
#define MAX_RANDOM_RANGE 0x7FFFFFFFUL

// fran1 -- return a random floating-point number on the interval [0,1)
//
#define AM (1.0/IM)
#define EPS 1.2e-7
#define RNMX (1.0-EPS)

void CValve_Random::SetSeed(const int iSeed)
{
	m_idum = ((iSeed < 0) ? iSeed : -iSeed);
	m_iy = 0;
}

int CValve_Random::GenerateRandomNumber()
{
	int j;
	int k;

	if (m_idum <= 0 || !m_iy)
	{
		if (-(m_idum) < 1)
			m_idum = 1;
		else
			m_idum = -(m_idum);

		for (j = NTAB + 7; j >= 0; j--)
		{
			k = (m_idum) / IQ;
			m_idum = IA * (m_idum - k * IQ) - IR * k;
			if (m_idum < 0)
				m_idum += IM;
			if (j < NTAB)
				m_iv[j] = m_idum;
		}
		m_iy = m_iv[0];
	}
	k = (m_idum) / IQ;
	m_idum = IA * (m_idum - k * IQ) - IR * k;
	if (m_idum < 0)
		m_idum += IM;
	j = m_iy / NDIV;

	// We're seeing some strange memory corruption in the contents of s_pUniformStream. 
	// Perhaps it's being caused by something writing past the end of this array? 
	// Bounds-check in release to see if that's the case.
	if (j >= NTAB || j < 0)
	{
		// Clamp j.
		j &= NTAB - 1;
	}

	m_iy = m_iv[j];
	m_iv[j] = m_idum;

	return m_iy;
}

float CValve_Random::RandomFloat(const float flLow, const float flHigh)
{
	// float in [0,1)
	float fl = float(AM * GenerateRandomNumber());
	if (fl > float(RNMX))
	{
		fl = float(RNMX);
	}
	return (fl * (flHigh - flLow)) + flLow; // float in [low,high)
}

float CValve_Random::RandomFloatExp(const float flMinVal, const float flMaxVal, const float flExponent)
{
	// float in [0,1)
	float fl = float(AM * GenerateRandomNumber());
	if (fl > float(RNMX))
	{
		fl = float(RNMX);
	}
	if (flExponent != 1.0f)
	{
		fl = powf(fl, flExponent);
	}
	return (fl * (flMaxVal - flMinVal)) + flMinVal; // float in [low,high)
}

int CValve_Random::RandomInt(const int iLow, const int iHigh)
{
	unsigned int maxAcceptable;
	unsigned int x = iHigh - iLow + 1;
	unsigned int n;

	// If you hit either of these assert, you're not getting back the random number that you thought you were. dont care never happens in normal gameplay
	//assert( x == iHigh-(int64_t)iLow+1 ); // Check that we didn't overflow int
	//assert( x-1 <= MAX_RANDOM_RANGE ); // Check that the values provide an acceptable range

	if (x <= 1 || MAX_RANDOM_RANGE < x - 1)
	{
		//assert( iLow == iHigh ); // This is the only time it is OK to have a range containing a single number
		return iLow;
	}

	// The following maps a uniform distribution on the interval [0,MAX_RANDOM_RANGE]
	// to a smaller, client-specified range of [0,x-1] in a way that doesn't bias
	// the uniform distribution unfavorably. Even for a worst case x, the loop is
	// guaranteed to be taken no more than half the time, so for that worst case x,
	// the average number of times through the loop is 2. For cases where x is
	// much smaller than MAX_RANDOM_RANGE, the average number of times through the
	// loop is very close to 1.
	//
	maxAcceptable = MAX_RANDOM_RANGE - ((MAX_RANDOM_RANGE + 1) % x);
	do
	{
		n = GenerateRandomNumber();
	}
	while (n > maxAcceptable);

	return iLow + (n % x);
}

//Vec3 CValve_Random::RandomAngle(const float flMinVal, const float flMaxVal)
//{
//	const float x = RandomFloat(flMinVal, flMaxVal);
//	const float y = RandomFloat(flMinVal, flMaxVal);
//	const float z = RandomFloat(flMinVal, flMaxVal);
//
//	return Vec3(x, y ,z);
//}
