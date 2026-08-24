#include "AutoQueue.h"
#include "MvmQueue.h"
#include "../../Players/PlayerUtils.h"
#include "../../NavBot/NavEngine/NavEngine.h"
#include "../Misc.h"
#include "../NamedPipe/NamedPipe.h"

void CAutoQueue::Run()
{
	static float flLastQueueTime = 0.0f;
	static bool bQueuedOnce = false;
	static bool bWasInGame = false;
	static bool bWasDisconnected = false;
	static bool bQueuedFromRQif = false;

	const bool bInGameNow = I::EngineClient->IsInGame();
	const bool bIsLoadingMapNow = I::EngineClient->IsDrawingLoadingImage();
	const bool bIsConnectedNow = I::EngineClient->IsConnected();
	const float flCurrentTime = I::GlobalVars->realtime;
	const char* pszLevelName = I::EngineClient->GetLevelName();
	const std::string sLevelName = pszLevelName ? pszLevelName : "";

	if (sLevelName != m_sLastLevelName)
	{
		m_sLastLevelName = sLevelName;
		m_bNavmeshAbandonTriggered = false;
		m_flNavmeshAbandonStartTime = 0.0f;
		m_bAutoDumpedThisMatch = false;
		m_flAutoDumpStartTime = 0.0f;
	}

	if (!Vars::Misc::Queueing::AutoCasualQueue.Value || !Vars::Misc::Queueing::AutoAbandonIfNoNavmesh.Value || !Vars::Misc::Movement::NavEngine::Enabled.Value)
	{
		m_bNavmeshAbandonTriggered = false;
		m_flNavmeshAbandonStartTime = 0.0f;
	}
	else if (bInGameNow && !bIsLoadingMapNow && !m_bNavmeshAbandonTriggered)
	{
		const bool bNavMeshUnavailable = !F::NavEngine.IsNavMeshLoaded();
		if (bNavMeshUnavailable)
		{
			if (m_flNavmeshAbandonStartTime <= 0.0f)
			{
				m_flNavmeshAbandonStartTime = flCurrentTime;
				F::NavEngine.Reset(true);
			}

			if ((flCurrentTime - m_flNavmeshAbandonStartTime) >= 10.0f)
			{
				m_bNavmeshAbandonTriggered = true;
				SDK::Output("AutoQueue", "No navmesh available for current map after 10 seconds, abandoning match", WARNING_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST, ICON_MD_WARNING);
				I::TFGCClientSystem->AbandonCurrentMatch();
				bWasInGame = false;
				bWasDisconnected = true;
				flLastQueueTime = 0.0f;
				bQueuedFromRQif = false;
				return;
			}
		}
		else
			m_flNavmeshAbandonStartTime = 0.0f;
	}

	if (Vars::Misc::Queueing::AutoDumpProfiles.Value && Vars::Misc::Queueing::AutoCasualQueue.Value)
	{
		if (bInGameNow && !bIsLoadingMapNow && !m_bAutoDumpedThisMatch)
		{
			const float flDelay = std::max(0, Vars::Misc::Queueing::AutoDumpDelay.Value);
			if (m_flAutoDumpStartTime <= 0.0f)
				m_flAutoDumpStartTime = flCurrentTime;

			if ((flCurrentTime - m_flAutoDumpStartTime) >= flDelay)
			{
				const auto tResult = F::Misc.DumpProfiles(false);
				if (!tResult.m_bResourceAvailable || tResult.m_uCandidateCount == 0)
					m_flAutoDumpStartTime = flCurrentTime;
				else
				{
					m_bAutoDumpedThisMatch = true;
					m_flAutoDumpStartTime = 0.0f;

					if (I::TFGCClientSystem)
					{
						const size_t uDuplicateCount = tResult.m_uSkippedSessionDuplicate + tResult.m_uSkippedFileDuplicate;
						SDK::Output("AutoQueue", std::format("Auto dump complete: {} new profiles, {} duplicates skipped, {} comma filtered. Avatars: {} saved, {} unavailable, {} failed. Abandoning match for requeue.",
							tResult.m_uAppendedCount,
							uDuplicateCount,
							tResult.m_uSkippedComma,
							tResult.m_uAvatarsSaved,
							tResult.m_uAvatarMissed,
							tResult.m_uAvatarFailed).c_str(), SUCCESS_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST, ICON_MD_CHECK);
						I::TFGCClientSystem->AbandonCurrentMatch();
						bWasInGame = false;
						bWasDisconnected = true;
						flLastQueueTime = 0.0f;
						bQueuedFromRQif = false;
						bQueuedOnce = false;
					}
				}
			}
		}
		else if (!bInGameNow)
		{
			m_bAutoDumpedThisMatch = false;
			m_flAutoDumpStartTime = 0.0f;
		}
	}
	else
	{
		m_flAutoDumpStartTime = 0.0f;
		if (!bInGameNow)
			m_bAutoDumpedThisMatch = false;
	}

	// Auto Mann Up queue
	static bool bWasMannUpEnabled = false;
	if (!Vars::Misc::Queueing::AutoMannUpQueue.Value)
	{
		if (bWasMannUpEnabled)
		{
			F::MvmQueue.ClearMannUpCriteria();
			bWasMannUpEnabled = false;
		}
	}
	else
	{
		bWasMannUpEnabled = true;
		if (!I::TFPartyClient->BInQueueForMatchGroup(k_eTFMatchGroup_MvM_MannUp))
		{
			bool bInGame = I::EngineClient->IsInGame();
			bool bIsLoadingMap = I::EngineClient->IsDrawingLoadingImage();
			if (bIsLoadingMap && Vars::Misc::Queueing::RQLTM.Value)
				return;

			float flQueueDelay = Vars::Misc::Queueing::QueueDelay.Value == 0 ? 20.0f : Vars::Misc::Queueing::QueueDelay.Value * 60.0f;

			static float flLastQueueTimeMannUp = 0.0f;
			static bool bQueuedOnceMannUp = false;

			bool bShouldQueue = !bQueuedOnceMannUp || (flCurrentTime - flLastQueueTimeMannUp >= flQueueDelay);
			if (!bIsConnectedNow && !bIsLoadingMap)
				bShouldQueue = true;

			if (bShouldQueue && (!bIsLoadingMap || !Vars::Misc::Queueing::RQLTM.Value) && !bInGame)
			{
				F::MvmQueue.ApplyMannUpCriteria();
				I::TFPartyClient->RequestQueueForMatch(k_eTFMatchGroup_MvM_MannUp);
				flLastQueueTimeMannUp = flCurrentTime;
				bQueuedOnceMannUp = true;
			}
		}
	}

	// Auto Boot Camp queue
	static bool bWasBootCampEnabled = false;
	if (!Vars::Misc::Queueing::AutoBootCampQueue.Value)
	{
		if (bWasBootCampEnabled)
		{
			F::MvmQueue.ClearBootcampCriteria();
			bWasBootCampEnabled = false;
		}
	}
	else
	{
		bWasBootCampEnabled = true;
		if (!I::TFPartyClient->BInQueueForMatchGroup(k_eTFMatchGroup_MvM_Practice))
		{
			bool bInGame = I::EngineClient->IsInGame();
			bool bIsLoadingMap = I::EngineClient->IsDrawingLoadingImage();
			if (bIsLoadingMap && Vars::Misc::Queueing::RQLTM.Value)
				return;

			float flQueueDelay = Vars::Misc::Queueing::QueueDelay.Value == 0 ? 20.0f : Vars::Misc::Queueing::QueueDelay.Value * 60.0f;

			static float flLastQueueTimeBootCamp = 0.0f;
			static bool bQueuedOnceBootCamp = false;

			bool bShouldQueue = !bQueuedOnceBootCamp || (flCurrentTime - flLastQueueTimeBootCamp >= flQueueDelay);
			if (!bIsConnectedNow && !bIsLoadingMap)
				bShouldQueue = true;

			if (bShouldQueue && (!bIsLoadingMap || !Vars::Misc::Queueing::RQLTM.Value) && !bInGame)
			{
				F::MvmQueue.ApplyBootcampCriteria();
				I::TFPartyClient->RequestQueueForMatch(k_eTFMatchGroup_MvM_Practice);
				flLastQueueTimeBootCamp = flCurrentTime;
				bQueuedOnceBootCamp = true;
			}
		}
	}

	// Auto Competitive queue
	if (Vars::Misc::Queueing::AutoCompetitiveQueue.Value)
	{
		if (!I::TFPartyClient->BInQueueForMatchGroup(k_eTFMatchGroup_Ladder_Default))
		{
			bool bInGame = I::EngineClient->IsInGame();
			bool bIsLoadingMap = I::EngineClient->IsDrawingLoadingImage();
			bool bIsConnected = I::EngineClient->IsConnected();
			bool bHasNetChannel = I::ClientState && I::ClientState->m_NetChannel;

			float flQueueDelay = Vars::Misc::Queueing::QueueDelay.Value == 0 ? 20.0f : Vars::Misc::Queueing::QueueDelay.Value * 60.0f;

			static float flLastQueueTimeCompetitive = 0.0f;
			static bool bQueuedOnceCompetitive = false;

			bool bShouldQueue = !bQueuedOnceCompetitive || (flCurrentTime - flLastQueueTimeCompetitive >= flQueueDelay);
			if (!bIsConnectedNow && !bIsLoadingMap)
				bShouldQueue = true;

			bool bStillAttachedToServer = bInGame || bIsConnected || bHasNetChannel;

			if (bShouldQueue && !bStillAttachedToServer && !bIsLoadingMap)
			{
				I::TFPartyClient->RequestQueueForMatch(k_eTFMatchGroup_Ladder_Default);
				flLastQueueTimeCompetitive = flCurrentTime;
				bQueuedOnceCompetitive = true;
			}
		}
	}

	if (Vars::Misc::Queueing::AutoCasualQueue.Value)
	{
		bool bInGame = I::EngineClient->IsInGame();
		bool bIsLoadingMap = I::EngineClient->IsDrawingLoadingImage();
		bool bIsConnected = I::EngineClient->IsConnected();
		bool bHasNetChannel = I::ClientState && I::ClientState->m_NetChannel;
		bool bIsQueued = I::TFPartyClient->BInQueueForMatchGroup(k_eTFMatchGroup_Casual_Default);

		if (bIsLoadingMap && bIsQueued && Vars::Misc::Queueing::RQLTM.Value)
		{
			I::TFPartyClient->CancelMatchQueueRequest(k_eTFMatchGroup_Casual_Default);
			SDK::Output("AutoQueue", "Loading screen active, canceling casual queue", INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST, ICON_MD_INFO);
			bQueuedFromRQif = false;
			flLastQueueTime = flCurrentTime;
			bIsQueued = false;
		}

		if (bIsLoadingMap && Vars::Misc::Queueing::RQLTM.Value)
			return;

		float flQueueDelay = Vars::Misc::Queueing::QueueDelay.Value == 0 ? 20.0f : Vars::Misc::Queueing::QueueDelay.Value * 60.0f;

		int nPlayerCount = 0;
		bool bRQConditionMet = false;

		if (bInGame && Vars::Misc::Queueing::RQif.Value)
		{
			if (auto pResource = H::Entities.GetResource())
			{
				for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
				{
					if (!pResource->m_bValid(i) || !pResource->m_bConnected(i) || pResource->m_iUserID(i) == -1)
						continue;

					if (pResource->IsFakePlayer(i))
						continue;

					bool bShouldCount = true;
					const uint32_t uFriendsID = pResource->m_iAccountID(i);

					if (Vars::Misc::Queueing::RQIgnoreFriends.Value)
					{
#ifdef TEXTMODE
						if (uFriendsID && F::NamedPipe.IsLocalBot(uFriendsID))
							bShouldCount = false;
#endif

						if (bShouldCount && (H::Entities.IsFriend(uFriendsID) ||
							H::Entities.InParty(uFriendsID) ||
							F::PlayerUtils.HasTag(uFriendsID, F::PlayerUtils.TagToIndex(FRIEND_TAG)) ||
							F::PlayerUtils.HasTag(uFriendsID, F::PlayerUtils.TagToIndex(IGNORED_TAG)) ||
							F::PlayerUtils.HasTag(uFriendsID, F::PlayerUtils.TagToIndex(PARTY_TAG))))
							bShouldCount = false;
					}

					if (bShouldCount)
						nPlayerCount++;
				}
			}

			int nPlayersLT = Vars::Misc::Queueing::RQplt.Value;
			int nPlayersGT = Vars::Misc::Queueing::RQpgt.Value;
			if ((nPlayersLT > 0 && nPlayerCount < nPlayersLT) || (nPlayersGT > 0 && nPlayerCount > nPlayersGT))
				bRQConditionMet = true;
		}

		if (bIsQueued && bQueuedFromRQif)
		{
			bool bMaintainQueue = Vars::Misc::Queueing::RQif.Value && bInGame && bRQConditionMet;
			if (!bMaintainQueue)
			{
				I::TFPartyClient->CancelMatchQueueRequest(k_eTFMatchGroup_Casual_Default);
				SDK::Output("AutoQueue", "RQif conditions cleared, canceling casual queue", INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST, ICON_MD_INFO);
				bQueuedFromRQif = false;
				flLastQueueTime = flCurrentTime;
				bIsQueued = false;
			}
		}

		if (bIsQueued)
		{
			bWasInGame = bInGame;
			return;
		}

		if (bWasInGame && !bInGame && !bIsLoadingMap)
		{
			bWasDisconnected = true;

			if (Vars::Misc::Queueing::RQif.Value && Vars::Misc::Queueing::RQkick.Value)
				flLastQueueTime = 0.0f;
		}

		bWasInGame = bInGame;

		if (bInGame && Vars::Misc::Queueing::RQif.Value && bRQConditionMet)
		{
			if (Vars::Misc::Queueing::RQnoAbandon.Value)
			{
				I::TFPartyClient->RequestQueueForMatch(k_eTFMatchGroup_Casual_Default);
				flLastQueueTime = flCurrentTime;
				bQueuedFromRQif = true;
			}
			else
			{
				I::TFGCClientSystem->AbandonCurrentMatch();
				bWasInGame = false;
				bWasDisconnected = true;
				flLastQueueTime = 0.0f;
				bQueuedFromRQif = false;
			}
		}

		bool bShouldQueue = !bQueuedOnce || (flCurrentTime - flLastQueueTime >= flQueueDelay);
		if (!bIsConnectedNow && !bIsLoadingMap)
			bShouldQueue = true;

		bool bStillAttachedToServer = bInGame || bIsConnected || bHasNetChannel;

		if (bShouldQueue && (!bIsLoadingMap || !Vars::Misc::Queueing::RQLTM.Value) && !bStillAttachedToServer)
		{
			static bool bHasLoaded = false;
			if (!bHasLoaded)
			{
				I::TFPartyClient->LoadSavedCasualCriteria();
				bHasLoaded = true;
			}

			I::TFPartyClient->RequestQueueForMatch(k_eTFMatchGroup_Casual_Default);
			flLastQueueTime = flCurrentTime;
			bQueuedOnce = true;
			bWasDisconnected = false;
			bQueuedFromRQif = false;
		}
	}
	else
	{
		bQueuedOnce = false;
		flLastQueueTime = 0.0f;
		bQueuedFromRQif = false;
	}

}
