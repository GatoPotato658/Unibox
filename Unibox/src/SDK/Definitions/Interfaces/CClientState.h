#pragma once
#include "Interface.h"
#include "../Network/INetChannel.h"
#include "../DataTable/ClientClass.h"
#include "../DataTable/ServerClass.h"
#include "../Utl/CUtlVector.h"
#include "../Utl/CUtlString.h"
#include "../Definitions.h"


#define	MAX_OSPATH 260 // max length of a filesystem pathname

class IChangeFrameList;

class PackedEntity
{
public:
	ServerClass* m_pServerClass;
	ClientClass* m_pClientClass;

	int m_nEntityIndex;
	int m_ReferenceCount;

private:
	CUtlVector<CSendProxyRecipients> m_Recipients;

	void* m_pData;
	int m_nBits;
	IChangeFrameList* m_pChangeFrameList;

	unsigned int m_nSnapshotCreationTick : 31;
	unsigned int m_nShouldCheckCreationTick : 1;
};

class CEventInfo
{
public:
	short classID;
	float fire_delay;
	byte pad8[88];
	CEventInfo* prev;
	CEventInfo* next;
};
static_assert(offsetof(CEventInfo, fire_delay) == 0x4);
static_assert(offsetof(CEventInfo, next) == 0x68);

struct CClockDriftMgr
{
	float m_ClockOffsets[16];
	int m_iCurClockOffset;
	int m_nServerTick;
	int m_nClientTick;
};

class CClientState
{
public:
	byte pad0[24];
	int m_Socket;
	CNetChannel* m_NetChannel;
	unsigned int m_nChallengeNr;
	double m_flConnectTime;
	int m_nRetryNumber;
	char m_szRetryAddress[MAX_OSPATH];
	CUtlString m_sRetrySourceTag;
	int m_retryChallenge;
	int m_nSignonState;
	double m_flNextCmdTime;
	int m_nServerCount;
	unsigned __int64 m_ulGameServerSteamID;
	int m_nCurrentSequence;
	CClockDriftMgr m_ClockDriftMgr;
	int m_nDeltaTick;
	bool m_bPaused;
	float m_flPausedExpireTime;
	int m_nViewEntity;
	int m_nPlayerSlot;
	char m_szLevelFileName[128];
	byte pad1[132];
	char m_szLevelBaseName[128];
	byte pad2[132];
	int m_nMaxClients;
	PackedEntity* m_pEntityBaselines[2][MAX_EDICTS];
	byte pad3[2068];
	void* m_StringTableContainer;
	bool m_bRestrictServerCommands;
	bool m_bRestrictClientCommands;
	byte pad4[106];
	bool insimulation;
	int oldtickcount;
	float m_tickRemainder;
	float m_frameTime;
	int lastoutgoingcommand;
	int chokedcommands;
	int last_command_ack;
	int command_ack;
	int m_nSoundSequence;
	bool ishltv;
	bool isreplay;
	byte pad5[278];
	int demonum;
	CUtlString demos[32];
	byte pad6[344184];
	bool m_bMarkedCRCsUnverified;

public:
	inline void SendStringCmd(const char* command)
	{
		S::CBaseClientState_SendStringCmd.Call<void>(this, command);
	}

	inline void ForceFullUpdate()
	{
		S::CBaseClientState_ForceFullUpdate.Call<void>(this);
	}

	inline bool IsPaused()
	{
		return S::CClientState_IsPaused.Call<bool>(this);
	}
};
