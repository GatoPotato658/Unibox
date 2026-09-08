#ifdef TEXTMODE
#include "NamedPipe.h"
#include "../../../SDK/SDK.h"
#include "../../Players/PlayerUtils.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <charconv>
#include <cmath>

static const std::string sBase64Chars =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"abcdefghijklmnopqrstuvwxyz"
	"0123456789+/";

const size_t MAX_PENDING_COMMANDS = 64;
const size_t MAX_PENDING_MESSAGES = 256;
const size_t MAX_PIPE_FRAME_SIZE = 1024 * 1024;
const size_t MAX_COMMAND_SIZE = 4096;

static bool IsSafeCommand(const std::string& sCommand)
{
	if (sCommand.empty() || sCommand.size() > MAX_COMMAND_SIZE)
		return false;
	for (const unsigned char cByte : sCommand)
	{
		if (cByte < 0x20 || cByte == ';' || cByte == '\0')
			return false;
	}
	return true;
}

static std::string base64_encode(const std::string& sInput)
{
	std::string sOutput;
	int iValue = 0;
	int iBitCount = -6;
	for (unsigned char cByte : sInput)
	{
		iValue = (iValue << 8) + cByte;
		iBitCount += 8;
		while (iBitCount >= 0)
		{
			sOutput.push_back(sBase64Chars[(iValue >> iBitCount) & 0x3F]);
			iBitCount -= 6;
		}
	}
	if (iBitCount > -6)
		sOutput.push_back(sBase64Chars[((iValue << 8) >> (iBitCount + 8)) & 0x3F]);
	while (sOutput.size() % 4)
		sOutput.push_back('=');

	return sOutput;
}

static int Base64Value(unsigned char cByte)
{
	if (cByte >= 'A' && cByte <= 'Z')
		return cByte - 'A';
	if (cByte >= 'a' && cByte <= 'z')
		return cByte - 'a' + 26;
	if (cByte >= '0' && cByte <= '9')
		return cByte - '0' + 52;
	if (cByte == '+')
		return 62;
	if (cByte == '/')
		return 63;
	return -1;
}

static bool base64_decode(const std::string& sInput, std::string& sOutput)
{
	if (sInput.empty() || sInput.size() % 4 != 0 || sInput.size() > MAX_PIPE_FRAME_SIZE)
		return false;

	sOutput.clear();
	sOutput.reserve(sInput.size() / 4 * 3);
	for (size_t i = 0; i < sInput.size(); i += 4)
	{
		const int iA = Base64Value(static_cast<unsigned char>(sInput[i]));
		const int iB = Base64Value(static_cast<unsigned char>(sInput[i + 1]));
		const bool bPadC = sInput[i + 2] == '=';
		const bool bPadD = sInput[i + 3] == '=';
		const int iC = bPadC ? 0 : Base64Value(static_cast<unsigned char>(sInput[i + 2]));
		const int iD = bPadD ? 0 : Base64Value(static_cast<unsigned char>(sInput[i + 3]));

		if (iA < 0 || iB < 0 || iC < 0 || iD < 0 || bPadC && !bPadD || (bPadC || bPadD) && i + 4 != sInput.size())
			return false;
		if (bPadC && (iB & 0x0F) || bPadD && (iC & 0x03))
			return false;

		sOutput.push_back(static_cast<char>((iA << 2) | (iB >> 4)));
		if (!bPadC)
		{
			sOutput.push_back(static_cast<char>((iB << 4) | (iC >> 2)));
			if (!bPadD)
				sOutput.push_back(static_cast<char>((iC << 6) | iD));
		}
	}

	return true;
}

static std::string EncodePipeMessage(const std::string& sContent)
{
	return base64_encode(sContent) + "\n";
}

static bool TryParsePipeFrame(const std::string& sFrame, std::string& sBotNumber, std::string& sMessageType, std::string& sContent)
{
	std::istringstream iss(sFrame);
	if (!std::getline(iss, sBotNumber, ':') || !std::getline(iss, sMessageType, ':'))
		return false;
	if (sFrame.find_first_of("\r\n") != std::string::npos || sMessageType.empty() || sMessageType.size() > 32)
		return false;

	uint32_t uBotNumber = 0;
	const auto [pEnd, ec] = std::from_chars(sBotNumber.data(), sBotNumber.data() + sBotNumber.size(), uBotNumber);
	if (ec != std::errc{} || pEnd != sBotNumber.data() + sBotNumber.size())
		return false;

	std::getline(iss, sContent);
	return true;
}

static bool TryParseInt(const std::string& sValue, int& iValue)
{
	const auto [pEnd, ec] = std::from_chars(sValue.data(), sValue.data() + sValue.size(), iValue);
	return ec == std::errc{} && pEnd == sValue.data() + sValue.size();
}

