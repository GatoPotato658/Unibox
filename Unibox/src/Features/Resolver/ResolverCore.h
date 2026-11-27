#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

enum class EResolverPattern : uint8_t
{
	None = 0,
	Static,
	Jitter,
	Spin,
	FakeAngle
};

enum class EYawHypothesis : uint8_t
{
	Sent = 0,
	Backwards,
	Left,
	Right,
	BackLeft,
	BackRight,
	AtTarget,
	VelocityBack,
	Count
};

enum class EPitchHypothesis : uint8_t
{
	Raw = 0,
	Inverse,
	SameSide,
	Zero,
	Count
};

struct ResolverSnapshot_t
{
	float m_flSimTime = 0.f;
	float m_flSentYaw = 0.f;
	float m_flSentPitch = 0.f;
	float m_flViewToLocalYaw = 0.f;
	float m_flMoveYaw = 0.f;
	float m_flSpeed2D = 0.f;
	int m_iChokedTicks = 0;
	bool m_bOnGround = true;
	bool m_bShot = false;
};

struct ResolverHypothesis_t
{
	float m_flConfidence = 0.f;
	int m_iHits = 0;
	int m_iMisses = 0;
	bool m_bTried = false;
};

struct ResolverTrack_t
{
	static constexpr int kHistorySize = 16;
	static constexpr int kYawHypCount = static_cast<int>(EYawHypothesis::Count);
	static constexpr int kPitchHypCount = static_cast<int>(EPitchHypothesis::Count);

	int m_iUserID = -1;
	float m_flYaw = 0.f;
	float m_flPitch = 0.f;

	bool m_bYaw = false;
	bool m_bPitch = false;
	bool m_bMinwalk = true;
	bool m_bView = false;

	bool m_bAutoSetYaw = true;
	bool m_bAutoSetPitch = true;
	bool m_bFirstOOBPitch = false;
	bool m_bInversePitch = false;
	bool m_bManualYaw = false;
	bool m_bManualPitch = false;
	bool m_bAutoPitchStepped = false;

	bool m_bHasSniperDot = false;
	float m_flSniperDotPitch = 0.f;

	EResolverPattern m_ePattern = EResolverPattern::None;
	float m_flJitterModeA = 0.f;
	float m_flJitterModeB = 0.f;
	bool m_bJitterPhase = false;
	float m_flJitterConfidence = 0.f;

	float m_flSpinRate = 0.f;
	float m_flSpinConfidence = 0.f;

	bool m_bLocked = false;
	int m_iHits = 0;
	int m_iMisses = 0;
	int m_iActiveYawHyp = static_cast<int>(EYawHypothesis::Sent);
	int m_iActivePitchHyp = static_cast<int>(EPitchHypothesis::Raw);

	std::array<ResolverHypothesis_t, kYawHypCount> m_aYawHyps = {};
	std::array<ResolverHypothesis_t, kPitchHypCount> m_aPitchHyps = {};
	std::array<ResolverSnapshot_t, kHistorySize> m_aHistory = {};
	int m_iHistoryHead = 0;
	int m_iHistoryCount = 0;

	static float NormalizeDeg(float flAngle)
	{
		if (!std::isfinite(flAngle))
			return 0.f;
		float flRes = std::fmod(flAngle, 360.f);
		if (flRes > 180.f)
			flRes -= 360.f;
		else if (flRes < -180.f)
			flRes += 360.f;
		return flRes;
	}

	const ResolverSnapshot_t& HistoryAt(int iOffsetFromNewest) const
	{
		const int iIdx = (m_iHistoryHead - 1 - iOffsetFromNewest + kHistorySize * 2) % kHistorySize;
		return m_aHistory[iIdx];
	}

	void Reset(int iUserID = -1)
	{
		*this = ResolverTrack_t{};
		m_iUserID = iUserID;
	}

