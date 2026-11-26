#include "../SDK.h"

#include "../../Features/NavBot/NavEngine.h"

#include <cctype>
#include <charconv>
#include <cstring>

static void SkipEntityWhitespace(std::string_view sData, size_t& iOffset)
	{
		while (iOffset < sData.size() && static_cast<unsigned char>(sData[iOffset]) <= ' ')
			iOffset++;
	}

	static bool ParseQuotedEntityTokenView(std::string_view sData, size_t& iOffset, std::string_view& sToken)
	{
		SkipEntityWhitespace(sData, iOffset);
		if (iOffset >= sData.size() || sData[iOffset] != '"')
		{
			sToken = {};
			return false;
		}

		iOffset++;
		const size_t iStart = iOffset;
		while (iOffset < sData.size())
		{
			if (sData[iOffset] == '\\' && iOffset + 1 < sData.size())
			{
				iOffset += 2;
				continue;
			}
			if (sData[iOffset] == '"')
				break;
			iOffset++;
		}

		const size_t iEnd = iOffset;
		if (iOffset < sData.size() && sData[iOffset] == '"')
			iOffset++;

		sToken = sData.substr(iStart, iEnd - iStart);
		return true;
	}

	static std::string UnescapeEntityToken(std::string_view sToken)
	{
		std::string sOut;
		sOut.reserve(sToken.size());
		for (size_t i = 0; i < sToken.size(); i++)
		{
			if (sToken[i] == '\\' && i + 1 < sToken.size())
				i++;
			sOut.push_back(sToken[i]);
		}
		return sOut;
	}

	static auto TriggerTypeFromClassname(std::string_view sClassname, bool& bRespawnRoomOut) -> TriggerTypeEnum::TriggerTypeEnum
	{
		bRespawnRoomOut = false;
		switch (FNV1A::Hash32(sClassname))
		{
		case FNV1A::Hash32Const("trigger_hurt"):
			return TriggerTypeEnum::Hurt;
		case FNV1A::Hash32Const("trigger_ignite"):
			return TriggerTypeEnum::Ignite;
		case FNV1A::Hash32Const("trigger_push"):
			return TriggerTypeEnum::Push;
		case FNV1A::Hash32Const("func_respawnroom"):
			bRespawnRoomOut = true;
			return TriggerTypeEnum::RespawnRoom;
		case FNV1A::Hash32Const("func_regenerate"):
			return TriggerTypeEnum::Regenerate;
		case FNV1A::Hash32Const("func_upgradestation"):
			return TriggerTypeEnum::UpgradeStation;
		case FNV1A::Hash32Const("trigger_capture_area"):
		case FNV1A::Hash32Const("func_capturezone"):
			return TriggerTypeEnum::CaptureArea;
		case FNV1A::Hash32Const("trigger_catapult"):
			return TriggerTypeEnum::Catapult;
		case FNV1A::Hash32Const("trigger_apply_impulse"):
			return TriggerTypeEnum::ApplyImpulse;
		case FNV1A::Hash32Const("func_passtime_no_ball_zone"):
			return TriggerTypeEnum::NoBallZone;
		default:
			return TriggerTypeEnum::None;
		}
	}

	static bool FastParseFloat(const char*& p, const char* pEnd, float& flOut)
	{
		while (p < pEnd && static_cast<unsigned char>(*p) <= ' ')
			p++;
		if (p >= pEnd)
			return false;
		if (*p == '+')
		{
			p++;
			if (p >= pEnd || *p == '+' || *p == '-')
				return false;
		}
		auto [next, ec] = std::from_chars(p, pEnd, flOut);
		if (ec != std::errc() || next == p)
			return false;
		p = next;
		return true;
	}

	static Vector ParseEntityVector(std::string_view sValue)
	{
		Vector vOut = {};
		if (sValue.empty())
			return vOut;

		const char* p = sValue.data();
		const char* pEnd = p + sValue.size();
		if (FastParseFloat(p, pEnd, vOut.x) && FastParseFloat(p, pEnd, vOut.y))
			FastParseFloat(p, pEnd, vOut.z);
		return vOut;
	}

	static int ParseEntityInt(std::string_view sValue)
	{
		if (sValue.empty())
			return 0;

		int iOut = 0;
		const char* p = sValue.data();
		const char* pEnd = p + sValue.size();
		while (p < pEnd && static_cast<unsigned char>(*p) <= ' ')
			p++;
		if (p < pEnd && *p == '+')
			p++;
		std::from_chars(p, pEnd, iOut);
		return iOut;
	}

	struct ParsedEntity_t
	{
		std::string_view m_sClassname = {};
		std::string_view m_sModel = {};
		std::string_view m_sParentname = {};
		std::string_view m_sOrigin = {};
		std::string_view m_sPushDir = {};
		std::string_view m_sImpulseDir = {};
		std::string_view m_sLaunchDir = {};
		std::string_view m_sAngles = {};
		std::string_view m_sTeamNum = {};
		std::string m_sTargetname = {};
		std::string_view m_sFilterName = {};
		std::string_view m_sMoveDir = {};
		std::string_view m_sSpeed = {};
		std::string_view m_sLip = {};
		std::string_view m_sWait = {};
		std::string_view m_sSpawnflags = {};
		std::string_view m_sStartDisabled = {};
		std::string_view m_sPoints = {};
		std::string_view m_sForceClosed = {};
		std::string_view m_sNegated = {};
		std::string_view m_sControlPoint = {};
		std::vector<std::pair<std::string, std::string>> m_vOutputs = {};
	};

	static bool AppendTriggerFromParsed(const ParsedEntity_t& tEnt)
	{
		if (tEnt.m_sClassname.empty() || tEnt.m_sModel.empty() || !tEnt.m_sParentname.empty())
			return false;

		bool bIsRespawnRoom = false;
		const auto eType = TriggerTypeFromClassname(tEnt.m_sClassname, bIsRespawnRoom);
		if (eType == TriggerTypeEnum::None)
			return false;

		char szModel[64] = {};
		if (tEnt.m_sModel.size() >= sizeof(szModel))
			return false;
		std::memcpy(szModel, tEnt.m_sModel.data(), tEnt.m_sModel.size());
		szModel[tEnt.m_sModel.size()] = '\0';

		model_t* pModel = I::ModelLoader->FindModel(szModel);
		if (!pModel)
			return false;

		const Vector vOrigin = ParseEntityVector(tEnt.m_sOrigin);
		Vector vAngles = {};
		Vector vRotate = {};

		if (!tEnt.m_sPushDir.empty())
			vAngles = ParseEntityVector(tEnt.m_sPushDir);
		else if (!tEnt.m_sImpulseDir.empty())
			vAngles = ParseEntityVector(tEnt.m_sImpulseDir);
		else if (!tEnt.m_sLaunchDir.empty())
			vAngles = ParseEntityVector(tEnt.m_sLaunchDir);

		if (!tEnt.m_sAngles.empty())
			vRotate = ParseEntityVector(tEnt.m_sAngles);

		const int iTeam = ParseEntityInt(tEnt.m_sTeamNum);

		TriggerData_t tTrigger = { pModel, eType, vOrigin, {}, vAngles, vRotate, iTeam, {} };
		SDK::BuildTriggerGeometry(tTrigger);
		G::TriggerStorage.push_back(tTrigger);
		if (bIsRespawnRoom)
			F::NavEngine.AddRespawnRoom(iTeam, tTrigger);
		return true;
	}

	static void AppendPasstimeGoalFromParsed(const ParsedEntity_t& tEnt)
	{
		if (tEnt.m_sClassname != "func_passtime_goal")
			return;

		PasstimeMapGoalData_t tGoal = {};
		tGoal.m_vOrigin = ParseEntityVector(tEnt.m_sOrigin);
		tGoal.m_sTargetname = tEnt.m_sTargetname;
		tGoal.m_iTeam = ParseEntityInt(tEnt.m_sTeamNum);
		tGoal.m_iSpawnflags = ParseEntityInt(tEnt.m_sSpawnflags);
		tGoal.m_bStartDisabled = ParseEntityInt(tEnt.m_sStartDisabled) != 0;
		if (!tEnt.m_sPoints.empty())
			tGoal.m_iPoints = ParseEntityInt(tEnt.m_sPoints);

		G::PasstimeGoalStorage.push_back(std::move(tGoal));
	}

	static bool EqualsIgnoreCase(std::string_view sLeft, std::string_view sRight)
	{
		if (sLeft.size() != sRight.size())
			return false;

		for (size_t i = 0; i < sLeft.size(); i++)
		{
			if (static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(sLeft[i])))
				!= static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(sRight[i]))))
				return false;
		}
		return true;
	}

	static bool NamesMatch(std::string_view sTarget, std::string_view sName)
	{
		if (sTarget.empty() || sName.empty())
			return false;

		if (sTarget.back() != '*')
			return EqualsIgnoreCase(sTarget, sName);

		sTarget.remove_suffix(1);
		return sName.size() >= sTarget.size() && EqualsIgnoreCase(sName.substr(0, sTarget.size()), sTarget);
	}

	static float ParseEntityFloat(std::string_view sValue)
	{
		float flOut = 0.f;
		if (sValue.empty())
			return flOut;

		const char* p = sValue.data();
		FastParseFloat(p, p + sValue.size(), flOut);
		return flOut;
	}

	static int BrushModel(std::string_view sModel)
	{
		if (sModel.size() < 2 || sModel.front() != '*')
			return -1;

		sModel.remove_prefix(1);
		return ParseEntityInt(sModel);
	}

	static bool ModelBounds(int iModel, Vector& vMins, Vector& vMaxs)
	{
		if (iModel < 0)
			return false;

		char szModel[32] = {};
		std::snprintf(szModel, sizeof(szModel), "*%d", iModel);
		const model_t* pModel = I::ModelLoader->FindModel(szModel);
		if (!pModel)
			return false;

		I::ModelInfoClient->GetModelBounds(pModel, vMins, vMaxs);
		return true;
	}

	static bool SplitOutput(std::string_view sValue, std::string_view* pFields, size_t uCount)
	{
		for (size_t i = 0; i < uCount; i++)
		{
			const size_t uSeparator = sValue.find_first_of(",\x1b");
			if (uSeparator == std::string_view::npos)
			{
				pFields[i] = sValue;
				return i + 1 == uCount;
			}

			pFields[i] = sValue.substr(0, uSeparator);
			sValue.remove_prefix(uSeparator + 1);
		}
		return true;
	}

	static bool IsGatingInput(std::string_view sInput)
	{
		return EqualsIgnoreCase(sInput, "Enable")
			|| EqualsIgnoreCase(sInput, "Disable")
			|| EqualsIgnoreCase(sInput, "DisableAndEndTouch")
			|| EqualsIgnoreCase(sInput, "Toggle")
			|| EqualsIgnoreCase(sInput, "Kill")
			|| EqualsIgnoreCase(sInput, "KillHierarchy");
	}

	static bool AddDoorLink(DoorMapData_t& tDoor, int iTrigger, bool bAll, bool bOpens, bool bCloses)
	{
		int iLink = 0;
		while (iLink < tDoor.m_iLinkCount && tDoor.m_aLinks[iLink].m_iTrigger != iTrigger)
			iLink++;

		if (iLink >= DOOR_MAP_MAX_LINKS)
			return false;

		if (iLink == tDoor.m_iLinkCount)
			tDoor.m_aLinks[tDoor.m_iLinkCount++] = { iTrigger };

		DoorMapLink_t& tLink = tDoor.m_aLinks[iLink];
		tLink.m_bAll = tLink.m_bAll || bAll;
		tLink.m_bOpens = tLink.m_bOpens || bOpens;
		tLink.m_bCloses = tLink.m_bCloses || bCloses;
		return true;
	}

	static bool BuildDoorStorageFromEntities(const std::vector<ParsedEntity_t>& vEntities)
	{
		std::vector<std::string> vGated;
		struct Filter_t
		{
			std::string m_sName;
			int m_iTeam;
		};
		std::vector<Filter_t> vFilters;

		for (const ParsedEntity_t& tEnt : vEntities)
		{
			for (const auto& [sName, sValue] : tEnt.m_vOutputs)
			{
				std::string_view aFields[5] = {};
				if (!SplitOutput(sValue, aFields, 5) || !IsGatingInput(aFields[1]) || vGated.size() >= 256)
					continue;
				vGated.emplace_back(UnescapeEntityToken(aFields[0]));
			}

			if (EqualsIgnoreCase(tEnt.m_sClassname, "filter_activator_tfteam") && !tEnt.m_sTargetname.empty()
				&& ParseEntityInt(tEnt.m_sNegated) == 0 && tEnt.m_sControlPoint.empty() && vFilters.size() < 64)
				vFilters.push_back({ tEnt.m_sTargetname, ParseEntityInt(tEnt.m_sTeamNum) });
		}

		for (const ParsedEntity_t& tEnt : vEntities)
		{
			if (!EqualsIgnoreCase(tEnt.m_sClassname, "func_door") || G::DoorStorage.size() >= DOOR_MAP_MAX_COUNT)
				continue;

			const int iFlags = ParseEntityInt(tEnt.m_sSpawnflags);
			const int iModel = BrushModel(tEnt.m_sModel);
			if ((iFlags & (SF_DOOR_NONSOLID_TO_PLAYER | SF_DOOR_PASSABLE)) || !tEnt.m_sParentname.empty() || iModel < 0)
				continue;

			Vector vMins = {};
			Vector vMaxs = {};
			if (!ModelBounds(iModel, vMins, vMaxs))
				continue;

			DoorMapData_t tDoor = {};
			tDoor.m_iModel = iModel;
			tDoor.m_vPos1 = ParseEntityVector(tEnt.m_sOrigin);
			tDoor.m_vMins = vMins + Vector(1.f, 1.f, 1.f);
			tDoor.m_vMaxs = vMaxs - Vector(1.f, 1.f, 1.f);
			Math::AngleVectors(ParseEntityVector(tEnt.m_sMoveDir), &tDoor.m_vDir);

			const Vector vSize = vMaxs - vMins - Vector(2.f, 2.f, 2.f);
			const float flTravel = std::fabs(tDoor.m_vDir.x * vSize.x) + std::fabs(tDoor.m_vDir.y * vSize.y)
				+ std::fabs(tDoor.m_vDir.z * vSize.z) - ParseEntityFloat(tEnt.m_sLip);
			tDoor.m_vPos2 = tDoor.m_vPos1 + tDoor.m_vDir * flTravel;

			float flSpeed = ParseEntityFloat(tEnt.m_sSpeed);
			if (flSpeed == 0.f)
				flSpeed = 100.f;
			tDoor.m_flStep = flSpeed * TICK_INTERVAL;

			const float flWait = ParseEntityFloat(tEnt.m_sWait);
			const bool bAutoReturns = flWait >= 0.f && !(iFlags & SF_DOOR_NO_AUTO_RETURN);
			tDoor.m_bReverses = flWait >= 0.f;
			tDoor.m_bPredicted = !(iFlags & (SF_DOOR_PTOUCH | SF_DOOR_LOCKED))
				&& ParseEntityInt(tEnt.m_sForceClosed) == 0 && !bAutoReturns;
			tDoor.m_sTargetname = tEnt.m_sTargetname;
			G::DoorStorage.push_back(std::move(tDoor));
		}

		int iTriggerCount = 0;
		for (const ParsedEntity_t& tEnt : vEntities)
		{
			if (!EqualsIgnoreCase(tEnt.m_sClassname, "trigger_multiple"))
				continue;

			int iTeam = 0;
			bool bUsable = (ParseEntityInt(tEnt.m_sSpawnflags) & SF_TRIGGER_ALLOW_CLIENTS)
				&& ParseEntityInt(tEnt.m_sStartDisabled) == 0;
			for (const std::string& sGated : vGated)
			{
				if (NamesMatch(sGated, tEnt.m_sTargetname))
				{
					bUsable = false;
					break;
				}
			}
			if (bUsable && !tEnt.m_sFilterName.empty())
			{
				bUsable = false;
				for (const Filter_t& tFilter : vFilters)
				{
					if (EqualsIgnoreCase(tFilter.m_sName, tEnt.m_sFilterName))
					{
						iTeam = tFilter.m_iTeam;
						bUsable = true;
						break;
					}
				}
			}

			Vector vMins = {};
			Vector vMaxs = {};
			const int iModel = BrushModel(tEnt.m_sModel);
			const bool bBounded = bUsable && iModel >= 0 && ModelBounds(iModel, vMins, vMaxs);
			const Vector vOrigin = ParseEntityVector(tEnt.m_sOrigin);
			const int iTrigger = iTriggerCount;

			for (const auto& [sOutputName, sOutputValue] : tEnt.m_vOutputs)
			{
				std::string_view aFields[5] = {};
				if (!SplitOutput(sOutputValue, aFields, 5))
					continue;

				const bool bAll = EqualsIgnoreCase(sOutputName, "OnStartTouchAll");
				const bool bOpens = EqualsIgnoreCase(aFields[1], "Open")
					&& (bAll || EqualsIgnoreCase(sOutputName, "OnStartTouch"));
				const bool bCloses = EqualsIgnoreCase(aFields[1], "Close")
					&& EqualsIgnoreCase(sOutputName, "OnEndTouchAll");

				for (DoorMapData_t& tDoor : G::DoorStorage)
				{
					if (!NamesMatch(aFields[0], tDoor.m_sTargetname))
						continue;

					if (!bBounded || (!bOpens && !bCloses) || ParseEntityFloat(aFields[3]) != 0.f
						|| iTriggerCount >= DOOR_MAP_MAX_TRIGGERS)
					{
						tDoor.m_bPredicted = false;
						continue;
					}

					if (!AddDoorLink(tDoor, iTrigger, bAll, bOpens, bCloses))
					{
						tDoor.m_bPredicted = false;
						continue;
					}

					if (iTriggerCount == iTrigger)
					{
						G::DoorTriggerStorage.push_back({
							vOrigin + vMins + Vector(1.f, 1.f, 1.f),
							vOrigin + vMaxs - Vector(1.f, 1.f, 1.f),
							iTeam
						});
						iTriggerCount++;
					}
				}
			}
		}

		for (const ParsedEntity_t& tEnt : vEntities)
		{
			if (EqualsIgnoreCase(tEnt.m_sClassname, "trigger_multiple"))
				continue;

			for (const auto& [sOutputName, sOutputValue] : tEnt.m_vOutputs)
			{
				std::string_view aFields[5] = {};
				if (!SplitOutput(sOutputValue, aFields, 5))
					continue;

				for (DoorMapData_t& tDoor : G::DoorStorage)
				{
					if (NamesMatch(aFields[0], tDoor.m_sTargetname))
						tDoor.m_bPredicted = false;
				}
			}
		}

		return true;
	}

	static bool BuildTriggerStorageFromEntityLump(std::string_view sEntityLump, int& iOutTriggers)
	{
		iOutTriggers = 0;
		std::vector<ParsedEntity_t> vEntities;
		size_t iOffset = 0;
		while (iOffset < sEntityLump.size())
		{
			SkipEntityWhitespace(sEntityLump, iOffset);
			if (iOffset >= sEntityLump.size())
				break;

			if (sEntityLump[iOffset] != '{')
			{
				iOffset++;
				continue;
			}

			iOffset++;
			ParsedEntity_t tEnt = {};
			bool bMalformed = false;
			while (iOffset < sEntityLump.size())
			{
				SkipEntityWhitespace(sEntityLump, iOffset);
				if (iOffset >= sEntityLump.size())
					break;
				if (sEntityLump[iOffset] == '}')
				{
					iOffset++;
					break;
				}

				std::string_view sKey, sValue;
				if (!ParseQuotedEntityTokenView(sEntityLump, iOffset, sKey)
					|| !ParseQuotedEntityTokenView(sEntityLump, iOffset, sValue))
				{
					bMalformed = true;
					break;
				}
				if (sKey.empty())
					continue;

				switch (FNV1A::Hash32(sKey))
				{
				case FNV1A::Hash32Const("classname"):       tEnt.m_sClassname = sValue; break;
				case FNV1A::Hash32Const("model"):           tEnt.m_sModel = sValue; break;
				case FNV1A::Hash32Const("parentname"):      tEnt.m_sParentname = sValue; break;
				case FNV1A::Hash32Const("origin"):          tEnt.m_sOrigin = sValue; break;
				case FNV1A::Hash32Const("pushdir"):         tEnt.m_sPushDir = sValue; break;
				case FNV1A::Hash32Const("impulse_dir"):     tEnt.m_sImpulseDir = sValue; break;
				case FNV1A::Hash32Const("launchDirection"): tEnt.m_sLaunchDir = sValue; break;
				case FNV1A::Hash32Const("angles"):          tEnt.m_sAngles = sValue; break;
				case FNV1A::Hash32Const("TeamNum"):         tEnt.m_sTeamNum = sValue; break;
				case FNV1A::Hash32Const("targetname"):      tEnt.m_sTargetname = UnescapeEntityToken(sValue); break;
				case FNV1A::Hash32Const("filtername"):      tEnt.m_sFilterName = sValue; break;
				case FNV1A::Hash32Const("movedir"):         tEnt.m_sMoveDir = sValue; break;
				case FNV1A::Hash32Const("speed"):           tEnt.m_sSpeed = sValue; break;
				case FNV1A::Hash32Const("lip"):             tEnt.m_sLip = sValue; break;
				case FNV1A::Hash32Const("wait"):            tEnt.m_sWait = sValue; break;
				case FNV1A::Hash32Const("spawnflags"):      tEnt.m_sSpawnflags = sValue; break;
				case FNV1A::Hash32Const("StartDisabled"):   tEnt.m_sStartDisabled = sValue; break;
				case FNV1A::Hash32Const("points"):          tEnt.m_sPoints = sValue; break;
				case FNV1A::Hash32Const("forceclosed"):     tEnt.m_sForceClosed = sValue; break;
				case FNV1A::Hash32Const("Negated"):         tEnt.m_sNegated = sValue; break;
				case FNV1A::Hash32Const("controlpoint"):    tEnt.m_sControlPoint = sValue; break;
				default: break;
				}

				if (sKey.size() > 1 && std::tolower(static_cast<unsigned char>(sKey[0])) == 'o'
					&& std::tolower(static_cast<unsigned char>(sKey[1])) == 'n' && tEnt.m_vOutputs.size() < 256)
					tEnt.m_vOutputs.emplace_back(sKey, sValue);
			}

			if (bMalformed)
				continue;
			AppendPasstimeGoalFromParsed(tEnt);
			iOutTriggers += AppendTriggerFromParsed(tEnt) ? 1 : 0;
			vEntities.push_back(std::move(tEnt));
		}

		if (!BuildDoorStorageFromEntities(vEntities))
			return false;
		return true;
	}

