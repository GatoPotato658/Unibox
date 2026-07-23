#pragma once

#include "../../SDK/SDK.h"

#include <filesystem>
#include <mutex>

class CSteamProfileCache
{
public:
	struct AvatarImage_t
	{
		std::shared_ptr<const std::vector<uint8_t>> m_pPixels;
		uint32_t m_uWidth = 0;
		uint32_t m_uHeight = 0;
		uint32_t m_uStride = 0;
		uint64_t m_uRevision = 0;

		[[nodiscard]] bool HasData() const
		{
			return m_pPixels && !m_pPixels->empty() && m_uWidth && m_uHeight;
		}
	};

	void Initialize();
	void Shutdown();
	void Touch(uint32_t uAccountID);
	void TouchAvatar(uint32_t uAccountID);
	void Refresh(uint32_t uAccountID);
	void Pump();
	std::string GetPersonaName(uint32_t uAccountID);
	bool TryGetAvatarImage(uint32_t uAccountID, AvatarImage_t& tOutImage);
	void CaptureNativeAvatar(uint64_t uSteamID, const uint8_t* pRgba, uint32_t uWidth, uint32_t uHeight);
	static std::filesystem::path GetAvatarPath(uint32_t uAccountID);
	static bool SaveAvatarToDisk(uint32_t uAccountID, const std::vector<uint8_t>& vPixels, uint32_t uWidth, uint32_t uHeight, std::filesystem::path* pOutPath = nullptr, bool bLog = true);

private:
	struct Entry_t
	{
		std::string m_sPersonaName;
		std::shared_ptr<std::vector<uint8_t>> m_pAvatarPixels;
		uint32_t m_uAvatarWidth = 0;
		uint32_t m_uAvatarHeight = 0;
		uint64_t m_uAvatarRevision = 0;
		std::filesystem::file_time_type m_tAvatarTimestamp = {};
		bool m_bDiskChecked = false;
		bool m_bNameRequested = false;
		bool m_bAvatarRequested = false;
		bool m_bSavePending = false;
		int m_iPendingImage = 0;
	};

	std::mutex m_mutex;
	std::unordered_map<uint32_t, Entry_t> m_mEntries;
	bool m_bCallbacksRegistered = false;
	void(__cdecl* m_pUnregisterCallback)(CCallbackBase*) = nullptr;

	void EnsureCallbacksRegistered();
	void RequestName(uint32_t uAccountID, Entry_t& tEntry);
	void RequestAvatar(uint32_t uAccountID, Entry_t& tEntry, bool bForce);
	void LoadAvatarFromDisk(uint32_t uAccountID, Entry_t& tEntry);
	void CaptureSteamAvatar(uint32_t uAccountID, int iImage, uint32_t uWidth = 0, uint32_t uHeight = 0);
	void StoreAvatar(uint32_t uAccountID, std::vector<uint8_t>&& vBgra, uint32_t uWidth, uint32_t uHeight, bool bSave);
	void HandlePersonaStateChange(const PersonaStateChange_t& tCallback);
	void HandleAvatarImageLoaded(const AvatarImageLoaded_t& tCallback);

	friend class CSteamProfilePersonaCallback;
	friend class CSteamProfileAvatarCallback;
};

ADD_FEATURE(CSteamProfileCache, SteamProfileCache);
