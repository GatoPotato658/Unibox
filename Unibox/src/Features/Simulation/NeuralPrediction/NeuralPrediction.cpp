#include "NeuralPrediction.h"
#include "NeuralCompute.h"
#include "NeuralWeightsEmbedded.h"
#include "../../World/World.h"
#include "../../Configs/Configs.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <random>

namespace
{
	static constexpr float kTickInterval = 0.015f;
	static constexpr float kGravity = 800.f;
	static constexpr float kJumpVel = 289.f;
	static constexpr float kSvAccel = 10.f;
	static constexpr float kSvAirAccel = 10.f;
	static constexpr float kSvFriction = 4.f;
	static constexpr float kSvStopSpeed = 100.f;
	static constexpr float kAirCap = 30.f;
	static constexpr float kDuckScale = 1.f / 3.f;

	static constexpr int kMoveNone = 0;
	static constexpr int kMoveInput = 1;
	static constexpr int kMoveStop = 2;
	static constexpr int kMoveWorld = 3;
	static constexpr int kMoveView = 4;
	static constexpr int kMoveSweep = 5;

	float ClassDefaultMaxSpeed(uint8_t uClassId)
	{
		switch (uClassId)
		{
		case 1: return 400.f;
		case 2: return 300.f;
		case 3: return 240.f;
		case 4: return 280.f;
		case 5: return 320.f;
		case 6: return 230.f;
		case 7: return 300.f;
		case 8: return 320.f;
		case 9: return 300.f;
		default: return 300.f;
		}
	}

	void ApplyFriction2D(float& flVx, float& flVy, float flDt)
	{
		const float flSpeed = hypotf(flVx, flVy);
		if (flSpeed < 0.1f)
			return;
		const float flCtrl = std::max(flSpeed, kSvStopSpeed);
		const float flDrop = flCtrl * kSvFriction * flDt;
		const float flNewSpeed = std::max(flSpeed - flDrop, 0.f);
		if (flNewSpeed != flSpeed)
		{
			const float flScale = flNewSpeed / flSpeed;
			flVx *= flScale;
			flVy *= flScale;
		}
	}

	void StepGround2D(float& flVx, float& flVy, float flWx, float flWy, float flWishSpeed, float flMaxSpeed, float flDt, bool bLostFooting)
	{
		ApplyFriction2D(flVx, flVy, flDt);
		if (flWishSpeed > 0.f)
		{
			const float flCur = flVx * flWx + flVy * flWy;
			const float flAdd = flWishSpeed - flCur;
			if (flAdd > 0.f)
			{
				float flAccel = kSvAccel;
				const float flThresh = 100.f * kSvFriction / kSvAccel;
				if (flWishSpeed < flThresh && !bLostFooting)
				{
					const float flSp = hypotf(flVx, flVy);
					const float flCtrl = std::max(flSp, kSvStopSpeed);
					flAccel = (flCtrl * kSvFriction) / flWishSpeed + 1.f;
				}
				const float flGain = std::min(flAccel * flWishSpeed * flDt, flAdd);
				flVx += flWx * flGain;
				flVy += flWy * flGain;
			}
		}
		const float flSp = hypotf(flVx, flVy);
		if (flMaxSpeed > 0.f && flSp > flMaxSpeed)
		{
			const float flS = flMaxSpeed / flSp;
			flVx *= flS;
			flVy *= flS;
		}
	}

	void StepAir2D(float& flVx, float& flVy, float flWx, float flWy, float flWishSpeed, float flDt)
	{
		if (flWishSpeed <= 0.f)
			return;
		const float flCap = std::min(flWishSpeed, kAirCap);
		const float flCur = flVx * flWx + flVy * flWy;
		const float flAdd = flCap - flCur;
		if (flAdd <= 0.f)
			return;
		const float flGain = std::min(kSvAirAccel * flWishSpeed * flDt, flAdd);
		flVx += flWx * flGain;
		flVy += flWy * flGain;
	}

	void InferWishDir(const CNeuralPrediction::ObsFrame_t& tPrev, const CNeuralPrediction::ObsFrame_t& tCurr, float& flOutFwd, float& flOutSide)
	{
		const int nTicks = std::clamp(tCurr.m_iServerTick - tPrev.m_iServerTick, 1, 4);
		const bool bOnGround = (tPrev.m_iFlags & FL_ONGROUND) != 0;
		const bool bDucking = (tPrev.m_iFlags & FL_DUCKING) != 0;
		const bool bLostFooting = tPrev.m_uLostFooting != 0;
		const float flBaseMax = tPrev.m_flMaxSpeed > 10.f ? tPrev.m_flMaxSpeed : ClassDefaultMaxSpeed(tPrev.m_uClassId);
		const float flEffMax = (bOnGround && bDucking) ? (flBaseMax * kDuckScale) : flBaseMax;

		const float flRad = Math::Deg2Rad(tPrev.m_flEyeYaw);
		const float flC = cosf(flRad);
		const float flS = sinf(flRad);

		float flIdleVx = tPrev.m_vVelocity.x;
		float flIdleVy = tPrev.m_vVelocity.y;
		if (bOnGround)
		{
			for (int i = 0; i < nTicks; ++i)
				ApplyFriction2D(flIdleVx, flIdleVy, kTickInterval);
		}
		const float flDvX = tCurr.m_vVelocity.x - flIdleVx;
		const float flDvY = tCurr.m_vVelocity.y - flIdleVy;
		const float flDvMag = hypotf(flDvX, flDvY);

		static constexpr float kCands[9][2] = {
			{ 0.f, 0.f },
			{ 1.f, 0.f },
			{ -1.f, 0.f },
			{ 0.f, 1.f },
			{ 0.f, -1.f },
			{ 1.f, 1.f },
			{ 1.f, -1.f },
			{ -1.f, 1.f },
			{ -1.f, -1.f }
		};

		int iBest = 0;
		float flBestErr2 = std::numeric_limits<float>::max();
		float flZeroErr2 = std::numeric_limits<float>::max();

		for (int i = 0; i < 9; ++i)
		{
			const float wf = kCands[i][0];
			const float ws = kCands[i][1];
			const float wx = flC * wf + flS * ws;
			const float wy = flS * wf - flC * ws;
			const float wlen = hypotf(wx, wy);
			float dx = 0.f, dy = 0.f, wsp = 0.f;
			if (wlen > 1e-6f)
			{
				dx = wx / wlen;
				dy = wy / wlen;
				wsp = flEffMax;
			}
			float vx = tPrev.m_vVelocity.x;
			float vy = tPrev.m_vVelocity.y;
			for (int s = 0; s < nTicks; ++s)
			{
				if (bOnGround)
					StepGround2D(vx, vy, dx, dy, wsp, flEffMax, kTickInterval, bLostFooting);
				else
					StepAir2D(vx, vy, dx, dy, wsp, kTickInterval);
			}
			const float ex = vx - tCurr.m_vVelocity.x;
			const float ey = vy - tCurr.m_vVelocity.y;
			const float err2 = ex * ex + ey * ey;
			if (i == 0)
				flZeroErr2 = err2;
			if (err2 < flBestErr2)
			{
				flBestErr2 = err2;
				iBest = i;
			}
		}

		if (iBest == 0)
		{
			const float flV1Mag = hypotf(tCurr.m_vVelocity.x, tCurr.m_vVelocity.y);
			const float flV0Mag = hypotf(tPrev.m_vVelocity.x, tPrev.m_vVelocity.y);
			if (bOnGround && flV1Mag > flEffMax * 0.92f && fabsf(flV1Mag - flV0Mag) < 2.f)
			{
				const float dx = tCurr.m_vVelocity.x / flV1Mag;
				const float dy = tCurr.m_vVelocity.y / flV1Mag;
				flOutFwd = std::clamp(dx * flC + dy * flS, -1.f, 1.f);
				flOutSide = std::clamp(dx * flS - dy * flC, -1.f, 1.f);
				return;
			}
			flOutFwd = 0.f;
			flOutSide = 0.f;
			return;
		}

		if ((flZeroErr2 - flBestErr2) < 0.25f && flDvMag < 1.5f)
		{
			flOutFwd = 0.f;
			flOutSide = 0.f;
			return;
		}

		if (flDvMag >= 2.f && flDvMag <= flEffMax * kSvAccel * kTickInterval * float(nTicks) * 1.1f)
		{
			const float cdx = flDvX / flDvMag;
			const float cdy = flDvY / flDvMag;
			float vx = tPrev.m_vVelocity.x;
			float vy = tPrev.m_vVelocity.y;
			for (int s = 0; s < nTicks; ++s)
			{
				if (bOnGround)
					StepGround2D(vx, vy, cdx, cdy, flEffMax, flEffMax, kTickInterval, bLostFooting);
				else
					StepAir2D(vx, vy, cdx, cdy, flEffMax, kTickInterval);
			}
			const float ex = vx - tCurr.m_vVelocity.x;
			const float ey = vy - tCurr.m_vVelocity.y;
			if (ex * ex + ey * ey < flBestErr2 * 0.6f)
			{
				const float pf = cdx * flC + cdy * flS;
				const float ps = cdx * flS - cdy * flC;
				const float mag = std::max(hypotf(pf, ps), 1e-6f);
				flOutFwd = pf / mag;
				flOutSide = ps / mag;
				return;
			}
		}

		const float bf = kCands[iBest][0];
		const float bs = kCands[iBest][1];
		const float mag = hypotf(bf, bs);
		flOutFwd = bf / mag;
		flOutSide = bs / mag;
	}