bool SDK::BuildTriggerGeometry(TriggerData_t& tTrigger)
{
	if (!tTrigger.m_pModel)
		return false;

	tTrigger.m_vBrushSurfaces.clear();
	tTrigger.m_vCenter = {};
	tTrigger.m_vMins = {};
	tTrigger.m_vMaxs = {};

	const int iNumSurfaces = tTrigger.m_pModel->brush.nummodelsurfaces;
	const bool bShouldRotate = !tTrigger.m_vRotate.IsZero();
	Vector vTopSurfCenter = { 0, -FLT_MAX, 0 }, vBottomSurfCenter = { 0, FLT_MAX, 0 };
	for (int i = 0; i < iNumSurfaces; i++)
	{
		const int iSurfaceIdx = tTrigger.m_pModel->brush.firstmodelsurface + i;
		auto pBrushData = tTrigger.m_pModel->brush.pShared;

		const auto uSurfacePtr = reinterpret_cast<uintptr_t>(pBrushData->surfaces2) + (iSurfaceIdx << 6);
		const auto uVertCount = *reinterpret_cast<char*>(uSurfacePtr + 3);
		const auto iFirstVertIndex = *reinterpret_cast<int*>(uSurfacePtr + 12);

		Vector vFirstPoint = pBrushData->vertexes[pBrushData->vertindices[iFirstVertIndex]].position, vLastPoint;
		std::vector<Vector> vPoints{ bShouldRotate ? vFirstPoint : tTrigger.m_vOrigin + vFirstPoint };

		Vector vSurfaceCenter = {};
		float flTotalArea = 0.0f;
		for (int j = 1; j < uVertCount; j++)
		{
			const auto iVertIdx = pBrushData->vertindices[iFirstVertIndex + j];
			const Vector vCurrentPoint = pBrushData->vertexes[iVertIdx].position;
			if (j > 1)
			{
				const Vector vNormal = (vCurrentPoint - vLastPoint).Cross(vCurrentPoint - vFirstPoint);
				const float flArea = vNormal.Length();
				flTotalArea += flArea;
				vSurfaceCenter += (vFirstPoint + vLastPoint + vCurrentPoint) * flArea / 3.0f;
			}
			Vector vFinal = vCurrentPoint;
			if (bShouldRotate)
				vFinal = Math::RotatePoint(vCurrentPoint, {}, tTrigger.m_vRotate);
			vPoints.push_back(tTrigger.m_vOrigin + vFinal);
			vLastPoint = vCurrentPoint;
		}

		if (flTotalArea)
			vSurfaceCenter /= flTotalArea;

		if (bShouldRotate)
		{
			auto& vFirstRotated = vPoints.front();
			vSurfaceCenter = Math::RotatePoint(vSurfaceCenter, {}, tTrigger.m_vRotate);
			vFirstRotated = tTrigger.m_vOrigin + Math::RotatePoint(vFirstRotated, {}, tTrigger.m_vRotate);
		}

		if (vBottomSurfCenter.y > vSurfaceCenter.y)
			vBottomSurfCenter = vSurfaceCenter;
		if (vTopSurfCenter.y < vSurfaceCenter.y)
			vTopSurfCenter = vSurfaceCenter;

		vSurfaceCenter += tTrigger.m_vOrigin;
		tTrigger.m_vBrushSurfaces.push_back(BrushSurface_t(vSurfaceCenter, vPoints));
	}

	if (!tTrigger.m_vBrushSurfaces.empty())
	{
		Vec3 vMins = tTrigger.m_vBrushSurfaces.front().m_vPoints.front();
		Vec3 vMaxs = vMins;
		for (const auto& tSurface : tTrigger.m_vBrushSurfaces)
		{
			for (const auto& vPoint : tSurface.m_vPoints)
			{
				vMins.x = std::min(vMins.x, vPoint.x), vMins.y = std::min(vMins.y, vPoint.y), vMins.z = std::min(vMins.z, vPoint.z);
				vMaxs.x = std::max(vMaxs.x, vPoint.x), vMaxs.y = std::max(vMaxs.y, vPoint.y), vMaxs.z = std::max(vMaxs.z, vPoint.z);
			}
		}
		tTrigger.m_vMins = vMins, tTrigger.m_vMaxs = vMaxs;
	}

	tTrigger.m_vCenter = tTrigger.m_vOrigin + Vector(vBottomSurfCenter.x, vBottomSurfCenter.y + (vTopSurfCenter.y - vBottomSurfCenter.y) / 2, vBottomSurfCenter.z);
	return !tTrigger.m_vBrushSurfaces.empty();
}

