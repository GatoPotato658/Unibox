#pragma once
#include "nav.h"
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <limits>
#include <unordered_map>

class CNavFile
{
public:
	CNavFile() {}

	// Intended to use with engine->GetLevelName() or mapname from server_spawn GameEvent
	// Change it if you get the nav file from elsewhere
	explicit CNavFile(const char* szLevelname)
	{
		if (!szLevelname)
			return;

		m_bOK = false;
		m_vPlaces.clear();
		m_vAreas.clear();
		m_sMapName = szLevelname;
		std::ifstream file(m_sMapName, std::ios::binary);
		if (!file.is_open())
			return;

		std::error_code tError;
		const uintmax_t uFileSize = std::filesystem::file_size(m_sMapName, tError);
		if (tError || uFileSize < 16 || uFileSize > 512ull * 1024ull * 1024ull)
			return;

		auto CanRead = [&](uintmax_t uSize)
		{
			const std::streampos tPosition = file.tellg();
			return tPosition >= 0 && static_cast<uintmax_t>(tPosition) <= uFileSize && uSize <= uFileSize - static_cast<uintmax_t>(tPosition);
		};
		auto Read = [&](auto& tValue)
		{
			if (!CanRead(sizeof(tValue)))
				return false;
			file.read(reinterpret_cast<char*>(&tValue), sizeof(tValue));
			return static_cast<bool>(file);
		};
		auto ReadBytes = [&](char* pData, size_t uSize)
		{
			if (!CanRead(uSize))
				return false;
			file.read(pData, static_cast<std::streamsize>(uSize));
			return static_cast<bool>(file);
		};
		auto HasItems = [&](uint64_t uCount, uint64_t uMinimumSize)
		{
			const std::streampos tPosition = file.tellg();
			if (tPosition < 0 || static_cast<uintmax_t>(tPosition) > uFileSize || !uMinimumSize)
				return false;
			return uCount <= (uFileSize - static_cast<uintmax_t>(tPosition)) / uMinimumSize;
		};

		uint32_t uMagic = 0;
		if (!Read(uMagic))
			return;
		if (uMagic != 0xFEEDFACE)
			return;

		uint32_t uVersion = 0;
		if (!Read(uVersion))
			return;
		if (uVersion < 16) // 16 is latest for TF2
			return;

		uint32_t uSubVersion = 0;
		if (!Read(uSubVersion))
			return;
		if (uSubVersion != 2) // 2 for TF2
			return;

		uint32_t uBspSize = 0;
		unsigned char bAnalyzed = 0;
		if (!Read(uBspSize) || !Read(bAnalyzed))
			return;

		uint16_t uPlacesCount = 0;
		if (!Read(uPlacesCount) || !HasItems(uPlacesCount, sizeof(uint16_t)))
			return;
		std::vector<NavPlace_t> vPlaces;
		vPlaces.reserve(uPlacesCount);
		for (uint16_t i = 0; i < uPlacesCount; ++i)
		{
			NavPlace_t tPlace{};
			if (!Read(tPlace.m_uLen) || tPlace.m_uLen > sizeof(tPlace.m_sName) || !ReadBytes(tPlace.m_sName, tPlace.m_uLen))
				return;

			vPlaces.push_back(tPlace);
		}

		unsigned char bHasUnnamedAreas = 0;
		if (!Read(bHasUnnamedAreas))
			return;

		uint32_t uAreaCount = 0;
		if (!Read(uAreaCount) || !HasItems(uAreaCount, 107))
			return;
		std::vector<CNavArea> vAreas;
		vAreas.reserve(uAreaCount);
		for (uint32_t i = 0; i < uAreaCount; ++i)
		{
			CNavArea tArea{};
			if (!Read(tArea.m_uId) || !Read(tArea.m_iAttributeFlags) || !Read(tArea.m_vNwCorner) || !Read(tArea.m_vSeCorner) ||
				!Read(tArea.m_flNeZ) || !Read(tArea.m_flSwZ))
				return;

			tArea.m_vCenter[0] = (tArea.m_vNwCorner[0] + tArea.m_vSeCorner[0]) / 2.0f;
			tArea.m_vCenter[1] = (tArea.m_vNwCorner[1] + tArea.m_vSeCorner[1]) / 2.0f;

			if ((tArea.m_vSeCorner.x - tArea.m_vNwCorner.x) > 0.0f &&
				(tArea.m_vSeCorner.y - tArea.m_vNwCorner.y) > 0.0f)
			{
				tArea.m_flInvDxCorners = 1.0f / (tArea.m_vSeCorner.x - tArea.m_vNwCorner.x);
				tArea.m_flInvDyCorners = 1.0f / (tArea.m_vSeCorner.y - tArea.m_vNwCorner.y);
			}
			else
				tArea.m_flInvDxCorners = tArea.m_flInvDyCorners = 0.0f;

			tArea.m_vCenter[2] = tArea.GetZ(tArea.m_vCenter.x, tArea.m_vCenter.y);
			tArea.m_flMinZ = std::min({ tArea.m_vNwCorner.z, tArea.m_flNeZ, tArea.m_flSwZ, tArea.m_vSeCorner.z }) - 18.f;
			tArea.m_flMaxZ = std::max({ tArea.m_vNwCorner.z, tArea.m_flNeZ, tArea.m_flSwZ, tArea.m_vSeCorner.z }) + 18.f;
			tArea.m_uConnectionCount = 0;

			for (int iDir = 0; iDir < 4; iDir++)
			{
				uint32_t uDirConnectionCount = 0;
				if (!Read(uDirConnectionCount) || !HasItems(uDirConnectionCount, sizeof(uint32_t)))
					return;
				for (uint32_t j = 0; j < uDirConnectionCount; j++)
				{
					NavConnect_t tConnect{};
					if (!Read(tConnect.m_uId))
						return;

					// Connection to the same area?
					if (tConnect.m_uId == tArea.m_uId)
						continue;

					// Note: If connection directions matter to you, uncomment
					// this
					tArea.m_vConnections /*[iDir]*/.push_back(tConnect);
					tArea.m_vConnectionsDir[iDir].push_back(tConnect);
					tArea.m_uConnectionCount++;
				}
			}

			if (!Read(tArea.m_uHidingSpotCount) || !HasItems(tArea.m_uHidingSpotCount, sizeof(uint32_t) + sizeof(Vector) + sizeof(unsigned char)))
				return;
			for (uint8_t j = 0; j < tArea.m_uHidingSpotCount; j++)
			{
				CHidingSpot tSpot{};
				if (!Read(tSpot.m_uId) || !Read(tSpot.m_vPos) || !Read(tSpot.m_fFlags))
					return;

				tArea.m_vHidingSpots.push_back(tSpot);
			}

			if (!Read(tArea.m_uEncounterSpotCount) || !HasItems(tArea.m_uEncounterSpotCount, 11))
				return;

			for (uint32_t j = 0; j < tArea.m_uEncounterSpotCount; j++)
			{
				SpotEncounter_t tSpot{};
				unsigned char iFromDir = 0, iToDir = 0;
				if (!Read(tSpot.m_tFrom.m_uId) || !Read(iFromDir) || !Read(tSpot.m_tTo.m_uId) ||
					!Read(iToDir) || !Read(tSpot.m_uSpotCount) || !HasItems(tSpot.m_uSpotCount, sizeof(uint32_t) + sizeof(unsigned char)))
					return;
				tSpot.m_iFromDir = iFromDir;
				tSpot.m_iToDir = iToDir;

			for (uint8_t s = 0; s < tSpot.m_uSpotCount; ++s)
			{
				SpotOrder_t tOrder{};
				unsigned char uT = 0;
				if (!Read(tOrder.m_uId) || !Read(uT))
					return;
				tOrder.flT = uT;
				tSpot.m_vSpots.push_back(tOrder);
			}

				tArea.m_vSpotEncounters.push_back(tSpot);
			}

			if (!Read(tArea.m_uIndexType))
				return;

			// TF2 does not use ladders either
			for (int iDir = 0; iDir < 2; iDir++)
			{
				if (!Read(tArea.m_uLadderCount) || !HasItems(tArea.m_uLadderCount, sizeof(uint32_t)))
					return;
				for (uint32_t j = 0; j < tArea.m_uLadderCount; j++)
				{
					uint32_t uLadder = 0;
					if (!Read(uLadder))
						return;
					tArea.m_vLadders[iDir].push_back(uLadder);
				}
			}

			for (float& j : tArea.m_flEarliestOccupyTime)
				if (!Read(j))
					return;

			for (float& j : tArea.m_flLightIntensity)
				if (!Read(j))
					return;

			if (!Read(tArea.m_uVisibleAreaCount) || !HasItems(tArea.m_uVisibleAreaCount, sizeof(uint32_t) + sizeof(unsigned char)))
				return;
			for (uint32_t j = 0; j < tArea.m_uVisibleAreaCount; ++j)
			{
				AreaBindInfo_t tInfo{};
				if (!Read(tInfo.m_uId) || !Read(tInfo.m_uAttributes))
					return;

				tArea.m_vPotentiallyVisibleAreas.push_back(tInfo);
			}

			if (!Read(tArea.m_uInheritVisibilityFrom) || !Read(tArea.m_iTFAttributeFlags))
				return;

			vAreas.push_back(std::move(tArea));
		}

		m_uBspSize = uBspSize;
		m_bAnalyzed = bAnalyzed;
		m_bHasUnnamedAreas = bHasUnnamedAreas;
		m_vPlaces = std::move(vPlaces);
		m_vAreas = std::move(vAreas);

		std::unordered_map<uint32_t, CNavArea*> mAreasById;
		mAreasById.reserve(m_vAreas.size());
		for (auto& tArea : m_vAreas)
			mAreasById.emplace(tArea.m_uId, &tArea);

		for (auto& tArea : m_vAreas)
		{
			for (auto& connection : tArea.m_vConnections)
				if (const auto it = mAreasById.find(connection.m_uId); it != mAreasById.end())
					connection.m_pArea = it->second;

			for (auto& bindinfo : tArea.m_vPotentiallyVisibleAreas)
				if (const auto it = mAreasById.find(bindinfo.m_uId); it != mAreasById.end())
					bindinfo.m_pArea = it->second;
		}
		m_bOK = true;
	}