	float CountReversals(const float* pVals, int iCount, float flThresh)
	{
		int iLastSign = 0;
		int iRev = 0;
		for (int i = 0; i < iCount; ++i)
		{
			if (fabsf(pVals[i]) <= flThresh)
				continue;
			const int iSign = pVals[i] > 0.f ? 1 : -1;
			if (iLastSign != 0 && iSign != iLastSign)
				++iRev;
			iLastSign = iSign;
		}
		return float(iRev);
	}

	void PackTensors(const std::deque<CNeuralPrediction::ObsFrame_t>& vHist, float* pXSeq, float* pXCtx)
	{
		using Obs = CNeuralPrediction::ObsFrame_t;
		constexpr int T = CNeuralPrediction::kSeqLen;
		std::array<Obs, T> aSeq = {};
		const int nSize = int(vHist.size());
		if (nSize >= T)
		{
			for (int t = 0; t < T; ++t)
				aSeq[t] = vHist[nSize - T + t];
		}
		else
		{
			const int iPad = T - nSize;
			for (int t = 0; t < iPad; ++t)
			{
				aSeq[t] = vHist[0];
				aSeq[t].m_iServerTick = vHist[0].m_iServerTick - (iPad - t);
			}
			for (int t = iPad; t < T; ++t)
				aSeq[t] = vHist[t - iPad];
		}

		const Obs& tAnchor = aSeq[T - 1];
		const float flPsi0Deg = tAnchor.m_flEyeYaw;
		const float flPsi0 = Math::Deg2Rad(flPsi0Deg);
		const float flC0 = cosf(flPsi0);
		const float flS0 = sinf(flPsi0);

		float vf[T], vr[T], vz[T], sp2d[T], maxsp[T], wf[T], ws[T], on_g[T], dyaw_rate[T];
		for (int t = 0; t < T; ++t)
		{
			const Obs& f = aSeq[t];
			vf[t] = f.m_vVelocity.x * flC0 + f.m_vVelocity.y * flS0;
			vr[t] = f.m_vVelocity.x * flS0 - f.m_vVelocity.y * flC0;
			vz[t] = f.m_vVelocity.z;
			sp2d[t] = hypotf(f.m_vVelocity.x, f.m_vVelocity.y);
			maxsp[t] = std::max(f.m_flMaxSpeed, 100.f);
			wf[t] = std::clamp(f.m_flWishFwd, -1.f, 1.f);
			ws[t] = std::clamp(f.m_flWishSide, -1.f, 1.f);
			on_g[t] = (f.m_iFlags & FL_ONGROUND) ? 1.f : 0.f;
		}

		std::memset(pXSeq, 0, size_t(T) * CNeuralPrediction::kSeqDim * sizeof(float));
		for (int t = 0; t < T; ++t)
		{
			const Obs& f = aSeq[t];
			const float dvf = (t > 0) ? (vf[t] - vf[t - 1]) : 0.f;
			const float dvr = (t > 0) ? (vr[t] - vr[t - 1]) : 0.f;
			const float dvz = (t > 0) ? (vz[t] - vz[t - 1]) : 0.f;
			const float dt_f = (t > 0) ? float(std::max(f.m_iServerTick - aSeq[t - 1].m_iServerTick, 1)) : 1.f;
			const float dyaw = Math::Deg2Rad(Math::NormalizeAngle(f.m_flEyeYaw - flPsi0Deg));
			dyaw_rate[t] = (t > 0) ? (Math::NormalizeAngle(f.m_flEyeYaw - aSeq[t - 1].m_flEyeYaw) / dt_f) : 0.f;
			const float pitch = Math::NormalizeAngle(f.m_flEyePitch);

			float* row = pXSeq + t * CNeuralPrediction::kSeqDim;
			row[0] = vf[t] / 520.f;
			row[1] = vr[t] / 520.f;
			row[2] = vz[t] / 800.f;
			row[3] = sp2d[t] / maxsp[t];
			row[4] = dvf / 100.f;
			row[5] = dvr / 100.f;
			row[6] = dvz / 100.f;
			row[7] = sinf(dyaw);
			row[8] = cosf(dyaw);
			row[9] = std::clamp(dyaw_rate[t] / 15.f, -1.f, 1.f);
			row[10] = std::clamp(pitch / 90.f, -1.f, 1.f);
			row[11] = wf[t];
			row[12] = ws[t];
			row[13] = on_g[t];
			row[14] = (f.m_iFlags & FL_DUCKING) ? 1.f : 0.f;
			row[15] = (f.m_uWaterLevel >= 2 || (f.m_iFlags & FL_INWATER)) ? 1.f : 0.f;
			row[16] = float(f.m_uBlast);
			row[17] = float(f.m_uCharge);
			row[18] = float(f.m_uAirDash) / 2.f;
			row[19] = f.m_flMaxSpeed / 520.f;
			row[20] = std::clamp(dt_f / 4.f, 0.25f, 2.f);
			row[21] = float(f.m_uWallContact);
		}

		std::memset(pXCtx, 0, size_t(CNeuralPrediction::kCtxDim) * sizeof(float));
		if (tAnchor.m_uClassId >= 1 && tAnchor.m_uClassId <= 9)
			pXCtx[tAnchor.m_uClassId - 1] = 1.f;
		const float flHpRatio = float(tAnchor.m_iHealth) / std::max(float(tAnchor.m_iMaxHealth), 100.f);
		pXCtx[9] = std::clamp(flHpRatio, 0.f, 1.5f);
		pXCtx[10] = tAnchor.m_uPara ? 1.f : 0.f;
		pXCtx[11] = float(tAnchor.m_uAirDucked) / 2.f;
		pXCtx[12] = tAnchor.m_uJumping ? 1.f : 0.f;
		pXCtx[13] = sp2d[T - 1] / 520.f;

		const float flRevSide = CountReversals(ws, T, 0.2f);
		const float flRevVel = CountReversals(vr, T, 40.f);
		pXCtx[14] = std::min(std::max(flRevSide, flRevVel), 6.f) / 6.f;

		float flYawAbsSum = 0.f, flWfSum = 0.f, flWsSum = 0.f;
		for (int t = T - 8; t < T; ++t)
		{
			flYawAbsSum += fabsf(dyaw_rate[t]);
			flWfSum += wf[t];
			flWsSum += ws[t];
		}
		pXCtx[15] = (flYawAbsSum / 8.f) / 15.f;

		int gRun = 0, aRun = 0;
		for (int t = T - 1; t >= 0; --t)
		{
			if (on_g[t] > 0.5f)
				++gRun;
			else
				break;
		}
		for (int t = T - 1; t >= 0; --t)
		{
			if (on_g[t] < 0.5f)
				++aRun;
			else
				break;
		}
		pXCtx[16] = float(gRun) / 32.f;
		pXCtx[17] = float(aRun) / 32.f;
		pXCtx[18] = flWfSum / 8.f;
		pXCtx[19] = flWsSum / 8.f;

		const float gnF = tAnchor.m_vGroundNormal.x * flC0 + tAnchor.m_vGroundNormal.y * flS0;
		const float gnR = tAnchor.m_vGroundNormal.x * flS0 - tAnchor.m_vGroundNormal.y * flC0;
		pXCtx[20] = tAnchor.m_flGroundDist / 512.f;
		pXCtx[21] = gnF;
		pXCtx[22] = gnR;
		pXCtx[23] = tAnchor.m_vGroundNormal.z;
		pXCtx[24] = tAnchor.m_flCeilingDist / 512.f;

		float flMinWall = 256.f;
		for (int k = 0; k < 8; ++k)
		{
			const float wd = tAnchor.m_aWallDists[k];
			if (wd < flMinWall)
				flMinWall = wd;
			pXCtx[25 + k] = std::clamp(wd / 256.f, 0.f, 1.f);
			pXCtx[33 + k] = std::clamp(tAnchor.m_aLedgeDepths[k] / 128.f, -0.5f, 4.f);
		}
		pXCtx[41] = flMinWall / 256.f;
		pXCtx[42] = (tAnchor.m_aWallDists[2] + tAnchor.m_aWallDists[6]) / 512.f;
		pXCtx[43] = (tAnchor.m_vGroundNormal.z >= 0.7f && tAnchor.m_flGroundDist <= 4.f) ? 1.f : 0.f;
	}

	NeuralCompute::BackendKind BackendFromVar(int iVal)
	{
		switch (iVal)
		{
		case Vars::Aimbot::Projectile::NeuralBackendEnum::Cuda: return NeuralCompute::BackendKind::Cuda;
		case Vars::Aimbot::Projectile::NeuralBackendEnum::DirectML: return NeuralCompute::BackendKind::DirectML;
		case Vars::Aimbot::Projectile::NeuralBackendEnum::CpuAvx2: return NeuralCompute::BackendKind::CpuAvx2;
		case Vars::Aimbot::Projectile::NeuralBackendEnum::CpuSse2: return NeuralCompute::BackendKind::CpuSse2;
		default: return NeuralCompute::BackendKind::Auto;
		}
	}

