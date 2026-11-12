#include "SpreadOptimizer.h"

#include "../../NoSpread/SpreadMath.h"
#include "../../NoSpread/NoSpreadHitscan/NoSpreadHitscan.h"

static constexpr int SpreadSeeds = 256;
static constexpr int MaxBullets = 64;
static constexpr int TraceBudget = 32;
static constexpr int PriorTraceBudget = 128;
static constexpr int ShiftSteps = 2;
static constexpr int PolishMaxEntries = 16;
static constexpr float SpreadEpsilon = 0.0001f;

static const std::array<std::pair<float, float>, SpreadSeeds + MaxBullets>& SpreadTable()
{
	static std::array<std::pair<float, float>, SpreadSeeds + MaxBullets> aTable = {};
	static bool bBuilt = false;
	if (!bBuilt)
	{
		CValve_Random tRandom = {};
		for (int iSeed = 0; iSeed < int(aTable.size()); iSeed++)
		{
			tRandom.SetSeed(iSeed);
			float x = tRandom.RandomFloat(-0.5f, 0.5f);
			x += tRandom.RandomFloat(-0.5f, 0.5f);
			float y = tRandom.RandomFloat(-0.5f, 0.5f);
			y += tRandom.RandomFloat(-0.5f, 0.5f);
			aTable[iSeed] = { x, y };
		}
		bBuilt = true;
	}
	return aTable;
}

static int ToCell(float flValue)
{
	return int(flValue + (flValue < 0.f ? -0.5f : 0.5f));
}

static bool RayHitsBox(const Vec3& vStart, const Vec3& vDirection, const Vec3& vMins, const Vec3& vMaxs)
{
	float flEnter = 0.f, flExit = FLT_MAX;
	for (int i = 0; i < 3; i++)
	{
		if (fabsf(vDirection[i]) < 0.0000001f)
		{
			if (vStart[i] < vMins[i] || vStart[i] > vMaxs[i])
				return false;
			continue;
		}

		const float flInverse = 1.f / vDirection[i];
		float flNear = (vMins[i] - vStart[i]) * flInverse;
		float flFar = (vMaxs[i] - vStart[i]) * flInverse;
		if (flNear > flFar)
			std::swap(flNear, flFar);

		flEnter = std::max(flEnter, flNear);
		flExit = std::min(flExit, flFar);
		if (flEnter > flExit)
			return false;
	}
	return true;
}

bool CSpreadOptimizer::Applies(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (!Vars::Aimbot::Hitscan::SpreadOptimization.Value || !pLocal || !pWeapon)
		return false;

	switch (Vars::Aimbot::General::AimType.Value)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
	case Vars::Aimbot::General::AimTypeEnum::Silent:
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		break;
	default:
		return false;
	}

	if (pWeapon->GetBulletsPerShot() < 1 || pWeapon->GetWeaponSpread() <= 0.f)
		return false;

	return !(Vars::Aimbot::General::NoSpread.Value && F::NoSpreadHitscan.Corrects(pWeapon));
}

bool CSpreadOptimizer::BuildKernel(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	m_vKernel.clear();
	m_flKernelWeight = 0.f;
	m_bPrior = false;

	const int iBullets = std::clamp(pWeapon->GetBulletsPerShot(), 1, MaxBullets);
	m_flSpread = pWeapon->GetWeaponSpread();
	m_flRange = pWeapon->GetRange();
	if (m_flSpread <= 0.f || m_flRange <= 0.f)
		return false;

	if (F::NoSpreadHitscan.IsFixedSpread(pWeapon))
	{
		for (int i = 0; i < iBullets; i++)
		{
			if (iBullets < 15)
				m_vKernel.push_back({ SpreadMath::aFixedPellets10[i % 10][0] * 0.5f, SpreadMath::aFixedPellets10[i % 10][1] * 0.5f, 1.f });
			else
				m_vKernel.push_back({ SpreadMath::aFixedPellets15[i % 15][0], SpreadMath::aFixedPellets15[i % 15][1], 1.f });
		}
	}
	else
	{
		const bool bBonus = TICKS_TO_TIME(pLocal->m_nTickBase()) - pWeapon->m_flLastFireTime() > (iBullets > 1 ? 0.25f : 1.25f);
		const int iFirst = bBonus && !SDK::AttribHookValue(0.f, "mult_spread_scale_first_shot", pWeapon) ? 1 : 0;
		if (iFirst)
			m_vKernel.push_back({ 0.f, 0.f, 1.f });

		const auto& aTable = SpreadTable();
		for (int iSeed = iFirst; iSeed < SpreadSeeds + iBullets - 1; iSeed++)
		{
			const int iLow = std::max(iFirst, iSeed - (SpreadSeeds - 1));
			const int iHigh = std::min(iBullets - 1, iSeed);
			if (iHigh < iLow)
				continue;

			m_vKernel.push_back({ aTable[iSeed].first, aTable[iSeed].second, float(iHigh - iLow + 1) / float(SpreadSeeds) });
		}
		m_bPrior = true;
	}

	std::sort(m_vKernel.begin(), m_vKernel.end(), [](const KernelEntry_t& a, const KernelEntry_t& b)
	{
		if (a.m_flWeight != b.m_flWeight)
			return a.m_flWeight > b.m_flWeight;
		return a.m_flX * a.m_flX + a.m_flY * a.m_flY < b.m_flX * b.m_flX + b.m_flY * b.m_flY;
	});

	for (auto& tEntry : m_vKernel)
		m_flKernelWeight += tEntry.m_flWeight;
	return !m_vKernel.empty();
}

