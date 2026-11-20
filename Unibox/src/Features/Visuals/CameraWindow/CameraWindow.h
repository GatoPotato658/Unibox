#pragma once
#include "../../../SDK/SDK.h"

class CCameraWindow
{
private:
	void RenderCustomView(void* rcx, const CViewSetup& pViewSetup, ITexture* pTexture);
	void RenderChannel(void* rcx, const CViewSetup& pViewSetup, ITexture* pTexture, WindowBox_t& tWindowBox, Vec3 vOrigin, Vec3 vAngles, CBaseEntity* pHide = nullptr);
	void DrawChannel(IMaterial* pMaterial, ITexture* pTexture, WindowBox_t& tWindowBox);

	IMaterial* m_pCameraMaterial = nullptr;
	ITexture* m_pCameraTexture = nullptr;
	IMaterial* m_pSpyMaterial = nullptr;
	ITexture* m_pSpyTexture = nullptr;

public:
	void Draw();
	void RenderView(void* rcx, const CViewSetup& pViewSetup);

	void Initialize();
	void Unload();

	bool m_bDrawing = false;
	bool m_bShouldDraw = false;
	Vec3 m_vCameraOrigin;
	Vec3 m_vCameraAngles;

	bool m_bShouldDrawSpy = false;
	Vec3 m_vSpyOrigin;
	Vec3 m_vSpyAngles;
	CTFPlayer* m_pSpyTarget = nullptr;
};

ADD_FEATURE(CCameraWindow, CameraWindow);
