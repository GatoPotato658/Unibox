#include "SteamProfileCache.h"
#include "../Configs/Configs.h"

#include <wincodec.h>
#include <wrl/client.h>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

static constexpr Color_t LogColor = { 175, 150, 255, 255 };
static constexpr Color_t ErrorColor = { 255, 150, 150, 255 };
static constexpr auto AvatarRefreshAge = std::chrono::days(7);
static constexpr uint32_t MaxAvatarDimension = 1024;
static constexpr size_t MaxAvatarBytes = size_t(MaxAvatarDimension) * MaxAvatarDimension * 4;

using RegisterCallbackFn = void(__cdecl*)(CCallbackBase*, int);
using UnregisterCallbackFn = void(__cdecl*)(CCallbackBase*);

static bool IsAvatarSizeValid(uint32_t uWidth, uint32_t uHeight)
{
	return uWidth && uHeight && uWidth <= MaxAvatarDimension && uHeight <= MaxAvatarDimension
		&& static_cast<size_t>(uWidth) * uHeight * 4 <= MaxAvatarBytes;
}

static std::filesystem::path GetAvatarFolder()
{
	if (F::Configs.m_sCorePath.empty())
		return {};

	std::filesystem::path tFolder = std::filesystem::path(F::Configs.m_sCorePath) / "avatars";
	std::error_code ec;
	std::filesystem::create_directories(tFolder, ec);
	return ec ? std::filesystem::path{} : tFolder;
}

static bool WriteAvatarPng(const std::filesystem::path& tPath, const std::vector<uint8_t>& vPixels, uint32_t uWidth, uint32_t uHeight)
{
		if (!IsAvatarSizeValid(uWidth, uHeight) || vPixels.size() != static_cast<size_t>(uWidth) * uHeight * 4)
			return false;

		const HRESULT hrInitialize = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool bInitialized = SUCCEEDED(hrInitialize);
		if (!bInitialized && hrInitialize != RPC_E_CHANGED_MODE)
			return false;

		HRESULT hr = S_OK;
		ComPtr<IWICImagingFactory> pFactory;
		hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
		ComPtr<IWICStream> pStream;
		if (SUCCEEDED(hr))
			hr = pFactory->CreateStream(&pStream);
		if (SUCCEEDED(hr))
			hr = pStream->InitializeFromFilename(tPath.c_str(), GENERIC_WRITE);
		ComPtr<IWICBitmapEncoder> pEncoder;
		if (SUCCEEDED(hr))
			hr = pFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &pEncoder);
		if (SUCCEEDED(hr))
			hr = pEncoder->Initialize(pStream.Get(), WICBitmapEncoderNoCache);
		ComPtr<IWICBitmapFrameEncode> pFrame;
		ComPtr<IPropertyBag2> pProperties;
		if (SUCCEEDED(hr))
			hr = pEncoder->CreateNewFrame(&pFrame, &pProperties);
		if (SUCCEEDED(hr))
			hr = pFrame->Initialize(pProperties.Get());
		if (SUCCEEDED(hr))
			hr = pFrame->SetSize(uWidth, uHeight);
		WICPixelFormatGUID tFormat = GUID_WICPixelFormat32bppBGRA;
		if (SUCCEEDED(hr))
			hr = pFrame->SetPixelFormat(&tFormat);
		if (SUCCEEDED(hr) && tFormat != GUID_WICPixelFormat32bppBGRA)
			hr = E_FAIL;
		if (SUCCEEDED(hr))
			hr = pFrame->WritePixels(uHeight, uWidth * 4, static_cast<UINT>(vPixels.size()), const_cast<BYTE*>(vPixels.data()));
		if (SUCCEEDED(hr))
			hr = pFrame->Commit();
		if (SUCCEEDED(hr))
			hr = pEncoder->Commit();

		if (bInitialized)
			CoUninitialize();
		return SUCCEEDED(hr);
	}

