#include "../SDK/SDK.h"
#include "../Features/NavBot/NavEngine/NavEngine.h"
#include "../Features/NavBot/NavBotJobs/NavBotJobs.h"

MAKE_SIGNATURE(CEntityMapData_ExtractValue, "client.dll", "48 8B 09 E9 ? ? ? ? CC CC CC CC CC CC CC CC 48 89 5C 24 ? 48 89 6C 24", 0x0);
MAKE_SIGNATURE(C_PhysPropClientside_ParseEntity_ExtractValue_Call, "client.dll", "84 C0 75 ? 48 8D 0D ? ? ? ? FF 15 ? ? ? ? CC 48 8D 15", 0x0);

static Vector ParseVector(const char* szValue)
{
	Vector vOut;
	std::string str(szValue);
	size_t uOff = 0, uMaxSize = str.size();
	for (int i = 0; i < 3; i++)
	{
		size_t uNextValue = str.find(' ', uOff) + 1;
		std::string strSub = str.substr(uOff, (uNextValue == std::string::npos ? uMaxSize : uNextValue) - uOff);
		uOff = uNextValue;
		vOut[i] = atof(strSub.c_str());
	}
	return vOut;
}

static bool ParseTrigger(CEntityMapData* pData, TriggerTypeEnum::TriggerTypeEnum eType, const char* szType)
{
	char szKeyName[MAPKEY_MAXLENGTH];
	char szValue[MAPKEY_MAXLENGTH];

	if (pData->GetFirstKey(szKeyName, szValue))
	{
		model_t* pModel = nullptr;
		Vector vOrigin = {}, vAngles = {}, vRotate = {};
		int iTeam = 0;

		bool bIsRespawnRoom = eType == TriggerTypeEnum::RespawnRoom;
		do
		{
			auto uKeyNameHash = FNV1A::Hash32(szKeyName);
			switch (uKeyNameHash)
			{
			case FNV1A::Hash32Const("model"):
			{
				pModel = I::ModelLoader->FindModel(szValue);
				break;
			}
			case FNV1A::Hash32Const("TeamNum"):
			{
				iTeam = atoi(szValue);
				break;
			}
			case FNV1A::Hash32Const("origin"):
			{
				vOrigin = ParseVector(szValue);
				break;
			}
			case FNV1A::Hash32Const("angles"):
			{
				vRotate = ParseVector(szValue);
				break;
			}
			case FNV1A::Hash32Const("parentname"):
			{
				// We can't get the actual position of this trigger.
				return false;
			}
			case FNV1A::Hash32Const("pushdir"):
			case FNV1A::Hash32Const("impulse_dir"):
			case FNV1A::Hash32Const("launchDirection"):
			{
				vAngles = ParseVector(szValue);
				break;
			}
			default:
				break;
			}
		}
		while (pData->GetNextKey(szKeyName, szValue));
		if (pModel)
		{
			TriggerData_t tData = TriggerData_t{ pModel, eType, vOrigin, {}, vAngles, vRotate, iTeam, {} };
			SDK::BuildTriggerGeometry(tData);
#ifndef TEXTMODE
			G::TriggerStorage.push_back(tData);
#endif 
			if (bIsRespawnRoom)
				F::NavEngine.AddRespawnRoom(iTeam, tData);
			return true;
		}
	}
	return false;
}

MAKE_HOOK(CEntityMapData_ExtractValue, S::CEntityMapData_ExtractValue(), bool,
	CEntityMapData* rcx, const char* keyName, char* Value)
{
	DEBUG_RETURN(CEntityMapData_ExtractValue, rcx, keyName, Value);

	static const auto dwParseEnt = S::C_PhysPropClientside_ParseEntity_ExtractValue_Call();
	const auto dwRetAddr = uintptr_t(_ReturnAddress());

	bool bReturn = CALL_ORIGINAL(rcx, keyName, Value);
	if (bReturn && dwRetAddr == dwParseEnt)
	{
		auto uClassNameHash = FNV1A::Hash32(Value);
		TriggerTypeEnum::TriggerTypeEnum eType = TriggerTypeEnum::None;
		bool bHealth = true;
		switch (uClassNameHash)
		{
#ifndef TEXTMODE
		case FNV1A::Hash32Const("trigger_hurt"):
			eType = TriggerTypeEnum::Hurt;
			break;
		case FNV1A::Hash32Const("trigger_ignite"):
			eType = TriggerTypeEnum::Ignite;
			break;
		case FNV1A::Hash32Const("trigger_push"):
			eType = TriggerTypeEnum::Push;
			break;
#endif
		case FNV1A::Hash32Const("func_respawnroom"):
			eType = TriggerTypeEnum::RespawnRoom;
			break;
#ifndef TEXTMODE
		case FNV1A::Hash32Const("func_regenerate"):
			eType = TriggerTypeEnum::Regenerate;
			break;
		case FNV1A::Hash32Const("func_upgradestation"):
			eType = TriggerTypeEnum::UpgradeStation;
			break;
		case FNV1A::Hash32Const("trigger_capture_area"):
		case FNV1A::Hash32Const("func_capturezone"):
			eType = TriggerTypeEnum::CaptureArea;
			break;
		case FNV1A::Hash32Const("trigger_catapult"):
			eType = TriggerTypeEnum::Catapult;
			break;
		case FNV1A::Hash32Const("trigger_apply_impulse"):
			eType = TriggerTypeEnum::ApplyImpulse;
			break;
#endif
		case FNV1A::Hash32Const("item_ammopack_full"):
		case FNV1A::Hash32Const("item_ammopack_medium"):
		case FNV1A::Hash32Const("item_ammopack_small"):
			bHealth = false;
			[[fallthrough]];
		case FNV1A::Hash32Const("item_healthkit_full"):
		case FNV1A::Hash32Const("item_healthkit_medium"):
		case FNV1A::Hash32Const("item_healthkit_small"):
		{
			char szValue[MAPKEY_MAXLENGTH];
			if (S::CEntityMapData_ExtractValue.Call<bool>(rcx, "origin", szValue))
				F::NavBotSupplies.AddCachedSupplyOrigin(ParseVector(szValue), bHealth);
			break;
		}
		default:
			break;
		}
		if (eType != TriggerTypeEnum::None)
			ParseTrigger(rcx, eType, Value);
	}
	return bReturn;
}
