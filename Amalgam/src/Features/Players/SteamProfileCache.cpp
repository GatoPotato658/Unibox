#include "SteamProfileCache.h"
#include "../Configs/Configs.h"
#ifndef TEXTMODE
#include "../ImGui/Render.h"
#endif

#include <cstring>
#include <wincodec.h>
#include <wrl/client.h>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

static constexpr Color_t LogColor = { 175, 150, 255, 255 };
static constexpr Color_t ErrorColor = { 255, 150, 150, 255 };
static constexpr auto AvatarRefreshAge = std::chrono::days(7);
static constexpr uint32_t MaxAvatarDimension = 1024;
static constexpr double AvatarRequestTimeout = 8.0;

static double NowSeconds()
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

using RegisterCallbackFn = void(__cdecl*)(CCallbackBase*, int);
using UnregisterCallbackFn = void(__cdecl*)(CCallbackBase*);

template <class T>
static T GetSteamExport(const char* sName)
{
	if (auto pExport = U::Memory.GetModuleExport<T>("steam_api64.dll", sName))
		return pExport;
	return U::Memory.GetModuleExport<T>("steam_api.dll", sName);
}

static bool IsAvatarSizeValid(uint32_t uWidth, uint32_t uHeight)
{
	return uWidth && uHeight && uWidth <= MaxAvatarDimension && uHeight <= MaxAvatarDimension;
}

static size_t GetAvatarByteSize(uint32_t uWidth, uint32_t uHeight)
{
	return static_cast<size_t>(uWidth) * uHeight * 4;
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

class CComScope
{
public:
	CComScope() : m_hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
	~CComScope()
	{
		if (SUCCEEDED(m_hr))
			CoUninitialize();
	}
	CComScope(const CComScope&) = delete;
	CComScope& operator=(const CComScope&) = delete;

	[[nodiscard]] bool Usable() const { return SUCCEEDED(m_hr) || m_hr == RPC_E_CHANGED_MODE; }

private:
	HRESULT m_hr;
};

static bool WriteAvatarPng(const std::filesystem::path& tPath, const std::vector<uint8_t>& vPixels, uint32_t uWidth, uint32_t uHeight)
{
	if (!IsAvatarSizeValid(uWidth, uHeight) || vPixels.size() != GetAvatarByteSize(uWidth, uHeight))
		return false;

	CComScope tCom;
	if (!tCom.Usable())
		return false;

	ComPtr<IWICImagingFactory> pFactory;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory))))
		return false;

	ComPtr<IWICStream> pStream;
	if (FAILED(pFactory->CreateStream(&pStream)) || FAILED(pStream->InitializeFromFilename(tPath.c_str(), GENERIC_WRITE)))
		return false;

	ComPtr<IWICBitmapEncoder> pEncoder;
	if (FAILED(pFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &pEncoder)) || FAILED(pEncoder->Initialize(pStream.Get(), WICBitmapEncoderNoCache)))
		return false;

	ComPtr<IWICBitmapFrameEncode> pFrame;
	ComPtr<IPropertyBag2> pProperties;
	if (FAILED(pEncoder->CreateNewFrame(&pFrame, &pProperties)) || FAILED(pFrame->Initialize(pProperties.Get())) || FAILED(pFrame->SetSize(uWidth, uHeight)))
		return false;

	WICPixelFormatGUID tFormat = GUID_WICPixelFormat32bppBGRA;
	if (FAILED(pFrame->SetPixelFormat(&tFormat)) || tFormat != GUID_WICPixelFormat32bppBGRA)
		return false;

	return SUCCEEDED(pFrame->WritePixels(uHeight, uWidth * 4, static_cast<UINT>(vPixels.size()), const_cast<BYTE*>(vPixels.data())))
		&& SUCCEEDED(pFrame->Commit())
		&& SUCCEEDED(pEncoder->Commit());
}

static bool DecodeAvatarPng(const std::filesystem::path& tPath, std::vector<uint8_t>& vPixels, uint32_t& uWidth, uint32_t& uHeight)
{
	CComScope tCom;
	if (!tCom.Usable())
		return false;

	ComPtr<IWICImagingFactory> pFactory;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory))))
		return false;

	ComPtr<IWICBitmapDecoder> pDecoder;
	ComPtr<IWICBitmapFrameDecode> pFrame;
	if (FAILED(pFactory->CreateDecoderFromFilename(tPath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &pDecoder)) || FAILED(pDecoder->GetFrame(0, &pFrame)))
		return false;

	ComPtr<IWICFormatConverter> pConverter;
	if (FAILED(pFactory->CreateFormatConverter(&pConverter))
		|| FAILED(pConverter->Initialize(pFrame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom))
		|| FAILED(pConverter->GetSize(&uWidth, &uHeight))
		|| !IsAvatarSizeValid(uWidth, uHeight))
		return false;

	vPixels.resize(GetAvatarByteSize(uWidth, uHeight));
	return SUCCEEDED(pConverter->CopyPixels(nullptr, uWidth * 4, static_cast<UINT>(vPixels.size()), vPixels.data()));
}