static bool DecodeAvatarPng(const std::filesystem::path& tPath, std::vector<uint8_t>& vPixels, uint32_t& uWidth, uint32_t& uHeight)
{
		const HRESULT hrInitialize = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool bInitialized = SUCCEEDED(hrInitialize);
		if (!bInitialized && hrInitialize != RPC_E_CHANGED_MODE)
			return false;

		HRESULT hr = S_OK;
		ComPtr<IWICImagingFactory> pFactory;
		hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
		ComPtr<IWICBitmapDecoder> pDecoder;
		if (SUCCEEDED(hr))
			hr = pFactory->CreateDecoderFromFilename(tPath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &pDecoder);
		ComPtr<IWICBitmapFrameDecode> pFrame;
		if (SUCCEEDED(hr))
			hr = pDecoder->GetFrame(0, &pFrame);
		ComPtr<IWICFormatConverter> pConverter;
		if (SUCCEEDED(hr))
			hr = pFactory->CreateFormatConverter(&pConverter);
		if (SUCCEEDED(hr))
			hr = pConverter->Initialize(pFrame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom);
		if (SUCCEEDED(hr))
			hr = pConverter->GetSize(&uWidth, &uHeight);
		if (SUCCEEDED(hr) && !IsAvatarSizeValid(uWidth, uHeight))
			hr = E_FAIL;
		if (SUCCEEDED(hr))
		{
			vPixels.resize(static_cast<size_t>(uWidth) * uHeight * 4);
			hr = pConverter->CopyPixels(nullptr, uWidth * 4, static_cast<UINT>(vPixels.size()), vPixels.data());
		}

		if (bInitialized)
			CoUninitialize();
		return SUCCEEDED(hr);
	}

class CSteamProfilePersonaCallback final : public CCallbackBase
	{
	public:
	explicit CSteamProfilePersonaCallback(CSteamProfileCache& tCache) : m_tCache(tCache) {}
	void Run(void* pParameter) override
	{
		if (pParameter)
			m_tCache.HandlePersonaStateChange(*static_cast<PersonaStateChange_t*>(pParameter));
	}
		void Run(void*, bool, SteamAPICall_t) override {}
		int GetCallbackSizeBytes() override { return sizeof(PersonaStateChange_t); }
	private:
		CSteamProfileCache& m_tCache;
	};

class CSteamProfileAvatarCallback final : public CCallbackBase
	{
	public:
	explicit CSteamProfileAvatarCallback(CSteamProfileCache& tCache) : m_tCache(tCache) {}
	void Run(void* pParameter) override
	{
		if (pParameter)
			m_tCache.HandleAvatarImageLoaded(*static_cast<AvatarImageLoaded_t*>(pParameter));
	}
		void Run(void*, bool, SteamAPICall_t) override {}
		int GetCallbackSizeBytes() override { return sizeof(AvatarImageLoaded_t); }
	private:
		CSteamProfileCache& m_tCache;
	};

static CSteamProfilePersonaCallback* s_pPersonaCallback = nullptr;
static CSteamProfileAvatarCallback* s_pAvatarCallback = nullptr;

std::filesystem::path CSteamProfileCache::GetAvatarPath(uint32_t uAccountID)
{
	if (!uAccountID)
		return {};

	const auto tFolder = GetAvatarFolder();
	if (tFolder.empty())
		return {};
	return tFolder / (std::to_string(CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64()) + ".png");
}

