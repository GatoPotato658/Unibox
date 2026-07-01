#include "Materials.h"

#include "../Glow/Glow.h"
#include "../CameraWindow/CameraWindow.h"
#include "../../Configs/Configs.h"
#include "../../Binds/Binds.h"
#include "../Groups/Groups.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

IMaterial* CMaterials::Create(char const* szName, KeyValues* pKV)
{
	IMaterial* pMaterial = I::MaterialSystem->CreateMaterial(szName, pKV);
	if (!pMaterial)
		return nullptr;

	m_mMatList[pMaterial];
	return pMaterial;
}

IMaterial* CMaterials::create_from_vmt(const char* name, const std::string& vmt)
{
	KeyValues* kv = new KeyValues(name);
	if (!kv)
		return nullptr;

	if (!kv->LoadFromBuffer(name, vmt.c_str()))
	{
		kv->DeleteThis();
		return nullptr;
	}

	return Create(name, kv);
}

void CMaterials::Remove(IMaterial* pMaterial)
{
	if (!pMaterial)
		return;

	if (m_mMatList.contains(pMaterial))
		m_mMatList.erase(pMaterial);

	pMaterial->DecrementReferenceCount();
	pMaterial->DeleteIfUnreferenced();
	pMaterial = nullptr;
}



void CMaterials::StoreStruct(const std::string& sName, const std::string& sVMT, bool bLocked)
{
	Material_t tMaterial = {};
	tMaterial.m_sName = sName;
	tMaterial.m_sVMT = sVMT;
	tMaterial.m_bLocked = bLocked;

	m_mMaterials[FNV1A::Hash32(sName.c_str())] = tMaterial;
}

static inline void StoreVars(Material_t& tMaterial)
{
	if (tMaterial.m_bStored || !tMaterial.m_pMaterial)
		return;

	tMaterial.m_bStored = true;

	bool bFound; auto $phongtint = tMaterial.m_pMaterial->FindVar("$phongtint", &bFound, false);
	if (bFound)
		tMaterial.m_phongtint = $phongtint;
	
	auto $envmaptint = tMaterial.m_pMaterial->FindVar("$envmaptint", &bFound, false);
	if (bFound)
		tMaterial.m_envmaptint = $envmaptint;
	
	auto $invertcull = tMaterial.m_pMaterial->FindVar("$invertcull", &bFound, false);
	if (bFound && $invertcull && $invertcull->GetIntValueInternal())
		tMaterial.m_bInvertCull = true;
	
	auto $blockoccluded = tMaterial.m_pMaterial->FindVar("$blockoccluded", &bFound, false);
	if (bFound && $blockoccluded && $blockoccluded->GetIntValueInternal())
		tMaterial.m_bBlockOccluded = true;
}

static inline void RemoveVars(Material_t& tMaterial)
{
	tMaterial.m_bStored = false;
	tMaterial.m_phongtint = nullptr;
	tMaterial.m_envmaptint = nullptr;
	tMaterial.m_bInvertCull = false;
	tMaterial.m_bBlockOccluded = false;
}

