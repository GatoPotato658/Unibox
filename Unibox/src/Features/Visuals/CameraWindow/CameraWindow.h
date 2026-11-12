#pragma once
#include "../../../SDK/SDK.h"

class CCameraWindow
{
private:
	void RenderCustomView(void* rcx, const CViewSetup& pViewSetup, ITexture* pTexture);

	std::shared_ptr<IMaterial> m_pCameraMaterial = {};
	std::shared_ptr<ITexture> m_pCameraTexture = {};

public:
	void Draw();
	void RenderView(void* rcx, const CViewSetup& pViewSetup);

	void Initialize();
	void Unload();

	bool m_bDrawing = false;
	bool m_bShouldDraw = false;
	Vec3 m_vCameraOrigin;
	Vec3 m_vCameraAngles;
};

ADD_FEATURE(CCameraWindow, CameraWindow);