bool CSteamProfileCache::SaveAvatarToDisk(uint32_t uAccountID, const std::vector<uint8_t>& vPixels, uint32_t uWidth, uint32_t uHeight, std::filesystem::path* pOutPath, bool bLog)
{
	const auto tPath = GetAvatarPath(uAccountID);
	if (tPath.empty())
		return false;
	if (pOutPath)
		*pOutPath = tPath;

	auto tTemporaryPath = tPath;
	tTemporaryPath += ".tmp";
	std::error_code ec;
	std::filesystem::remove(tTemporaryPath, ec);
	const bool bSaved = WriteAvatarPng(tTemporaryPath, vPixels, uWidth, uHeight);
	const bool bCommitted = bSaved && MoveFileExW(tTemporaryPath.c_str(), tPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
	if (!bCommitted)
		std::filesystem::remove(tTemporaryPath, ec);

	if (bLog)
	{
		const std::string sMessage = std::format("{} avatar {}", bCommitted ? "Saved" : "Failed to save", tPath.string());
		SDK::Output("SteamProfileCache", sMessage.c_str(), bCommitted ? LogColor : ErrorColor, OUTPUT_CONSOLE | OUTPUT_DEBUG);
	}
	return bCommitted;
}

void CSteamProfileCache::Initialize()
{
	EnsureCallbacksRegistered();
}

void CSteamProfileCache::Shutdown()
{
	std::lock_guard tLock(m_mutex);
	if (!m_bCallbacksRegistered)
		return;

	if (m_pUnregisterCallback)
	{
		m_pUnregisterCallback(s_pPersonaCallback);
		m_pUnregisterCallback(s_pAvatarCallback);
	}
	m_bCallbacksRegistered = false;
	m_pUnregisterCallback = nullptr;
}

void CSteamProfileCache::EnsureCallbacksRegistered()
{
	std::lock_guard tLock(m_mutex);
	if (m_bCallbacksRegistered)
		return;

	auto RegisterCallback = U::Memory.GetModuleExport<RegisterCallbackFn>("steam_api64.dll", "SteamAPI_RegisterCallback");
	if (!RegisterCallback)
		RegisterCallback = U::Memory.GetModuleExport<RegisterCallbackFn>("steam_api.dll", "SteamAPI_RegisterCallback");
	if (!RegisterCallback)
		return;

	static CSteamProfilePersonaCallback tPersonaCallback(*this);
	static CSteamProfileAvatarCallback tAvatarCallback(*this);
	s_pPersonaCallback = &tPersonaCallback;
	s_pAvatarCallback = &tAvatarCallback;
	RegisterCallback(s_pPersonaCallback, PersonaStateChange_t::k_iCallback);
	RegisterCallback(s_pAvatarCallback, AvatarImageLoaded_t::k_iCallback);
	m_pUnregisterCallback = U::Memory.GetModuleExport<UnregisterCallbackFn>("steam_api64.dll", "SteamAPI_UnregisterCallback");
	if (!m_pUnregisterCallback)
		m_pUnregisterCallback = U::Memory.GetModuleExport<UnregisterCallbackFn>("steam_api.dll", "SteamAPI_UnregisterCallback");
	m_bCallbacksRegistered = true;
}

void CSteamProfileCache::Touch(uint32_t uAccountID)
{
	if (!uAccountID)
		return;
	EnsureCallbacksRegistered();
	std::lock_guard tLock(m_mutex);
	RequestName(uAccountID, m_mEntries[uAccountID]);
}

void CSteamProfileCache::TouchAvatar(uint32_t uAccountID)
{
	if (!uAccountID)
		return;
	EnsureCallbacksRegistered();
	std::lock_guard tLock(m_mutex);
	auto& tEntry = m_mEntries[uAccountID];
	LoadAvatarFromDisk(uAccountID, tEntry);
	RequestName(uAccountID, tEntry);
	RequestAvatar(uAccountID, tEntry, false);
}

void CSteamProfileCache::Refresh(uint32_t uAccountID)
{
	if (!uAccountID)
		return;
	EnsureCallbacksRegistered();
	std::lock_guard tLock(m_mutex);
	auto& tEntry = m_mEntries[uAccountID];
	LoadAvatarFromDisk(uAccountID, tEntry);
	tEntry.m_bNameRequested = false;
	tEntry.m_bAvatarRequested = false;
	RequestName(uAccountID, tEntry);
	RequestAvatar(uAccountID, tEntry, true);
}

void CSteamProfileCache::Pump()
{
	EnsureCallbacksRegistered();
	std::vector<std::tuple<uint32_t, std::shared_ptr<std::vector<uint8_t>>, uint32_t, uint32_t>> vPendingSaves;
	std::vector<std::pair<uint32_t, int>> vPendingImages;
	{
		std::lock_guard tLock(m_mutex);
		for (auto& [uAccountID, tEntry] : m_mEntries)
		{
			if (tEntry.m_bSavePending && tEntry.m_pAvatarPixels)
			{
				tEntry.m_bSavePending = false;
				vPendingSaves.emplace_back(uAccountID, tEntry.m_pAvatarPixels, tEntry.m_uAvatarWidth, tEntry.m_uAvatarHeight);
			}
			if (tEntry.m_iPendingImage > 0)
			{
				vPendingImages.emplace_back(uAccountID, tEntry.m_iPendingImage);
				tEntry.m_iPendingImage = 0;
			}
		}
	}
	for (const auto& [uAccountID, pPixels, uWidth, uHeight] : vPendingSaves)
		SaveAvatarToDisk(uAccountID, *pPixels, uWidth, uHeight, nullptr, false);
	for (const auto& [uAccountID, iImage] : vPendingImages)
		CaptureSteamAvatar(uAccountID, iImage);
}

std::string CSteamProfileCache::GetPersonaName(uint32_t uAccountID)
{
	if (!uAccountID)
		return {};
	Touch(uAccountID);
	std::lock_guard tLock(m_mutex);
	return m_mEntries[uAccountID].m_sPersonaName;
}

bool CSteamProfileCache::TryGetAvatarImage(uint32_t uAccountID, AvatarImage_t& tOutImage)
{
	tOutImage = {};
	if (!uAccountID)
		return false;
	TouchAvatar(uAccountID);
	std::lock_guard tLock(m_mutex);
	const auto it = m_mEntries.find(uAccountID);
	if (it == m_mEntries.end() || !it->second.m_pAvatarPixels)
		return false;
	const auto& tEntry = it->second;
	tOutImage = { tEntry.m_pAvatarPixels, tEntry.m_uAvatarWidth, tEntry.m_uAvatarHeight, tEntry.m_uAvatarWidth * 4, tEntry.m_uAvatarRevision };
	return tOutImage.HasData();
}

void CSteamProfileCache::CaptureNativeAvatar(uint64_t uSteamID, const uint8_t* pRgba, uint32_t uWidth, uint32_t uHeight)
{
	if (!pRgba || !IsAvatarSizeValid(uWidth, uHeight))
		return;
	const CSteamID tSteamID(uSteamID);
	if (tSteamID.GetEAccountType() != k_EAccountTypeIndividual || !tSteamID.GetAccountID())
		return;

	std::vector<uint8_t> vBgra(static_cast<size_t>(uWidth) * uHeight * 4);
	for (size_t uIndex = 0; uIndex < vBgra.size(); uIndex += 4)
	{
		vBgra[uIndex] = pRgba[uIndex + 2];
		vBgra[uIndex + 1] = pRgba[uIndex + 1];
		vBgra[uIndex + 2] = pRgba[uIndex];
		vBgra[uIndex + 3] = pRgba[uIndex + 3];
	}
	std::lock_guard tLock(m_mutex);
	StoreAvatar(tSteamID.GetAccountID(), std::move(vBgra), uWidth, uHeight, true);
}

void CSteamProfileCache::RequestName(uint32_t uAccountID, Entry_t& tEntry)
{
	if (tEntry.m_bNameRequested || !I::SteamFriends)
		return;
	I::SteamFriends->RequestUserInformation(CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual), true);
	tEntry.m_bNameRequested = true;
}