bool CSpreadOptimizer::Project(const Vec3& vPoint, float& flU, float& flV) const
{
	const Vec3 vToPoint = vPoint - m_vEye;
	const float flDepth = vToPoint.Dot(m_vForward);
	if (flDepth <= SpreadEpsilon)
		return false;

	const Vec3 vOnPlane = m_vEye + vToPoint * (m_flDistance / flDepth) - m_vCentre;
	flU = vOnPlane.Dot(m_vRight);
	flV = vOnPlane.Dot(m_vUp);
	return true;
}

Vec3 CSpreadOptimizer::Point(float flI, float flJ) const
{
	return m_vCentre + m_vRight * (flI * m_flCell) + m_vUp * (flJ * m_flCell);
}

bool CSpreadOptimizer::SetupMask(CTFPlayer* pLocal, CBaseEntity* pTarget, const matrix3x4* aBones, float flModelScale, const Vec3& vHullMins, const Vec3& vHullMaxs, const Vec3& vEye)
{
	auto pSet = pTarget->As<CBaseAnimating>()->GetAimHitboxSet();
	if (!pSet || pSet->numhitboxes <= 0 || !aBones)
		return false;

	m_pLocal = pLocal;
	m_pTarget = pTarget;
	m_aBones = aBones;
	m_pSet = pSet;
	m_iHitboxes = std::min(pSet->numhitboxes, MaxHitboxes);
	m_flModelScale = flModelScale;

	m_vEye = vEye;
	m_vHullMins = vHullMins;
	m_vHullMaxs = vHullMaxs;
	m_vCentre = (vHullMins + vHullMaxs) / 2;
	m_vForward = m_vCentre - vEye;
	m_flDistance = m_vForward.Normalize();
	if (m_flDistance <= SpreadEpsilon)
		return false;

	Math::AngleVectors(Math::VectorAngles(m_vForward), nullptr, &m_vRight, &m_vUp);
	m_flCell = std::clamp(m_flSpread * m_flDistance / 5.f, 2.f, 8.f);

	for (auto& tCell : m_aCells)
		tCell = Cell_t::Unknown;
	m_iTraces = 0;
	m_iTraceLimit = m_bPrior ? PriorTraceBudget : TraceBudget;
	m_flOpenHits = 0.f;

	for (int nHitbox = 0; nHitbox < m_iHitboxes; nHitbox++)
	{
		auto pBox = pSet->pHitbox(nHitbox);
		Rect_t& tRect = m_aRects[nHitbox];
		tRect.m_bValid = pBox && pBox->bone >= 0 && pBox->bone < MAXSTUDIOBONES;
		if (!tRect.m_bValid)
			continue;

		tRect.m_flLowU = tRect.m_flLowV = FLT_MAX;
		tRect.m_flHighU = tRect.m_flHighV = -FLT_MAX;
		for (int iCorner = 0; iCorner < 8; iCorner++)
		{
			const Vec3 vCorner = { iCorner & 1 ? pBox->bbmax.x : pBox->bbmin.x, iCorner & 2 ? pBox->bbmax.y : pBox->bbmin.y, iCorner & 4 ? pBox->bbmax.z : pBox->bbmin.z };
			const Vec3 vWorld = Math::VectorTransform(vCorner, aBones[pBox->bone]);

			float flU, flV;
			if (!Project(vWorld, flU, flV))
			{
				tRect.m_bValid = false;
				break;
			}

			tRect.m_flLowU = std::min(tRect.m_flLowU, flU);
			tRect.m_flHighU = std::max(tRect.m_flHighU, flU);
			tRect.m_flLowV = std::min(tRect.m_flLowV, flV);
			tRect.m_flHighV = std::max(tRect.m_flHighV, flV);
		}
	}

	return true;
}