static inline std::string to_lower(std::string value)
{
	std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

static inline bool has_material_key(const std::string& vmt, const char* key)
{
	return to_lower(vmt).find(to_lower(key)) != std::string::npos;
}

static inline bool is_vertex_lit_generic(const std::string& vmt)
{
	const std::string lower = to_lower(vmt);
	const size_t body = lower.find('{');
	const size_t shader = lower.find("vertexlitgeneric");
	return shader != std::string::npos && (body == std::string::npos || shader < body);
}

static inline std::string modify_vmt(const std::string& vmt)
{
	const size_t insert_pos = vmt.find_last_of('}');
	if (insert_pos == std::string::npos)
		return vmt;

	std::string modified = vmt;
	std::string append;
	const bool has_cloak_factor = has_material_key(vmt, "$cloakfactor");

	if (!has_material_key(vmt, "$model"))
		append += "\n\t$model \"1\"";

	if (!has_cloak_factor && !has_material_key(vmt, "$cloakpassenabled"))
		append += "\n\t$cloakpassenabled \"1\"";

	if (!has_cloak_factor && is_vertex_lit_generic(vmt) && !has_material_key(vmt, "proxies"))
		append += "\n\tProxies\n\t{\n\t\tinvis\n\t\t{\n\t\t}\n\t}";

	if (!append.empty())
		modified.insert(insert_pos, append);

	return modified;
}



void CMaterials::LoadMaterials()
{
	// default materials
	StoreStruct( // hacky
		"None",
			"\"UnlitGeneric\""
			"\n{"
			"\n\t$color2 \"[0 0 0]\""
			"\n\t$additive \"1\""
			"\n}",
		true);
	StoreStruct(
		"Flat",
			"\"UnlitGeneric\""
			"\n{"
			"\n\t$basetexture \"white\""
			"\n}",
		true);
	StoreStruct(
		"Shaded",
			"\"VertexLitGeneric\""
			"\n{"
			"\n\t$basetexture \"white\""
			"\n}",
		true);
	StoreStruct(
		"Wireframe",
			"\"UnlitGeneric\""
			"\n{"
			"\n\t$basetexture \"white\""
			"\n\t$wireframe \"1\""
			"\n}",
		true);
	StoreStruct(
		"Fresnel",
			"\"VertexLitGeneric\""
			"\n{"
			"\n\t$basetexture \"white\""
			"\n\t$bumpmap \"models/player/shared/shared_normal\""
			"\n\t$color2 \"[0 0 0]\""
			"\n\t$additive \"1\""
			"\n\t$phong \"1\""
			"\n\t$phongfresnelranges \"[0 0.5 1]\""
			"\n\t$envmap \"skybox/sky_dustbowl_01\""
			"\n\t$envmapfresnel \"1\""
			"\n}",
		true);
	StoreStruct(
		"Shine",
			"\"VertexLitGeneric\""
			"\n{"
			"\n\t$additive \"1\""
			"\n\t$envmap \"cubemaps/cubemap_sheen002.hdr\""
			"\n\t$envmaptint \"[1 1 1]\""
			"\n}",
		true);
	StoreStruct(
		"Tint",
			"\"VertexLitGeneric\""
			"\n{"
			"\n\t$basetexture \"models/player/shared/ice_player\""
			"\n\t$bumpmap \"models/player/shared/shared_normal\""
			"\n\t$additive \"1\""
			"\n\t$phong \"1\""
			"\n\t$phongfresnelranges \"[0 0.001 0.001]\""
			"\n\t$envmap \"skybox/sky_dustbowl_01\""
			"\n\t$envmapfresnel \"1\""
			"\n\t$selfillum \"1\""
			"\n\t$selfillumtint \"[0 0 0]\""
			"\n}",
		true);
	// user materials
	for (auto& tEntry : std::filesystem::directory_iterator(F::Configs.m_sMaterialsPath))
	{
		// Ignore all non-material files
		if (!tEntry.is_regular_file() || tEntry.path().extension() != std::string(".vmt"))
			continue;

		std::ifstream fStream(tEntry.path());
		if (!fStream.good())
			continue;

		std::string sName = tEntry.path().filename().string();
		sName.erase(sName.end() - 4, sName.end());
		std::string sVMT((std::istreambuf_iterator(fStream)), std::istreambuf_iterator<char>());

		auto uHash = FNV1A::Hash32(sName.c_str());
		if (uHash == FNV1A::Hash32Const("Original") || m_mMaterials.contains(uHash))
			continue;

		StoreStruct(sName, sVMT);
	}
	// create materials
	for (auto& tMaterial : m_mMaterials | std::views::values)
	{
		const std::string material_vmt = modify_vmt(tMaterial.m_sVMT);
		tMaterial.m_pMaterial = create_from_vmt(tMaterial.m_sName.c_str(), material_vmt);
		//StoreVars(tMaterial);
	}

	F::Glow.Initialize();
	F::CameraWindow.Initialize();

	S::InitializeStandardMaterials.Call<void>();
	auto pMaterial = *reinterpret_cast<IMaterial**>(U::Memory.RelToAbs(S::Wireframe()));
	if (pMaterial)
		pMaterial->SetMaterialVarFlag(MATERIAL_VAR_VERTEXALPHA, true);

	static std::unordered_map<std::string, int> mSkyboxes = {};
	static std::vector<const char*> vFaces = { "rt.vmt", "lf.vmt", "bk.vmt", "ft.vmt", "up.vmt", "dn.vmt" };
	FileFindHandle_t hFind;
	for (char const* szFile = I::FileSystem->FindFirst("materials/skybox/*.vmt", &hFind); szFile && *szFile; szFile = I::FileSystem->FindNext(hFind))
	{
		std::string sFile = szFile;

		int iFace = -1;
		for (int i = 0; i < vFaces.size(); i++)
		{
			auto sFace = vFaces[i];
			if (sFile.find(sFace) == sFile.length() - strlen(sFace))
			{
				iFace = 1 << i;
				sFile = sFile.substr(0, sFile.length() - strlen(sFace));
				break;
			}
		}
		if (iFace == -1)
			continue;

		mSkyboxes[sFile] |= iFace;
	}
	Vars::Visuals::World::SkyboxChanger.m_vValues = { "Off" };
	for (auto& [sSkybox, iFaces] : mSkyboxes)
	{
		if (iFaces == 0b111111)
			Vars::Visuals::World::SkyboxChanger.m_vValues.push_back(sSkybox.c_str());
	}

	m_bLoaded = true;
}

void CMaterials::UnloadMaterials()
{
	m_bLoaded = false;
	
	F::Glow.Unload();
	F::CameraWindow.Unload();

	for (auto& tMaterial : m_mMaterials | std::views::values)
		Remove(tMaterial.m_pMaterial);
	m_mMaterials.clear();
	m_mMatList.clear();
}

void CMaterials::ReloadMaterials()
{
	UnloadMaterials();

	LoadMaterials();
}



void CMaterials::SetColor(Material_t* pMaterial, Color_t tColor)
{
	float r = tColor.r / 255.f;
	float g = tColor.g / 255.f;
	float b = tColor.b / 255.f;
	float a = tColor.a / 255.f;

	I::RenderView->SetColorModulation(r, g, b);
	I::RenderView->SetBlend(a);

	if (pMaterial)
	{
		StoreVars(*pMaterial);
		if (pMaterial->m_phongtint)
			pMaterial->m_phongtint->SetVecValue(r, g, b);
		if (pMaterial->m_envmaptint)
			pMaterial->m_envmaptint->SetVecValue(r, g, b);
	}
}



Material_t* CMaterials::GetMaterial(uint32_t uHash)
{
	if (uHash == FNV1A::Hash32Const("Original"))
		return nullptr;

	return m_mMaterials.contains(uHash) ? &m_mMaterials[uHash] : nullptr;
}

std::string CMaterials::GetVMT(uint32_t uHash)
{
	if (m_mMaterials.contains(uHash))
		return m_mMaterials[uHash].m_sVMT;

	return "";
}

void CMaterials::AddMaterial(const char* sName)
{
	auto uHash = FNV1A::Hash32(sName);
	if (uHash == FNV1A::Hash32Const("Original") || std::filesystem::exists(F::Configs.m_sMaterialsPath + sName + ".vmt") || m_mMaterials.contains(uHash))
		return;

	StoreStruct(
		sName,
			"\"VertexLitGeneric\""
			"\n{"
			"\n\t"
			"\n}"
		);
	auto& tMaterial = m_mMaterials[uHash];

	const std::string material_vmt = modify_vmt(tMaterial.m_sVMT);
	tMaterial.m_pMaterial = create_from_vmt(sName, material_vmt);
	if (!tMaterial.m_pMaterial)
		return;

	//StoreVars(tMaterial);

	std::ofstream outStream(F::Configs.m_sMaterialsPath + sName + ".vmt");
	outStream << tMaterial.m_sVMT;
	outStream.close();
}

void CMaterials::EditMaterial(const char* sName, const char* sVMT)
{
	if (!std::filesystem::exists(F::Configs.m_sMaterialsPath + sName + ".vmt"))
		return;

	m_bLoaded = false;

	auto uHash = FNV1A::Hash32(sName);
	if (m_mMaterials.contains(uHash) && !m_mMaterials[uHash].m_bLocked)
	{
		auto& tMaterial = m_mMaterials[uHash];

		Remove(tMaterial.m_pMaterial);
		RemoveVars(tMaterial);
		tMaterial.m_sVMT = sVMT;

		const std::string material_vmt = modify_vmt(sVMT);
		tMaterial.m_pMaterial = create_from_vmt(sName, material_vmt);
		if (!tMaterial.m_pMaterial)
		{
			m_bLoaded = true;
			return;
		}

		//StoreVars(tMaterial);

		std::ofstream outStream(F::Configs.m_sMaterialsPath + sName + ".vmt");
		outStream << sVMT;
		outStream.close();
	}

	m_bLoaded = true;
}

void CMaterials::RemoveMaterial(const char* sName)
{
	if (!std::filesystem::exists(F::Configs.m_sMaterialsPath + sName + ".vmt"))
		return;

	m_bLoaded = false;

	auto uHash = FNV1A::Hash32(sName);
	if (m_mMaterials.contains(uHash) && !m_mMaterials[uHash].m_bLocked)
	{
		Remove(m_mMaterials[uHash].m_pMaterial);
		m_mMaterials.erase(uHash);

		std::filesystem::remove(F::Configs.m_sMaterialsPath + sName + ".vmt");

		auto fRemoveFromVal = [&](std::vector<std::pair<std::string, ChamsMaterial_t>>& val)
		{
			for (auto it = val.begin(); it != val.end();)
			{
				if (FNV1A::Hash32(it->first.c_str()) == uHash)
					it = val.erase(it);
				else
					++it;
			}
		};
		auto fRemoveFromVar = [&](ConfigVar<std::vector<std::pair<std::string, ChamsMaterial_t>>>& var)
		{
			for (auto& [iBind, vVal] : var.Map)
			{
				fRemoveFromVal(vVal);
			}
		};
		for (auto& tGroup : F::Groups.m_vGroups)
		{
			fRemoveFromVal(tGroup.m_tChams.Visible);
			fRemoveFromVal(tGroup.m_tChams.Occluded);
			fRemoveFromVal(tGroup.m_tBacktrackChams.Visible);
			fRemoveFromVal(tGroup.m_tBacktrackChams.Occluded);
		}
	}

	m_bLoaded = true;
}
