#pragma once
#include "IHandleEntity.h"

class ICollideable;
class IServerNetworkable;
class CBaseEntity;
class IServerUnknown : public IHandleEntity
{
public:
	// Gets the interface to the collideable + networkable representation of the entity
	virtual ICollideable*		GetCollideable() = 0;
	virtual IServerNetworkable*	GetNetworkable() = 0;
	virtual CBaseEntity*		GetBaseEntity() = 0;
};