#pragma once
#include "../../../SDK/SDK.h"
#include "D3DMaterials.h"
#include <vector>

Enum(Model, Visible, Occluded);

struct Group_t;
struct Material_t;

class CChams
{
public:
	struct ActiveLayer_t
	{
		ChamsMaterial_t m_tMaterial = {};
		Material_t* m_pMaterial = nullptr;
		bool m_bInvertCull = false;
		bool m_bBlockOccluded = false;
	};

private:
	void Begin();
	void End();
	template <typename FDraw>
	bool RenderViewmodelMaterial(IMatRenderContext* pRenderContext, Group_t* pGroup, MaterialCullMode_t restore, FDraw fDraw);

	void DrawModel(CBaseEntity* pEntity, const Chams_t& tChams, IMatRenderContext* pRenderContext, int iModel, bool bSame, int iStencilReference);
	void RenderActiveModel(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld);
	void DrawSourceLayer(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld, const ActiveLayer_t& tLayer, bool bOccluded, bool bTwoModel, int iStencilRef);

	void RenderFakeAngle(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo);

	struct ChamsInfo_t
	{
		CBaseEntity* m_pEntity;
		Chams_t* m_pChams;
		float m_flDistance;
		int m_iFlags = 0;
	};
	std::vector<ChamsInfo_t> m_vEntities = {};
	std::vector<ActiveLayer_t> m_vActiveVisible = {};
	std::vector<ActiveLayer_t> m_vActiveOccluded = {};

	Color_t m_tOriginalColor = {};
	float m_flOriginalBlend = 1.f;
	IMaterial* m_pOriginalMaterial = nullptr;
	OverrideType_t m_iOriginalOverride = OVERRIDE_NORMAL;
	float m_flCurrentDistance = -1.f;

	int m_iFlags = false;
	ChamsMaterial_t m_tCurrentMaterial = {};
	bool m_bOccludedMaterial = false;
	bool m_bViewmodelMaterial = false;
	int m_iViewmodelCullOverride = -1;

	IMatRenderContext* m_pActiveRenderContext = nullptr;
	int m_iActiveStencilRef = 0;
	bool m_bActiveSame = false;
	bool m_bActiveSingleDraw = false;

public:
	void Reset();
	void Store(CTFPlayer* pLocal);
	void RenderMain();
	void RenderHandler(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld);
	bool RenderMaterial(const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld);

	bool RenderViewmodel(CBaseAnimating* rcx, int flags, int* iReturn);
	bool RenderViewmodel(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld);

	bool m_bRendering = false;

	std::unordered_mapset<int> m_mEntities = {};
};

ADD_FEATURE(CChams, Chams);