static bool TryParseUInt32(const std::string& sValue, uint32_t& uValue)
{
	const auto [pEnd, ec] = std::from_chars(sValue.data(), sValue.data() + sValue.size(), uValue);
	return ec == std::errc{} && pEnd == sValue.data() + sValue.size();
}

static bool TryParseFloat(const std::string& sValue, float& flValue)
{
	const auto [pEnd, ec] = std::from_chars(sValue.data(), sValue.data() + sValue.size(), flValue, std::chars_format::general);
	return ec == std::errc{} && pEnd == sValue.data() + sValue.size() && std::isfinite(flValue);
}

const char* PIPE_NAME = "\\\\.\\pipe\\AwootismBotPipe";
const int BASE_RECONNECT_DELAY_MS = 500;
const int MAX_RECONNECT_DELAY_MS = 10000;

static double GetNowSeconds()
{
	return static_cast<double>(GetTickCount64()) / 1000.0;
}

void CNamedPipe::Initialize()
{
	m_shouldRun.store(true);
	bool bLogOpen = false;
	{
		std::lock_guard lock(m_logMutex);
		m_logFile.clear();
		m_logFile.open("C:\\pipe_log.txt", std::ios::app);
		bLogOpen = m_logFile.is_open();
	}
	if (!bLogOpen)
		std::cerr << "Failed to open log file" << std::endl;
	Log("NamedPipe::Initialize() called");

	m_iBotId = ReadBotIdFromFile();

	if (m_iBotId == -1)
		Log("Failed to read bot ID from file");
	else
		Log("Bot ID read from file: " + std::to_string(m_iBotId));

	m_mLocalBots.clear();
	Log("Cleared local bots list on startup");
	ClearCaptureReservations();

	m_pipeThread = std::thread(ConnectAndMaintainPipe);
	Log("Pipe thread started");
}

void CNamedPipe::Shutdown()
{
	m_shouldRun.store(false);
	if (m_pipeThread.joinable())
	{
		CancelSynchronousIo(m_pipeThread.native_handle());
		m_pipeThread.join();
	}
	ClosePipe();
	{
		std::lock_guard lock(m_logMutex);
		m_logFile.flush();
		m_logFile.close();
	}
}

void CNamedPipe::Store(CTFPlayer* pLocal, bool bCreateMove)
{
	std::unique_lock lock(m_infoMutex);

	if (bCreateMove)
	{
		tInfo.m_iCurrentHealth = pLocal->IsAlive() ? pLocal->m_iHealth() : -1;
		tInfo.m_iCurrentClass = pLocal->IsInValidTeam() ? pLocal->m_iClass() : TF_CLASS_UNDEFINED;
		tInfo.m_iCurrentFPS = I::GlobalVars->absoluteframetime > 0.f ? static_cast<int>(1.f / I::GlobalVars->absoluteframetime) : -1;
		if (auto pResource = H::Entities.GetResource())
		{
			int iLocalIdx = pLocal->entindex();
			tInfo.m_iCurrentKills = pResource->m_iScore(iLocalIdx);
			tInfo.m_iCurrentDeaths = pResource->m_iDeaths(iLocalIdx);
		}

		UpdateLocalBotIgnoreStatus();
		return;
	}
	else
	{
		if (!tInfo.m_uAccountID)
			tInfo.m_uAccountID = H::Entities.GetLocalAccountID();

		tInfo.m_bInGame = I::EngineClient->IsInGame();
		if (tInfo.m_bInGame)
		{
			if (!m_bSetMapName)
			{
				tInfo.m_sCurrentMapName = SDK::GetLevelName();
				m_bSetMapName = true;
			}

			if (!m_bSetServerName)
			{
				if (auto pNetChan = I::EngineClient->GetNetChannelInfo())
				{
					const char* cAddr = pNetChan->GetAddress();
					if (cAddr && cAddr[0] != '\0' && std::string(cAddr) != "loopback")
					{
						tInfo.m_sCurrentServer = std::string(cAddr);
						m_bSetServerName = true;
					}
				}
			}
		}
		else
		{
			tInfo.m_sCurrentMapName = "N/A";
			tInfo.m_sCurrentServer = "N/A";
			m_bSetMapName = false;
			m_bSetServerName = false;
		}

		static Timer tNameRefreshTimer{};
		if (tNameRefreshTimer.Run(5.f))
		{
			const char* szPersonaName = I::SteamFriends->GetPersonaName();
			tInfo.m_sBotName = szPersonaName != nullptr ? szPersonaName : "Unknown";
		}
	}
}

void CNamedPipe::Event(IGameEvent* pEvent, uint32_t uHash)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("client_disconnect"):
	case FNV1A::Hash32Const("game_newmap"):
		Reset();
	}
}

void CNamedPipe::Reset()
{
	std::lock_guard lock(m_infoMutex);
	tInfo = ClientInfo_t(-1, TF_CLASS_UNDEFINED, -1, -1, -1, "N/A", "N/A", tInfo.m_sBotName, tInfo.m_uAccountID, false);
	m_bSetServerName = false;
	m_bSetMapName = false;
	ClearCaptureReservations();
}