template <class T>
class CSteamProfileCallback final : public CCallbackBase
{
public:
	using Handler_t = void(CSteamProfileCache::*)(const T&);

	explicit CSteamProfileCallback(Handler_t pHandler) : m_pHandler(pHandler) {}

	void Run(void* pParameter) override
	{
		if (pParameter)
			(F::SteamProfileCache.*m_pHandler)(*static_cast<T*>(pParameter));
	}
	void Run(void* pParameter, bool, SteamAPICall_t) override { Run(pParameter); }
	int GetCallbackSizeBytes() override { return sizeof(T); }

private:
	Handler_t m_pHandler;
};

static CSteamProfileCallback<PersonaStateChange_t> s_tPersonaCallback(&CSteamProfileCache::HandlePersonaStateChange);
static CSteamProfileCallback<AvatarImageLoaded_t> s_tAvatarCallback(&CSteamProfileCache::HandleAvatarImageLoaded);

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
	const bool bCommitted = WriteAvatarPng(tTemporaryPath, vPixels, uWidth, uHeight)
		&& MoveFileExW(tTemporaryPath.c_str(), tPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
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
		m_pUnregisterCallback(&s_tPersonaCallback);
		m_pUnregisterCallback(&s_tAvatarCallback);
	}
	m_bCallbacksRegistered = false;
	m_pUnregisterCallback = nullptr;
}

void CSteamProfileCache::EnsureCallbacksRegistered()
{
	std::lock_guard tLock(m_mutex);
	if (m_bCallbacksRegistered)
		return;

	const auto RegisterCallback = GetSteamExport<RegisterCallbackFn>("SteamAPI_RegisterCallback");
	if (!RegisterCallback)
		return;

	RegisterCallback(&s_tPersonaCallback, PersonaStateChange_t::k_iCallback);
	RegisterCallback(&s_tAvatarCallback, AvatarImageLoaded_t::k_iCallback);
	m_pUnregisterCallback = GetSteamExport<UnregisterCallbackFn>("SteamAPI_UnregisterCallback");
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
	RequestName(uAccountID, tEntry);
	if (!tEntry.m_bDiskChecked)
		tEntry.m_bDiskLoadQueued = true;
	else
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
	RequestName(uAccountID, tEntry);
	RequestAvatar(uAccountID, tEntry, true);
}

void CSteamProfileCache::Pump()
{
	constexpr size_t MaxDiskLoadsPerPump = 2;
	constexpr size_t MaxImagesPerPump = 4;
	constexpr size_t MaxSavesPerPump = 2;

	struct PendingSave_t
	{
		uint32_t m_uAccountID;
		std::shared_ptr<std::vector<uint8_t>> m_pPixels;
		uint32_t m_uWidth;
		uint32_t m_uHeight;
	};

	EnsureCallbacksRegistered();
	std::vector<PendingSave_t> vPendingSaves;
	std::vector<std::pair<uint32_t, int>> vPendingImages;
	std::vector<uint32_t> vPendingDiskLoads;
	{
		std::lock_guard tLock(m_mutex);
		for (auto& [uAccountID, tEntry] : m_mEntries)
		{
			if (tEntry.m_bSavePending && tEntry.m_pAvatarPixels)
			{
				// never overwrite the file before it was read, a smaller capture would replace a larger cached avatar
				if (!tEntry.m_bDiskChecked)
					tEntry.m_bDiskLoadQueued = true;
				else if (vPendingSaves.size() < MaxSavesPerPump)
				{
					tEntry.m_bSavePending = false;
					vPendingSaves.push_back({ uAccountID, tEntry.m_pAvatarPixels, tEntry.m_uAvatarWidth, tEntry.m_uAvatarHeight });
				}
			}
			if (vPendingImages.size() < MaxImagesPerPump && tEntry.m_iPendingImage > 0)
			{
				vPendingImages.emplace_back(uAccountID, tEntry.m_iPendingImage);
				tEntry.m_iPendingImage = 0;
			}
			if (vPendingDiskLoads.size() < MaxDiskLoadsPerPump && tEntry.m_bDiskLoadQueued && !tEntry.m_bDiskChecked)
			{
				tEntry.m_bDiskLoadQueued = false;
				tEntry.m_bDiskChecked = true;
				vPendingDiskLoads.push_back(uAccountID);
			}
		}
	}

	for (const uint32_t uAccountID : vPendingDiskLoads)
	{
		std::vector<uint8_t> vPixels;
		uint32_t uWidth = 0, uHeight = 0;
		std::filesystem::file_time_type tTimestamp = {};
		const bool bLoaded = ReadAvatarFile(uAccountID, vPixels, uWidth, uHeight, tTimestamp);

		std::lock_guard tLock(m_mutex);
		auto& tEntry = m_mEntries[uAccountID];
		if (bLoaded)
		{
			StoreAvatar(uAccountID, std::move(vPixels), uWidth, uHeight, false);
			tEntry.m_tAvatarTimestamp = tTimestamp;
		}
		RequestAvatar(uAccountID, tEntry, false);
	}
	for (const auto& [uAccountID, pPixels, uWidth, uHeight] : vPendingSaves)
		SaveAvatarToDisk(uAccountID, *pPixels, uWidth, uHeight, nullptr, false);
	for (const auto& [uAccountID, iImage] : vPendingImages)
		CaptureSteamAvatar(uAccountID, iImage);
}