CSpreadOptimizer::Cell_t CSpreadOptimizer::Ray(const Vec3& vEnd, float flU, float flV)
{
	const Vec3 vDirection = (vEnd - m_vEye).Normalized();

	float flNearest = FLT_MAX;
	for (int nHitbox = 0; nHitbox < m_iHitboxes; nHitbox++)
	{
		const Rect_t& tRect = m_aRects[nHitbox];
		if (!tRect.m_bValid || flU < tRect.m_flLowU || flU > tRect.m_flHighU || flV < tRect.m_flLowV || flV > tRect.m_flHighV)
			continue;

		auto pBox = m_pSet->pHitbox(nHitbox);
		float flDistance = 0.f;
		if (Math::RayToOBB(m_vEye, vDirection, pBox->bbmin, pBox->bbmax, m_aBones[pBox->bone], m_flModelScale, &flDistance))
			flNearest = std::min(flNearest, flDistance);
	}

	if (flNearest > m_flRange || !RayHitsBox(m_vEye, vDirection, m_vHullMins, m_vHullMaxs))
		return Cell_t::Miss;

	if (m_iTraces >= m_iTraceLimit)
		return Cell_t::Blocked;

	m_iTraces++;
	return SDK::VisPos(m_pLocal, m_pTarget, m_vEye, m_vEye + vDirection * flNearest) ? Cell_t::Hit : Cell_t::Blocked;
}

CSpreadOptimizer::Cell_t CSpreadOptimizer::Cell(int iI, int iJ)
{
	if (iI < -MaskHalf || iI >= MaskHalf || iJ < -MaskHalf || iJ >= MaskHalf)
		return Cell_t::Miss;

	Cell_t& tCell = m_aCells[(iJ + MaskHalf) * MaskSize + (iI + MaskHalf)];
	if (tCell == Cell_t::Unknown)
	{
		const Vec3 vEnd = m_vEye + (Point(float(iI), float(iJ)) - m_vEye).Normalized() * m_flRange;
		tCell = Ray(vEnd, iI * m_flCell, iJ * m_flCell);
	}
	return tCell;
}

CSpreadOptimizer::Score_t CSpreadOptimizer::Score(float flA, float flB)
{
	Vec3 vForward, vRight, vUp;
	Math::AngleVectors(Math::VectorAngles(Point(flA, flB) - m_vEye), &vForward, &vRight, &vUp);

	Score_t tScore = {};
	float flSumI = 0.f, flSumJ = 0.f;
	for (auto& tEntry : m_vKernel)
	{
		const Vec3 vDirection = vForward + vRight * (tEntry.m_flX * m_flSpread) + vUp * (tEntry.m_flY * m_flSpread);
		const float flDepth = vDirection.Dot(m_vForward);
		if (flDepth <= SpreadEpsilon)
			continue;

		const float flScale = m_flDistance / (flDepth * m_flCell);
		const float flI = vDirection.Dot(m_vRight) * flScale;
		const float flJ = vDirection.Dot(m_vUp) * flScale;
		const Cell_t tState = m_bPrior
			? Cell(ToCell(flI), ToCell(flJ))
			: Ray(m_vEye + vDirection.Normalized() * m_flRange, flI * m_flCell, flJ * m_flCell);

		switch (tState)
		{
		case Cell_t::Hit:
			tScore.m_flOpenHits += tEntry.m_flWeight;
			tScore.m_flHits += tEntry.m_flWeight;
			flSumI += tEntry.m_flWeight * (m_bPrior ? float(ToCell(flI)) : flI);
			flSumJ += tEntry.m_flWeight * (m_bPrior ? float(ToCell(flJ)) : flJ);
			break;
		case Cell_t::Blocked:
			tScore.m_flOpenHits += tEntry.m_flWeight;
			break;
		default:
			break;
		}
	}

	if (tScore.m_flHits > 0.f)
	{
		tScore.m_flMeanI = flSumI / tScore.m_flHits;
		tScore.m_flMeanJ = flSumJ / tScore.m_flHits;
	}

	m_flOpenHits = std::max(m_flOpenHits, tScore.m_flOpenHits);
	return tScore;
}

