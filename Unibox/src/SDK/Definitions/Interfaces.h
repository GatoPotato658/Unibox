#pragma once

#include "../../Utils/Macros/Macros.h"

#include "Interfaces/IMemAlloc.h"

#include "Interfaces/IFileSystem.h"
MAKE_INTERFACE_VERSION(IFileSystem, FileSystem, "filesystem_stdio.dll", "VFileSystem022");

#include "Interfaces/IVEngineClient.h"
MAKE_INTERFACE_VERSION(IVEngineClient, EngineClient, "engine.dll", "VEngineClient014");

#include "Interfaces/CClientModeShared.h"
#include "Interfaces/CClientState.h"
#include "Interfaces/CCollisionBSPData.h"
#include "Interfaces/CDispCollTree.h"
#include "Interfaces/CGlobalVarsBase.h"
#include "Interfaces/CHLClient.h"
#include "Interfaces/CHLTVCamera.h"
#include "Interfaces/CMDLCache.h"
#include "Interfaces/CStaticPropMgr.h"
#include "Interfaces/CTFGameRules.h"
#include "Interfaces/CTFGCClientSystem.h"
#include "Interfaces/CThirdPersonManager.h"
#include "Interfaces/CTFPartyClient.h"
#include "Interfaces/IClientEntityList.h"
#include "Interfaces/ICVar.h"
#include "Interfaces/IEngineTrace.h"
#include "Interfaces/IEngineVGui.h"
#include "Interfaces/IGameEvents.h"
#include "Interfaces/IGameMovement.h"
#include "Interfaces/IInput.h"
#include "Interfaces/IInputSystem.h"
#include "Interfaces/IKeyValuesSystem.h"
#include "Interfaces/ILocalize.h"
#include "Interfaces/IMaterialSystem.h"
#include "Interfaces/IMatSystemSurface.h"
#include "Interfaces/IMoveHelper.h"
#include "Interfaces/IPanel.h"
#include "Interfaces/ISpatialPartition.h"
#include "Interfaces/IStudioRender.h"
#include "Interfaces/IUniformRandomStream.h"
#include "Interfaces/IViewRender.h"
#include "Interfaces/IVModelInfo.h"
#include "Interfaces/IVModelRender.h"
#include "Interfaces/IVRenderView.h"
#include "Interfaces/Prediction.h"
#include "Interfaces/SteamInterfaces.h"
#include "Interfaces/ViewRenderBeams.h"
#include "Interfaces/VPhysics.h"
#include "Interfaces/CServerTools.h"
#include "Interfaces/CTFInventoryManager.h"
#include "Interfaces/CModelLoader.h"
#include "Interfaces/IDemoPlayer.h"
#include "Interfaces/IDemoRecorder.h"
#include "Interfaces/WorldBrushData.h"

MAKE_INTERFACE_VERSION(IBaseClientDLL, Client, "client.dll", "VClient017");
MAKE_INTERFACE_VERSION(CMDLCache, MDLCache, "datacache.dll", "MDLCache004");
MAKE_INTERFACE_VERSION(CServerTools, ServerTools, "server.dll", "VSERVERTOOLS003");
MAKE_INTERFACE_VERSION(IClientEntityList, ClientEntityList, "client.dll", "VClientEntityList003");
MAKE_INTERFACE_VERSION(ICvar, CVar, "vstdlib.dll", "VEngineCvar004");
MAKE_INTERFACE_VERSION(CEngineTrace, EngineTrace, "engine.dll", "EngineTraceClient003");
MAKE_INTERFACE_VERSION(IEngineVGui, EngineVGui, "engine.dll", "VEngineVGui002");
MAKE_INTERFACE_VERSION(IGameEventManager2, GameEventManager, "engine.dll", "GAMEEVENTSMANAGER002");
MAKE_INTERFACE_VERSION(CTFGameMovement, GameMovement, "client.dll", "GameMovement001");
MAKE_INTERFACE_VERSION(IInputSystem, InputSystem, "inputsystem.dll", "InputSystemVersion001");
MAKE_INTERFACE_VERSION(ILocalize, Localize, "vgui2.dll", "VGUI_Localize005");
MAKE_INTERFACE_VERSION(IMaterialSystem, MaterialSystem, "materialsystem.dll", "VMaterialSystem082");
MAKE_INTERFACE_VERSION(IMatSystemSurface, MatSystemSurface, "vguimatsurface.dll", "VGUI_Surface030");
MAKE_INTERFACE_VERSION(IPanel, Panel, "vgui2.dll", "VGUI_Panel009");
MAKE_INTERFACE_VERSION(ISpatialPartition, SpatialPartition, "engine.dll", "SpatialPartition001");
MAKE_INTERFACE_VERSION(IStudioRender, StudioRender, "studiorender.dll", "VStudioRender025");
MAKE_INTERFACE_VERSION(IVModelInfoClient, ModelInfoClient, "engine.dll", "VModelInfoClient006");
MAKE_INTERFACE_VERSION(IVModelRender, ModelRender, "engine.dll", "VEngineModel016");
MAKE_INTERFACE_VERSION(IVRenderView, RenderView, "engine.dll", "VEngineRenderView014");
MAKE_INTERFACE_VERSION(CPrediction, Prediction, "client.dll", "VClientPrediction001");
MAKE_INTERFACE_VERSION(ISteamClient, SteamClient, "steamclient64.dll", STEAMCLIENT_INTERFACE_VERSION);
MAKE_INTERFACE_VERSION(IPhysicsCollision, PhysicsCollision, "vphysics.dll", "VPhysicsCollision007");
MAKE_INTERFACE_VERSION(IPhysics, Physics, "vphysics.dll", "VPhysics031");

