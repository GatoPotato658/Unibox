#include "CartSim.h"

static constexpr float flCartHistoryWindow = 0.25f;
static constexpr float flCartMovingSpeed = 5.f;
static constexpr float flCartTeleportDist = 256.f;

void CCartSim::Store()
{
	if (m_iPosedCount)
		RestoreCarts();
	m_vCarts.clear();
	m_bHasMoving = false;

	if (!I::ClientEntityList || !I::GlobalVars)
		return;

	const float flNow = I::GlobalVars->curtime;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (!pEntity || pEntity->GetClassID() != ETFClassID::CFuncTrackTrain || pEntity->IsDormant())
			continue;

		const int iIndex = pEntity->entindex();
		const Vec3 vOrigin = pEntity->GetAbsOrigin();
		const Vec3 vAngles = pEntity->GetAbsAngles();
		auto& vHistory = m_mHistory[iIndex];

		if (!vHistory.empty() && (vOrigin - vHistory.back().m_vOrigin).LengthSqr() > flCartTeleportDist * flCartTeleportDist)
			vHistory.clear();
		if (vHistory.empty() || vHistory.back().m_flTime != flNow || vHistory.back().m_vOrigin != vOrigin)
			vHistory.push_back({ flNow, vOrigin, vAngles });
		while (vHistory.size() > 2 && flNow - vHistory.front().m_flTime > flCartHistoryWindow)
			vHistory.pop_front();

		CartState_t tCart = { pEntity, vOrigin, vAngles };
		const Vec3 vSize = pEntity->GetSize();
		tCart.m_flRadius = vSize.Length() * 0.5f;

		if (vHistory.size() > 1)
		{
			const float flDelta = vHistory.back().m_flTime - vHistory.front().m_flTime;
			if (flDelta > 0.f)
			{
				tCart.m_vVelocity = (vHistory.back().m_vOrigin - vHistory.front().m_vOrigin) / flDelta;
				for (int i = 0; i < 3; i++)
					tCart.m_vAngVelocity[i] = Math::DeltaAngle(vHistory.back().m_vAngles[i], vHistory.front().m_vAngles[i]) / flDelta;
			}
		}
		if (tCart.m_vVelocity.Length() < flCartMovingSpeed)
			tCart.m_vVelocity = {};
		if (tCart.m_vAngVelocity.Length() < 1.f)
			tCart.m_vAngVelocity = {};
		m_bHasMoving |= !tCart.m_vVelocity.IsZero() || !tCart.m_vAngVelocity.IsZero();
		m_vCarts.push_back(tCart);
	}

	for (auto it = m_mHistory.begin(); it != m_mHistory.end();)
	{
		const bool bFound = std::ranges::any_of(m_vCarts, [&](const CartState_t& tCart) { return tCart.m_pEntity->entindex() == it->first; });
		it = bFound ? std::next(it) : m_mHistory.erase(it);
	}
}

const CartState_t* CCartSim::GetCart(const CBaseEntity* pEntity) const
{
	for (auto& tCart : m_vCarts)
		if (tCart.m_pEntity == pEntity)
			return &tCart;
	return nullptr;
}

bool CCartSim::IsMovingCart(const CBaseEntity* pEntity) const
{
	auto pCart = GetCart(pEntity);
	return pCart && (!pCart->m_vVelocity.IsZero() || !pCart->m_vAngVelocity.IsZero());
}

void CCartSim::PoseCarts(float flTime)
{
	for (auto& tCart : m_vCarts)
	{
		if (tCart.m_vVelocity.IsZero() && tCart.m_vAngVelocity.IsZero())
			continue;
		tCart.m_pEntity->SetAbsOrigin(tCart.m_vOrigin + tCart.m_vVelocity * flTime);
		if (!tCart.m_vAngVelocity.IsZero())
			tCart.m_pEntity->SetAbsAngles(tCart.m_vAngles + tCart.m_vAngVelocity * flTime);
		m_iPosedCount++;
	}
}

void CCartSim::PoseCartsNear(float flTime, const Vec3& vOrigin, float flDist)
{
	for (auto& tCart : m_vCarts)
	{
		if (tCart.m_vVelocity.IsZero() && tCart.m_vAngVelocity.IsZero())
			continue;
		if ((tCart.m_vOrigin - vOrigin).LengthSqr() > powf(flDist + tCart.m_flRadius, 2.f))
			continue;
		tCart.m_pEntity->SetAbsOrigin(tCart.m_vOrigin + tCart.m_vVelocity * flTime);
		if (!tCart.m_vAngVelocity.IsZero())
			tCart.m_pEntity->SetAbsAngles(tCart.m_vAngles + tCart.m_vAngVelocity * flTime);
		m_iPosedCount++;
	}
}

void CCartSim::RestoreCarts()
{
	if (!m_iPosedCount)
		return;
	m_iPosedCount = 0;
	for (auto& tCart : m_vCarts)
	{
		auto pEntity = I::ClientEntityList ? I::ClientEntityList->GetClientEntity(tCart.m_pEntity->entindex())->As<CBaseEntity>() : nullptr;
		if (!pEntity || pEntity != tCart.m_pEntity)
			continue;
		pEntity->SetAbsOrigin(tCart.m_vOrigin);
		if (!tCart.m_vAngVelocity.IsZero())
			pEntity->SetAbsAngles(tCart.m_vAngles);
	}
}