	NeuralHorizon_t DecodeOutputToHorizon(const float* pOut, float flSimTime, float flBaseYaw, uint32_t uSteamId32, const CNeuralPrediction::SteamAdapt_t* pAdapt)
	{
		NeuralHorizon_t tH;
		tH.m_flSimTime = flSimTime;
		tH.m_flBaseYaw = flBaseYaw;
		tH.m_uSteamId32 = uSteamId32;
		tH.m_bValid = true;
		for (int k = 0; k < NeuralHorizon_t::kNumKnots; ++k)
		{
			const float* pK = pOut + k * 8;
			auto& tK = tH.m_aKnots[k];
			tK.m_flWishFwd = std::clamp(pK[0], -1.f, 1.f);
			tK.m_flWishSide = std::clamp(pK[1], -1.f, 1.f);
			tK.m_flTurnRateDeg = std::clamp(pK[2] * 15.f, -15.f, 15.f);
			tK.m_flJumpProb = std::clamp(pK[3], 0.f, 1.f);
			tK.m_flDuckProb = std::clamp(pK[4], 0.f, 1.f);
			tK.m_flSigmaXY = std::max(pK[5], 1.f);
			tK.m_flSigmaZ = std::max(pK[6], 1.f);
			tK.m_flCommitProb = std::clamp(pK[7], 0.f, 1.f);
			if (pAdapt && pAdapt->m_iSamples > 0)
			{
				tK.m_flWishFwd = std::clamp(tK.m_flWishFwd * pAdapt->m_flScaleMult + pAdapt->m_flBiasFwd, -1.f, 1.f);
				tK.m_flWishSide = std::clamp(tK.m_flWishSide * pAdapt->m_flScaleMult + pAdapt->m_flBiasSide, -1.f, 1.f);
				tK.m_flTurnRateDeg = std::clamp(tK.m_flTurnRateDeg + pAdapt->m_flBiasTurn, -15.f, 15.f);
			}
		}
		return tH;
	}

	void RolloutNeuralEgo(const NeuralHorizon_t& tH, const CNeuralPrediction::ObsFrame_t& tAnchor, std::array<Vec3, 16>& aOutPos)
	{
		const float flPsi0 = Math::Deg2Rad(tAnchor.m_flEyeYaw);
		const float flC0 = cosf(flPsi0);
		const float flS0 = sinf(flPsi0);
		float vx = tAnchor.m_vVelocity.x * flC0 + tAnchor.m_vVelocity.y * flS0;
		float vy = tAnchor.m_vVelocity.x * flS0 - tAnchor.m_vVelocity.y * flC0;
		float vz = tAnchor.m_vVelocity.z;
		const float flMaxSp = std::clamp(tAnchor.m_flMaxSpeed, 100.f, 520.f);
		bool bOnG = (tAnchor.m_iFlags & FL_ONGROUND) != 0;
		const float flFloorZ = bOnG ? 0.f : -std::clamp(tAnchor.m_flGroundDist, 0.f, 512.f);
		float px = 0.f, py = 0.f, pz = 0.f, yaw = 0.f;
		const float flHalfGrav = 0.5f * kGravity * kTickInterval;

		int iKnotIdx = 0;
		for (int t = 1; t <= 120; ++t)
		{
			const auto tK = tH.SampleAtTick(t);
			if (bOnG && tK.m_flJumpProb >= 0.55f)
			{
				vz += kJumpVel;
				bOnG = false;
			}
			if (!bOnG)
				vz -= flHalfGrav;

			const float flRad = Math::Deg2Rad(yaw);
			const float cy = cosf(flRad);
			const float sy = sinf(flRad);
			const float wx = cy * tK.m_flWishFwd + sy * tK.m_flWishSide;
			const float wy = sy * tK.m_flWishFwd - cy * tK.m_flWishSide;
			const float wmag = hypotf(wx, wy);
			const float dx = wmag > 1e-5f ? wx / wmag : 0.f;
			const float dy = wmag > 1e-5f ? wy / wmag : 0.f;
			const float wscale = std::min(wmag, 1.f);
			const bool bDuck = tK.m_flDuckProb >= 0.5f;
			const float flEffMax = flMaxSp * (bDuck ? kDuckScale : 1.f);

			if (bOnG)
			{
				StepGround2D(vx, vy, dx, dy, flEffMax * wscale, flEffMax, kTickInterval, false);
				vz = 0.f;
			}
			else
			{
				StepAir2D(vx, vy, dx, dy, flEffMax * wscale, kTickInterval);
				vz -= flHalfGrav;
			}

			px += vx * kTickInterval;
			py += vy * kTickInterval;
			pz += vz * kTickInterval;
			if (pz <= flFloorZ && vz <= 0.f)
			{
				pz = flFloorZ;
				vz = 0.f;
				bOnG = true;
			}
			yaw += tK.m_flTurnRateDeg;

			if (iKnotIdx < 16 && t == NeuralHorizon_t::kKnots[iKnotIdx])
			{
				aOutPos[iKnotIdx] = { px, py, pz };
				++iKnotIdx;
			}
		}
	}

	void RolloutAnalyticEgo(const NeuralBaseline_t& tBase, const CNeuralPrediction::ObsFrame_t& tAnchor, std::array<Vec3, 16>& aOutPos)
	{
		const float flPsi0 = Math::Deg2Rad(tAnchor.m_flEyeYaw);
		const float flC0 = cosf(flPsi0);
		const float flS0 = sinf(flPsi0);
		float vx = tAnchor.m_vVelocity.x * flC0 + tAnchor.m_vVelocity.y * flS0;
		float vy = tAnchor.m_vVelocity.x * flS0 - tAnchor.m_vVelocity.y * flC0;
		float vz = tAnchor.m_vVelocity.z;
		const float flMaxSp = std::clamp(tAnchor.m_flMaxSpeed, 100.f, 520.f);
		bool bOnG = (tAnchor.m_iFlags & FL_ONGROUND) != 0;
		const bool bDucked = (tAnchor.m_iFlags & FL_DUCKING) != 0;
		const float flFloorZ = bOnG ? 0.f : -std::clamp(tAnchor.m_flGroundDist, 0.f, 512.f);
		float px = 0.f, py = 0.f, pz = 0.f, yaw = 0.f;
		float gTurn = tBase.m_flGroundTurn;
		float aTurn = tBase.m_flAirTurn;
		float flSweepKept = 1.f;
		int iGroundTicks = tBase.m_iGroundTicks;
		const float flHalfGrav = 0.5f * kGravity * kTickInterval;

		int iKnotIdx = 0;
		for (int t = 1; t <= 120; ++t)
		{
			if (bOnG && !bDucked && tBase.m_iHopTicks >= 0)
			{
				if (iGroundTicks >= tBase.m_iHopTicks)
				{
					vz += kJumpVel;
					bOnG = false;
					iGroundTicks = 0;
					flSweepKept = 1.f;
				}
				else
					++iGroundTicks;
			}
			if (!bOnG)
				vz -= flHalfGrav;

			const int iModel = bOnG ? tBase.m_iGroundModel : tBase.m_iAirModel;
			const float flYawParam = bOnG ? (tBase.m_flGroundYaw - tAnchor.m_flEyeYaw) : (tBase.m_flAirYaw - tAnchor.m_flEyeYaw);
			const float flScale = bOnG ? tBase.m_flGroundScale : tBase.m_flAirScale;
			float dx = 0.f, dy = 0.f, wsp = 0.f;
			const float flCap = flMaxSp * (bOnG && bDucked ? kDuckScale : 1.f);
			if (iModel == kMoveWorld)
			{
				const float r = Math::Deg2Rad(flYawParam);
				dx = cosf(r);
				dy = sinf(r);
				wsp = flScale * flCap;
			}
			else if (iModel == kMoveView)
			{
				const float r = Math::Deg2Rad(yaw + (bOnG ? tBase.m_flGroundYaw : tBase.m_flAirYaw));
				dx = cosf(r);
				dy = sinf(r);
				wsp = flScale * flCap;
			}
			else if (iModel == kMoveSweep)
			{
				const float sp = hypotf(vx, vy);
				const float h = sp > 0.01f ? Math::Rad2Deg(atan2f(vy, vx)) : yaw;
				const float r = Math::Deg2Rad(h + tBase.m_flAirYaw * flSweepKept);
				dx = cosf(r);
				dy = sinf(r);
				wsp = flScale * flCap;
			}

			if (bOnG)
			{
				StepGround2D(vx, vy, dx, dy, wsp, flMaxSp, kTickInterval, false);
				vz = 0.f;
			}
			else
			{
				StepAir2D(vx, vy, dx, dy, wsp, kTickInterval);
				vz -= flHalfGrav;
			}

			px += vx * kTickInterval;
			py += vy * kTickInterval;
			pz += vz * kTickInterval;
			if (pz <= flFloorZ && vz <= 0.f)
			{
				pz = flFloorZ;
				vz = 0.f;
				bOnG = true;
				iGroundTicks = 1;
			}

			yaw += bOnG ? gTurn : aTurn;
			gTurn *= 0.79f;
			aTurn *= 0.79f;
			if (!bOnG)
				flSweepKept *= 0.97f;

			if (iKnotIdx < 16 && t == NeuralHorizon_t::kKnots[iKnotIdx])
			{
				aOutPos[iKnotIdx] = { px, py, pz };
				++iKnotIdx;
			}
		}
	}

