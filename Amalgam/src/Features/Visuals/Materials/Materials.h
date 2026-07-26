#pragma once
#include "../../../SDK/SDK.h"

#include <atomic>

struct Material_t
{
	IMaterial* m_pMaterial;

	std::string m_sName;
	std::string m_sVMT;
	bool m_bLocked = false;

	bool m_bStored = false;
	IMaterialVar* m_phongtint = nullptr;
	IMaterialVar* m_envmaptint = nullptr;
	bool m_bInvertCull = false;
	bool m_bBlockOccluded = false;
};

class CMaterials
{
public:
	enum class PendingOperation : uint8_t
	{
		None,
		Load,
		Reload,
		Unload
	};

	void LoadMaterials();
	void UnloadMaterials();
	void ReloadMaterials();
	void RequestLoad();
	void RequestUnload();
	void ServicePendingOperation();
	bool IsUnloadComplete() const;

	IMaterial* Create(char const* szName, KeyValues* pKV);
	IMaterial* create_from_vmt(const char* name, const std::string& vmt);
	void Remove(IMaterial* pMaterial);
	void StoreStruct(const std::string& sName, const std::string& sVMT, bool bLocked = false);

	void SetColor(Material_t* pMaterial, Color_t tColor);

	Material_t* GetMaterial(uint32_t uHash);
	std::string GetVMT(uint32_t uHash);
	void AddMaterial(const char* sName);
	void EditMaterial(const char* sName, const char* sVMT);
	void RemoveMaterial(const char* sName);

	std::unordered_map<uint32_t, Material_t> m_mMaterials = {};

	bool m_bLoaded = false;

private:
	std::atomic<PendingOperation> m_ePendingOperation = PendingOperation::None;
	std::atomic_bool m_bUnloadComplete = false;
};

ADD_FEATURE(CMaterials, Materials);