	void PushSnapshot(const ResolverSnapshot_t& tSnap)
	{
		m_aHistory[m_iHistoryHead] = tSnap;
		m_iHistoryHead = (m_iHistoryHead + 1) % kHistorySize;
		if (m_iHistoryCount < kHistorySize)
			m_iHistoryCount++;

		bool bRapidPitchFlip = false;
		if (m_iHistoryCount >= 2)
		{
			const float flPrevPitch = HistoryAt(1).m_flSentPitch;
			if (std::fabs(tSnap.m_flSentPitch - flPrevPitch) > 120.f)
				bRapidPitchFlip = true;
		}

		const bool bOOBPitch = std::fabs(tSnap.m_flSentPitch) >= 88.5f || bRapidPitchFlip;
		if (bOOBPitch)
		{
			m_bPitch = true;
			if (!m_bFirstOOBPitch)
			{
				m_bFirstOOBPitch = true;
				if (!m_bManualPitch && m_bAutoSetPitch)
				{
					m_iActivePitchHyp = static_cast<int>(EPitchHypothesis::Inverse);
					m_flPitch = -tSnap.m_flSentPitch;
				}
			}
		}
		else if (!m_bManualPitch && !m_bHasSniperDot)
		{
			m_bPitch = false;
		}

		if (m_iHistoryCount >= 4)
		{
			const int iDeltaCount = std::min(m_iHistoryCount - 1, 8);
			std::array<float, 8> aDeltas = {};
			float flSumDelta = 0.f;
			for (int i = 0; i < iDeltaCount; i++)
			{
				aDeltas[i] = NormalizeDeg(HistoryAt(i).m_flSentYaw - HistoryAt(i + 1).m_flSentYaw);
				flSumDelta += aDeltas[i];
			}
			const float flMeanDelta = flSumDelta / static_cast<float>(iDeltaCount);

			float flVarDelta = 0.f;
			for (int i = 0; i < iDeltaCount; i++)
			{
				const float flDiff = aDeltas[i] - flMeanDelta;
				flVarDelta += flDiff * flDiff;
			}
			flVarDelta /= static_cast<float>(iDeltaCount);

			float flSumChoked = 0.f;
			const int iChokeCount = std::min(m_iHistoryCount, 8);
			for (int i = 0; i < iChokeCount; i++)
				flSumChoked += static_cast<float>(HistoryAt(i).m_iChokedTicks);
			const float flAvgChoked = flSumChoked / static_cast<float>(iChokeCount);

			int iAlternating = 0;
			for (int i = 0; i < iDeltaCount - 1; i++)
			{
				if (aDeltas[i] * aDeltas[i + 1] < 0.f && std::fabs(aDeltas[i]) > 25.f && std::fabs(aDeltas[i + 1]) > 25.f)
					iAlternating++;
			}

			if (std::fabs(flMeanDelta) > 15.f && flVarDelta < 100.f)
			{
				m_ePattern = EResolverPattern::Spin;
				m_flSpinRate = flMeanDelta;
				m_flSpinConfidence = std::clamp(1.f - flVarDelta / 100.f, 0.f, 1.f);
				m_flJitterConfidence = 0.f;
			}
			else if (iDeltaCount >= 3 && iAlternating >= std::max(2, iDeltaCount - 2))
			{
				m_ePattern = EResolverPattern::Jitter;
				m_flJitterModeA = HistoryAt(0).m_flSentYaw;
				m_flJitterModeB = HistoryAt(1).m_flSentYaw;
				m_bJitterPhase = true;
				m_flJitterConfidence = std::clamp(static_cast<float>(iAlternating) / static_cast<float>(iDeltaCount - 1), 0.f, 1.f);
				m_flSpinConfidence = 0.f;
			}
			else if (flAvgChoked >= 2.f)
			{
				m_ePattern = EResolverPattern::FakeAngle;
				m_flSpinConfidence = 0.f;
				m_flJitterConfidence = 0.f;
			}
			else if (flVarDelta < 25.f && std::fabs(flMeanDelta) < 5.f)
			{
				m_ePattern = EResolverPattern::Static;
				m_flSpinConfidence = 0.f;
				m_flJitterConfidence = 0.f;
			}
			else
			{
				m_ePattern = EResolverPattern::None;
				m_flSpinConfidence = 0.f;
				m_flJitterConfidence = 0.f;
			}
		}
		else if (tSnap.m_iChokedTicks >= 2)
		{
			m_ePattern = EResolverPattern::FakeAngle;
		}

		m_bYaw = (m_ePattern != EResolverPattern::None) || (tSnap.m_iChokedTicks >= 2) || m_bManualYaw || (m_flYaw != 0.f) || (m_iActiveYawHyp != static_cast<int>(EYawHypothesis::Sent));
	}

	float EvaluateYaw(float flSentYaw, float flViewToLocalYaw, float flMoveYaw, float flSpeed2D, bool bOnGround) const
	{
		(void)bOnGround;
		if (m_bManualYaw)
			return NormalizeDeg((m_bView ? flViewToLocalYaw : flSentYaw) + m_flYaw);

		if (m_ePattern == EResolverPattern::Spin && m_flSpinConfidence > 0.6f && !m_bLocked)
			return NormalizeDeg(flSentYaw + m_flSpinRate);

		if (m_ePattern == EResolverPattern::Jitter && m_flJitterConfidence > 0.6f && !m_bLocked)
			return NormalizeDeg((m_bJitterPhase ? m_flJitterModeB : m_flJitterModeA) + m_flYaw);

		float flBase = flSentYaw;
		switch (static_cast<EYawHypothesis>(std::clamp(m_iActiveYawHyp, 0, kYawHypCount - 1)))
		{
		case EYawHypothesis::Sent:
			flBase = flSentYaw;
			break;
		case EYawHypothesis::Backwards:
			flBase = flViewToLocalYaw + 180.f;
			break;
		case EYawHypothesis::Left:
			flBase = flViewToLocalYaw + 90.f;
			break;
		case EYawHypothesis::Right:
			flBase = flViewToLocalYaw - 90.f;
			break;
		case EYawHypothesis::BackLeft:
			flBase = flViewToLocalYaw + 135.f;
			break;
		case EYawHypothesis::BackRight:
			flBase = flViewToLocalYaw - 135.f;
			break;
		case EYawHypothesis::AtTarget:
			flBase = flViewToLocalYaw;
			break;
		case EYawHypothesis::VelocityBack:
			flBase = (flSpeed2D > 5.f) ? (flMoveYaw + 180.f) : (flViewToLocalYaw + 180.f);
			break;
		default:
			flBase = flSentYaw;
			break;
		}

		return NormalizeDeg(flBase + m_flYaw);
	}

