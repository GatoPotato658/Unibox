#include "Interface.h"
#include "../Definitions.h"

class CServerBaseEntity;

//-----------------------------------------------------------------------------
// Interface from engine to tools for manipulating entities
//-----------------------------------------------------------------------------
class CServerTools/* : public IServerTools*/
{
public:
	// Inherited from IServerTools
	virtual	~CServerTools() = 0;
	virtual void* GetIServerEntity(void* pClientEntity) = 0;
	virtual bool GetPlayerPosition(Vector& org, QAngle& ang, void* pClientPlayer = NULL) = 0;
	virtual bool SnapPlayerToPosition(const Vector& org, const QAngle& ang, void* pClientPlayer = NULL) = 0;
	virtual int GetPlayerFOV(void* pClientPlayer = NULL) = 0;
	virtual bool SetPlayerFOV(int fov, void* pClientPlayer = NULL) = 0;
	virtual bool IsInNoClipMode(void* pClientPlayer = NULL) = 0;
	virtual CServerBaseEntity* FirstEntity(void) = 0;
	virtual CServerBaseEntity* NextEntity(CServerBaseEntity* pEntity) = 0;
	virtual CServerBaseEntity* FindEntityByHammerID(int iHammerID) = 0;
	virtual bool GetKeyValue(CServerBaseEntity* pEntity, const char* szField, char* szValue, int iMaxLen) = 0;
	virtual bool SetKeyValue(CServerBaseEntity* pEntity, const char* szField, const char* szValue) = 0;
	virtual bool SetKeyValue(CServerBaseEntity* pEntity, const char* szField, float flValue) = 0;
	virtual bool SetKeyValue(CServerBaseEntity* pEntity, const char* szField, const Vector& vecValue) = 0;
	virtual CServerBaseEntity* CreateEntityByName(const char* szClassName) = 0;
	virtual void DispatchSpawn(CServerBaseEntity* pEntity) = 0;
	virtual void ReloadParticleDefintions(const char* pFileName, const void* pBufData, int nLen) = 0;
	virtual void AddOriginToPVS(const Vector& org) = 0;
	virtual void MoveEngineViewTo(const Vector& vPos, const QAngle& vAngles) = 0;
	virtual bool DestroyEntityByHammerId(int iHammerID) = 0;
	virtual CServerBaseEntity* GetBaseEntityByEntIndex(int iEntIndex) = 0;
	virtual void RemoveEntity(CServerBaseEntity* pEntity) = 0;
	virtual void RemoveEntityImmediate(CServerBaseEntity* pEntity) = 0;
	virtual void* GetEntityFactoryDictionary(void) = 0;
	virtual void SetMoveType(CServerBaseEntity* pEntity, int val) = 0;
	virtual void SetMoveType(CServerBaseEntity* pEntity, int val, int moveCollide) = 0;
	virtual void ResetSequence(void* pEntity, int nSequence) = 0;
	virtual void ResetSequenceInfo(void* pEntity) = 0;
	virtual void ClearMultiDamage(void) = 0;
	virtual void ApplyMultiDamage(void) = 0;
	virtual void AddMultiDamage(void* pTakeDamageInfo, CServerBaseEntity* pEntity) = 0;
	virtual void RadiusDamage(void* info, const Vector& vecSrc, float flRadius, int iClassIgnore, CServerBaseEntity* pEntityIgnore) = 0;

	virtual void* GetTempEntsSystem(void) = 0;
	virtual void* GetTempEntList(void) = 0;
	virtual void* GetEntityList(void) = 0; // CGlobalEntityList
	virtual bool IsEntityPtr(void* pTest) = 0;
	virtual CServerBaseEntity* FindEntityByClassname(CServerBaseEntity* pStartEntity, const char* szName) = 0;
	virtual CServerBaseEntity* FindEntityByName(CServerBaseEntity* pStartEntity, const char* szName, CServerBaseEntity* pSearchingEntity = NULL, CServerBaseEntity* pActivator = NULL, CServerBaseEntity* pCaller = NULL, void* pFilter = NULL) = 0;
	virtual CServerBaseEntity* FindEntityInSphere(CServerBaseEntity* pStartEntity, const Vector& vecCenter, float flRadius) = 0;
	virtual CServerBaseEntity* FindEntityByTarget(CServerBaseEntity* pStartEntity, const char* szName) = 0;
	virtual CServerBaseEntity* FindEntityByModel(CServerBaseEntity* pStartEntity, const char* szModelName) = 0;
	virtual CServerBaseEntity* FindEntityByNameNearest(const char* szName, const Vector& vecSrc, float flRadius, CServerBaseEntity* pSearchingEntity = NULL, CServerBaseEntity* pActivator = NULL, CServerBaseEntity* pCaller = NULL) = 0;
	virtual CServerBaseEntity* FindEntityByNameWithin(CServerBaseEntity* pStartEntity, const char* szName, const Vector& vecSrc, float flRadius, CServerBaseEntity* pSearchingEntity = NULL, CServerBaseEntity* pActivator = NULL, CServerBaseEntity* pCaller = NULL) = 0;
	virtual CServerBaseEntity* FindEntityByClassnameNearest(const char* szName, const Vector& vecSrc, float flRadius) = 0;
	virtual CServerBaseEntity* FindEntityByClassnameWithin(CServerBaseEntity* pStartEntity, const char* szName, const Vector& vecSrc, float flRadius) = 0;
	virtual CServerBaseEntity* FindEntityByClassnameWithin(CServerBaseEntity* pStartEntity, const char* szName, const Vector& vecMins, const Vector& vecMaxs) = 0;
	virtual CServerBaseEntity* FindEntityGeneric(CServerBaseEntity* pStartEntity, const char* szName, CServerBaseEntity* pSearchingEntity = NULL, CServerBaseEntity* pActivator = NULL, CServerBaseEntity* pCaller = NULL) = 0;
	virtual CServerBaseEntity* FindEntityGenericWithin(CServerBaseEntity* pStartEntity, const char* szName, const Vector& vecSrc, float flRadius, CServerBaseEntity* pSearchingEntity = NULL, CServerBaseEntity* pActivator = NULL, CServerBaseEntity* pCaller = NULL) = 0;
	virtual CServerBaseEntity* FindEntityGenericNearest(const char* szName, const Vector& vecSrc, float flRadius, CServerBaseEntity* pSearchingEntity = NULL, CServerBaseEntity* pActivator = NULL, CServerBaseEntity* pCaller = NULL) = 0;
	virtual CServerBaseEntity* FindEntityNearestFacing(const Vector& origin, const Vector& facing, float threshold) = 0;
	virtual CServerBaseEntity* FindEntityClassNearestFacing(const Vector& origin, const Vector& facing, float threshold, char* classname) = 0;
	virtual CServerBaseEntity* FindEntityProcedural(const char* szName, CServerBaseEntity* pSearchingEntity = NULL, CServerBaseEntity* pActivator = NULL, CServerBaseEntity* pCaller = NULL) = 0;
};

MAKE_INTERFACE_VERSION(CServerTools, ServerTools, "server.dll", "VSERVERTOOLS003");