MAKE_INTERFACE_SIGNATURE(CClientModeShared, ClientModeShared, "client.dll", "48 8B 0D ? ? ? ? 48 8B 10 48 8B 19 48 8B C8 FF 92", 0x0, 1);
MAKE_INTERFACE_SIGNATURE(CClientState, ClientState, "engine.dll", "48 8D 0D ? ? ? ? E8 ? ? ? ? F3 0F 5E 05", 0x0, 0);
MAKE_INTERFACE_SIGNATURE(CCollisionBSPData, BSPData, "engine.dll", "48 8D 15 ? ? ? ? 4C 03 05", 0x0, 0);
MAKE_INTERFACE_SIGNATURE(CDispCollTree*, DispCollTrees, "engine.dll", "48 89 05 ? ? ? ? E8 ? ? ? ? 48 8B D0", 0x0, 0, false);
MAKE_INTERFACE_SIGNATURE(int, DispCollTreeCount, "engine.dll", "44 89 35 ? ? ? ? E8", 0x0, 0, false);
MAKE_INTERFACE_SIGNATURE(CGlobalVarsBase, GlobalVars, "engine.dll", "48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 8B CA", 0x0, 0);
MAKE_INTERFACE_SIGNATURE(CModelLoader, ModelLoader, "engine.dll", "48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8B D8 8B 40", 0x0, 0);
MAKE_INTERFACE_SIGNATURE(CStaticPropMgr, StaticPropMgr, "engine.dll", "48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 89 5C 24 ? 48 89 74 24 ? 57 48 81 EC ? ? ? ? 41 8B C0", 0x0, 0);
MAKE_INTERFACE_SIGNATURE(CTFGCClientSystem, TFGCClientSystem, "client.dll", "48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 8B 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24", 0x0, 0);
MAKE_INTERFACE_SIGNATURE(CThirdPersonManager, ThirdPersonManager, "client.dll", "48 8D 0D ? ? ? ? 0F 29 BC 24", 0x0, 0);
MAKE_INTERFACE_SIGNATURE(IDemoPlayer, DemoPlayer, "engine.dll", "48 8B 0D ? ? ? ? 40 B7", 0x0, 1);
MAKE_INTERFACE_SIGNATURE(IDemoRecorder, DemoRecorder, "engine.dll", "48 8B 0D ? ? ? ? 8D 56", 0x0, 1);
MAKE_INTERFACE_SIGNATURE(IInput, Input, "client.dll", "48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 85 C0 0F 84 ? ? ? ? F3 0F 10 05", 0x0, 1);
MAKE_INTERFACE_SIGNATURE(IMoveHelper, MoveHelper, "client.dll", "48 8B 0D ? ? ? ? 48 8B 01 FF 50 ? 0F B7 D7", 0x0, 1);
MAKE_INTERFACE_SIGNATURE(IUniformRandomStream, UniformRandomStream, "client.dll", "48 8B 0D ? ? ? ? F3 0F 59 CA 44 8D 42", 0x0, 1);
MAKE_INTERFACE_SIGNATURE(IViewRender, ViewRender, "client.dll", "48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 48 8B 03 48 8D 54 24", 0x0, 1);
MAKE_INTERFACE_SIGNATURE(IViewRenderBeams, ViewRenderBeams, "client.dll", "48 8B 0D ? ? ? ? 48 8B D3 48 8B 01 FF 50 ? 0F B7 93", 0x0, 1);
MAKE_INTERFACE_SIGNATURE(worldbrushdata_t, WorldBrushData, "engine.dll", "48 8D 05 ? ? ? ? 48 89 05 ? ? ? ? 48 8B 5C 24 ? 48 83 C4", 0x0, 0);

MAKE_INTERFACE_NULL(CTFPartyClient, TFPartyClient);
MAKE_INTERFACE_NULL(IKeyValuesSystem, KeyValuesSystem);
MAKE_INTERFACE_NULL(ISteamFriends, SteamFriends);
MAKE_INTERFACE_NULL(ISteamUtils, SteamUtils);
MAKE_INTERFACE_NULL(ISteamApps, SteamApps);
MAKE_INTERFACE_NULL(ISteamUserStats, SteamUserStats);
MAKE_INTERFACE_NULL(ISteamUser, SteamUser);
MAKE_INTERFACE_NULL(ISteamScreenshots, SteamScreenshots);
MAKE_INTERFACE_NULL(ISteamNetworkingUtils, SteamNetworkingUtils);
MAKE_INTERFACE_NULL(ISteamMatchmakingServers, SteamMatchmakingServers);
MAKE_INTERFACE_NULL(ISteamGameCoordinator, SteamGameCoordinator);

inline IHandleEntity* CBaseHandle::Get() const
{
	return reinterpret_cast<IHandleEntity*>(I::ClientEntityList->GetClientEntityFromHandle(m_Index));
}

#ifndef TEXTMODE
#include <d3d9.h>
MAKE_INTERFACE_SIGNATURE(IDirect3DDevice9, DirectXDevice, "shaderapidx9.dll, shaderapivk.dll", "48 8B 0D ? ? ? ? 48 8B 01 FF 50 ? 8B F8", 0x0, 1);
#endif

class CNullInterfaces
{
private:
	bool m_bFailed = false;

public:
	bool Initialize();
};

ADD_FEATURE_CUSTOM(CNullInterfaces, Interfaces, H);
