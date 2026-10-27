#pragma once
#include "../../Utils/Math/Math.h"
#include "../../SDK/Definitions/Main/ValveRandom.h"

namespace SpreadMath
{
	inline constexpr float aFixedPellets10[10][2] = {
		{ 0.f, 0.f }, { 1.f, 0.f }, { -1.f, 0.f }, { 0.f, -1.f }, { 0.f, 1.f },
		{ 0.85f, -0.85f }, { 0.85f, 0.85f }, { -0.85f, -0.85f }, { -0.85f, 0.85f }, { 0.f, 0.f }
	};
	inline constexpr float aFixedPellets15[15][2] = {
		{ 0.f, 0.f }, { -0.5f, 0.f }, { -1.f, 0.f }, { 0.5f, 0.f }, { 1.f, 0.f },
		{ 0.f, 0.5f }, { -0.5f, 0.5f }, { -1.f, 0.5f }, { 0.5f, 0.5f }, { 1.f, 0.5f },
		{ 0.f, -0.5f }, { -0.5f, -0.5f }, { -1.f, -0.5f }, { 0.5f, -0.5f }, { 1.f, -0.5f }
	};

	struct Pellet_t
	{
		float m_flRight = 0.f;
		float m_flUp = 0.f;
	};

	struct Shot_t
	{
		int m_iSeed = 0;
		float m_flSpread = 0.f;
		int m_iBullets = 1;
		bool m_bFixed = false;
		bool m_bFirstPerfect = false;
		float m_flFirstRange = 0.5f;
	};

	inline std::vector<Pellet_t> GetPellets(const Shot_t& tShot)
	{
		std::vector<Pellet_t> vPellets;
		vPellets.reserve(std::max(tShot.m_iBullets, 0));

		CValve_Random tRandom = {};
		for (int iBullet = 0; iBullet < tShot.m_iBullets; iBullet++)
		{
			float x = 0.f, y = 0.f;
			if (tShot.m_bFixed)
			{
				if (tShot.m_iBullets < 15)
					x = aFixedPellets10[iBullet % 10][0] * 0.5f, y = aFixedPellets10[iBullet % 10][1] * 0.5f;
				else
					x = aFixedPellets15[iBullet % 15][0], y = aFixedPellets15[iBullet % 15][1];
			}
			else if (iBullet || !tShot.m_bFirstPerfect)
			{
				const float flRange = !iBullet ? tShot.m_flFirstRange : 0.5f;
				tRandom.SetSeed(tShot.m_iSeed + iBullet);
				x = tRandom.RandomFloat(-flRange, flRange);
				x += tRandom.RandomFloat(-flRange, flRange);
				y = tRandom.RandomFloat(-flRange, flRange);
				y += tRandom.RandomFloat(-flRange, flRange);
			}
			vPellets.push_back({ x * tShot.m_flSpread, y * tShot.m_flSpread });
		}
		return vPellets;
	}

	inline Vec3 GetDirection(const Vec3& vAngles, const Pellet_t& tPellet)
	{
		Vec3 vForward, vRight, vUp; Math::AngleVectors(vAngles, &vForward, &vRight, &vUp);
		Vec3 vDirection = vForward + vRight * tPellet.m_flRight + vUp * tPellet.m_flUp;
		vDirection.Normalize();
		return vDirection;
	}

	inline int ChoosePellet(const std::vector<Pellet_t>& vPellets, const Vec3& vAngles, bool bFirstPerfect)
	{
		if (vPellets.size() <= 1 || bFirstPerfect)
			return 0;

		std::vector<Vec3> vDirections;
		vDirections.reserve(vPellets.size());
		Vec3 vAverage = {};
		for (auto& tPellet : vPellets)
		{
			vDirections.push_back(GetDirection(vAngles, tPellet));
			vAverage += vDirections.back();
		}
		vAverage /= static_cast<float>(vPellets.size());

		int iChosen = 0;
		float flBest = std::numeric_limits<float>::max();
		for (int i = 0; i < int(vDirections.size()); i++)
		{
			float flDist = vDirections[i].DistToSqr(vAverage);
			if (flDist < flBest)
				flBest = flDist, iChosen = i;
		}
		return iChosen;
	}

	inline Vec3 SolveAngles(const Vec3& vDirection, const Pellet_t& tPellet, float flRoll, float flPreferredPitch)
	{
		const double dRad = Math::PI_V<double> / 180.0;
		const double dRight = tPellet.m_flRight, dUp = tPellet.m_flUp;
		const double dSr = sin(flRoll * dRad), dCr = cos(flRoll * dRad);
		const double dU = -dRight * dSr + dUp * dCr;
		const double dW = -dRight * dCr - dUp * dSr;
		const double dLength = sqrt(1.0 + dRight * dRight + dUp * dUp);

		const double dRho = sqrt(1.0 + dU * dU);
		const double dPhi = atan2(dU, 1.0);
		const double dAsin = asin(std::clamp(vDirection.z * dLength / dRho, -1.0, 1.0));

		const double dPitch1 = (dPhi - dAsin) / dRad;
		const double dPitch2 = (dPhi - Math::PI_V<double> + dAsin) / dRad;
		const double dPitch = fabs(remainder(dPitch1 - flPreferredPitch, 360.0)) <= fabs(remainder(dPitch2 - flPreferredPitch, 360.0)) ? dPitch1 : dPitch2;

		const double dSp = sin(dPitch * dRad), dCp = cos(dPitch * dRad);
		const double dYaw = (atan2(vDirection.y, vDirection.x) - atan2(dW, dCp + dU * dSp)) / dRad;

		return { float(remainder(dPitch, 360.0)), float(remainder(dYaw, 360.0)), flRoll };
	}
}