	// Im not sure why but it takes away last 4 bytes of the nav file
	// Might be related to the fact that im not using CUtlBuffer for saving this
	bool Write(const char* szFilename = nullptr)
	{
		if (!m_bOK)
			return false;

		std::filesystem::path tFilePath;
		if (szFilename)
			tFilePath = std::filesystem::path(szFilename);
		else
		{
			const std::string sLevelName = SDK::GetLevelName();
			if (sLevelName.empty() || sLevelName == "None")
				return false;
			tFilePath = std::filesystem::current_path() / "unibox" / "Nav" / (sLevelName + ".nav");
		}
		if (tFilePath.empty())
			return false;

		if (m_vPlaces.size() > (std::numeric_limits<uint16_t>::max)() || m_vAreas.size() > (std::numeric_limits<uint32_t>::max)())
			return false;
		for (const auto& tPlace : m_vPlaces)
			if (tPlace.m_uLen > sizeof(tPlace.m_sName))
				return false;
		for (const auto& tArea : m_vAreas)
		{
			for (const auto& vConnections : tArea.m_vConnectionsDir)
				if (vConnections.size() > (std::numeric_limits<uint32_t>::max)())
					return false;
			if (tArea.m_vHidingSpots.size() > (std::numeric_limits<uint8_t>::max)() ||
				tArea.m_vSpotEncounters.size() > (std::numeric_limits<uint32_t>::max)() ||
				tArea.m_vPotentiallyVisibleAreas.size() > (std::numeric_limits<uint32_t>::max)())
				return false;
			for (const auto& tEncounter : tArea.m_vSpotEncounters)
				if (tEncounter.m_iFromDir < 0 || tEncounter.m_iFromDir > (std::numeric_limits<uint8_t>::max)() ||
					tEncounter.m_iToDir < 0 || tEncounter.m_iToDir > (std::numeric_limits<uint8_t>::max)() ||
					tEncounter.m_vSpots.size() > (std::numeric_limits<uint8_t>::max)())
					return false;
			for (const auto& vLadders : tArea.m_vLadders)
				if (vLadders.size() > (std::numeric_limits<uint32_t>::max)())
					return false;
		}

		std::error_code tError;
		if (!tFilePath.parent_path().empty())
			std::filesystem::create_directories(tFilePath.parent_path(), tError);
		if (tError)
			return false;

		std::filesystem::path tTempPath = tFilePath;
		tTempPath += ".tmp";
		std::ofstream file(tTempPath, std::ios::binary | std::ios::trunc);
		if (!file.is_open())
		{
			SDK::Output("CNavFile::Write", std::format("Couldn't open file {}", tFilePath.string()).c_str(), { 200, 150, 150 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			return false;
		}

		uint32_t uMagic = 0xFEEDFACE;
		uint32_t uVersion = 16;
		uint32_t uSubVersion = 2;
		file.write((char*)&uMagic, sizeof(uint32_t));
		file.write((char*)&uVersion, sizeof(uint32_t));
		file.write((char*)&uSubVersion, sizeof(uint32_t));
		file.write((char*)&m_uBspSize, sizeof(uint32_t));
		file.write((char*)&m_bAnalyzed, sizeof(unsigned char));

		uint16_t uPlacesCount = static_cast<uint16_t>(m_vPlaces.size());
		file.write((char*)&uPlacesCount, sizeof(uint16_t));
		for (auto& tPlace : m_vPlaces)
		{
			file.write((char*)&tPlace.m_uLen, sizeof(uint16_t));
			file.write((char*)&tPlace.m_sName, tPlace.m_uLen);
		}

		file.write((char*)&m_bHasUnnamedAreas, sizeof(unsigned char));

		uint32_t uAreaCount = static_cast<uint32_t>(m_vAreas.size());
		file.write((char*)&uAreaCount, sizeof(uint32_t));
		for (auto& tArea : m_vAreas)
		{
			file.write((char*)&tArea.m_uId, sizeof(uint32_t));
			file.write((char*)&tArea.m_iAttributeFlags, sizeof(uint32_t));
			file.write((char*)&tArea.m_vNwCorner, sizeof(Vector));
			file.write((char*)&tArea.m_vSeCorner, sizeof(Vector));
			file.write((char*)&tArea.m_flNeZ, sizeof(float));
			file.write((char*)&tArea.m_flSwZ, sizeof(float));

			for (int iDir = 0; iDir < 4; iDir++)
			{
				uint32_t uConnectionCount = static_cast<uint32_t>(tArea.m_vConnectionsDir[iDir].size());
				file.write((char*)&uConnectionCount, sizeof(uint32_t));
				for (auto& tConnect : tArea.m_vConnectionsDir[iDir])
					file.write((char*)&tConnect.m_uId, sizeof(uint32_t));
			}

			uint8_t uHidingSpotCount = static_cast<uint8_t>(tArea.m_vHidingSpots.size());
			file.write((char*)&uHidingSpotCount, sizeof(uint8_t));
			for (auto& tHidingSpot : tArea.m_vHidingSpots)
			{
				file.write((char*)&tHidingSpot.m_uId, sizeof(uint32_t));
				file.write((char*)&tHidingSpot.m_vPos, sizeof(Vector));
				file.write((char*)&tHidingSpot.m_fFlags, sizeof(unsigned char));
			}

			uint32_t uEncounterSpotCount = static_cast<uint32_t>(tArea.m_vSpotEncounters.size());
			file.write((char*)&uEncounterSpotCount, sizeof(uint32_t));
			for (auto& tEncounterSpot : tArea.m_vSpotEncounters)
			{
				file.write((char*)&tEncounterSpot.m_tFrom.m_uId, sizeof(uint32_t));
				file.write((char*)&tEncounterSpot.m_iFromDir, sizeof(unsigned char));
				file.write((char*)&tEncounterSpot.m_tTo.m_uId, sizeof(uint32_t));
				file.write((char*)&tEncounterSpot.m_iToDir, sizeof(unsigned char));

				uint8_t uSpotCount = static_cast<uint8_t>(tEncounterSpot.m_vSpots.size());
				file.write((char*)&uSpotCount, sizeof(unsigned char));
			for (auto& tOrder : tEncounterSpot.m_vSpots)
			{
				file.write((char*)&tOrder.m_uId, sizeof(uint32_t));
				const unsigned char uT = static_cast<unsigned char>(tOrder.flT);
				file.write((char*)&uT, sizeof(unsigned char));
			}
			}

			file.write((char*)&tArea.m_uIndexType, sizeof(uint16_t));

			for (int iDir = 0; iDir < 2; iDir++)
			{
				uint32_t uLadderCount = static_cast<uint32_t>(tArea.m_vLadders[iDir].size());
				file.write((char*)&uLadderCount, sizeof(uint32_t));
				for (auto& uLadder : tArea.m_vLadders[iDir])
					file.write((char*)&uLadder, sizeof(uint32_t));
			}

			for (float& j : tArea.m_flEarliestOccupyTime)
				file.write((char*)&j, sizeof(float));

			for (float& j : tArea.m_flLightIntensity)
				file.write((char*)&j, sizeof(float));

			uint32_t uPotentiallyVisibleCount = static_cast<uint32_t>(tArea.m_vPotentiallyVisibleAreas.size());
			file.write((char*)&uPotentiallyVisibleCount, sizeof(uint32_t));
			for (auto& tVisibleArea : tArea.m_vPotentiallyVisibleAreas)
			{
				file.write((char*)&tVisibleArea.m_uId, sizeof(uint32_t));
				file.write((char*)&tVisibleArea.m_uAttributes, sizeof(unsigned char));
			}

			file.write((char*)&tArea.m_uInheritVisibilityFrom, sizeof(uint32_t));
			file.write((char*)&tArea.m_iTFAttributeFlags, sizeof(uint32_t));
		}

		file.flush();
		if (!file)
		{
			file.close();
			std::filesystem::remove(tTempPath, tError);
			return false;
		}
		file.close();
		if (file.fail())
		{
			std::filesystem::remove(tTempPath, tError);
			return false;
		}

		std::filesystem::rename(tTempPath, tFilePath, tError);
		if (tError)
		{
			std::filesystem::remove(tTempPath, tError);
			return false;
		}
		return true;
	}

	std::vector<NavPlace_t> m_vPlaces;
	std::vector<CNavArea> m_vAreas;
	std::string m_sMapName;

	unsigned int m_uBspSize = 0;
	bool m_bHasUnnamedAreas{};
	bool m_bAnalyzed{};
	bool m_bOK = false;
};