	struct TrainingSample_t
	{
		std::array<float, CNeuralPrediction::kSeqLen * CNeuralPrediction::kSeqDim> m_aXSeq = {};
		std::array<float, CNeuralPrediction::kCtxDim> m_aXCtx = {};
		std::array<float, CNeuralPrediction::kOutSteps * CNeuralPrediction::kOutDims> m_aTarget = {};
		uint16_t m_uObsKnotMask = 0;
		uint32_t m_uSteamId32 = 0;
		int32_t m_iAnchorTick = 0;
		float m_flWeight = 1.f;
	};

#pragma pack(push, 1)
	struct SnrbHeader_t
	{
		char m_szMagic[4];
		uint32_t m_uVersion;
		uint32_t m_uCount;
		uint32_t m_uReserved;
	};

	struct SnrbRecord_t
	{
		float m_aXSeq[CNeuralPrediction::kSeqLen * CNeuralPrediction::kSeqDim];
		float m_aXCtx[CNeuralPrediction::kCtxDim];
		float m_aTarget[CNeuralPrediction::kOutSteps * CNeuralPrediction::kOutDims];
		uint16_t m_uObsKnotMask;
		uint16_t m_uPad;
		uint32_t m_uSteamId32;
		int32_t m_iAnchorTick;
		float m_flWeight;
	};
#pragma pack(pop)