bool CSteamProfileCache::IsAvatarPending(uint32_t uAccountID)
{
	if (!uAccountID)
		return false;

	TouchAvatar(uAccountID);
	std::lock_guard tLock(m_mutex);
	const auto it = m_mEntries.find(uAccountID);
	if (it == m_mEntries.end() || it->second.m_pAvatarPixels)
		return false;

	auto& tEntry = it->second;
	if (tEntry.m_bAvatarRequested && !tEntry.m_bAvatarUnavailable
		&& NowSeconds() - tEntry.m_flAvatarRequestedAt > AvatarRequestTimeout)
	{
		tEntry.m_bAvatarRequested = false;
		tEntry.m_bAvatarUnavailable = true;
	}
	return tEntry.m_bAvatarRequested || tEntry.m_bDiskLoadQueued || tEntry.m_iPendingImage > 0;
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

	std::vector<uint8_t> vBgra(GetAvatarByteSize(uWidth, uHeight));
	for (size_t i = 0; i < vBgra.size(); i += 4)
	{
		vBgra[i] = pRgba[i + 2];
		vBgra[i + 1] = pRgba[i + 1];
		vBgra[i + 2] = pRgba[i];
		vBgra[i + 3] = pRgba[i + 3];
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
	if (!I::SteamFriends)
		return;
	if (!bForce)
	{
		const bool bFresh = tEntry.m_pAvatarPixels && tEntry.m_tAvatarTimestamp.time_since_epoch().count()
			&& std::filesystem::file_time_type::clock::now() - tEntry.m_tAvatarTimestamp < AvatarRefreshAge;
		if (bFresh || tEntry.m_bAvatarRequested || tEntry.m_bAvatarUnavailable)
			return;
	}

	const CSteamID tSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual);
	I::SteamFriends->RequestUserInformation(tSteamID, false);
	tEntry.m_bAvatarRequested = true;
	tEntry.m_bAvatarUnavailable = false;
	tEntry.m_flAvatarRequestedAt = NowSeconds();
	if (const int iImage = I::SteamFriends->GetLargeFriendAvatar(tSteamID); iImage > 0)
		tEntry.m_iPendingImage = iImage;
}

bool CSteamProfileCache::ReadAvatarFile(uint32_t uAccountID, std::vector<uint8_t>& vPixels, uint32_t& uWidth, uint32_t& uHeight, std::filesystem::file_time_type& tTimestamp)
{
	const auto tPath = GetAvatarPath(uAccountID);
	std::error_code ec;
	if (tPath.empty() || !std::filesystem::is_regular_file(tPath, ec) || !DecodeAvatarPng(tPath, vPixels, uWidth, uHeight))
		return false;

	tTimestamp = std::filesystem::last_write_time(tPath, ec);
	if (ec)
		tTimestamp = std::filesystem::file_time_type::clock::now();
	return true;
}

void CSteamProfileCache::LoadAvatarFromDisk(uint32_t uAccountID, Entry_t& tEntry)
{
	if (tEntry.m_bDiskChecked)
		return;
	tEntry.m_bDiskChecked = true;
	tEntry.m_bDiskLoadQueued = false;

	std::vector<uint8_t> vPixels;
	uint32_t uWidth = 0, uHeight = 0;
	std::filesystem::file_time_type tTimestamp = {};
	if (!ReadAvatarFile(uAccountID, vPixels, uWidth, uHeight, tTimestamp))
		return;
	StoreAvatar(uAccountID, std::move(vPixels), uWidth, uHeight, false);
	tEntry.m_tAvatarTimestamp = tTimestamp;
}

