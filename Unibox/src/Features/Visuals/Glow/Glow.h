#pragma once
#include "../../../SDK/SDK.h"

#include <boost/functional/hash.hpp>

class CGlow
{
private:
	void Begin();
	void End();
	void FirstBegin(IMatRenderContext* pRenderContext);
	void FirstEnd(IMatRenderContext* pRenderContext);
	void SecondBegin(const Glow_t& tGlow, IMatRenderContext* pRenderContext, int w, int h);
	void SecondEnd(Glow_t tGlow, IMatRenderContext* pRenderContext, int w, int h);
	void BeginVisibleMask(IMatRenderContext* pRenderContext);
	void EndVisibleMask(IMatRenderContext* pRenderContext);
	bool GetDimensions(IMatRenderContext* pRenderContext, int& w, int& h);
	void LegacyBegin();
	void LegacyFirstBegin(IMatRenderContext* pRenderContext);
	void LegacyFirstEnd(IMatRenderContext* pRenderContext);
	void LegacySecondBegin(IMatRenderContext* pRenderContext, int w, int h);
	void LegacySecondEnd(const Glow_t& tGlow, IMatRenderContext* pRenderContext, int w, int h);
	bool LegacyReady() const;
	void InitializeLegacy(int nWidth, int nHeight);
	void RenderLegacy(IMatRenderContext* pRenderContext);
	void MarkEnhanced(IMatRenderContext* pRenderContext);

	void DrawModel(CBaseEntity* pEntity);

	void RenderFakeAngle(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo);

	IMaterial* m_pMatGlowColor = nullptr;
	IMaterial* m_pMatGlowVisible = nullptr;
	ITexture* m_pRenderBuffer1 = nullptr;
	ITexture* m_pRenderBuffer2 = nullptr;
	ITexture* m_pRenderBuffer3 = nullptr;
	IMaterial* m_pMatHaloAddToScreen = nullptr;
	IMaterial* m_pMatHaloAlpha = nullptr;
	IMaterial* m_pMatBloomAdd = nullptr;
	IMaterial* m_pMatBloomAlpha = nullptr;
	IMaterial* m_pMatDownsample = nullptr;
	IMaterial* m_pMatBlurX = nullptr;
	IMaterial* m_pMatBlurY = nullptr;
	bool m_bD3DPass = false;
	IMaterial* m_pLegacyGlowColor = nullptr;
	ITexture* m_pLegacyBuffer1 = nullptr;
	ITexture* m_pLegacyBuffer2 = nullptr;
	IMaterial* m_pLegacyHalo = nullptr;
	IMaterial* m_pLegacyBlurX = nullptr;
	IMaterial* m_pLegacyBlurY = nullptr;

	struct GlowHasher_t
	{
		std::size_t operator()(const Glow_t& k) const
		{
			std::size_t seed = 0;

			boost::hash_combine(seed, boost::hash_value(k.Style));
			boost::hash_combine(seed, boost::hash_value(k.Stencil));
			boost::hash_combine(seed, boost::hash_value(k.Blur));
			boost::hash_combine(seed, boost::hash_value(k.Start));
			boost::hash_combine(seed, boost::hash_value(k.End));
			boost::hash_combine(seed, boost::hash_value(k.SmoothAlpha));
			boost::hash_combine(seed, boost::hash_value(k.Intensity));
			boost::hash_combine(seed, boost::hash_value(k.Opacity));
			boost::hash_combine(seed, boost::hash_value(k.Falloff));
			boost::hash_combine(seed, boost::hash_value(k.Bloom));
			boost::hash_combine(seed, boost::hash_value(k.Layers));
			boost::hash_combine(seed, boost::hash_value(k.Blend));
			boost::hash_combine(seed, boost::hash_value(k.Depth));
			boost::hash_combine(seed, boost::hash_value(k.CustomOuter));
			boost::hash_combine(seed, boost::hash_value(k.OuterColor.r));
			boost::hash_combine(seed, boost::hash_value(k.OuterColor.g));
			boost::hash_combine(seed, boost::hash_value(k.OuterColor.b));
			boost::hash_combine(seed, boost::hash_value(k.OuterColor.a));

			return seed;
		}
	};
	struct GlowInfo_t
	{
		CBaseEntity* m_pEntity;
		Color_t m_tColor;
		int m_iFlags = 0;
	};
	std::unordered_map<Glow_t, std::vector<GlowInfo_t>, GlowHasher_t> m_mEntities = {};

	Color_t m_tOriginalColor = {};
	float m_flOriginalBlend = 1.f;
	IMaterial* m_pOriginalMaterial = nullptr;
	OverrideType_t m_iOriginalOverride = OVERRIDE_NORMAL;

	int m_iFlags = false;
public:

	void Store(CTFPlayer* pLocal);
	void RenderFirst();
	void RenderSecond();
	void RenderHandler(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld);

	void RenderViewmodel(CBaseAnimating* rcx, int flags);
	void RenderViewmodel(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld);

	void Initialize();
	void Unload();
	void OnDeviceLost();

	bool m_bRendering = false;
};

ADD_FEATURE(CGlow, Glow);