	float EvaluatePitch(float flSentPitch) const
	{
		if (m_bHasSniperDot)
			return std::clamp(m_flSniperDotPitch, -89.f, 89.f);

		if (m_bManualPitch)
			return std::clamp(m_flPitch, -89.f, 89.f);

		if (m_bAutoPitchStepped)
			return std::clamp(m_flPitch, -89.f, 89.f);

		if (m_bInversePitch)
			return std::clamp(-flSentPitch, -89.f, 89.f);

		float flOut = flSentPitch;
		switch (static_cast<EPitchHypothesis>(std::clamp(m_iActivePitchHyp, 0, kPitchHypCount - 1)))
		{
		case EPitchHypothesis::Inverse:
			flOut = (flSentPitch >= 0.f) ? -89.f : 89.f;
			break;
		case EPitchHypothesis::SameSide:
			flOut = (flSentPitch >= 0.f) ? 89.f : -89.f;
			break;
		case EPitchHypothesis::Zero:
			flOut = 0.f;
			break;
		case EPitchHypothesis::Raw:
		default:
			flOut = flSentPitch;
			break;
		}

		return std::clamp(flOut, -89.f, 89.f);
	}

	void OnShotOutcome(bool bHit, bool bHeadshotRequired, bool bWasHeadshot, int iYawHyp, int iPitchHyp, float flYawStep, float flPitchStep, bool bStepPitch)
	{
		const int iYH = std::clamp(iYawHyp, 0, kYawHypCount - 1);
		const int iPH = std::clamp(iPitchHyp, 0, kPitchHypCount - 1);
		const bool bConfirmedHit = bHit && (!bHeadshotRequired || bWasHeadshot);

		if (bConfirmedHit)
		{
			m_aYawHyps[iYH].m_flConfidence = std::min(1.f, m_aYawHyps[iYH].m_flConfidence + 0.45f);
			m_aYawHyps[iYH].m_iHits++;
			m_aYawHyps[iYH].m_bTried = true;

			m_aPitchHyps[iPH].m_flConfidence = std::min(1.f, m_aPitchHyps[iPH].m_flConfidence + 0.45f);
			m_aPitchHyps[iPH].m_iHits++;
			m_aPitchHyps[iPH].m_bTried = true;

			m_iHits++;
			m_iMisses = 0;
			if (m_iHits >= 2)
				m_bLocked = true;
			return;
		}

		m_aYawHyps[iYH].m_flConfidence = std::max(-1.f, m_aYawHyps[iYH].m_flConfidence - 0.35f);
		m_aYawHyps[iYH].m_iMisses++;
		m_aYawHyps[iYH].m_bTried = true;

		m_aPitchHyps[iPH].m_flConfidence = std::max(-1.f, m_aPitchHyps[iPH].m_flConfidence - 0.35f);
		m_aPitchHyps[iPH].m_iMisses++;
		m_aPitchHyps[iPH].m_bTried = true;

		m_iMisses++;
		if (m_iMisses >= 2)
		{
			m_bLocked = false;
			m_iHits = 0;
		}

		int iBestYaw = -1;
		float flBestYawConf = -2.f;
		for (int i = 0; i < kYawHypCount; i++)
		{
			if (!m_aYawHyps[i].m_bTried && m_aYawHyps[i].m_flConfidence > flBestYawConf)
			{
				flBestYawConf = m_aYawHyps[i].m_flConfidence;
				iBestYaw = i;
			}
		}

		if (iBestYaw != -1)
		{
			m_iActiveYawHyp = iBestYaw;
		}
		else
		{
			for (int i = 0; i < kYawHypCount; i++)
				m_aYawHyps[i].m_bTried = false;

			if (flYawStep != 0.f)
				m_flYaw = NormalizeDeg(m_flYaw + flYawStep);
		}

		if (m_iActiveYawHyp != static_cast<int>(EYawHypothesis::Sent) || m_flYaw != 0.f)
			m_bYaw = true;

		if (bStepPitch)
		{
			int iBestPitch = -1;
			float flBestPitchConf = -2.f;
			for (int i = 0; i < kPitchHypCount; i++)
			{
				if (!m_aPitchHyps[i].m_bTried && m_aPitchHyps[i].m_flConfidence > flBestPitchConf)
				{
					flBestPitchConf = m_aPitchHyps[i].m_flConfidence;
					iBestPitch = i;
				}
			}

			if (iBestPitch != -1)
			{
				m_iActivePitchHyp = iBestPitch;
			}
			else
			{
				for (int i = 0; i < kPitchHypCount; i++)
					m_aPitchHyps[i].m_bTried = false;

				if (flPitchStep != 0.f)
				{
					m_flPitch = std::clamp(m_flPitch + flPitchStep, -89.f, 89.f);
					m_bAutoPitchStepped = true;
				}
			}
		}
	}
};
