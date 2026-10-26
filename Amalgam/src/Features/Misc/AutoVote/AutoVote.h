#pragma once
#include "../../../SDK/SDK.h"

class CAutoVote
{
private:
	struct Vote_t
	{
		int m_iTeam = -1;
		int m_iCaller = -1;
		int m_iTarget = -1;
		float m_flStartTime = 0.f;
		float m_flVoteTime = 0.f;
		bool m_bVoted = false;
	};

	float m_flCallCooldownExpireTime = 0.f;
	std::unordered_map<int, Vote_t> m_mPendingVotes = {};
public:
	void OnVoteStart(const int iTeam, const int iVoteID, const int iCaller, const int iTarget);
	void OnVoteEnd(int iVoteID);
	void OnCallVoteFail(int iTimeLeft);

	void Run(CTFPlayer* pLocal);
	void Reset();

	bool m_bActiveVote = false;
};

ADD_FEATURE(CAutoVote, AutoVote);