void CNamedPipe::Log(std::string sMessage)
{
	std::lock_guard lock(m_logMutex);
	if (!m_logFile.is_open())
		return;

	m_logFile << sMessage << std::endl;
	m_logFile.flush();
	OutputDebugStringA(("NamedPipe: " + sMessage + "\n").c_str());
}

std::string CNamedPipe::GetErrorMessage(DWORD dwError)
{
	char* cMessageBuffer = nullptr;
	size_t uSize = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, dwError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&cMessageBuffer, 0, NULL);
	if (!cMessageBuffer || !uSize)
		return "Unknown error";

	std::string sMessage(cMessageBuffer, uSize);
	LocalFree(cMessageBuffer);
	return sMessage;
}

int CNamedPipe::GetBotIdFromEnv()
{
	char* cEnvVal = nullptr;
	size_t len = 0;

	if (_dupenv_s(&cEnvVal, &len, "BOTID") == 0 && cEnvVal)
	{
		int iId = atoi(cEnvVal);
		free(cEnvVal);
		if (iId > 0)
		{
			Log("Found BOTID environment variable: " + std::to_string(iId));
			return iId;
		}
	}

	return -1;
}

int CNamedPipe::ReadBotIdFromFile()
{
	int iEnvId = GetBotIdFromEnv();
	if (iEnvId == -1)
	{
		Log("BOTID environment variable not set. Using fallback ID 1.");
		return 1;
	}
	return iEnvId;
}

int CNamedPipe::GetReconnectDelayMs()
{
	int iDelay = std::min(
		BASE_RECONNECT_DELAY_MS * (1 << std::min(m_iCurrentReconnectAttempts, 10)),
		MAX_RECONNECT_DELAY_MS
	);

	int iJitter = iDelay * 0.2 * (static_cast<double>(rand()) / RAND_MAX - 0.5);
	return iDelay + iJitter;
}

bool CNamedPipe::HasPipe()
{
	std::lock_guard lock(m_pipeMutex);
	return m_hPipe != INVALID_HANDLE_VALUE;
}

void CNamedPipe::ClosePipe()
{
	std::lock_guard lock(m_pipeMutex);
	if (m_hPipe != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hPipe);
		m_hPipe = INVALID_HANDLE_VALUE;
	}
}