	bool AtomicReplaceFile(const std::string& sTmpPath, const std::string& sDstPath)
	{
		if (MoveFileExA(sTmpPath.c_str(), sDstPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			return true;
		std::error_code ec;
		std::filesystem::rename(sTmpPath, sDstPath, ec);
		if (!ec)
			return true;
		std::filesystem::remove(sTmpPath, ec);
		return false;
	}

	bool LoadReplayBin(const std::string& sPath, std::vector<TrainingSample_t>& vAnchor, std::deque<TrainingSample_t>& qRecent, uint64_t& uTotalSeen)
	{
		std::ifstream f(sPath, std::ios::binary);
		if (!f)
			return false;
		SnrbHeader_t h{};
		f.read(reinterpret_cast<char*>(&h), sizeof(h));
		if (!f.good() || std::memcmp(h.m_szMagic, "SNRB", 4) != 0 || h.m_uVersion != 1 || h.m_uCount == 0 || h.m_uCount > 2048)
			return false;

		std::vector<SnrbRecord_t> vRecs(h.m_uCount);
		f.read(reinterpret_cast<char*>(vRecs.data()), static_cast<std::streamsize>(vRecs.size() * sizeof(SnrbRecord_t)));
		if (!f.good())
			return false;

		const size_t nHalf = std::min<size_t>(vRecs.size() / 2, 1024);
		for (size_t i = 0; i < vRecs.size(); ++i)
		{
			TrainingSample_t s;
			std::memcpy(s.m_aXSeq.data(), vRecs[i].m_aXSeq, sizeof(vRecs[i].m_aXSeq));
			std::memcpy(s.m_aXCtx.data(), vRecs[i].m_aXCtx, sizeof(vRecs[i].m_aXCtx));
			std::memcpy(s.m_aTarget.data(), vRecs[i].m_aTarget, sizeof(vRecs[i].m_aTarget));
			s.m_uObsKnotMask = vRecs[i].m_uObsKnotMask;
			s.m_uSteamId32 = vRecs[i].m_uSteamId32;
			s.m_iAnchorTick = vRecs[i].m_iAnchorTick;
			s.m_flWeight = std::clamp(vRecs[i].m_flWeight, 0.5f, 4.f);
			if (i < nHalf && vAnchor.size() < 1024)
				vAnchor.push_back(s);
			else
			{
				if (qRecent.size() >= 1024)
					qRecent.pop_front();
				qRecent.push_back(s);
			}
			++uTotalSeen;
		}
		return true;
	}

	bool SaveReplayBinAtomic(const std::string& sPath, const std::vector<TrainingSample_t>& vAnchor, const std::deque<TrainingSample_t>& qRecent)
	{
		const size_t nA = std::min<size_t>(vAnchor.size(), 1024);
		const size_t nR = std::min<size_t>(qRecent.size(), 1024);
		const size_t nTotal = nA + nR;
		if (nTotal == 0)
			return false;

		const std::string sTmp = sPath + ".tmp";
		{
			std::ofstream f(sTmp, std::ios::binary | std::ios::trunc);
			if (!f)
				return false;
			SnrbHeader_t h{};
			h.m_szMagic[0] = 'S'; h.m_szMagic[1] = 'N'; h.m_szMagic[2] = 'R'; h.m_szMagic[3] = 'B';
			h.m_uVersion = 1;
			h.m_uCount = static_cast<uint32_t>(nTotal);
			f.write(reinterpret_cast<const char*>(&h), sizeof(h));

			auto fWriteOne = [&](const TrainingSample_t& s)
			{
				SnrbRecord_t r{};
				std::memcpy(r.m_aXSeq, s.m_aXSeq.data(), sizeof(r.m_aXSeq));
				std::memcpy(r.m_aXCtx, s.m_aXCtx.data(), sizeof(r.m_aXCtx));
				std::memcpy(r.m_aTarget, s.m_aTarget.data(), sizeof(r.m_aTarget));
				r.m_uObsKnotMask = s.m_uObsKnotMask;
				r.m_uSteamId32 = s.m_uSteamId32;
				r.m_iAnchorTick = s.m_iAnchorTick;
				r.m_flWeight = s.m_flWeight;
				f.write(reinterpret_cast<const char*>(&r), sizeof(r));
			};
			for (size_t i = 0; i < nA; ++i)
				fWriteOne(vAnchor[i]);
			const size_t iStartR = qRecent.size() - nR;
			for (size_t i = iStartR; i < qRecent.size(); ++i)
				fWriteOne(qRecent[i]);
			f.flush();
			if (!f.good())
				return false;
		}
		return AtomicReplaceFile(sTmp, sPath);
	}

	void SaveCheckpointsAtomic(const NeuralCompute::StrafeNetWeights& tWeights, const std::vector<TrainingSample_t>& vAnchor, const std::deque<TrainingSample_t>& qRecent, const std::string& sConfigPath)
	{
		if (sConfigPath.empty())
			return;
		const std::string sWeightsPath = sConfigPath + "neural_strafe.bin";
		const std::string sWeightsTmp = sWeightsPath + ".tmp";
		if (tWeights.save_snv1(sWeightsTmp))
			AtomicReplaceFile(sWeightsTmp, sWeightsPath);
		SaveReplayBinAtomic(sConfigPath + "neural_replay.bin", vAnchor, qRecent);
	}

	struct PendingWindow_t
	{
		int m_iEntIndex = 0;
		uint32_t m_uSteamId32 = 0;
		CNeuralPrediction::ObsFrame_t m_tAnchor = {};
		std::array<float, CNeuralPrediction::kSeqLen * CNeuralPrediction::kSeqDim> m_aXSeq = {};
		std::array<float, CNeuralPrediction::kCtxDim> m_aXCtx = {};
		NeuralBaseline_t m_tBaseline = {};
		std::vector<CNeuralPrediction::ObsFrame_t> m_vFuture = {};
	};
}

void CNeuralPrediction::Initialize()
{
	if (m_bInitialized.load(std::memory_order_acquire))
		return;

	NeuralCompute::StrafeNetWeights tInitWeights;
	bool bLoaded = false;
	const std::string sPath = F::Configs.m_sConfigPath + "neural_strafe.bin";
	if (!F::Configs.m_sConfigPath.empty() && std::filesystem::exists(sPath))
		bLoaded = tInitWeights.load_snv1(sPath);
	if (!bLoaded)
		bLoaded = tInitWeights.load_snv1_memory(NeuralCompute::g_aEmbeddedWeights, NeuralCompute::g_uEmbeddedWeightsSize);
	if (!bLoaded)
		tInitWeights.init_random(NeuralCompute::ArchType::TCN_ResMLP);

	{
		std::lock_guard lock(m_mtxWeights);
		m_vPromotedPlastic = tInitWeights.plastic_theta;
		m_uWeightVersion.fetch_add(1, std::memory_order_release);
	}

	m_bRunning.store(true, std::memory_order_release);
	m_bInitialized.store(true, std::memory_order_release);

	m_tInferThread = std::jthread([this, tInitWeights](std::stop_token tStop) mutable
	{
		NeuralCompute::ComputeDispatcher tRt;
		tRt.init(BackendFromVar(Vars::Aimbot::Projectile::NeuralBackend.Value), 64);
		tRt.load_weights(tInitWeights, 32);

		uint64_t uAppliedVer = m_uWeightVersion.load(std::memory_order_acquire);
		std::vector<float> vBatchSeq;
		std::vector<float> vBatchCtx;
		std::vector<float> vBatchOut;
		std::vector<int> vBatchSlots;
		std::vector<float> vBatchSimTime;
		std::vector<float> vBatchYaw;
		std::vector<uint32_t> vBatchSteam;

		while (!tStop.stop_requested() && m_bRunning.load(std::memory_order_acquire))
		{
			const uint32_t uGen = m_uInferWake.load(std::memory_order_acquire);

			const uint64_t uCurVer = m_uWeightVersion.load(std::memory_order_acquire);
			if (uCurVer != uAppliedVer)
			{
				std::vector<float> vNewPlastic;
				{
					std::lock_guard lock(m_mtxWeights);
					vNewPlastic = m_vPromotedPlastic;
				}
				tRt.sync_plastic_weights(vNewPlastic);
				uAppliedVer = uCurVer;
			}

			const auto ePref = BackendFromVar(Vars::Aimbot::Projectile::NeuralBackend.Value);
			if (tRt.preference != ePref)
				tRt.preference = ePref;

			vBatchSeq.clear();
			vBatchCtx.clear();
			vBatchSlots.clear();
			vBatchSimTime.clear();
			vBatchYaw.clear();
			vBatchSteam.clear();

			for (int i = 1; i < kMaxPlayers; ++i)
			{
				auto& tMail = m_aMailbox[i];
				if (!tMail.m_bDirty.load(std::memory_order_acquire))
					continue;
				if (tMail.m_tLock.test_and_set(std::memory_order_acquire))
					continue;
				if (tMail.m_bDirty.load(std::memory_order_relaxed))
				{
					vBatchSlots.push_back(i);
					vBatchSimTime.push_back(tMail.m_flSimTime);
					vBatchYaw.push_back(tMail.m_flEyeYaw);
					vBatchSteam.push_back(tMail.m_uSteamId32);
					vBatchSeq.insert(vBatchSeq.end(), tMail.m_aXSeq.begin(), tMail.m_aXSeq.end());
					vBatchCtx.insert(vBatchCtx.end(), tMail.m_aXCtx.begin(), tMail.m_aXCtx.end());
					tMail.m_bDirty.store(false, std::memory_order_release);
				}
				tMail.m_tLock.clear(std::memory_order_release);
			}

			if (!vBatchSlots.empty())
			{
				const uint32_t uBatch = uint32_t(vBatchSlots.size());
				vBatchOut.resize(size_t(uBatch) * kOutSteps * kOutDims);
				tRt.forward(vBatchSeq.data(), vBatchCtx.data(), vBatchOut.data(), uBatch);

				std::unordered_map<uint32_t, SteamAdapt_t> mAdaptSnap;
				{
					std::lock_guard lock(m_mtxAdapt);
					mAdaptSnap = m_mSteamAdapt;
				}

				for (uint32_t b = 0; b < uBatch; ++b)
				{
					const int iSlot = vBatchSlots[b];
					const uint32_t uSteam = vBatchSteam[b];
					const SteamAdapt_t* pAdapt = nullptr;
					if (uSteam != 0)
					{
						if (auto it = mAdaptSnap.find(uSteam); it != mAdaptSnap.end())
							pAdapt = &it->second;
					}
					const NeuralHorizon_t tH = DecodeOutputToHorizon(
						vBatchOut.data() + size_t(b) * kOutSteps * kOutDims,
						vBatchSimTime[b],
						vBatchYaw[b],
						uSteam,
						pAdapt
					);
					auto& tPub = m_aPublished[iSlot];
					const uint64_t s = tPub.m_uSeq.load(std::memory_order_relaxed);
					tPub.m_uSeq.store(s + 1, std::memory_order_release);
					std::atomic_thread_fence(std::memory_order_release);
					const int iNext = 1 - tPub.m_iIdx.load(std::memory_order_relaxed);
					tPub.m_aBuf[iNext] = tH;
					tPub.m_iIdx.store(iNext, std::memory_order_release);
					tPub.m_uSeq.store(s + 2, std::memory_order_release);
				}
			}
			else
			{
				if (tStop.stop_requested() || !m_bRunning.load(std::memory_order_acquire))
					break;
				m_uInferWake.wait(uGen, std::memory_order_acquire);
			}
		}
		tRt.destroy();
	});

	m_tTrainThread = std::jthread([this, tInitWeights](std::stop_token tStop) mutable
	{
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

		NeuralCompute::ComputeDispatcher tRt;
		tRt.init(NeuralCompute::BackendKind::Auto, 64);
		tRt.load_weights(tInitWeights, 32);

		std::array<std::vector<PendingWindow_t>, kMaxPlayers> aPending;
		std::array<int, kMaxPlayers> aLastAnchorTick = {};
		aLastAnchorTick.fill(-1000);

		std::vector<TrainingSample_t> vAnchorReservoir;
		vAnchorReservoir.reserve(1024);
		std::deque<TrainingSample_t> qRecentRing;
		uint64_t uTotalSeen = 0;
		uint32_t uNewValidSinceStep = 0;
		uint32_t uPromotedSinceSave = 0;
		uint32_t uSamplesSinceSave = 0;
		std::mt19937 tRng(1337u);

		if (!F::Configs.m_sConfigPath.empty())
			LoadReplayBin(F::Configs.m_sConfigPath + "neural_replay.bin", vAnchorReservoir, qRecentRing, uTotalSeen);

		auto fEvalWindowAndCommit = [&](PendingWindow_t& tWin, int iMaxObsDt)
		{
			if (iMaxObsDt < 42 || tWin.m_vFuture.empty())
				return;

			float aOutPred[kOutSteps * kOutDims] = {};
			tRt.forward(tWin.m_aXSeq.data(), tWin.m_aXCtx.data(), aOutPred, 1, NeuralCompute::BackendKind::Auto);
			const NeuralHorizon_t tPredH = DecodeOutputToHorizon(aOutPred, tWin.m_tAnchor.m_flSimTime, tWin.m_tAnchor.m_flEyeYaw, tWin.m_uSteamId32, nullptr);

			std::array<Vec3, 16> aNeuralPos = {};
			std::array<Vec3, 16> aAnalyticPos = {};
			RolloutNeuralEgo(tPredH, tWin.m_tAnchor, aNeuralPos);
			RolloutAnalyticEgo(tWin.m_tBaseline, tWin.m_tAnchor, aAnalyticPos);

			const float flPsi0 = Math::Deg2Rad(tWin.m_tAnchor.m_flEyeYaw);
			const float flC0 = cosf(flPsi0);
			const float flS0 = sinf(flPsi0);
			const int t0 = tWin.m_tAnchor.m_iServerTick;

			TrainingSample_t tSample;
			tSample.m_aXSeq = tWin.m_aXSeq;
			tSample.m_aXCtx = tWin.m_aXCtx;
			tSample.m_uSteamId32 = tWin.m_uSteamId32;
			tSample.m_iAnchorTick = t0;
			tSample.m_flWeight = 1.f;

			double dNeuralSq = 0.0;
			double dAnalyticSq = 0.0;
			int iValidKnots = 0;

			for (int k = 0; k < 16; ++k)
			{
				const int iKnotOff = NeuralHorizon_t::kKnots[k];
				if (iKnotOff > iMaxObsDt)
					continue;
				const int iTargetTick = t0 + iKnotOff;

				int iHi = -1;
				for (int j = 0; j < int(tWin.m_vFuture.size()); ++j)
				{
					if (tWin.m_vFuture[j].m_iServerTick >= iTargetTick)
					{
						iHi = j;
						break;
					}
				}
				if (iHi < 0)
					continue;
				const int iLo = std::max(iHi - 1, 0);
				const int tLo = (iHi == 0) ? t0 : tWin.m_vFuture[iLo].m_iServerTick;
				const int tHi = tWin.m_vFuture[iHi].m_iServerTick;
				if ((tHi - iTargetTick) > 4 || (iTargetTick - tLo) > 4)
					continue;

				const float flAlpha = (tHi == tLo) ? 0.f : float(iTargetTick - tLo) / float(std::max(tHi - tLo, 1));
				const Vec3& vOriginLo = (iHi == 0) ? tWin.m_tAnchor.m_vOrigin : tWin.m_vFuture[iLo].m_vOrigin;
				const Vec3& vOriginHi = tWin.m_vFuture[iHi].m_vOrigin;
				const Vec3 vOrgK = vOriginLo + (vOriginHi - vOriginLo) * flAlpha;
				const Vec3 vDOrg = vOrgK - tWin.m_tAnchor.m_vOrigin;
				const float pf = vDOrg.x * flC0 + vDOrg.y * flS0;
				const float pr = vDOrg.x * flS0 - vDOrg.y * flC0;
				const float pz = vDOrg.z;

				const float dxN = aNeuralPos[k].x - pf;
				const float dyN = aNeuralPos[k].y - pr;
				const float dzN = aNeuralPos[k].z - pz;
				const float errXY = hypotf(dxN, dyN);
				const float errZ = fabsf(dzN);
				dNeuralSq += double(dxN * dxN + dyN * dyN + dzN * dzN);

				const float dxA = aAnalyticPos[k].x - pf;
				const float dyA = aAnalyticPos[k].y - pr;
				const float dzA = aAnalyticPos[k].z - pz;
				dAnalyticSq += double(dxA * dxA + dyA * dyA + dzA * dzA);
				++iValidKnots;

				const auto& fHi = tWin.m_vFuture[iHi];
				const auto& fPrev = (iHi > 0) ? tWin.m_vFuture[iHi - 1] : tWin.m_tAnchor;
				float flWishFwd = fHi.m_flWishFwd;
				float flWishSide = fHi.m_flWishSide;
				if (iHi == int(tWin.m_vFuture.size()) - 1)
					InferWishDir(fPrev, fHi, flWishFwd, flWishSide);
				const float flDt = float(std::max(fHi.m_iServerTick - fPrev.m_iServerTick, 1));
				const float flTurnDeg = std::clamp(Math::NormalizeAngle(fHi.m_flEyeYaw - fPrev.m_flEyeYaw) / flDt, -15.f, 15.f);

				float* pT = tSample.m_aTarget.data() + k * 8;
				pT[0] = std::clamp(flWishFwd, -1.f, 1.f);
				pT[1] = std::clamp(flWishSide, -1.f, 1.f);
				pT[2] = std::clamp(flTurnDeg / 15.f, -1.f, 1.f);
				pT[3] = fHi.m_uJumping ? 1.f : 0.f;
				pT[4] = (fHi.m_iFlags & FL_DUCKING) ? 1.f : 0.f;
				pT[5] = std::clamp(errXY, 1.f, 128.f);
				pT[6] = std::clamp(errZ, 1.f, 128.f);
				pT[7] = (errXY < 48.f) ? 1.f : 0.f;
				tSample.m_uObsKnotMask |= uint16_t(1u << k);
			}

			if (iValidKnots < 6)
				return;

			const float flNeuralRmse = float(std::sqrt(dNeuralSq / double(iValidKnots)));
			const float flAnalyticRmse = float(std::sqrt(dAnalyticSq / double(iValidKnots)));
			const int iValCount = m_iValidationSamples.load(std::memory_order_relaxed);
			if (iValCount == 0)
			{
				m_flNeuralEmaErr.store(flNeuralRmse, std::memory_order_relaxed);
				m_flAnalyticEmaErr.store(flAnalyticRmse, std::memory_order_relaxed);
			}
			else
			{
				const float nEma = m_flNeuralEmaErr.load(std::memory_order_relaxed);
				const float aEma = m_flAnalyticEmaErr.load(std::memory_order_relaxed);
				m_flNeuralEmaErr.store(0.95f * nEma + 0.05f * flNeuralRmse, std::memory_order_relaxed);
				m_flAnalyticEmaErr.store(0.95f * aEma + 0.05f * flAnalyticRmse, std::memory_order_relaxed);
			}
			m_iValidationSamples.fetch_add(1, std::memory_order_relaxed);

			++uTotalSeen;
			if (vAnchorReservoir.size() < 1024)
				vAnchorReservoir.push_back(tSample);
			else
			{
				std::uniform_int_distribution<uint64_t> dist(0, uTotalSeen - 1);
				const uint64_t r = dist(tRng);
				if (r < 1024)
					vAnchorReservoir[size_t(r)] = tSample;
			}
			if (qRecentRing.size() >= 1024)
				qRecentRing.pop_front();
			qRecentRing.push_back(std::move(tSample));
			++uNewValidSinceStep;
			++uSamplesSinceSave;
		};

		while (!tStop.stop_requested() && m_bRunning.load(std::memory_order_acquire))
		{
			std::array<bool, kMaxPlayers> aResetCopy = {};
			{
				std::lock_guard lock(m_mtxReset);
				aResetCopy = m_aPendingReset;
				m_aPendingReset.fill(false);
			}
			for (int i = 1; i < kMaxPlayers; ++i)
			{
				if (aResetCopy[i])
				{
					aPending[i].clear();
					aLastAnchorTick[i] = -1000;
				}
			}

			std::deque<ObsEvent_t> qLocal;
			{
				std::lock_guard lock(m_mtxObsQueue);
				qLocal.swap(m_qObsEvents);
			}

			for (auto& tEv : qLocal)
			{
				const int iIdx = tEv.m_iEntIndex;
				if (iIdx <= 0 || iIdx >= kMaxPlayers)
					continue;
				auto& vWins = aPending[iIdx];

				for (auto it = vWins.begin(); it != vWins.end();)
				{
					const int iPrevTick = it->m_vFuture.empty() ? it->m_tAnchor.m_iServerTick : it->m_vFuture.back().m_iServerTick;
					const Vec3& vPrevOrg = it->m_vFuture.empty() ? it->m_tAnchor.m_vOrigin : it->m_vFuture.back().m_vOrigin;
					const int dt = tEv.m_tFrame.m_iServerTick - iPrevTick;
					const float dpos = (tEv.m_tFrame.m_vOrigin - vPrevOrg).Length();
					if (dt <= 0 || dt > 8 || dpos > 60.f * float(dt) || (tEv.m_tFrame.m_uSteamId32 && it->m_uSteamId32 && tEv.m_tFrame.m_uSteamId32 != it->m_uSteamId32))
					{
						it = vWins.erase(it);
						continue;
					}
					if (!it->m_vFuture.empty())
					{
						auto& tPrevFut = it->m_vFuture.back();
						InferWishDir(tPrevFut, tEv.m_tFrame, tPrevFut.m_flWishFwd, tPrevFut.m_flWishSide);
					}
					it->m_vFuture.push_back(tEv.m_tFrame);
					const int iObsDt = tEv.m_tFrame.m_iServerTick - it->m_tAnchor.m_iServerTick;
					if (iObsDt >= 121)
					{
						fEvalWindowAndCommit(*it, 120);
						it = vWins.erase(it);
					}
					else
						++it;
				}

				if (tEv.m_bHasSnapshot && (tEv.m_tFrame.m_iServerTick - aLastAnchorTick[iIdx]) >= 16 && vWins.size() < 8)
				{
					PendingWindow_t tWin;
					tWin.m_iEntIndex = iIdx;
					tWin.m_uSteamId32 = tEv.m_tFrame.m_uSteamId32;
					tWin.m_tAnchor = tEv.m_tFrame;
					tWin.m_aXSeq = tEv.m_aXSeq;
					tWin.m_aXCtx = tEv.m_aXCtx;
					tWin.m_tBaseline = tEv.m_tBaseline;
					vWins.push_back(std::move(tWin));
					aLastAnchorTick[iIdx] = tEv.m_tFrame.m_iServerTick;
				}
			}

			std::deque<ShotFeedback_t> qShotLocal;
			{
				std::lock_guard lock(m_mtxShotQueue);
				qShotLocal.swap(m_qShotEvents);
			}
			for (const auto& tFb : qShotLocal)
			{
				if (tFb.m_flHullGap <= 0.f || qRecentRing.empty())
					continue;
				const size_t nCurr = qRecentRing.size();
				for (size_t i = nCurr; i > 0; --i)
				{
					const auto& tCand = qRecentRing[i - 1];
					const bool bSteamMatch = (tFb.m_uSteamId32 != 0 && tCand.m_uSteamId32 == tFb.m_uSteamId32);
					const bool bTickMatch = std::abs(tCand.m_iAnchorTick - tFb.m_iStartTick) <= 48;
					if (bSteamMatch && bTickMatch)
					{
						TrainingSample_t tUp = tCand;
						tUp.m_flWeight = 2.f;
						if (qRecentRing.size() >= 1024)
							qRecentRing.pop_front();
						qRecentRing.push_back(std::move(tUp));
						++uNewValidSinceStep;
						++uSamplesSinceSave;
						break;
					}
				}
			}

			if (Vars::Aimbot::Projectile::NeuralOnlineLearning.Value
				&& uNewValidSinceStep >= 32
				&& (vAnchorReservoir.size() + qRecentRing.size()) >= 32)
			{
				constexpr uint32_t kBatch = 32;
				std::vector<TrainingSample_t> vBatch;
				vBatch.reserve(kBatch);
				const uint32_t uAnchorCount = std::min<uint32_t>(16u, uint32_t(vAnchorReservoir.size()));
				const uint32_t uRecentCount = kBatch - uAnchorCount;

				if (uAnchorCount > 0)
				{
					std::uniform_int_distribution<size_t> dA(0, vAnchorReservoir.size() - 1);
					for (uint32_t i = 0; i < uAnchorCount; ++i)
						vBatch.push_back(vAnchorReservoir[dA(tRng)]);
				}
				if (uRecentCount > 0 && !qRecentRing.empty())
				{
					std::uniform_int_distribution<size_t> dR(0, qRecentRing.size() - 1);
					for (uint32_t i = 0; i < uRecentCount; ++i)
						vBatch.push_back(qRecentRing[dR(tRng)]);
				}

				std::vector<float> vXSeq(kBatch * kSeqLen * kSeqDim);
				std::vector<float> vXCtx(kBatch * kCtxDim);
				std::vector<float> vTarget(kBatch * kOutSteps * kOutDims);
				std::vector<float> vPred(kBatch * kOutSteps * kOutDims);

				for (uint32_t b = 0; b < kBatch; ++b)
				{
					std::memcpy(vXSeq.data() + size_t(b) * kSeqLen * kSeqDim, vBatch[b].m_aXSeq.data(), kSeqLen * kSeqDim * sizeof(float));
					std::memcpy(vXCtx.data() + size_t(b) * kCtxDim, vBatch[b].m_aXCtx.data(), kCtxDim * sizeof(float));
					std::memcpy(vTarget.data() + size_t(b) * kOutSteps * kOutDims, vBatch[b].m_aTarget.data(), kOutSteps * kOutDims * sizeof(float));
				}

				auto fCalcMse = [&](const float* pP, const float* pT, const std::vector<TrainingSample_t>& vS) -> float
				{
					double dSum = 0.0;
					size_t uCnt = 0;
					for (uint32_t b = 0; b < kBatch; ++b)
					{
						for (int k = 0; k < kOutSteps; ++k)
						{
							if (!(vS[b].m_uObsKnotMask & (1u << k)))
								continue;
							for (int c = 0; c < kOutDims; ++c)
							{
								const size_t idx = (size_t(b) * kOutSteps + k) * kOutDims + c;
								const double d = double(pP[idx]) - double(pT[idx]);
								dSum += 0.5 * d * d;
								++uCnt;
							}
						}
					}
					return uCnt > 0 ? float(dSum / double(uCnt)) : 0.f;
				};

				tRt.forward(vXSeq.data(), vXCtx.data(), vPred.data(), kBatch);
				for (uint32_t b = 0; b < kBatch; ++b)
				{
					for (int k = 0; k < kOutSteps; ++k)
					{
						const bool bObserved = (vBatch[b].m_uObsKnotMask & (1u << k)) != 0;
						for (int c = 0; c < kOutDims; ++c)
						{
							const size_t idx = (size_t(b) * kOutSteps + k) * kOutDims + c;
							if (!bObserved)
								vTarget[idx] = vPred[idx];
							else if (c == 5 || c == 6)
								vTarget[idx] = std::max(1.f, vPred[idx] + std::clamp(vTarget[idx] - vPred[idx], -0.5f, 0.5f));
						}
					}
				}

				const float flValBefore = fCalcMse(vPred.data(), vTarget.data(), vBatch);
				const std::vector<float> vSavedPlastic = tRt.weights.plastic_theta;

				tRt.train_cfg.lr = std::clamp(Vars::Aimbot::Projectile::NeuralOnlineLR.Value, 1e-5f, 3e-3f);
				tRt.train_cfg.lambda_anchor = 1e-2f;
				tRt.train_cfg.max_grad_norm = 1.f;
				const auto tMetrics = tRt.train_step(vXSeq.data(), vXCtx.data(), vTarget.data(), kBatch);

				bool bPromote = tMetrics.applied;
				if (bPromote)
				{
					for (float v : tRt.weights.plastic_theta)
					{
						if (!std::isfinite(v))
						{
							bPromote = false;
							break;
						}
					}
				}
				if (bPromote)
				{
					tRt.forward(vXSeq.data(), vXCtx.data(), vPred.data(), kBatch);
					const float flValAfter = fCalcMse(vPred.data(), vTarget.data(), vBatch);
					if (!std::isfinite(flValAfter) || flValAfter > flValBefore * 1.03f + 1e-4f)
						bPromote = false;
				}

				if (bPromote)
				{
					{
						std::lock_guard lock(m_mtxWeights);
						m_vPromotedPlastic = tRt.weights.plastic_theta;
						m_uWeightVersion.fetch_add(1, std::memory_order_release);
					}
					m_iTrainSteps.fetch_add(1, std::memory_order_relaxed);
					++uPromotedSinceSave;
					if (Vars::Aimbot::Projectile::NeuralSaveCheckpoints.Value && uPromotedSinceSave >= 20)
					{
						SaveCheckpointsAtomic(tRt.weights, vAnchorReservoir, qRecentRing, F::Configs.m_sConfigPath);
						uPromotedSinceSave = 0;
						uSamplesSinceSave = 0;
					}
				}
				else
				{
					tRt.weights.plastic_theta = vSavedPlastic;
					if (tRt.has_cuda)
						tRt.cuda.sync_plastic_weights(tRt.weights);
					if (tRt.has_dml)
						tRt.dml.sync_plastic_weights(tRt.weights);
				}

				uNewValidSinceStep = 0;
			}

			if (m_bFlushCheckpoint.exchange(false, std::memory_order_acq_rel))
			{
				if (Vars::Aimbot::Projectile::NeuralSaveCheckpoints.Value && (uPromotedSinceSave > 0 || uSamplesSinceSave > 0))
				{
					SaveCheckpointsAtomic(tRt.weights, vAnchorReservoir, qRecentRing, F::Configs.m_sConfigPath);
					uPromotedSinceSave = 0;
					uSamplesSinceSave = 0;
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(150));
		}

		if (Vars::Aimbot::Projectile::NeuralSaveCheckpoints.Value && (uPromotedSinceSave > 0 || uSamplesSinceSave > 0))
			SaveCheckpointsAtomic(tRt.weights, vAnchorReservoir, qRecentRing, F::Configs.m_sConfigPath);

		tRt.destroy();
	});
}

void CNeuralPrediction::Shutdown()
{
	if (!m_bInitialized.exchange(false, std::memory_order_acq_rel))
		return;

	m_bFlushCheckpoint.store(true, std::memory_order_release);
	m_bRunning.store(false, std::memory_order_release);
	if (m_tInferThread.joinable())
		m_tInferThread.request_stop();
	if (m_tTrainThread.joinable())
		m_tTrainThread.request_stop();

	m_uInferWake.fetch_add(1, std::memory_order_release);
	m_uInferWake.notify_all();

	if (m_tInferThread.joinable())
		m_tInferThread.join();
	if (m_tTrainThread.joinable())
		m_tTrainThread.join();

	OnLevelChange();
}

void CNeuralPrediction::OnLevelChange()
{
	m_bFlushCheckpoint.store(true, std::memory_order_release);
	for (int i = 0; i < kMaxPlayers; ++i)
		ResetPlayer(i);
	{
		std::lock_guard lock(m_mtxAdapt);
		m_mSteamAdapt.clear();
	}
	{
		std::lock_guard lock(m_mtxObsQueue);
		m_qObsEvents.clear();
	}
	{
		std::lock_guard lock(m_mtxShotQueue);
		m_qShotEvents.clear();
	}
}

void CNeuralPrediction::ResetPlayer(int iEntIndex)
{
	if (iEntIndex <= 0 || iEntIndex >= kMaxPlayers)
		return;

	m_aHist[iEntIndex].clear();
	{
		auto& tMail = m_aMailbox[iEntIndex];
		while (tMail.m_tLock.test_and_set(std::memory_order_acquire)) {}
		tMail.m_bDirty.store(false, std::memory_order_release);
		tMail.m_flSimTime = -1.f;
		tMail.m_tLock.clear(std::memory_order_release);
	}
	{
		auto& tPub = m_aPublished[iEntIndex];
		const uint64_t s = tPub.m_uSeq.load(std::memory_order_relaxed);
		tPub.m_uSeq.store(s + 1, std::memory_order_release);
		std::atomic_thread_fence(std::memory_order_release);
		const int iNext = 1 - tPub.m_iIdx.load(std::memory_order_relaxed);
		tPub.m_aBuf[iNext] = {};
		tPub.m_iIdx.store(iNext, std::memory_order_release);
		tPub.m_uSeq.store(s + 2, std::memory_order_release);
	}
	{
		std::lock_guard lock(m_mtxReset);
		m_aPendingReset[iEntIndex] = true;
	}
}

void CNeuralPrediction::OnPlayerUpdate(CTFPlayer* pPlayer, const Vec3& vOrigin, const Vec3& vVelocity, float flSimTime, float flEyeYaw, float flMaxSpeed, bool bDucked, const NeuralBaseline_t& tBaseline)
{
	if (!m_bInitialized.load(std::memory_order_acquire) || !pPlayer)
		return;
	if (Vars::Aimbot::Projectile::NeuralPrediction.Value == Vars::Aimbot::Projectile::NeuralPredictionEnum::Off)
		return;

	const int iIndex = pPlayer->entindex();
	if (iIndex <= 0 || iIndex >= kMaxPlayers)
		return;

	uint32_t uSteamId32 = 0;
	player_info_t tPlayerInfo = {};
	if (I::EngineClient && I::EngineClient->GetPlayerInfo(iIndex, &tPlayerInfo))
		uSteamId32 = tPlayerInfo.friendsID;

	CTraceFilterWorldAndPropsOnly tFilter(pPlayer);
	float flGroundDist = 512.f, flCeilingDist = 512.f;
	Vec3 vGroundNormal = {};
	std::array<float, 8> aWallDists = {};
	std::array<float, 8> aLedgeDepths = {};
	bool bWallContact = false;
	F::World.ProbePlayerGeometry(vOrigin, flEyeYaw, bDucked, &tFilter, flGroundDist, vGroundNormal, flCeilingDist, aWallDists.data(), aLedgeDepths.data(), bWallContact);

	ObsFrame_t tFrame;
	tFrame.m_uSteamId32 = uSteamId32;
	tFrame.m_iServerTick = TIME_TO_TICKS(flSimTime);
	tFrame.m_flSimTime = flSimTime;
	tFrame.m_vOrigin = vOrigin;
	tFrame.m_vVelocity = vVelocity;
	tFrame.m_flEyePitch = pPlayer->m_angEyeAnglesX();
	tFrame.m_flEyeYaw = flEyeYaw;
	tFrame.m_flMaxSpeed = flMaxSpeed;
	tFrame.m_flGroundDist = flGroundDist;
	tFrame.m_vGroundNormal = vGroundNormal;
	tFrame.m_flCeilingDist = flCeilingDist;
	tFrame.m_aWallDists = aWallDists;
	tFrame.m_aLedgeDepths = aLedgeDepths;
	tFrame.m_iFlags = pPlayer->m_fFlags();
	tFrame.m_iHealth = pPlayer->m_iHealth();
	tFrame.m_iMaxHealth = pPlayer->GetMaxHealth();
	tFrame.m_uClassId = static_cast<uint8_t>(std::clamp(pPlayer->m_iClass(), 0, 15));
	tFrame.m_uWaterLevel = static_cast<uint8_t>(pPlayer->m_nWaterLevel());
	tFrame.m_uAirDash = static_cast<uint8_t>(std::clamp(pPlayer->m_iAirDash(), 0, 255));
	tFrame.m_uAirDucked = static_cast<uint8_t>(std::clamp(pPlayer->m_nAirDucked(), 0, 255));
	tFrame.m_uJumping = pPlayer->m_bJumping() ? 1 : 0;
	tFrame.m_uBlast = pPlayer->InCond(TF_COND_BLASTJUMPING) ? 1 : 0;
	tFrame.m_uCharge = pPlayer->InCond(TF_COND_SHIELD_CHARGE) ? 1 : 0;
	tFrame.m_uPara = (pPlayer->InCond(TF_COND_PARACHUTE_ACTIVE) || pPlayer->InCond(TF_COND_PARACHUTE_DEPLOYED)) ? 1 : 0;
	tFrame.m_uLostFooting = pPlayer->InCond(TF_COND_LOST_FOOTING) ? 1 : 0;
	tFrame.m_uWallContact = bWallContact ? 1 : 0;

	auto& vHist = m_aHist[iIndex];
	if (!vHist.empty())
	{
		auto& tLast = vHist.back();
		const int dt = tFrame.m_iServerTick - tLast.m_iServerTick;
		const float dpos = (tFrame.m_vOrigin - tLast.m_vOrigin).Length();
		if (dt <= 0 || dt > 8 || dpos > 60.f * float(dt) || (uSteamId32 && tLast.m_uSteamId32 && uSteamId32 != tLast.m_uSteamId32))
		{
			vHist.clear();
			std::lock_guard lock(m_mtxReset);
			m_aPendingReset[iIndex] = true;
		}
		else
		{
			InferWishDir(tLast, tFrame, tLast.m_flWishFwd, tLast.m_flWishSide);
			tFrame.m_flWishFwd = tLast.m_flWishFwd;
			tFrame.m_flWishSide = tLast.m_flWishSide;
		}
	}

	vHist.push_back(tFrame);
	if (vHist.size() > 64)
		vHist.pop_front();

	std::array<float, kSeqLen * kSeqDim> aXSeq = {};
	std::array<float, kCtxDim> aXCtx = {};
	const bool bHasSnapshot = vHist.size() >= 4;
	if (bHasSnapshot)
	{
		PackTensors(vHist, aXSeq.data(), aXCtx.data());

		auto& tMail = m_aMailbox[iIndex];
		if (!tMail.m_tLock.test_and_set(std::memory_order_acquire))
		{
			tMail.m_aXSeq = aXSeq;
			tMail.m_aXCtx = aXCtx;
			tMail.m_flSimTime = flSimTime;
			tMail.m_flEyeYaw = flEyeYaw;
			tMail.m_uSteamId32 = uSteamId32;
			tMail.m_bDirty.store(true, std::memory_order_release);
			tMail.m_tLock.clear(std::memory_order_release);
			m_uInferWake.fetch_add(1, std::memory_order_release);
			m_uInferWake.notify_one();
		}
	}

	{
		std::lock_guard lock(m_mtxObsQueue);
		if (m_qObsEvents.size() < 4096)
		{
			ObsEvent_t tEv;
			tEv.m_iEntIndex = iIndex;
			tEv.m_tFrame = tFrame;
			tEv.m_bHasSnapshot = bHasSnapshot;
			if (bHasSnapshot)
			{
				tEv.m_aXSeq = aXSeq;
				tEv.m_aXCtx = aXCtx;
				tEv.m_tBaseline = tBaseline;
			}
			m_qObsEvents.push_back(std::move(tEv));
		}
	}
}

void CNeuralPrediction::OnPlayerEvent(int iEntIndex, bool bDied)
{
	if (!m_bInitialized.load(std::memory_order_acquire))
		return;
	if (iEntIndex <= 0 || iEntIndex >= kMaxPlayers)
		return;
	if (bDied)
		ResetPlayer(iEntIndex);
}

void CNeuralPrediction::RecordShotFeedback(const ShotFeedback_t& tFeedback)
{
	if (!m_bInitialized.load(std::memory_order_acquire))
		return;
	if (!tFeedback.m_bTargetAlive || tFeedback.m_bOccluded || tFeedback.m_bHasSpread)
		return;
	if (std::abs(tFeedback.m_iLateTicks) > 2)
		return;
	if (tFeedback.m_flHullGap <= 0.f && !tFeedback.m_bHitEvent)
		return;

	if (tFeedback.m_uSteamId32 != 0)
	{
		const Vec3 vErr = tFeedback.m_vActual - tFeedback.m_vPredicted;
		const float flErr2D = vErr.Length2D();
		const float flRad = Math::Deg2Rad(tFeedback.m_flTargetEyeYaw);
		const float flC = cosf(flRad);
		const float flS = sinf(flRad);
		const float flErrFwd = vErr.x * flC + vErr.y * flS;
		const float flErrSide = vErr.x * flS - vErr.y * flC;

		std::lock_guard lock(m_mtxAdapt);
		auto& tAdapt = m_mSteamAdapt[tFeedback.m_uSteamId32];
		tAdapt.m_flEmaErr = (tAdapt.m_iSamples == 0) ? flErr2D : (0.85f * tAdapt.m_flEmaErr + 0.15f * flErr2D);
		++tAdapt.m_iSamples;
		if (tFeedback.m_flHullGap > 0.f)
		{
			tAdapt.m_flBiasFwd = std::clamp(tAdapt.m_flBiasFwd + std::clamp(flErrFwd / 256.f, -0.05f, 0.05f), -0.25f, 0.25f);
			tAdapt.m_flBiasSide = std::clamp(tAdapt.m_flBiasSide + std::clamp(flErrSide / 256.f, -0.05f, 0.05f), -0.25f, 0.25f);
			if (fabsf(flErrSide) > 8.f)
				tAdapt.m_flBiasTurn = std::clamp(tAdapt.m_flBiasTurn + std::clamp(flErrSide / 64.f, -0.35f, 0.35f), -2.f, 2.f);
		}
		else if (tFeedback.m_bHitEvent)
		{
			tAdapt.m_flBiasFwd *= 0.9f;
			tAdapt.m_flBiasSide *= 0.9f;
			tAdapt.m_flBiasTurn *= 0.9f;
		}
	}

	{
		std::lock_guard lock(m_mtxShotQueue);
		if (m_qShotEvents.size() < 256)
			m_qShotEvents.push_back(tFeedback);
	}
}

bool CNeuralPrediction::GetPrediction(int iEntIndex, uint32_t uSteamId32, NeuralHorizon_t& tOut) const
{
	if (!m_bInitialized.load(std::memory_order_acquire) || iEntIndex <= 0 || iEntIndex >= kMaxPlayers)
		return false;

	const auto& tPub = m_aPublished[iEntIndex];
	for (int iAttempt = 0; iAttempt < 4; ++iAttempt)
	{
		const uint64_t s1 = tPub.m_uSeq.load(std::memory_order_acquire);
		if (s1 & 1ull)
			continue;
		const int idx = tPub.m_iIdx.load(std::memory_order_acquire);
		const NeuralHorizon_t tCopy = tPub.m_aBuf[idx];
		std::atomic_thread_fence(std::memory_order_acquire);
		const uint64_t s2 = tPub.m_uSeq.load(std::memory_order_relaxed);
		if (s1 == s2)
		{
			if (!tCopy.m_bValid)
				return false;
			if (uSteamId32 && tCopy.m_uSteamId32 && tCopy.m_uSteamId32 != uSteamId32)
				return false;
			tOut = tCopy;
			return true;
		}
	}
	return false;
}

bool CNeuralPrediction::ShouldFallback() const
{
	if (Vars::Aimbot::Projectile::NeuralPrediction.Value == Vars::Aimbot::Projectile::NeuralPredictionEnum::NeuralOnly)
		return false;
	const int iSamples = m_iValidationSamples.load(std::memory_order_relaxed);
	if (iSamples < 20)
		return false;
	const float flNeural = m_flNeuralEmaErr.load(std::memory_order_relaxed);
	const float flAnalytic = m_flAnalyticEmaErr.load(std::memory_order_relaxed);
	return flNeural > flAnalytic * 1.05f;
}