CSpreadOptimizer::Score_t CSpreadOptimizer::Refine(float& flA, float& flB, Score_t tBest)
{
	const float flSeedA = flA, flSeedB = flB;

	if (!m_bPrior)
	{
		const Vec3 vDirection = (Point(flSeedA, flSeedB) - m_vEye).Normalized();
		const float flPitch = Math::VectorAngles(vDirection).x;
		const int iCount = std::min(int(m_vKernel.size()), PolishMaxEntries);
		for (int i = 0; i < iCount && tBest.m_flHits < m_flKernelWeight; i++)
		{
			const auto& tEntry = m_vKernel[i];
			const Vec3 vAngles = SpreadMath::SolveAngles(vDirection, { tEntry.m_flX * m_flSpread, tEntry.m_flY * m_flSpread }, 0.f, flPitch);

			Vec3 vForward; Math::AngleVectors(vAngles, &vForward);
			float flU, flV;
			if (!Project(m_vEye + vForward * m_flDistance, flU, flV))
				continue;

			const float flNextA = flU / m_flCell, flNextB = flV / m_flCell;
			const Score_t tMoved = Score(flNextA, flNextB);
			if (tMoved.m_flHits > tBest.m_flHits)
			{
				flA = flNextA, flB = flNextB;
				tBest = tMoved;
			}
		}
	}

	for (int iStep = 0; iStep < ShiftSteps && tBest.m_flHits > 0.f && tBest.m_flHits < m_flKernelWeight; iStep++)
	{
		const float flNextA = m_bPrior ? float(ToCell(tBest.m_flMeanI)) : flA + flSeedA - tBest.m_flMeanI;
		const float flNextB = m_bPrior ? float(ToCell(tBest.m_flMeanJ)) : flB + flSeedB - tBest.m_flMeanJ;
		if (flNextA == flA && flNextB == flB)
			break;

		const Score_t tMoved = Score(flNextA, flNextB);
		if (tMoved.m_flHits < tBest.m_flHits)
			break;

		flA = flNextA, flB = flNextB;
		tBest = tMoved;
	}

	if (int(m_vKernel.size()) > PolishMaxEntries || tBest.m_flHits >= m_flKernelWeight)
		return tBest;

	float flBestA = flA, flBestB = flB;
	for (int iDI = -1; iDI <= 1; iDI++)
	{
		for (int iDJ = -1; iDJ <= 1; iDJ++)
		{
			if (!iDI && !iDJ)
				continue;

			const Score_t tMoved = Score(flA + iDI, flB + iDJ);
			if (tMoved.m_flHits > tBest.m_flHits)
			{
				tBest = tMoved;
				flBestA = flA + iDI, flBestB = flB + iDJ;
			}
		}
	}

	flA = flBestA, flB = flBestB;
	return tBest;
}

bool CSpreadOptimizer::Optimize(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTarget, const matrix3x4* aBones, float flModelScale, const Vec3& vHullMins, const Vec3& vHullMaxs, const Vec3& vEye, Vec3& vAimPos)
{
	if (!BuildKernel(pLocal, pWeapon) || !SetupMask(pLocal, pTarget, aBones, flModelScale, vHullMins, vHullMaxs, vEye))
		return true;

	float flU, flV;
	if (!Project(vAimPos, flU, flV))
		return true;

	float flA = flU / m_flCell, flB = flV / m_flCell;
	const float flSeedA = flA, flSeedB = flB;
	const Score_t tBest = Refine(flA, flB, Score(flA, flB));
	if (flA != flSeedA || flB != flSeedB)
		vAimPos = Point(flA, flB);

	const float flCoverage = m_flOpenHits > 0.f ? tBest.m_flHits / m_flOpenHits : 0.f;
	return flCoverage * 100.f >= Vars::Aimbot::Hitscan::SpreadHitChance.Value;
}