void CNamedPipe::ConnectAndMaintainPipe()
{
	F::NamedPipe.Log("ConnectAndMaintainPipe started");
	srand(static_cast<unsigned int>(time(nullptr)));

	std::string sReadBuffer;
	DWORD dwLastHeartbeat = 0;
	DWORD dwLastStatus = 0;

	while (F::NamedPipe.m_shouldRun.load())
	{
		DWORD dwCurrentTime = GetTickCount();
		if (!F::NamedPipe.HasPipe())
		{
			sReadBuffer.clear();
			int iReconnectDelay = F::NamedPipe.GetReconnectDelayMs();
			if (dwCurrentTime - F::NamedPipe.m_dwLastConnectAttemptTime > static_cast<DWORD>(iReconnectDelay) || F::NamedPipe.m_dwLastConnectAttemptTime == 0)
			{
				F::NamedPipe.m_dwLastConnectAttemptTime = dwCurrentTime;
				F::NamedPipe.m_iCurrentReconnectAttempts++;

				F::NamedPipe.Log("Attempting to connect to pipe (attempt " + std::to_string(F::NamedPipe.m_iCurrentReconnectAttempts) +
					", delay: " + std::to_string(iReconnectDelay) + "ms)");

				const auto hPipe = CreateFile(
					PIPE_NAME,
					GENERIC_READ | GENERIC_WRITE,
					0,
					NULL,
					OPEN_EXISTING,
					0,
					NULL);

				if (hPipe != INVALID_HANDLE_VALUE)
				{
					if (!F::NamedPipe.m_shouldRun.load())
					{
						CloseHandle(hPipe);
						break;
					}
					{
						std::lock_guard lock(F::NamedPipe.m_pipeMutex);
						F::NamedPipe.m_hPipe = hPipe;
					}
					F::NamedPipe.m_iCurrentReconnectAttempts = 0;
					F::NamedPipe.Log("Connected to pipe");

					{
						std::lock_guard lock(F::NamedPipe.m_pipeMutex);
						DWORD dwPipeMode = PIPE_READMODE_MESSAGE;
						SetNamedPipeHandleState(F::NamedPipe.m_hPipe, &dwPipeMode, NULL, NULL);
					}

					F::NamedPipe.QueueMessage("Status", "Connected", true);
					F::NamedPipe.ProcessMessageQueue();

					if (F::NamedPipe.m_iBotId != -1)
						F::NamedPipe.Log("Using Bot ID: " + std::to_string(F::NamedPipe.m_iBotId));
					else
						F::NamedPipe.Log("Warning: Bot ID not set");
					F::NamedPipe.ClearLocalBots();
					F::NamedPipe.ClearCaptureReservations();
				}
				else
				{
					DWORD dwError = GetLastError();
					F::NamedPipe.Log("Failed to connect to pipe: " + std::to_string(dwError) + " - " + F::NamedPipe.GetErrorMessage(dwError));
				}
			}
			else
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		if (F::NamedPipe.HasPipe())
		{
			DWORD dwBytesAvail = 0;
			DWORD dwPeekError = ERROR_SUCCESS;
			bool bPeekFailed = false;
			{
				std::lock_guard lock(F::NamedPipe.m_pipeMutex);
				if (F::NamedPipe.m_hPipe != INVALID_HANDLE_VALUE && !PeekNamedPipe(F::NamedPipe.m_hPipe, NULL, 0, NULL, &dwBytesAvail, NULL))
				{
					dwPeekError = GetLastError();
					CloseHandle(F::NamedPipe.m_hPipe);
					F::NamedPipe.m_hPipe = INVALID_HANDLE_VALUE;
					bPeekFailed = true;
				}
			}
			if (bPeekFailed)
			{
				F::NamedPipe.Log("Pipe check failed: " + std::to_string(dwPeekError) + " - " + F::NamedPipe.GetErrorMessage(dwPeekError));
				continue;
			}
			F::NamedPipe.ProcessMessageQueue();

			DWORD dwNow = GetTickCount();
			if (dwNow - dwLastHeartbeat >= 1000)
			{
				F::NamedPipe.QueueMessage("Status", "Heartbeat", true);
				F::NamedPipe.ProcessMessageQueue();
				dwLastHeartbeat = dwNow;
			}

			if (dwNow - dwLastStatus >= 1000)
			{
				dwLastStatus = dwNow;
				std::unique_lock lock(F::NamedPipe.m_infoMutex);

				// Client info
				F::NamedPipe.QueueMessage("Map", F::NamedPipe.tInfo.m_sCurrentMapName, false);
				F::NamedPipe.QueueMessage("Server", F::NamedPipe.tInfo.m_sCurrentServer, false);
				F::NamedPipe.QueueMessage("BotName", F::NamedPipe.tInfo.m_sBotName, false);
				F::NamedPipe.QueueMessage("PlayerClass", F::NamedPipe.GetPlayerClassName(F::NamedPipe.tInfo.m_iCurrentClass), false);
				F::NamedPipe.QueueMessage("Health", std::to_string(F::NamedPipe.tInfo.m_iCurrentHealth), false);
				F::NamedPipe.QueueMessage("FPS", std::to_string(F::NamedPipe.tInfo.m_iCurrentFPS), false);
				F::NamedPipe.QueueMessage("Kills", std::to_string(F::NamedPipe.tInfo.m_iCurrentKills), false);
				F::NamedPipe.QueueMessage("Deaths", std::to_string(F::NamedPipe.tInfo.m_iCurrentDeaths), false);
				F::NamedPipe.ProcessMessageQueue();

				if (!F::NamedPipe.tInfo.m_bInGame)
				{
					// Not in game: ensure panel receives disconnect-ish state
					F::NamedPipe.QueueMessage("Status", "NotInGame", true);
					F::NamedPipe.ProcessMessageQueue();
				}
				else
				{
					uint32_t uAccountID = F::NamedPipe.tInfo.m_uAccountID;
					if (F::NamedPipe.tInfo.m_uAccountID != 0)
					{
						std::string sContent = std::format("{}|{}|{}", F::NamedPipe.tInfo.m_uAccountID, F::NamedPipe.tInfo.m_sCurrentServer, F::NamedPipe.m_iBotId);
						F::NamedPipe.QueueMessage("LocalBot", sContent, true);
						F::NamedPipe.Log("Queued local bot ID broadcast: " + sContent);

						if (F::NamedPipe.HasPipe())
							F::NamedPipe.ProcessMessageQueue();
					}
				}
				lock.unlock();
			}

			char cBuffer[4096] = { 0 };
			DWORD dwBytesRead = 0;
			BOOL bReadSuccess = FALSE;
			DWORD dwReadError = ERROR_SUCCESS;
			if (dwBytesAvail > 0)
			{
				std::lock_guard lock(F::NamedPipe.m_pipeMutex);
				if (F::NamedPipe.m_hPipe != INVALID_HANDLE_VALUE)
				{
					bReadSuccess = ReadFile(F::NamedPipe.m_hPipe, cBuffer, sizeof(cBuffer) - 1, &dwBytesRead, NULL);
					if (!bReadSuccess)
					{
						dwReadError = GetLastError();
						if (dwReadError != ERROR_MORE_DATA)
						{
							CloseHandle(F::NamedPipe.m_hPipe);
							F::NamedPipe.m_hPipe = INVALID_HANDLE_VALUE;
						}
					}
				}
				if (!bReadSuccess && dwReadError != ERROR_MORE_DATA && dwReadError != ERROR_SUCCESS)
				{
					F::NamedPipe.Log("ReadFile failed: " + std::to_string(dwReadError) + " - " + F::NamedPipe.GetErrorMessage(dwReadError));
					continue;
				}

				if (dwBytesRead > 0)
				{
					sReadBuffer.append(cBuffer, dwBytesRead);
					// F::NamedPipe.Log("Received chunk: " + std::string(cBuffer, dwBytesRead));

					size_t pos = 0;
					size_t newlinePos;
					while ((newlinePos = sReadBuffer.find('\n', pos)) != std::string::npos)
					{
						if (newlinePos - pos > MAX_PIPE_FRAME_SIZE)
						{
							F::NamedPipe.Log("Received oversized message frame");
							pos = newlinePos + 1;
							continue;
						}

						std::string sLine = sReadBuffer.substr(pos, newlinePos - pos);
						pos = newlinePos + 1;

						if (sLine.empty())
							continue;

						// F::NamedPipe.Log("Processing line: " + sLine);
						if (!sLine.empty() && sLine.back() == '\r')
							sLine.pop_back();

						std::string sDecoded;
						if (!base64_decode(sLine, sDecoded))
						{
							F::NamedPipe.Log("Received malformed base64 frame");
							continue;
						}
						std::string sBotNumber, sMessageType, sContent;
						if (!TryParsePipeFrame(sDecoded, sBotNumber, sMessageType, sContent))
						{
							F::NamedPipe.Log("Received malformed message frame");
							continue;
						}

						if (sMessageType == "Command")
							F::NamedPipe.QueueCommand(sContent);
						else if (sMessageType == "LocalBot")
							F::NamedPipe.ProcessLocalBotMessage(sContent);
						else if (sMessageType == "CPCapture")
							F::NamedPipe.ProcessCaptureReservationMessage(sContent);
						else
							F::NamedPipe.Log("Received unknown message type: " + sMessageType);
					}
					sReadBuffer.erase(0, pos);
					if (sReadBuffer.size() > MAX_PIPE_FRAME_SIZE)
					{
						F::NamedPipe.Log("Discarding oversized unterminated message frame");
						sReadBuffer.clear();
					}
				}
			}
		}

		if (!F::NamedPipe.HasPipe())
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		else
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	{
		std::lock_guard lock(F::NamedPipe.m_messageQueueMutex);
		F::NamedPipe.m_vMessageQueue.clear();
	}

	{
		std::lock_guard lock(F::NamedPipe.m_pipeMutex);
		if (F::NamedPipe.m_hPipe != INVALID_HANDLE_VALUE)
		{
			try
			{
				if (F::NamedPipe.m_iBotId != -1)
				{
					std::string sContent = std::to_string(F::NamedPipe.m_iBotId) + ":Status:Disconnecting";
					std::string sMessage = EncodePipeMessage(sContent);
					DWORD dwBytesWritten = 0;
					WriteFile(F::NamedPipe.m_hPipe, sMessage.c_str(), static_cast<DWORD>(sMessage.length()), &dwBytesWritten, NULL);
				}
			}
			catch (...)
			{
			}

			CloseHandle(F::NamedPipe.m_hPipe);
			F::NamedPipe.m_hPipe = INVALID_HANDLE_VALUE;
		}
	}
	F::NamedPipe.Log("ConnectAndMaintainPipe ended");
}

void CNamedPipe::SendStatusUpdate(std::string sStatus)
{
	QueueMessage("Status", sStatus, true);
}

void CNamedPipe::QueueCommand(std::string sCommand)
{
	if (!IsSafeCommand(sCommand))
	{
		Log("Rejected invalid command");
		return;
	}

	Log("QueueCommand called with: " + sCommand);

	{
		std::lock_guard lock(m_commandQueueMutex);
		if (m_vCommandQueue.size() >= MAX_PENDING_COMMANDS)
			m_vCommandQueue.erase(m_vCommandQueue.begin());
		m_vCommandQueue.push_back(sCommand);
	}

	SendStatusUpdate("CommandQueued:" + sCommand);
}

void CNamedPipe::ProcessCommandQueue()
{
	std::vector<std::string> vCommands;
	{
		std::lock_guard lock(m_commandQueueMutex);
		if (m_vCommandQueue.empty())
			return;
		vCommands.swap(m_vCommandQueue);
	}

	if (vCommands.empty())
		return;
	if (!I::EngineClient)
	{
		std::lock_guard lock(m_commandQueueMutex);
		m_vCommandQueue.insert(m_vCommandQueue.begin(), vCommands.begin(), vCommands.end());
		if (m_vCommandQueue.size() > MAX_PENDING_COMMANDS)
			m_vCommandQueue.erase(m_vCommandQueue.begin(), m_vCommandQueue.begin() + (m_vCommandQueue.size() - MAX_PENDING_COMMANDS));
		return;
	}

	for (const auto& sCommand : vCommands)
	{
		if (!IsSafeCommand(sCommand))
			continue;
		I::EngineClient->ClientCmd_Unrestricted(sCommand.c_str());
		Log("Executed queued command: " + sCommand);
		SendStatusUpdate("CommandExecuted:" + sCommand);
	}
}

void CNamedPipe::QueueMessage(std::string sType, std::string sContent, bool bIsPriority)
{
	std::lock_guard lock(m_messageQueueMutex);

	if (m_vMessageQueue.size() < MAX_PENDING_MESSAGES)
		m_vMessageQueue.push_back({ sType, sContent, bIsPriority });
	else
	{
		auto pEviction = m_vMessageQueue.end();
		for (auto it = m_vMessageQueue.begin(); it != m_vMessageQueue.end(); ++it)
		{
			if (!it->m_bIsPriority)
			{
				pEviction = it;
				break;
			}
		}
		if (pEviction == m_vMessageQueue.end())
		{
			if (!bIsPriority)
				return;
			pEviction = m_vMessageQueue.begin();
		}
		m_vMessageQueue.erase(pEviction);
		m_vMessageQueue.push_back({ sType, sContent, bIsPriority });
	}
}

void CNamedPipe::ProcessMessageQueue()
{
	std::lock_guard pipeLock(m_pipeMutex);
	if (m_hPipe == INVALID_HANDLE_VALUE)
		return;

	int processCount = 0;
	while (processCount < 10)
	{
		PendingMessage tMessage;
		{
			std::lock_guard lock(m_messageQueueMutex);
			if (m_vMessageQueue.empty())
				break;
			tMessage = std::move(m_vMessageQueue.front());
			m_vMessageQueue.erase(m_vMessageQueue.begin());
		}

		std::string sContent;
		if (m_iBotId != -1)
			sContent = std::to_string(m_iBotId) + ":" + tMessage.m_sType + ":" + tMessage.m_sContent;
		else
			sContent = "0:" + tMessage.m_sType + ":" + tMessage.m_sContent;

		std::string sMessage = EncodePipeMessage(sContent);

		DWORD dwBytesWritten = 0;
		BOOL bSuccess = WriteFile(m_hPipe, sMessage.c_str(), static_cast<DWORD>(sMessage.length()), &dwBytesWritten, NULL);
		if (bSuccess && dwBytesWritten == sMessage.length())
		{
			processCount++;
			continue;
		}

		const DWORD dwError = GetLastError();
		QueueMessage(std::move(tMessage.m_sType), std::move(tMessage.m_sContent), tMessage.m_bIsPriority);

		Log("Failed to write queued message: " + std::to_string(dwError) + " - " + GetErrorMessage(dwError));
		if (dwError == ERROR_BROKEN_PIPE || dwError == ERROR_PIPE_NOT_CONNECTED || dwError == ERROR_NO_DATA)
		{
			CloseHandle(m_hPipe);
			m_hPipe = INVALID_HANDLE_VALUE;
		}
		break;
	}
}

bool CNamedPipe::IsLocalBot(uint32_t uAccountID)
{
	if (uAccountID == 0)
		return false;

	std::lock_guard lock(m_localBotsMutex);
	return m_mLocalBots.find(uAccountID) != m_mLocalBots.end();
}

void CNamedPipe::AnnounceCaptureSpotClaim(const std::string& sMap, int iPointIdx, const Vector& vSpot, float flDurationSeconds)
{
	if (sMap.empty() || iPointIdx < 0 || !std::isfinite(flDurationSeconds) || flDurationSeconds <= 0.f)
		return;

	const double flExpiry = GetNowSeconds() + flDurationSeconds;
	{
		std::lock_guard lock(m_captureMutex);
		bool bUpdated = false;
		for (auto& reservation : m_vCaptureReservations)
		{
			if (reservation.m_sMap == sMap && reservation.m_iPointIndex == iPointIdx && reservation.m_uOwnerAccountID == tInfo.m_uAccountID)
			{
				reservation.m_vSpot = vSpot;
				reservation.m_flExpiresAt = flExpiry;
				reservation.m_iBotId = m_iBotId;
				bUpdated = true;
				break;
			}
		}
		if (!bUpdated)
		{
			CaptureSpotReservation tReservation{};
			tReservation.m_sMap = sMap;
			tReservation.m_iPointIndex = iPointIdx;
			tReservation.m_vSpot = vSpot;
			tReservation.m_uOwnerAccountID = tInfo.m_uAccountID;
			tReservation.m_iBotId = m_iBotId;
			tReservation.m_flExpiresAt = flExpiry;
			m_vCaptureReservations.emplace_back(tReservation);
		}
	}

	std::ostringstream oss;
	oss << std::fixed << std::setprecision(2) << "Claim|" << sMap << '|' << iPointIdx << '|' << vSpot.x << '|' << vSpot.y << '|' << vSpot.z
		<< '|' << tInfo.m_uAccountID << '|' << flDurationSeconds << '|' << m_iBotId;
	QueueMessage("CPCapture", oss.str(), true);
}

void CNamedPipe::AnnounceCaptureSpotRelease(const std::string& sMap, int iPointIdx)
{
	if (sMap.empty() || iPointIdx < 0)
		return;

	{
		std::lock_guard lock(m_captureMutex);
		m_vCaptureReservations.erase(std::remove_if(m_vCaptureReservations.begin(), m_vCaptureReservations.end(),
			[&](const CaptureSpotReservation& reservation)
			{
				return reservation.m_sMap == sMap && reservation.m_iPointIndex == iPointIdx && reservation.m_uOwnerAccountID == tInfo.m_uAccountID;
			}), m_vCaptureReservations.end());
	}

	std::ostringstream oss;
	oss << "Release|" << sMap << '|' << iPointIdx << '|' << tInfo.m_uAccountID;
	QueueMessage("CPCapture", oss.str(), true);
}

std::vector<Vector> CNamedPipe::GetReservedCaptureSpots(const std::string& sMap, int iPointIdx, uint32_t uIgnoreAccountID)
{
	PurgeExpiredCaptureReservations();
	std::vector<Vector> vReserved;
	std::lock_guard lock(m_captureMutex);
	for (const auto& reservation : m_vCaptureReservations)
	{
		if (reservation.m_sMap != sMap || reservation.m_iPointIndex != iPointIdx)
			continue;
		if (uIgnoreAccountID != 0 && reservation.m_uOwnerAccountID == uIgnoreAccountID)
			continue;
		vReserved.push_back(reservation.m_vSpot);
	}
	return vReserved;
}

std::vector<int> CNamedPipe::GetOtherBotsOnServer(std::string sServerIP)
{
	std::vector<int> vBotIds;
	if (sServerIP == "N/A" || sServerIP.empty())
		return vBotIds;

	std::lock_guard lock(m_otherBotsMutex);
	double flNow = GetNowSeconds();
	for (auto it = m_mOtherBots.begin(); it != m_mOtherBots.end();)
	{
		if (flNow - it->second.m_flLastUpdate > 30.0) // 30 second timeout
		{
			it = m_mOtherBots.erase(it);
			continue;
		}

		if (it->second.m_sServerIP == sServerIP)
			vBotIds.push_back(it->second.m_iBotId);
		++it;
	}
	return vBotIds;
}

void CNamedPipe::ProcessLocalBotMessage(std::string sContent)
{
	if (!sContent.empty())
	{
		try
		{
			std::vector<std::string> vTokens;
			{
				std::stringstream ss(sContent);
				std::string sToken;
				while (std::getline(ss, sToken, '|'))
					vTokens.emplace_back(sToken);
			}

			if (vTokens.size() != 1 && vTokens.size() != 3)
				return;

			uint32_t uAccountID = 0;
			if (!TryParseUInt32(vTokens[0], uAccountID) || uAccountID == 0)
				return;

			int iBotId = -1;
			if (vTokens.size() == 3 && (!TryParseInt(vTokens[2], iBotId) || vTokens[1].empty()))
				return;

			{
				std::lock_guard lock(m_localBotsMutex);
				m_mLocalBots[uAccountID] = true;
			}

			if (vTokens.size() >= 3)
			{
				std::lock_guard lock(m_otherBotsMutex);
				OtherBotInfo_t& tInfo = m_mOtherBots[uAccountID];
				tInfo.m_sServerIP = vTokens[1];
				tInfo.m_iBotId = iBotId;
				tInfo.m_flLastUpdate = GetNowSeconds();
			}

			Log("Processed local bot message: " + sContent);
		}
		catch (const std::exception& e)
		{
			Log("Error processing LocalBot message: " + std::string(e.what()));
		}
	}
}

void CNamedPipe::UpdateLocalBotIgnoreStatus()
{
	// Copy keys under lock to avoid holding lock while tagging
	std::vector<uint32_t> vLocalBotIds;
	{
		std::unique_lock lock(m_localBotsMutex);
		vLocalBotIds.reserve(m_mLocalBots.size());
		for (const auto& kv : m_mLocalBots)
			vLocalBotIds.push_back(kv.first);
		lock.unlock();
	}

	int iIgnoredTagIdx = F::PlayerUtils.TagToIndex(IGNORED_TAG);
	int iFriendTagIdx = F::PlayerUtils.TagToIndex(FRIEND_TAG);
	for (const auto& uAccountID : vLocalBotIds)
	{
		if (!F::PlayerUtils.HasTag(uAccountID, iIgnoredTagIdx) ||
			!F::PlayerUtils.HasTag(uAccountID, iFriendTagIdx))
		{
			const char* szName = I::SteamFriends->GetFriendPersonaName(CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual));;

			F::PlayerUtils.AddTag(uAccountID, iIgnoredTagIdx, true, szName);
			F::PlayerUtils.AddTag(uAccountID, iFriendTagIdx, true, szName);
			Log("Marked local bot as ignored and friend: " + std::string(szName));
		}
	}
}

void CNamedPipe::ClearLocalBots()
{
	std::lock_guard lock(m_localBotsMutex);
	m_mLocalBots.clear();
	Log("Cleared local bots list");
}

void CNamedPipe::ClearCaptureReservations()
{
	std::lock_guard lock(m_captureMutex);
	m_vCaptureReservations.clear();
}

void CNamedPipe::PurgeExpiredCaptureReservations()
{
	std::lock_guard lock(m_captureMutex);
	const double flNow = GetNowSeconds();
	m_vCaptureReservations.erase(std::remove_if(m_vCaptureReservations.begin(), m_vCaptureReservations.end(),
		[&](const CaptureSpotReservation& reservation)
		{
			return reservation.m_flExpiresAt < flNow;
		}), m_vCaptureReservations.end());
}

void CNamedPipe::ProcessCaptureReservationMessage(const std::string& sContent)
{
	if (sContent.empty())
		return;

	std::vector<std::string> vTokens;
	{
		std::stringstream ss(sContent);
		std::string sToken;
		while (std::getline(ss, sToken, '|'))
			vTokens.emplace_back(sToken);
	}

	if (vTokens.empty())
		return;

	const std::string& sCommand = vTokens.front();
	if (sCommand == "Claim")
	{
		if (vTokens.size() != 9)
			return;

		const std::string& sMap = vTokens[1];
		if (sMap.empty())
			return;
		int iPointIdx = -1;
		if (!TryParseInt(vTokens[2], iPointIdx) || iPointIdx < 0)
			return;
		Vector vSpot{};
		if (!TryParseFloat(vTokens[3], vSpot.x) || !TryParseFloat(vTokens[4], vSpot.y) || !TryParseFloat(vTokens[5], vSpot.z))
			return;
		uint32_t uOwner = 0;
		if (!TryParseUInt32(vTokens[6], uOwner) || uOwner == 0)
			return;
		float flDuration = 0.f;
		if (!TryParseFloat(vTokens[7], flDuration) || flDuration <= 0.f)
			return;
		int iBotId = -1;
		if (!TryParseInt(vTokens[8], iBotId))
			return;

		const double flExpiry = GetNowSeconds() + flDuration;

		{
			std::lock_guard lock(m_captureMutex);
			bool bUpdated = false;
			for (auto& reservation : m_vCaptureReservations)
			{
				if (reservation.m_sMap == sMap && reservation.m_iPointIndex == iPointIdx && reservation.m_uOwnerAccountID == uOwner)
				{
					reservation.m_vSpot = vSpot;
					reservation.m_flExpiresAt = flExpiry;
					reservation.m_iBotId = iBotId;
					bUpdated = true;
					break;
				}
			}

			if (!bUpdated)
			{
				CaptureSpotReservation tReservation{};
				tReservation.m_sMap = sMap;
				tReservation.m_iPointIndex = iPointIdx;
				tReservation.m_vSpot = vSpot;
				tReservation.m_uOwnerAccountID = uOwner;
				tReservation.m_iBotId = iBotId;
				tReservation.m_flExpiresAt = flExpiry;
				m_vCaptureReservations.emplace_back(tReservation);
			}
		}
	}
	else if (sCommand == "Release")
	{
		if (vTokens.size() != 4)
			return;

		const std::string& sMap = vTokens[1];
		if (sMap.empty())
			return;
		int iPointIdx = -1;
		if (!TryParseInt(vTokens[2], iPointIdx) || iPointIdx < 0)
			return;
		uint32_t uOwner = 0;
		if (!TryParseUInt32(vTokens[3], uOwner) || uOwner == 0)
			return;

		std::lock_guard lock(m_captureMutex);
		m_vCaptureReservations.erase(std::remove_if(m_vCaptureReservations.begin(), m_vCaptureReservations.end(),
			[&](const CaptureSpotReservation& reservation)
			{
				return reservation.m_sMap == sMap && reservation.m_iPointIndex == iPointIdx && reservation.m_uOwnerAccountID == uOwner;
			}), m_vCaptureReservations.end());
	}
}

std::string CNamedPipe::GetPlayerClassName(int iPlayerClass)
{
	switch (iPlayerClass)
	{
	case 1: return "Scout";
	case 2: return "Sniper";
	case 3: return "Soldier";
	case 4: return "Demoman";
	case 5: return "Medic";
	case 6: return "Heavy";
	case 7: return "Pyro";
	case 8: return "Spy";
	case 9: return "Engineer";
	default: return "N/A";
	}
}
#endif