void CSteamProfileCache::CaptureSteamAvatar(uint32_t uAccountID, int iImage, uint32_t uWidth, uint32_t uHeight)
{
	if (!I::SteamUtils || iImage <= 0)
		return;
	if ((!uWidth || !uHeight) && !I::SteamUtils->GetImageSize(iImage, &uWidth, &uHeight))
		return;
	if (!IsAvatarSizeValid(uWidth, uHeight))
		return;

	std::vector<uint8_t> vRgba(GetAvatarByteSize(uWidth, uHeight));
	if (!I::SteamUtils->GetImageRGBA(iImage, vRgba.data(), static_cast<int>(vRgba.size())))
		return;
	CaptureNativeAvatar(CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64(), vRgba.data(), uWidth, uHeight);
}

void CSteamProfileCache::StoreAvatar(uint32_t uAccountID, std::vector<uint8_t>&& vBgra, uint32_t uWidth, uint32_t uHeight, bool bSave)
{
	if (!IsAvatarSizeValid(uWidth, uHeight) || vBgra.size() != GetAvatarByteSize(uWidth, uHeight))
		return;

	auto& tEntry = m_mEntries[uAccountID];
	tEntry.m_bAvatarRequested = false;
	tEntry.m_bAvatarUnavailable = false;
	if (tEntry.m_pAvatarPixels)
	{
		// the game feeds small thumbnails through the same path, never swap a larger avatar for one
		if (GetAvatarByteSize(uWidth, uHeight) < tEntry.m_pAvatarPixels->size())
			return;
		if (tEntry.m_uAvatarWidth == uWidth && tEntry.m_uAvatarHeight == uHeight && *tEntry.m_pAvatarPixels == vBgra)
		{
			tEntry.m_tAvatarTimestamp = std::filesystem::file_time_type::clock::now();
			return;
		}
	}

	tEntry.m_pAvatarPixels = std::make_shared<std::vector<uint8_t>>(std::move(vBgra));
	tEntry.m_uAvatarWidth = uWidth;
	tEntry.m_uAvatarHeight = uHeight;
	++tEntry.m_uAvatarRevision;
	tEntry.m_tAvatarTimestamp = std::filesystem::file_time_type::clock::now();
	tEntry.m_bSavePending |= bSave;
}

void CSteamProfileCache::HandlePersonaStateChange(const PersonaStateChange_t& tCallback)
{
	const CSteamID tSteamID(tCallback.m_ulSteamID);
	const uint32_t uAccountID = tSteamID.GetAccountID();
	if (!uAccountID || tSteamID.GetEAccountType() != k_EAccountTypeIndividual)
		return;

	std::lock_guard tLock(m_mutex);
	const auto it = m_mEntries.find(uAccountID);
	if (it == m_mEntries.end())
		return;

	auto& tEntry = it->second;
	if (tCallback.m_nChangeFlags & k_EPersonaChangeName)
	{
		if (const char* pszName = I::SteamFriends ? I::SteamFriends->GetFriendPersonaName(tSteamID) : nullptr; pszName && *pszName)
			tEntry.m_sPersonaName = pszName;
	}
	if (tCallback.m_nChangeFlags & k_EPersonaChangeAvatar)
		RequestAvatar(uAccountID, tEntry, true);
}

void CSteamProfileCache::HandleAvatarImageLoaded(const AvatarImageLoaded_t& tCallback)
{
	const uint32_t uAccountID = tCallback.m_steamID.GetAccountID();
	if (!uAccountID || tCallback.m_steamID.GetEAccountType() != k_EAccountTypeIndividual)
		return;
	CaptureSteamAvatar(uAccountID, tCallback.m_iImage, static_cast<uint32_t>(tCallback.m_iWide), static_cast<uint32_t>(tCallback.m_iTall));
}

#ifndef TEXTMODE
struct AvatarTexture_t
{
	ComPtr<IDirect3DTexture9> m_pTexture;
	uint64_t m_uRevision = 0;
	uint64_t m_uFailedRevision = 0;
	double m_flLastUsed = 0.0;
	bool m_bLoggedSuccess = false;
};

static std::unordered_map<uint32_t, AvatarTexture_t> s_mTextures;
static IDirect3DDevice9* s_pTextureDevice = nullptr;
static bool s_bDefaultPool = false;

