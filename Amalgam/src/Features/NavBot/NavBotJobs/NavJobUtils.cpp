#include "NavBotJobs.h"

namespace NavJobUtils
{
	ClosestEnemy_t FindClosestTargetEnemy(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
	{
		if (!pLocal || !pWeapon)
			return {};

		ClosestEnemy_t tBestEnemy{};
		const Vector vLocalOrigin = pLocal->GetAbsOrigin();
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
		{
			const int iEntIndex = pEntity->entindex();
			if (F::BotUtils.ShouldTarget(pLocal, pWeapon, iEntIndex) != ShouldTargetEnum::Target)
				continue;

			const Vector vOrigin = pEntity->GetAbsOrigin();
			const float flDist = vLocalOrigin.DistTo(vOrigin);
			if (flDist >= tBestEnemy.m_flDist)
				continue;

			tBestEnemy.m_iEntIdx = iEntIndex;
			tBestEnemy.m_pPlayer = pEntity->As<CTFPlayer>();
			tBestEnemy.m_vOrigin = vOrigin;
			tBestEnemy.m_flDist = flDist;
		}

		return tBestEnemy;
	}

	bool TryNavToAreaScores(std::vector<NavAreaScore_t>& vAreaScores, PriorityListEnum::PriorityListEnum ePriority, bool bLowestScoreFirst, size_t nMaxAttempts)
	{
		if (vAreaScores.empty())
			return false;

		std::sort(vAreaScores.begin(), vAreaScores.end(), [bLowestScoreFirst](const NavAreaScore_t& a, const NavAreaScore_t& b)
			{
				return bLowestScoreFirst ? a.m_flScore < b.m_flScore : a.m_flScore > b.m_flScore;
			});

		size_t nAttempts = 0;
		for (const auto& tAreaScore : vAreaScores)
		{
			if (!tAreaScore.m_pArea)
				continue;

			if (nMaxAttempts && nAttempts++ >= nMaxAttempts)
				break;

			if (F::NavEngine.NavTo(tAreaScore.m_pArea->m_vCenter, ePriority, true, !F::NavEngine.IsPathing()))
				return true;
		}

		return false;
	}
}
