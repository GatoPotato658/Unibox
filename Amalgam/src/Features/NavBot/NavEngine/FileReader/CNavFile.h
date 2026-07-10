#pragma once
#include "nav.h"
#include <fstream>
#include <filesystem>

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

		m_sMapName.append(szLevelname);
		std::ifstream file(m_sMapName, std::ios::binary);
		if (!file.is_open())
		{
			//.nav file does not exist
			return;
		}

		uint32_t uMagic;
		file.read((char*)&uMagic, sizeof(uint32_t));
		if (uMagic != 0xFEEDFACE)
		{
			// Wrong magic number
			return;
		}

		uint32_t uVersion;
		file.read((char*)&uVersion, sizeof(uint32_t));
		if (uVersion < 16) // 16 is latest for TF2
		{
			// Version is too old
			return;
		}

		uint32_t uSubVersion;
		file.read((char*)&uSubVersion, sizeof(uint32_t));
		if (uSubVersion != 2) // 2 for TF2
		{
			// Not TF2 nav file
			return;
		}

		// We do not really need to check the size
		file.read((char*)&m_uBspSize, sizeof(uint32_t));
		file.read((char*)&m_bAnalyzed, sizeof(unsigned char));

		// TF2 does not use places, but in case they exist
		unsigned short uPlacesCount;
		file.read((char*)&uPlacesCount, sizeof(uint16_t));
		for (int i = 0; i < uPlacesCount; ++i)
		{
			NavPlace_t tPlace;
			file.read((char*)&tPlace.m_uLen, sizeof(uint16_t));
			file.read((char*)&tPlace.m_sName, tPlace.m_uLen);

			m_vPlaces.push_back(tPlace);
		}

		file.read((char*)&m_bHasUnnamedAreas, sizeof(unsigned char));

		unsigned int uAreaCount;
		file.read((char*)&uAreaCount, sizeof(uint32_t));
		for (size_t i = 0; i < uAreaCount; ++i)
		{
			CNavArea tArea;
			file.read((char*)&tArea.m_uId, sizeof(uint32_t));
			file.read((char*)&tArea.m_iAttributeFlags, sizeof(uint32_t));
			file.read((char*)&tArea.m_vNwCorner, sizeof(Vector));
			file.read((char*)&tArea.m_vSeCorner, sizeof(Vector));
			file.read((char*)&tArea.m_flNeZ, sizeof(float));
			file.read((char*)&tArea.m_flSwZ, sizeof(float));

			tArea.m_vCenter[0] = (tArea.m_vNwCorner[0] + tArea.m_vSeCorner[0]) / 2.0f;
			tArea.m_vCenter[1] = (tArea.m_vNwCorner[1] + tArea.m_vSeCorner[1]) / 2.0f;
			tArea.m_vCenter[2] = (tArea.m_vNwCorner[2] + tArea.m_vSeCorner[2]) / 2.0f;

			if ((tArea.m_vSeCorner.x - tArea.m_vNwCorner.x) > 0.0f &&
				(tArea.m_vSeCorner.y - tArea.m_vNwCorner.y) > 0.0f)
			{
				tArea.m_flInvDxCorners = 1.0f / (tArea.m_vSeCorner.x - tArea.m_vNwCorner.x);
				tArea.m_flInvDyCorners = 1.0f / (tArea.m_vSeCorner.y - tArea.m_vNwCorner.y);
			}
			else
				tArea.m_flInvDxCorners = tArea.m_flInvDyCorners = 0.0f;

			// Change the tolerance if you wish
			tArea.m_flMinZ = std::min(tArea.m_vSeCorner.z, tArea.m_vNwCorner.z) - 18.f;
			tArea.m_flMaxZ = std::max(tArea.m_vSeCorner.z, tArea.m_vNwCorner.z) + 18.f;
			tArea.m_uConnectionCount = 0;

			for (int iDir = 0; iDir < 4; iDir++)
			{
				uint32_t uDirConnectionCount = 0;
				file.read((char*)&uDirConnectionCount, sizeof(uint32_t));
				for (size_t j = 0; j < uDirConnectionCount; j++)
				{
					NavConnect_t tConnect;
					file.read((char*)&tConnect.m_uId, sizeof(uint32_t));

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

			file.read((char*)&tArea.m_uHidingSpotCount, sizeof(uint8_t));
			for (size_t j = 0; j < tArea.m_uHidingSpotCount; j++)
			{
				CHidingSpot tSpot;
				file.read((char*)&tSpot.m_uId, sizeof(uint32_t));
				file.read((char*)&tSpot.m_vPos, sizeof(Vector));
				file.read((char*)&tSpot.m_fFlags, sizeof(unsigned char));

				tArea.m_vHidingSpots.push_back(tSpot);
			}

			file.read((char*)&tArea.m_uEncounterSpotCount, sizeof(uint32_t));

			for (size_t j = 0; j < tArea.m_uEncounterSpotCount; j++)
			{
				SpotEncounter_t tSpot;
				file.read((char*)&tSpot.m_tFrom.m_uId, sizeof(uint32_t));
				file.read((char*)&tSpot.m_iFromDir, sizeof(unsigned char));
				file.read((char*)&tSpot.m_tTo.m_uId, sizeof(uint32_t));
				file.read((char*)&tSpot.m_iToDir, sizeof(unsigned char));
				file.read((char*)&tSpot.m_uSpotCount, sizeof(unsigned char));

				for (int s = 0; s < tSpot.m_uSpotCount; ++s)
				{
					SpotOrder_t tOrder;
					file.read((char*)&tOrder.m_uId, sizeof(uint32_t));
					file.read((char*)&tOrder.flT, sizeof(unsigned char));
					tSpot.m_vSpots.push_back(tOrder);
				}

				tArea.m_vSpotEncounters.push_back(tSpot);
			}

			file.read((char*)&tArea.m_uIndexType, sizeof(uint16_t));

			// TF2 does not use ladders either
			for (int iDir = 0; iDir < 2; iDir++)
			{
				file.read((char*)&tArea.m_uLadderCount, sizeof(uint32_t));
				for (size_t j = 0; j < tArea.m_uLadderCount; j++)
				{
					int iTemp;
					file.read((char*)&iTemp, sizeof(uint32_t));
					tArea.m_vLadders[iDir].push_back(iTemp);
				}
			}

			for (float& j : tArea.m_flEarliestOccupyTime)
				file.read((char*)&j, sizeof(float));

			for (float& j : tArea.m_flLightIntensity)
				file.read((char*)&j, sizeof(float));

			file.read((char*)&tArea.m_uVisibleAreaCount, sizeof(uint32_t));
			for (size_t j = 0; j < tArea.m_uVisibleAreaCount; ++j)
			{
				AreaBindInfo_t tInfo;
				file.read((char*)&tInfo.m_uId, sizeof(uint32_t));
				file.read((char*)&tInfo.m_uAttributes, sizeof(unsigned char));

				tArea.m_vPotentiallyVisibleAreas.push_back(tInfo);
			}

			file.read((char*)&tArea.m_uInheritVisibilityFrom, sizeof(uint32_t));

			// TF2 Specific area flags
			file.read((char*)&tArea.m_iTFAttributeFlags, sizeof(uint32_t));

			m_vAreas.push_back(tArea);
		}

		file.close();

		// Fill connection for every area with their area ptrs instead of IDs
		// This will come in handy in path finding

		for (auto& tArea : m_vAreas)
		{
			for (auto& connection : tArea.m_vConnections)
				for (auto& connected_area : m_vAreas)
					if (connection.m_uId == connected_area.m_uId)
						connection.m_pArea = &connected_area;

			// Fill potentially visible areas as well
			for (auto& bindinfo : tArea.m_vPotentiallyVisibleAreas)
				for (auto& boundarea : m_vAreas)
					if (bindinfo.m_uId == boundarea.m_uId)
						bindinfo.m_pArea = &boundarea;

		}
		m_bOK = true;
	}


	// Im not sure why but it takes away last 4 bytes of the nav file
	// Might be related to the fact that im not using CUtlBuffer for saving this
	bool Write(const char* szFilename = nullptr)
	{
		std::string sFilePath{ szFilename ? szFilename : std::filesystem::current_path().string() + "\\unibox\\Nav\\" + SDK::GetLevelName() + ".nav" };
		std::ofstream file(sFilePath, std::ios::binary | std::ios::ate);
		if (!file.is_open())
		{
			SDK::Output("CNavFile::Write", std::format("Couldn't open file {}", sFilePath).c_str(), { 200, 150, 150 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
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

		size_t uPlacesCount = m_vPlaces.size();
		file.write((char*)&uPlacesCount, sizeof(uint16_t));
		for (auto& tPlace : m_vPlaces)
		{
			file.write((char*)&tPlace.m_uLen, sizeof(uint16_t));
			file.write((char*)&tPlace.m_sName, tPlace.m_uLen);
		}

		file.write((char*)&m_bHasUnnamedAreas, sizeof(unsigned char));

		size_t uAreaCount = m_vAreas.size();
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
				size_t uConnectionCount = tArea.m_vConnectionsDir[iDir].size();
				file.write((char*)&uConnectionCount, sizeof(uint32_t));
				for (auto& tConnect : tArea.m_vConnectionsDir[iDir])
					file.write((char*)&tConnect.m_uId, sizeof(uint32_t));
			}

			size_t uHidingSpotCount = tArea.m_vHidingSpots.size();
			file.write((char*)&uHidingSpotCount, sizeof(uint8_t));
			for (auto& tHidingSpot : tArea.m_vHidingSpots)
			{
				file.write((char*)&tHidingSpot.m_uId, sizeof(uint32_t));
				file.write((char*)&tHidingSpot.m_vPos, sizeof(Vector));
				file.write((char*)&tHidingSpot.m_fFlags, sizeof(unsigned char));
			}

			size_t uEncounterSpotCount = tArea.m_vSpotEncounters.size();
			file.write((char*)&uEncounterSpotCount, sizeof(uint32_t));
			for (auto& tEncounterSpot : tArea.m_vSpotEncounters)
			{
				file.write((char*)&tEncounterSpot.m_tFrom.m_uId, sizeof(uint32_t));
				file.write((char*)&tEncounterSpot.m_iFromDir, sizeof(unsigned char));
				file.write((char*)&tEncounterSpot.m_tTo.m_uId, sizeof(uint32_t));
				file.write((char*)&tEncounterSpot.m_iToDir, sizeof(unsigned char));

				size_t uSpotCount = tEncounterSpot.m_vSpots.size();
				file.write((char*)&uSpotCount, sizeof(unsigned char));
				for (auto& tOrder : tEncounterSpot.m_vSpots)
				{
					file.write((char*)&tOrder.m_uId, sizeof(uint32_t));
					file.write((char*)&tOrder.flT, sizeof(unsigned char));
				}
			}

			file.write((char*)&tArea.m_uIndexType, sizeof(uint16_t));

			for (int iDir = 0; iDir < 2; iDir++)
			{
				size_t uLadderCount = tArea.m_vLadders[iDir].size();
				file.write((char*)&uLadderCount, sizeof(uint32_t));
				for (auto& uLadder : tArea.m_vLadders[iDir])
					file.write((char*)&uLadder, sizeof(uint32_t));
			}

			for (float& j : tArea.m_flEarliestOccupyTime)
				file.write((char*)&j, sizeof(float));

			for (float& j : tArea.m_flLightIntensity)
				file.write((char*)&j, sizeof(float));

			size_t uPotentiallyVisibleCount = tArea.m_vPotentiallyVisibleAreas.size();
			file.write((char*)&uPotentiallyVisibleCount, sizeof(uint32_t));
			for (auto& tVisibleArea : tArea.m_vPotentiallyVisibleAreas)
			{
				file.write((char*)&tVisibleArea.m_uId, sizeof(uint32_t));
				file.write((char*)&tVisibleArea.m_uAttributes, sizeof(unsigned char));
			}

			file.write((char*)&tArea.m_uInheritVisibilityFrom, sizeof(uint32_t));
			file.write((char*)&tArea.m_iTFAttributeFlags, sizeof(uint32_t));
		}

		file.close();
		return true;
	}

	std::vector<NavPlace_t> m_vPlaces;
	std::vector<CNavArea> m_vAreas;
	std::string m_sMapName;

	unsigned int m_uBspSize;
	bool m_bHasUnnamedAreas{};
	bool m_bAnalyzed{};
	bool m_bOK = false;
};