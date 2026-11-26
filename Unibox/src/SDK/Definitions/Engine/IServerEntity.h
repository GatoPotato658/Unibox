#pragma once
#include "IServerUnknown.h"

class IServerEntity	: public IServerUnknown
{
public:
	virtual					~IServerEntity() {}

// Previously in pev
	virtual int				GetModelIndex( void ) const = 0;
 	virtual int		GetModelName( void ) const = 0;

	virtual void			SetModelIndex( int index ) = 0;
	
	template <typename T> inline T* As() { return reinterpret_cast<T*>(this); }
};