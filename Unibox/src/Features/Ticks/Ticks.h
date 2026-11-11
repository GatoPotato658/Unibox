#pragma once
#include "../../SDK/SDK.h"
#include "../ImGui/IndicatorCache.h"

struct TickbaseFix_t
{
	int m_iCommand = -1;
	int m_iSimulationTicks = 0;
	int m_iServerTick = 0;
	int m_iFrameTicks = 1;
	int m_iCorrectionTicks = 0;
	bool m_bMultiplayer = false;
	bool m_bSent = false;

	int AdjustedTick(int iTickBase) const;
	bool SameFrame(const TickbaseFix_t& tOther) const;
};

struct SentBatch_t
{
	int m_iLastCommand = -1;
	double m_dSent = 0.0;
	int m_iAfter = -1;
	int m_iBy = -1;
};

class CTicks
{
private:
	struct DrawCache_t
	{
		int m_iTicks = 0;
		int m_iMaxTicks = 0;
		bool m_bWait = false;
		bool m_bValid = false;
	};
	CIndicatorCache<DrawCache_t> m_tDrawCache = {};

	void SendMoveFunc();
	void MoveFunc(float accumulated_extra_samples, bool bFinalTick);
	void MoveManage();

	void Recharge(CTFPlayer* pLocal);
	void Warp();
	void Doubletap(CTFPlayer* pLocal, CUserCmd* pCmd);
	bool ValidWeapon(CTFWeaponBase* pWeapon);

	void ManagePacket(CUserCmd* pCmd);

	bool m_bGoalReached = true;
	Vec3 m_vShootPos = {};
	std::optional<Vec3> m_vShootAngle = std::nullopt;

	bool m_bPredictAntiwarp = false;
	bool m_bTimingUnsure = false; // we aren't sure when we'll actually fire, hold aim

	double ClockNow();
	int ArrivalTick(double dClock);
	void LocateOffset();
	int ProcessTickLimit();
	bool RecordFix(int iCommand, int iCommands);
	int GroupStart(int iCommand);
	void OnMoveSent(int iCommand, int iCommands);

	std::array<TickbaseFix_t, MULTIPLAYER_BACKUP> m_aFixes = {};
	std::array<SentBatch_t, MULTIPLAYER_BACKUP> m_aBatches = {};
	int m_iSendTick = 0;
	int m_iLastSendTick = -1;
	double m_dSendClock = 0.0;
	double m_dLastSendClock = 0.0;
	int m_iServerTick = -1;
	int m_iServerAck = -1;
	double m_dOffsetLow = 0.0;
	double m_dOffsetHigh = 0.0;
	bool m_bOffsetKnown = false;

public:
	void Move(float accumulated_extra_samples, bool bFinalTick);
	void CreateMove(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void CacheDrawInfo(CTFPlayer* pLocal);
	void Draw();
	void Reset();

	void Start(CTFPlayer* pLocal, CUserCmd* pCmd);
	void End(CTFPlayer* pLocal, CUserCmd* pCmd);

	void AntiWarp(CTFPlayer* pLocal, float flYaw, float& flForwardMove, float& flSideMove, int iTicks);
	void AntiWarp(CTFPlayer* pLocal, float flYaw, float& flForwardMove, float& flSideMove);
	void AntiWarp(CTFPlayer* pLocal, CUserCmd* pCmd);

	bool CanChoke(bool bCanShift, int iMaxTicks);
	bool CanChoke(bool bCanShift = false);
	int GetTicks(CTFWeaponBase* pWeapon = nullptr);
	int GetShotsWithinPacket(CTFWeaponBase* pWeapon, int iTicks = Vars::Doubletap::TickLimit.Value);

	void SaveShootPos(CTFPlayer* pLocal);
	Vec3 GetShootPos();
	void SaveShootAngle(CUserCmd* pCmd);
	Vec3* GetShootAngle();
	bool IsTimingUnsure();

	static double PacketTime(bool bLoopback);
	void ObserveServerTick();
	bool AdjustTickbase(CTFPlayer* pPlayer, int iCommand);

	bool m_bDoubletap = false;
	bool m_bWarp = false;
	bool m_bRecharge = false;
	bool m_bRechargeQueue = false;
	bool m_bAntiWarp = false;

	int m_iShiftedTicks = 0;
	int m_iShiftedGoal = 0;
	int m_iShiftStart = 0;
	bool m_bShifted = false;

	int m_iWait = 0;
	int m_iMaxUsrCmdProcessTicks = 24;
	int m_iMaxShift = 24;
	int m_iDeficit = 0;
};

ADD_FEATURE(CTicks, Ticks);
