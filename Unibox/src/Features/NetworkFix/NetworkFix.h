#pragma once
#include "../../SDK/SDK.h"

class CReadPacketState
{
private:
    float m_flFrameTimeClientState = 0.f;
    float m_flFrameTime = 0.f;
    float m_flCurTime = 0.f;
    int m_nTickCount = 0;

public:
    void Store();
    void Restore();
};

enum class EPropScope
{
	Any,
	Local,
	NonLocal
};

enum class EPropEncoding
{
	None,
	FixedPoint,
	Normal,
	Scaled
};

struct Quantiser_t
{
	EPropEncoding m_eEncoding = EPropEncoding::None;
	int m_iDenominator = 0;
	float m_flLow = 0.f;
	float m_flHigh = 0.f;
	float m_flScale = 0.f;
	float m_flDecodeScale = 0.f;
	bool m_bWraps = false;

	int Encode(float flValue) const;
	int Invert(float flDecoded) const;
	bool Holds(float flCandidate, float flDecoded) const;
};

struct CompressedProp_t
{
	int m_iOffset = 0;
	int m_iComponents = 1;
	EPropScope m_eScope = EPropScope::Any;
	Quantiser_t m_tQuantiser = {};
};

struct CompressionSnapshot_t
{
	int m_iCommand = -1;
	std::vector<Vec3> m_vValues = {};
};

class CNetworkFix
{
private:
    CReadPacketState m_tState = {};
    CReadPacketState m_tBackup = {};

	bool BuildCompression();
	void CollectProps(RecvTable* pRecv, SendTable* pSend, const std::vector<std::pair<std::string_view, std::string_view>>& vExcluded, int iBaseOffset, int iDepth, EPropScope eScope);

	ClientClass* m_pCompressedClass = nullptr;
	std::vector<CompressedProp_t> m_vCompressedProps = {};
	std::array<CompressionSnapshot_t, 64> m_aSnapshots = {};
	int m_iBuildAttempts = 0;

public:
    void FixInputDelay(bool bFinalTick);
    bool ShouldReadPackets();

	void StorePredicted(CBaseEntity* pLocal, int iCommand);
	void RestoreCompressed();
	void Reset();
};

ADD_FEATURE(CNetworkFix, NetworkFix);