void CSteamProfileCache::RequestAvatar(uint32_t uAccountID, Entry_t& tEntry, bool bForce)
{
	const bool bStale = !tEntry.m_tAvatarTimestamp.time_since_epoch().count() || std::filesystem::file_time_type::clock::now() - tEntry.m_tAvatarTimestamp >= AvatarRefreshAge;
	if (!I::SteamFriends || (!bForce && !bStale && tEntry.m_pAvatarPixels) || (tEntry.m_bAvatarRequested && !bForce))
		return;

	const CSteamID tSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual);
	I::SteamFriends->RequestUserInformation(tSteamID, false);
	tEntry.m_bAvatarRequested = true;
	if (const int iImage = I::SteamFriends->GetLargeFriendAvatar(tSteamID); iImage > 0)
		tEntry.m_iPendingImage = iImage;
}

void CSteamProfileCache::LoadAvatarFromDisk(uint32_t uAccountID, Entry_t& tEntry)
{
	if (tEntry.m_bDiskChecked)
		return;
	tEntry.m_bDiskChecked = true;
	const auto tPath = GetAvatarPath(uAccountID);
	std::error_code ec;
	if (tPath.empty() || !std::filesystem::is_regular_file(tPath, ec))
		return;

	std::vector<uint8_t> vPixels;
	uint32_t uWidth = 0, uHeight = 0;
	if (!DecodeAvatarPng(tPath, vPixels, uWidth, uHeight))
		return;
	const auto tTimestamp = std::filesystem::last_write_time(tPath, ec);
	StoreAvatar(uAccountID, std::move(vPixels), uWidth, uHeight, false);
	if (!ec)
		tEntry.m_tAvatarTimestamp = tTimestamp;
}

