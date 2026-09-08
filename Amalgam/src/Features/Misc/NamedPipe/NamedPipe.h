#pragma once

#ifdef TEXTMODE
#include "../../../SDK/SDK.h"
#include <thread>
#include <atomic>
#include <fstream>
#include <shared_mutex>

class CNamedPipe
{
private:
	HANDLE m_hPipe = INVALID_HANDLE_VALUE;
	std::mutex m_pipeMutex;
	std::atomic<bool> m_shouldRun = true;
	std::thread m_pipeThread;
	std::ofstream m_logFile;
	int m_iBotId = -1;

	int m_iCurrentReconnectAttempts = 0;
	DWORD m_dwLastConnectAttemptTime = 0;

	std::mutex m_messageQueueMutex;
	struct PendingMessage
	{
		std::string m_sType;
		std::string m_sContent;
		bool m_bIsPriority;
	};
	std::vector<PendingMessage> m_vMessageQueue;

	struct CaptureSpotReservation
	{
		std::string m_sMap;
		int m_iPointIndex = -1;
		Vector m_vSpot{};
		uint32_t m_uOwnerAccountID = 0;
		int m_iBotId = -1;
		double m_flExpiresAt = 0.0;
	};
	std::mutex m_captureMutex;
	std::vector<CaptureSpotReservation> m_vCaptureReservations;

	std::mutex m_localBotsMutex;
	std::unordered_map<uint32_t, bool> m_mLocalBots;

	struct OtherBotInfo_t
	{
		std::string m_sServerIP;
		int m_iBotId = -1;
		double m_flLastUpdate = 0.0;
	};
	std::mutex m_otherBotsMutex;
	std::unordered_map<uint32_t, OtherBotInfo_t> m_mOtherBots;

	std::shared_mutex m_infoMutex;
	struct ClientInfo_t
	{
		int m_iCurrentHealth = -1;
		int m_iCurrentClass = TF_CLASS_UNDEFINED;
		int m_iCurrentFPS = -1;
		int m_iCurrentKills = -1;
		int m_iCurrentDeaths = -1;

		std::string m_sCurrentServer = "N/A";
		std::string m_sCurrentMapName = "N/A";
		std::string m_sBotName = "Unknown";
		uint32_t m_uAccountID = 0;

		bool m_bInGame = false;
	};
	ClientInfo_t tInfo;
	bool m_bSetServerName = false;
	bool m_bSetMapName = false;

	std::string GetPlayerClassName(int iPlayerClass);

	int GetBotIdFromEnv();
	int ReadBotIdFromFile();
	int GetReconnectDelayMs();

	static void ConnectAndMaintainPipe();
	bool HasPipe();
	void ClosePipe();

	void SendStatusUpdate(std::string sStatus);
	void QueueCommand(std::string sCommand);
	void QueueMessage(std::string sType, std::string sContent, bool bIsPriority);
	void ProcessMessageQueue();

	std::mutex m_commandQueueMutex;
	std::vector<std::string> m_vCommandQueue;

	void ProcessLocalBotMessage(std::string sAccountID);
	void UpdateLocalBotIgnoreStatus();
	void ClearLocalBots();
	void ProcessCaptureReservationMessage(const std::string& sContent);
	void ClearCaptureReservations();
	void PurgeExpiredCaptureReservations();

	std::mutex m_logMutex;
	void Log(std::string sMessage);
	std::string GetErrorMessage(DWORD dwError);
public:
	void Initialize();
	void Shutdown();
	void ProcessCommandQueue();

	bool IsLocalBot(uint32_t uAccountID);
	std::vector<int> GetOtherBotsOnServer(std::string sServerIP);
	void AnnounceCaptureSpotClaim(const std::string& sMap, int iPointIdx, const Vector& vSpot, float flDurationSeconds = 1.0f);
	void AnnounceCaptureSpotRelease(const std::string& sMap, int iPointIdx);
	std::vector<Vector> GetReservedCaptureSpots(const std::string& sMap, int iPointIdx, uint32_t uIgnoreAccountID = 0);

	void Store(CTFPlayer* pLocal = nullptr, bool bCreateMove = false);
	void Event(IGameEvent* pEvent, uint32_t uHash);
	void Reset();
	int GetBotId() { return m_iBotId; }
};

ADD_FEATURE(CNamedPipe, NamedPipe);
#endif
