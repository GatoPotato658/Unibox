#pragma once

#define MAPKEY_MAXLENGTH	2048

class CEntityMapData
{
private:
	char* m_pEntData;
	int	m_nEntDataSize;
	char* m_pCurrentKey;

public:
	explicit CEntityMapData(char* entBlock, int nEntBlockSize = -1) :
		m_pEntData(entBlock), m_nEntDataSize(nEntBlockSize), m_pCurrentKey(entBlock)
	{}

	// find the keyName in the entdata and puts it's value into Value.  returns false if key is not found
	//bool ExtractValue(const char* keyName, char* Value);

	// find the nth keyName in the endata and change its value to specified one
	// where n == nKeyInstance
	//bool SetValue(const char* keyName, char* NewValue, int nKeyInstance = 0);

	bool GetFirstKey(char* keyName, char* Value);
	bool GetNextKey(char* keyName, char* Value);
	char* CurrentBufferPosition(void) { return m_pCurrentKey; }
};