void CSteamProfileCache::CaptureSteamAvatar(uint32_t uAccountID, int iImage, uint32_t uWidth, uint32_t uHeight)
{
	if (!I::SteamUtils || iImage <= 0)
		return;
	if (!uWidth || !uHeight)
	{
		if (!I::SteamUtils->GetImageSize(iImage, &uWidth, &uHeight))
			return;
	}
	if (!IsAvatarSizeValid(uWidth, uHeight))
		return;

	std::vector<uint8_t> vRgba(static_cast<size_t>(uWidth) * uHeight * 4);
	if (!I::SteamUtils->GetImageRGBA(iImage, vRgba.data(), static_cast<int>(vRgba.size())))
		return;
	CaptureNativeAvatar(CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64(), vRgba.data(), uWidth, uHeight);
}

void CSteamProfileCache::StoreAvatar(uint32_t uAccountID, std::vector<uint8_t>&& vBgra, uint32_t uWidth, uint32_t uHeight, bool bSave)
{
	if (!IsAvatarSizeValid(uWidth, uHeight) || vBgra.size() != static_cast<size_t>(uWidth) * uHeight * 4)
		return;
	auto& tEntry = m_mEntries[uAccountID];
	if (tEntry.m_pAvatarPixels && tEntry.m_uAvatarWidth == uWidth && tEntry.m_uAvatarHeight == uHeight && *tEntry.m_pAvatarPixels == vBgra)
	{
		tEntry.m_tAvatarTimestamp = std::filesystem::file_time_type::clock::now();
		tEntry.m_bAvatarRequested = false;
		return;
	}
	tEntry.m_pAvatarPixels = std::make_shared<std::vector<uint8_t>>(std::move(vBgra));
	tEntry.m_uAvatarWidth = uWidth;
	tEntry.m_uAvatarHeight = uHeight;
	++tEntry.m_uAvatarRevision;
	tEntry.m_tAvatarTimestamp = std::filesystem::file_time_type::clock::now();
	tEntry.m_bAvatarRequested = false;
	tEntry.m_bSavePending = tEntry.m_bSavePending || bSave;
}

void CSteamProfileCache::HandlePersonaStateChange(const PersonaStateChange_t& tCallback)
{
	const CSteamID tSteamID(tCallback.m_ulSteamID);
	const uint32_t uAccountID = tSteamID.GetAccountID();
	if (!uAccountID || tSteamID.GetEAccountType() != k_EAccountTypeIndividual)
		return;

	std::lock_guard tLock(m_mutex);
	auto it = m_mEntries.find(uAccountID);
	if (it == m_mEntries.end())
		return;
	if (tCallback.m_nChangeFlags & k_EPersonaChangeName)
	{
		if (const char* pszName = I::SteamFriends ? I::SteamFriends->GetFriendPersonaName(tSteamID) : nullptr; pszName && *pszName)
			it->second.m_sPersonaName = pszName;
	}
	if (tCallback.m_nChangeFlags & k_EPersonaChangeAvatar)
	{
		it->second.m_bAvatarRequested = false;
		RequestAvatar(uAccountID, it->second, true);
	}
}

void CSteamProfileCache::HandleAvatarImageLoaded(const AvatarImageLoaded_t& tCallback)
{
	const uint32_t uAccountID = tCallback.m_steamID.GetAccountID();
	if (!uAccountID || tCallback.m_steamID.GetEAccountType() != k_EAccountTypeIndividual)
		return;
	CaptureSteamAvatar(uAccountID, tCallback.m_iImage, static_cast<uint32_t>(tCallback.m_iWide), static_cast<uint32_t>(tCallback.m_iTall));
}