static void PruneTextures()
{
	static int iLastFrame = -1;
	if (ImGui::GetFrameCount() == iLastFrame)
		return;
	iLastFrame = ImGui::GetFrameCount();

	const double flNow = ImGui::GetTime();
	std::erase_if(s_mTextures, [&](const auto& tPair) { return flNow - tPair.second.m_flLastUsed > 10.0; });
}

static bool ConsumeUploadBudget()
{
	constexpr int MaxUploadsPerFrame = 3;
	static int iLastFrame = -1, iUploads = 0;
	if (ImGui::GetFrameCount() != iLastFrame)
	{
		iLastFrame = ImGui::GetFrameCount();
		iUploads = 0;
	}
	return iUploads++ < MaxUploadsPerFrame;
}

static ComPtr<IDirect3DTexture9> CreateAvatarTexture(IDirect3DDevice9* pDevice, const CSteamProfileCache::AvatarImage_t& tImage, HRESULT& hr)
{
	// D3D9Ex devices (what the game runs on) reject D3DPOOL_MANAGED with D3DERR_INVALIDCALL
	const D3DPOOL aPools[] = { s_bDefaultPool ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED, s_bDefaultPool ? D3DPOOL_MANAGED : D3DPOOL_DEFAULT };
	for (const D3DPOOL ePool : aPools)
	{
		const bool bDynamic = ePool == D3DPOOL_DEFAULT;
		ComPtr<IDirect3DTexture9> pTexture;
		hr = pDevice->CreateTexture(tImage.m_uWidth, tImage.m_uHeight, 1, bDynamic ? D3DUSAGE_DYNAMIC : 0, D3DFMT_A8R8G8B8, ePool, &pTexture, nullptr);
		if (FAILED(hr))
			continue;

		D3DLOCKED_RECT tRect = {};
		hr = pTexture->LockRect(0, &tRect, nullptr, bDynamic ? D3DLOCK_DISCARD : 0);
		if (FAILED(hr))
			continue;

		const size_t uRowBytes = size_t(tImage.m_uWidth) * 4;
		for (uint32_t y = 0; y < tImage.m_uHeight; y++)
			std::memcpy(static_cast<uint8_t*>(tRect.pBits) + size_t(y) * tRect.Pitch, tImage.m_pPixels->data() + y * uRowBytes, uRowBytes);
		pTexture->UnlockRect(0);

		s_bDefaultPool = bDynamic;
		return pTexture;
	}
	return {};
}

void* CSteamProfileCache::GetAvatarTexture(uint32_t uAccountID)
{
	auto* pDevice = F::Render.GetDevice();
	if (!uAccountID || !pDevice)
		return nullptr;

	if (pDevice != s_pTextureDevice)
	{
		ReleaseTextures();
		s_pTextureDevice = pDevice;
		ComPtr<IDirect3DDevice9Ex> pDeviceEx;
		s_bDefaultPool = SUCCEEDED(pDevice->QueryInterface(IID_PPV_ARGS(&pDeviceEx)));
	}

	PruneTextures();
	auto& tTexture = s_mTextures[uAccountID];
	tTexture.m_flLastUsed = ImGui::GetTime();

	AvatarImage_t tImage;
	if (!TryGetAvatarImage(uAccountID, tImage))
		return nullptr;

	if ((!tTexture.m_pTexture || tTexture.m_uRevision != tImage.m_uRevision) && tTexture.m_uFailedRevision != tImage.m_uRevision && ConsumeUploadBudget())
	{
		const uint64_t uSteamID64 = CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64();
		HRESULT hr = S_OK;
		if (auto pTexture = CreateAvatarTexture(pDevice, tImage, hr))
		{
			tTexture.m_pTexture = std::move(pTexture);
			tTexture.m_uRevision = tImage.m_uRevision;
			tTexture.m_uFailedRevision = 0;
			if (!tTexture.m_bLoggedSuccess)
			{
				tTexture.m_bLoggedSuccess = true;
				const std::string sMessage = std::format("ImGui now displaying avatar for {} ({}x{}).", uSteamID64, tImage.m_uWidth, tImage.m_uHeight);
				SDK::Output("SteamProfileCache", sMessage.c_str(), LogColor, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_MENU);
			}
		}
		else
		{
			tTexture.m_uFailedRevision = tImage.m_uRevision;
			const std::string sMessage = std::format("Failed to create D3D texture for avatar {} (hr=0x{:08X}).", uSteamID64, static_cast<uint32_t>(hr));
			SDK::Output("SteamProfileCache", sMessage.c_str(), ErrorColor, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_MENU);
		}
	}
	return tTexture.m_pTexture.Get();
}

void CSteamProfileCache::ReleaseTextures()
{
	s_mTextures.clear();
	s_pTextureDevice = nullptr;
}
#endif