static uint32_t s_uMapEntityStorageGeneration = 0;
static uint32_t s_uParsedMapEntityStorageGeneration = 0;

void SDK::InvalidateMapEntityStorage()
{
	s_uMapEntityStorageGeneration++;
}

bool SDK::RefreshTriggerStorage(bool bForce)
{
	if (!I::EngineClient->IsInGame())
		return false;

	static Timer tRetryTimer = {};
	static std::string sLastMap = {};
	static std::string sLastParsedMap = {};
	const std::string sMapName = SDK::GetLevelName();
	if (sMapName != sLastMap)
	{
		sLastMap = sMapName;
		sLastParsedMap.clear();
		tRetryTimer.Update();
	}

	if (!bForce && !sLastParsedMap.empty() && sLastParsedMap == sMapName
		&& s_uParsedMapEntityStorageGeneration == s_uMapEntityStorageGeneration)
		return true;
	if (!bForce && !tRetryTimer.Run(1.0f))
		return false;

	const char* pszEntityString = I::EngineClient->GetMapEntitiesString();
	if (!pszEntityString || !*pszEntityString)
	{
		if (I::BSPData && I::BSPData->numentitychars > 0)
			pszEntityString = I::BSPData->map_entitystring.Get();
	}
	if (!pszEntityString || !*pszEntityString)
		return false;

	const auto vOldTriggers = G::TriggerStorage;
	const auto vOldPasstimeGoals = G::PasstimeGoalStorage;
	const auto vOldDoors = G::DoorStorage;
	const auto vOldDoorTriggers = G::DoorTriggerStorage;
	const auto vOldRespawnRooms = F::NavEngine.GetRespawnRooms();
	G::TriggerStorage.clear();
	G::PasstimeGoalStorage.clear();
	G::DoorStorage.clear();
	G::DoorTriggerStorage.clear();
	F::NavEngine.ClearRespawnRooms();

	int iTriggerCount = 0;
	if (!BuildTriggerStorageFromEntityLump(pszEntityString, iTriggerCount))
	{
		G::TriggerStorage = vOldTriggers;
		G::PasstimeGoalStorage = vOldPasstimeGoals;
		G::DoorStorage = vOldDoors;
		G::DoorTriggerStorage = vOldDoorTriggers;
		for (const auto& tRespawnRoom : vOldRespawnRooms)
			F::NavEngine.AddRespawnRoom(tRespawnRoom.m_iTeam, tRespawnRoom.tData);
		return false;
	}

	G::DoorStorageRevision++;
	sLastParsedMap = sMapName;
	s_uParsedMapEntityStorageGeneration = s_uMapEntityStorageGeneration;

	if (Vars::Debug::Logging.Value)
	{
		SDK::Output("TriggerStorage", std::format(
			"Refresh: map={} triggers={} respawn_rooms={} passtime_goals={}",
			sMapName, iTriggerCount, F::NavEngine.GetRespawnRooms().size(), G::PasstimeGoalStorage.size()).c_str(),
			{ 180, 220, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
	}

	return true;
}

int SDK::GetPasstimeGoalMapTeam(const Vec3& vOrigin, std::string* pTargetname)
{
	constexpr float flMaxMatchDistSqr = 256.0f * 256.0f;

	const PasstimeMapGoalData_t* pBestGoal = nullptr;
	float flBestDist = flMaxMatchDistSqr;
	for (const auto& tGoal : G::PasstimeGoalStorage)
	{
		const float flDist = tGoal.m_vOrigin.DistToSqr(vOrigin);
		if (!pBestGoal || flDist < flBestDist)
		{
			pBestGoal = &tGoal;
			flBestDist = flDist;
		}
	}

	if (!pBestGoal)
		return TEAM_UNASSIGNED;

	if (pTargetname)
		*pTargetname = pBestGoal->m_sTargetname;
	return pBestGoal->m_iTeam;
}

bool SDK::IsInPasstimeNoBallZone(const Vec3& vOrigin)
{
	for (const auto& tTrigger : G::TriggerStorage)
	{
		if (tTrigger.m_eType != TriggerTypeEnum::NoBallZone || !tTrigger.m_pModel)
			continue;

		if (vOrigin.x < tTrigger.m_vMins.x || vOrigin.x > tTrigger.m_vMaxs.x
			|| vOrigin.y < tTrigger.m_vMins.y || vOrigin.y > tTrigger.m_vMaxs.y
			|| vOrigin.z < tTrigger.m_vMins.z || vOrigin.z > tTrigger.m_vMaxs.z)
			continue;

		if (tTrigger.PointIsWithin(vOrigin))
			return true;
	}
	return false;
}

std::string SDK::GetLevelName()
{
	const std::string name = I::EngineClient->GetLevelName();
	if (name.empty())
		return "None";

	const char* data = name.data();
	const size_t length = name.length();
	size_t slash = 0;
	size_t bsp = length;

	for (size_t i = length - 1; i != std::string::npos; --i)
	{
		if (data[i] == '/')
		{
			slash = i + 1;
			break;
		}
		if (data[i] == '.')
			bsp = i;
	}

	return { data + slash, bsp - slash };
}

bool TriggerData_t::PointIsWithin(Vec3 vPoint) const
{
	CGameTrace trace;
	Ray_t ray;
	ray.Init(vPoint, vPoint);
	S::CM_TransformedBoxTrace.Call<void>(ray, m_pModel->brush.firstnode, 0x0, m_vOrigin, m_vRotate, &trace);
	return trace.startsolid;
}
