#pragma once

#include <cstdint>
#include <memory>

#ifdef _WIN32
#include "../../../SDK/SDK.h"
#else
#include "../../../Utils/Macros/Macros.h"
struct DrawModelState_t;
struct ModelRenderInfo_t;
using matrix3x4 = float[3][4];
struct ChamsMaterial_t;
class IMatRenderContext;
#endif

struct IDirect3DDevice9;

struct D3DStencilState_t
{
	bool m_bEnable = false;
	std::uint32_t m_uCompareFunc = 8;
	std::uint32_t m_uPassOp = 1;
	std::uint32_t m_uFailOp = 1;
	std::uint32_t m_uZFailOp = 1;
	std::uint32_t m_uRef = 0;
	std::uint32_t m_uWriteMask = 0xFF;
	std::uint32_t m_uTestMask = 0xFF;
};

struct D3DGeometryData;
struct D3DPoseData;

class CD3DMaterials
{
public:
	using GeometryRef = std::shared_ptr<const D3DGeometryData>;
	using PoseRef = std::shared_ptr<const D3DPoseData>;

	void Initialize(IDirect3DDevice9* pDevice);
	bool Draw(const DrawModelState_t& state, const ModelRenderInfo_t& info, const matrix3x4* bones, const ChamsMaterial_t& params, bool bOccluded, bool bViewmodel = false);

	bool PrepareGeometry(const DrawModelState_t& state, const ModelRenderInfo_t& info, GeometryRef& outGeometry, bool& outNeedsFallback);
	bool PreparePoses(const GeometryRef& geometry, const matrix3x4* bones, PoseRef& outPoses);
	bool DrawPrepared(IMatRenderContext* pContext, const GeometryRef& geometry, const PoseRef& poses, const ChamsMaterial_t& params, bool bOccluded, int iCullOverride = -1, const D3DStencilState_t* pStencil = nullptr, bool bMaskOnly = false);
	void BeginBatch(IMatRenderContext* pContext);
	void FlushBatch(IMatRenderContext* pContext);
	void EndBatch(IMatRenderContext* pContext);
	void ClearCache();

	void Unload();
};

ADD_FEATURE(CD3DMaterials, D3DMaterials);
