#include "Pages.h"

#ifndef TEXTMODE

#include "Components.h"
#include "../../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include "../../Players/PlayerUtils.h"
#include "../../Misc/AutoQueue/MvmQueue.h"
#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"
#if __has_include("../../Misc/ProfileStalker/ProfileStalker.h")
#include "../../Misc/ProfileStalker/ProfileStalker.h"
#define MENU_PROFILE_STALKER
#endif

#include <atomic>
#include <filesystem>

#pragma comment(lib, "comdlg32.lib")

using namespace ImGui;

static bool DebugRow(const char* sLabel)
{
	FLabel(sLabel);
	return FPopupButton(sLabel);
}

static std::atomic<bool> s_bFontDialogOpen = false, s_bFontDialogReady = false;
static std::string s_sFontDialogPath;

static DWORD WINAPI FontDialogThread(LPVOID pOwner)
{
	char sFile[MAX_PATH] = {};
	OPENFILENAMEA tOpen = {};
	tOpen.lStructSize = sizeof(tOpen);
	tOpen.hwndOwner = static_cast<HWND>(pOwner);
	tOpen.lpstrFilter = "Font files (*.ttf;*.otf)\0*.ttf;*.otf\0All files (*.*)\0*.*\0";
	tOpen.lpstrFile = sFile;
	tOpen.nMaxFile = MAX_PATH;
	tOpen.lpstrTitle = "Select font file";
	tOpen.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
	if (GetOpenFileNameA(&tOpen))
	{
		s_sFontDialogPath = sFile;
		s_bFontDialogReady = true;
	}
	s_bFontDialogOpen = false;
	return 0;
}

static void FontPicker()
{
	if (s_bFontDialogReady.exchange(false))
		Vars::Menu::CustomFont.Value = s_sFontDialogPath;

	const bool bOpen = s_bFontDialogOpen;
	const bool bSet = !Vars::Menu::CustomFont.Value.empty();
	const std::string sLabel = bOpen ? "Choosing..." : bSet
		? std::format("Font: {}", TruncateText(std::filesystem::path(Vars::Menu::CustomFont.Value).filename().string(), int(GetWindowWidth() / 3)))
		: "Choose custom font";

	PushTransparent(bOpen);
	if (FButton((sLabel + "##CustomFont").c_str(), bSet ? FButtonEnum::Left : FButtonEnum::None) && !bOpen)
	{
		s_bFontDialogOpen = true;
		if (const HANDLE hThread = CreateThread(nullptr, 0, FontDialogThread, GetMainViewport()->PlatformHandleRaw, 0, nullptr))
			CloseHandle(hThread);
		else
			s_bFontDialogOpen = false;
	}
	PopTransparent();
	if (bSet && FButton("Reset font", FButtonEnum::Right))
		Vars::Menu::CustomFont.Value.clear();
}

static void AimbotGeneral()
{
	if (!PageTable("AimbotGeneral", 2))
		return;

	TableNextColumn();
	if (Section("Targeting"))
	{
		FDropdown(Vars::Aimbot::General::AimType);
		FDropdown(Vars::Aimbot::General::Target);
		FDropdown(Vars::Aimbot::General::Ignore);
		PushTransparent(!(Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Invisible));
		{
			FSlider(Vars::Aimbot::General::IgnoreInvisible);
		}
		PopTransparent();
		PushTransparent(!(Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Unsimulated));
		{
			FSlider(Vars::Aimbot::General::TickTolerance);
		}
		PopTransparent();
		FSlider(Vars::Aimbot::General::MaxTargets);

		Subheader("Selection");
		FDropdown(Vars::Aimbot::General::TargetSelectionHitscan);
		FDropdown(Vars::Aimbot::General::TargetSelectionProjectile);
		FDropdown(Vars::Aimbot::General::TargetSelectionMelee);
	} EndSection();
	if (Section("Aim"))
	{
		FSlider(Vars::Aimbot::General::AimFOV);
		FToggle(Vars::Aimbot::General::FOVCircle);
		PushTransparent(!Vars::Aimbot::General::FOVCircle.Value);
		{
			FColorPicker(Vars::Colors::FOVCircle);
		}
		PopTransparent();
		FDropdown(Vars::Aimbot::General::SmoothCurve);
		FSlider(Vars::Aimbot::General::SmoothCurveAmount);
		FSlider(Vars::Aimbot::General::AssistStrength);
		FToggle(Vars::Aimbot::General::AutoShoot);
		FToggle(Vars::Aimbot::General::LeadAndRestrict);
		PushDisabled(F::AntiCheatCompatibility.Active());
		{
			FToggle(Vars::Aimbot::General::NoSpread);
		}
		PopDisabled();
	} EndSection();

	TableNextColumn();
	if (Section("Crithack"))
	{
		PushDisabled(F::AntiCheatCompatibility.Active());
		{
			FToggle(Vars::CritHack::ForceCrits);
			FToggle(Vars::CritHack::AvoidRandomCrits);
			FToggle(Vars::CritHack::AlwaysMeleeCrit);
			FToggle(Vars::CritHack::CritEffects);
		}
		PopDisabled();
	} EndSection();
	if (Section("Backtrack"))
	{
		PushDisabled(F::AntiCheatCompatibility.Active());
		{
			FSlider(Vars::Backtrack::Latency);
			FSlider(Vars::Backtrack::Interp);
			FSlider(Vars::Backtrack::Window);
		}
		PopDisabled();
	} EndSection();
	if (Vars::Debug::Options.Value)
	{
		if (Section("Debug##General"))
		{
			if (DebugRow("Aimbot"))
			{
				FDropdown(Vars::Aimbot::General::AimHoldsFire);
				FSlider(Vars::Aimbot::General::MoveSimAccuracy);
				FTooltip("high = cpu rape, low = misswaffen");
				FSlider(Vars::Aimbot::General::NoSpreadOffset);
				FSlider(Vars::Aimbot::General::NoSpreadAverage);
				FSlider(Vars::Aimbot::General::NoSpreadInterval);
				FSlider(Vars::Aimbot::General::NoSpreadBackupInterval);

				EndPopup();
			}
			if (DebugRow("Backtrack"))
			{
				FSlider(Vars::Backtrack::Offset);

				EndPopup();
			}
		} EndSection();
	}

	EndTable();
}

static void AimbotWeapons()
{
	if (!PageTable("AimbotWeapons", 2))
		return;

	TableNextColumn();
	if (Section("Hitscan"))
	{
		FDropdown(Vars::Aimbot::Hitscan::Hitboxes);
		FDropdown(Vars::Aimbot::Hitscan::MultipointHitboxes);
		FSlider(Vars::Aimbot::Hitscan::MultipointScale);
		FDropdown(Vars::Aimbot::Hitscan::Modifiers);
		PushTransparent(!(Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::Tapfire));
		{
			FSlider(Vars::Aimbot::Hitscan::TapfireDistance);
		}
		PopTransparent();
		FToggle(Vars::Aimbot::Hitscan::ForwardTrack);
		FTooltip("Simulates the target forward to where they will be by the time your shot lands. The mode adapts automatically. Likes to miss when your ping is >80.");
	} EndSection();
	if (Section("Melee"))
	{
		FToggle(Vars::Aimbot::Melee::AutoBackstab);
		FToggle(Vars::Aimbot::Melee::IgnoreRazorback);
		FToggle(Vars::Aimbot::Melee::SwingPrediction);
		FToggle(Vars::Aimbot::Melee::WhipTeam);
	} EndSection();
	if (Vars::Debug::Options.Value)
	{
		if (Section("Debug##Weapons"))
		{
			if (DebugRow("Hitscan"))
			{
				FDropdown(Vars::Aimbot::Hitscan::ForwardTrackMode);
				FToggle(Vars::Aimbot::Hitscan::ForwardTrackAirOnly);
				FDropdown(Vars::Aimbot::Hitscan::PeekCheck, FDropdownEnum::None, 0, &Hovered); FTooltip("This should stay as doubletap only or off if you want to be able to target hitboxes other than the highest priority one", Hovered);
				FSlider(Vars::Aimbot::Hitscan::PeekAmount);
				FSlider(Vars::Aimbot::Hitscan::BoneSizeSubtract);
				FSlider(Vars::Aimbot::Hitscan::BoneSizeMinimumScale);

				EndPopup();
			}
			if (DebugRow("Projectile"))
			{
				FSlider(Vars::Aimbot::Projectile::VelocityAverageCount);
				FSlider(Vars::Aimbot::Projectile::VerticalShift);
				FSlider(Vars::Aimbot::Projectile::DragOverride);
				FSlider(Vars::Aimbot::Projectile::TimeOverride);
				FToggle(Vars::Aimbot::Projectile::LobAnglesUnderpredict);
#ifdef PROJ_PROFILER
				FToggle(Vars::Aimbot::Projectile::LagProfile);
				FSlider(Vars::Aimbot::Projectile::LagProfileFPS);
				FTooltip("writes unibox/ProjLag.txt when fps drops below this");
#endif

				Subheader("Huntsman");
				FSlider(Vars::Aimbot::Projectile::HuntsmanLerp);
				FSlider(Vars::Aimbot::Projectile::HuntsmanLerpLow);
				FSlider(Vars::Aimbot::Projectile::HuntsmanAdd);
				FSlider(Vars::Aimbot::Projectile::HuntsmanAddLow);
				FSlider(Vars::Aimbot::Projectile::HuntsmanClamp);
				FToggle(Vars::Aimbot::Projectile::HuntsmanPullPoint);
				FToggle(Vars::Aimbot::Projectile::HuntsmanPullNoZ);

				EndPopup();
			}
			if (DebugRow("Splash"))
			{
				PushTransparent(Vars::Aimbot::Projectile::SplashMode.Value != Vars::Aimbot::Projectile::SplashModeEnum::Trace);
				{
					FSlider(Vars::Aimbot::Projectile::SplashPointsDirect);
					FSlider(Vars::Aimbot::Projectile::SplashPointsArc);
					FSlider(Vars::Aimbot::Projectile::SplashRotateX, FSliderEnum::None, Vars::Aimbot::Projectile::SplashRotateX[DEFAULT_BIND] < 0.f ? "random" : "%g");
					FSlider(Vars::Aimbot::Projectile::SplashRotateY, FSliderEnum::None, Vars::Aimbot::Projectile::SplashRotateY[DEFAULT_BIND] < 0.f ? "random" : "%g");
				}
				PopTransparent();
				PushTransparent(Vars::Aimbot::Projectile::SplashMode.Value != Vars::Aimbot::Projectile::SplashModeEnum::Face);
				{
					FSlider(Vars::Aimbot::Projectile::SplashDensityDirect);
					FSlider(Vars::Aimbot::Projectile::SplashDensityArc);
					FSlider(Vars::Aimbot::Projectile::SplashSamplesCutoff);
				}
				PopTransparent();
				FSlider(Vars::Aimbot::Projectile::SplashAirCount, FSliderEnum::None, !Vars::Aimbot::Projectile::SplashAirCount[DEFAULT_BIND] ? "random" : "%i");

				Subheader("Restrict");
				FSlider(Vars::Aimbot::Projectile::SplashRestrictDirect);
				FSlider(Vars::Aimbot::Projectile::SplashRestrictArc);
				FSlider(Vars::Aimbot::Projectile::SplashRestrictFirst);

				Subheader("Trace intervals");
				FSlider(Vars::Aimbot::Projectile::DirectTraceInterval);
				FSlider(Vars::Aimbot::Projectile::SplashTraceInterval);
				FSlider(Vars::Aimbot::Projectile::LobTraceInterval);
				FToggle(Vars::Aimbot::Projectile::IntervalRetest);

				EndPopup();
			}
			if (DebugRow("Ground"))
			{
				FSlider(Vars::Aimbot::Projectile::GroundSamples);
				FSlider(Vars::Aimbot::Projectile::GroundStraightFuzzyValue);
				FSlider(Vars::Aimbot::Projectile::GroundLowMinimumSamples);
				FSlider(Vars::Aimbot::Projectile::GroundHighMinimumSamples);
				FSlider(Vars::Aimbot::Projectile::GroundLowMinimumDistance);
				FSlider(Vars::Aimbot::Projectile::GroundHighMinimumDistance);
				FSlider(Vars::Aimbot::Projectile::GroundMaxChanges);
				FSlider(Vars::Aimbot::Projectile::GroundMaxChangeTime);

				EndPopup();
			}
			if (DebugRow("Air"))
			{
				FSlider(Vars::Aimbot::Projectile::AirSamples);
				FSlider(Vars::Aimbot::Projectile::AirStraightFuzzyValue);
				FSlider(Vars::Aimbot::Projectile::AirLowMinimumSamples);
				FSlider(Vars::Aimbot::Projectile::AirHighMinimumSamples);
				FSlider(Vars::Aimbot::Projectile::AirLowMinimumDistance);
				FSlider(Vars::Aimbot::Projectile::AirHighMinimumDistance);
				FSlider(Vars::Aimbot::Projectile::AirMaxChanges);
				FSlider(Vars::Aimbot::Projectile::AirMaxChangeTime);

				EndPopup();
			}
			if (DebugRow("Projectile misc"))
			{
				FSlider(Vars::Aimbot::Projectile::DeltaCount);
				FDropdown(Vars::Aimbot::Projectile::DeltaMode);
				FDropdown(Vars::Aimbot::Projectile::MovesimFrictionFlags);
				FToggle(Vars::Aimbot::Projectile::AccountPing);
				FToggle(Vars::Aimbot::Projectile::AutodetAccountPing);

				EndPopup();
			}
			if (DebugRow("Melee"))
			{
				FSlider(Vars::Aimbot::Melee::SwingTicks);
				FToggle(Vars::Aimbot::Melee::SwingPredictLag);
				FToggle(Vars::Aimbot::Melee::SwingAccountPing);
				FDropdown(Vars::Aimbot::Melee::SwingValidateMode);
				FDropdown(Vars::Aimbot::Melee::BackstabFlags);

				EndPopup();
			}
		} EndSection();
	}

	TableNextColumn();
	if (Section("Projectile"))
	{
		FDropdown(Vars::Aimbot::Projectile::StrafePrediction);
		FDropdown(Vars::Aimbot::Projectile::SplashPrediction);
		FDropdown(Vars::Aimbot::Projectile::Hitboxes);
		FDropdown(Vars::Aimbot::Projectile::SplashMode);
		FDropdown(Vars::Aimbot::Projectile::Modifiers);
		FSlider(Vars::Aimbot::Projectile::MaxSimulationTime);
		FTooltip("Longer = less accuracy, shorter = less shoot candidates");
		PushTransparent(!Vars::Aimbot::Projectile::StrafePrediction.Value);
		{
			FSlider(Vars::Aimbot::Projectile::HitChance);
		}
		PopTransparent();
		FSlider(Vars::Aimbot::Projectile::SplashRadius);
		PushTransparent(!Vars::Aimbot::Projectile::AutoRelease.Value);
		{
			FSlider(Vars::Aimbot::Projectile::AutoRelease);
		}
		PopTransparent();

		Subheader("Utility");
		FDropdown(Vars::Aimbot::Projectile::AutoDetonate);
		FTooltip("WARNING: \"Aim at stickies\" silently snaps your view to stickies so the Scottish Resistance can detonate them. This is a blatant feature.");
		FSlider(Vars::Aimbot::Projectile::AutodetRadius);
		FDropdown(Vars::Aimbot::Projectile::AutoAirblast);
		FToggle(Vars::Aimbot::Projectile::GrapplingHookAim);
		FSlider(Vars::Aimbot::Projectile::AutoSCMetalAmount);
	} EndSection();

	EndTable();
}

static void AimbotSupport()
{
	if (!PageTable("AimbotSupport", 2))
		return;

	TableNextColumn();
	if (Section("Healing"))
	{
		FDropdown(Vars::Aimbot::Healing::HealPriority);
		FDropdown(Vars::Aimbot::Healing::DangerIgnore);
		FToggle(Vars::Aimbot::Healing::AutoHeal);
		FToggle(Vars::Aimbot::Healing::AutoArrow);
		PushTransparent(!Vars::Aimbot::Healing::AutoArrow.Value);
		{
			FToggleSlider(Vars::Aimbot::Healing::AutoSwitch, Vars::Aimbot::Healing::AutoSwitchHealth);
			FTooltip("Automatically switch to crossbow and heal if target is below the selected health percent and has resistance or invulnerability or medigun does not have enough charges.");
		}
		PopTransparent();
		FToggle(Vars::Aimbot::Healing::AutoSandvich);

		Subheader("Activation");
		FToggle(Vars::Aimbot::Healing::AutoVaccinator);
		FToggle(Vars::Aimbot::Healing::ActivateOnVoice);
		FToggle(Vars::Aimbot::Healing::ActivateFriendsOnly);
		FSlider(Vars::Aimbot::Healing::ActivationHealthPercent, FSliderEnum::None, Vars::Aimbot::Healing::ActivationHealthPercent[DEFAULT_BIND] <= 0.f ? "Off" : "%g%%");
	} EndSection();
	if (Vars::Debug::Options.Value)
	{
		if (Section("Debug##Support"))
		{
			if (DebugRow("Vaccinator scales"))
			{
				FSlider(Vars::Aimbot::Healing::AutoVaccinatorBulletScale);
				FSlider(Vars::Aimbot::Healing::AutoVaccinatorBlastScale);
				FSlider(Vars::Aimbot::Healing::AutoVaccinatorFireScale);
				FToggle(Vars::Aimbot::Healing::AutoVaccinatorFlamethrowerDamageOnly);

				EndPopup();
			}
		} EndSection();
	}

	TableNextColumn();
	if (Section("Engineer"))
	{
		FDropdown(Vars::Aimbot::AutoEngie::AutoRepair);
		FDropdown(Vars::Aimbot::AutoEngie::AutoUpgrade);
		FSlider(Vars::Aimbot::AutoEngie::AutoUpgradeSentryLVL);
		FSlider(Vars::Aimbot::AutoEngie::AutoUpgradeDispenserLVL);
		FSlider(Vars::Aimbot::AutoEngie::AutoUpgradeTeleporterLVL);
	} EndSection();
	if (Section("Zombie infection"))
	{
		FDropdown(Vars::Aimbot::Zombie::Abilities);
		FToggle(Vars::Aimbot::Zombie::AutoCast);
		FSlider(Vars::Aimbot::Zombie::MaxDistance, FSliderEnum::None, Vars::Aimbot::Zombie::MaxDistance[DEFAULT_BIND] <= 0.f ? "Auto" : "%g");
		FTooltip("Aims the Sniper spit, Spy EMP, Pyro spew and Heavy rock abilities on zi_ maps. Needs an aim type other than off. Auto = no distance limit besides what the projectile can reach.");
	} EndSection();
	if (Section("Passtime"))
	{
		FDropdown(Vars::Aimbot::Passtime::Mode);
		FDropdown(Vars::Aimbot::Passtime::PassPreference);
		FToggle(Vars::Aimbot::Passtime::AutoThrow);
		FDropdown(Vars::Aimbot::Passtime::Arc);
		FSlider(Vars::Aimbot::Passtime::MaxGoalDistance, FSliderEnum::None, Vars::Aimbot::Passtime::MaxGoalDistance[DEFAULT_BIND] <= 0.f ? "Auto" : "%g");
		FSlider(Vars::Aimbot::Passtime::PreferGoalDistance, FSliderEnum::None, Vars::Aimbot::Passtime::PreferGoalDistance[DEFAULT_BIND] <= 0.f ? "Auto" : "%g");
		FSlider(Vars::Aimbot::Passtime::MinPassGain, FSliderEnum::None, Vars::Aimbot::Passtime::MinPassGain[DEFAULT_BIND] <= 0.f ? "Auto" : "%g");
		FSlider(Vars::Aimbot::Passtime::PressureRange, FSliderEnum::None, Vars::Aimbot::Passtime::PressureRange[DEFAULT_BIND] <= 0.f ? "Auto" : "%g");
		FTooltip("Jack throws: shoots goals when a collision free arc exists, otherwise passes to a teammate the game can lock. Auto values come from the game: max goal distance = whatever the throw can reach, prefer goal within = the class' flat ground throw range (speed^2 / gravity), min pass gain = tf_passtime_ball_seek_range, pressure range = tf_passtime_pack_range.");
	} EndSection();

	EndTable();
}

static void ApplyCurrentTrajectory()
{
	auto pLocal = H::Entities.GetLocal();
	auto pWeapon = H::Entities.GetWeapon();
	if (!pLocal || !pWeapon)
		return;

	ProjectileInfo tProjInfo = {};
	bool bOriginal = Vars::Visuals::Trajectory::Override.Value;
	Vars::Visuals::Trajectory::Override.Value = false;
	bool bSetup = F::ProjSim.GetInfo(pLocal, pWeapon, {}, tProjInfo, ProjSimEnum::Interp)
		&& F::ProjSim.Initialize(tProjInfo, false);
	Vars::Visuals::Trajectory::Override.Value = bOriginal;
	if (!bSetup)
		return;

	Vec3 vLocalEye = pLocal->GetEyePosition();
	Vec3 vOffset = tProjInfo.m_vPos - vLocalEye; vOffset.y *= -1;
	float flForwardRedirect = 0.f, flForwardCutoff = 0.f;
	switch (tProjInfo.m_uType)
	{
	case FNV1A::Hash32Const("models/weapons/w_models/w_rocket.mdl"):
	case FNV1A::Hash32Const("models/weapons/w_models/w_drg_ball.mdl"):
	case FNV1A::Hash32Const("models/weapons/w_models/w_flaregun_shell.mdl"):
	case FNV1A::Hash32Const("models/weapons/w_models/w_arrow.mdl"):
	case FNV1A::Hash32Const("models/weapons/w_models/w_syringe_proj.mdl"):
	case FNV1A::Hash32Const("models/weapons/w_models/w_repair_claw.mdl"):
		flForwardRedirect = 2000.f, flForwardCutoff = 0.1f; break;
	case FNV1A::Hash32Const("models/weapons/c_models/c_flameball/c_flameball.mdl"):
	{
		static auto tf_fireball_distance = H::ConVars.FindVar("tf_fireball_distance");
		flForwardRedirect = tf_fireball_distance ? tf_fireball_distance->GetFloat() : 0.f, flForwardCutoff = 1.f;
	}
	}
	physics_performanceparams_t params = {}; F::ProjSim.m_pEnv->GetPerformanceSettings(&params);
	Vec3 vVelocity, vAngVelocity; F::ProjSim.m_pObj->GetVelocity(&vVelocity, &vAngVelocity);

	Vars::Visuals::Trajectory::OffsetX[DEFAULT_BIND] = vOffset.x;
	Vars::Visuals::Trajectory::OffsetY[DEFAULT_BIND] = vOffset.y;
	Vars::Visuals::Trajectory::OffsetZ[DEFAULT_BIND] = vOffset.z;
	Vars::Visuals::Trajectory::ForwardRedirect[DEFAULT_BIND] = flForwardRedirect;
	Vars::Visuals::Trajectory::ForwardCutoff[DEFAULT_BIND] = flForwardCutoff;
	Vars::Visuals::Trajectory::Hull[DEFAULT_BIND] = tProjInfo.m_vHull.x;
	Vars::Visuals::Trajectory::Speed[DEFAULT_BIND] = vVelocity.x;
	Vars::Visuals::Trajectory::Gravity[DEFAULT_BIND] = tProjInfo.m_flGravity;
	Vars::Visuals::Trajectory::LifeTime[DEFAULT_BIND] = tProjInfo.m_flLifetime;
	Vars::Visuals::Trajectory::UpVelocity[DEFAULT_BIND] = vVelocity.z;
	Vars::Visuals::Trajectory::AngularVelocityX[DEFAULT_BIND] = vAngVelocity.x;
	Vars::Visuals::Trajectory::AngularVelocityY[DEFAULT_BIND] = vAngVelocity.y;
	Vars::Visuals::Trajectory::AngularVelocityZ[DEFAULT_BIND] = vAngVelocity.z;
	Vars::Visuals::Trajectory::Drag[DEFAULT_BIND] = F::ProjSim.m_pObj->m_dragCoefficient;
	Vars::Visuals::Trajectory::DragX[DEFAULT_BIND] = F::ProjSim.m_pObj->m_dragBasis.x;
	Vars::Visuals::Trajectory::DragY[DEFAULT_BIND] = F::ProjSim.m_pObj->m_dragBasis.y;
	Vars::Visuals::Trajectory::DragZ[DEFAULT_BIND] = F::ProjSim.m_pObj->m_dragBasis.z;
	Vars::Visuals::Trajectory::AngularDragX[DEFAULT_BIND] = F::ProjSim.m_pObj->m_angDragBasis.x;
	Vars::Visuals::Trajectory::AngularDragY[DEFAULT_BIND] = F::ProjSim.m_pObj->m_angDragBasis.y;
	Vars::Visuals::Trajectory::AngularDragZ[DEFAULT_BIND] = F::ProjSim.m_pObj->m_angDragBasis.z;
	Vars::Visuals::Trajectory::MaxVelocity[DEFAULT_BIND] = params.maxVelocity;
	Vars::Visuals::Trajectory::MaxAngularVelocity[DEFAULT_BIND] = params.maxAngularVelocity;
}

static void PathColors(ConfigVar<Color_t>& tVisible, ConfigVar<Color_t>& tOccluded)
{
	FColorPicker(tVisible);
	FColorPicker(tOccluded);
}

static void AimbotDraw()
{
	if (!PageTable("AimbotDraw", 2))
		return;

	TableNextColumn();
	if (Section("Line"))
	{
		FToggle(Vars::Visuals::Line::TracersEnabled);
		PathColors(Vars::Colors::Line, Vars::Colors::LineIgnoreZ);
		FSlider(Vars::Visuals::Line::DrawDuration);
	} EndSection();
	if (Section("Hitbox"))
	{
		FDropdown(Vars::Visuals::Hitbox::BonesEnabled);
		FLabel("Target edge");
		PathColors(Vars::Colors::TargetHitboxEdge, Vars::Colors::TargetHitboxEdgeIgnoreZ);
		FLabel("Target face");
		PathColors(Vars::Colors::TargetHitboxFace, Vars::Colors::TargetHitboxFaceIgnoreZ);
		FLabel("Bone edge");
		PathColors(Vars::Colors::BoneHitboxEdge, Vars::Colors::BoneHitboxEdgeIgnoreZ);
		FLabel("Bone face");
		PathColors(Vars::Colors::BoneHitboxFace, Vars::Colors::BoneHitboxFaceIgnoreZ);

		Subheader("Bounds");
		FDropdown(Vars::Visuals::Hitbox::BoundsEnabled);
		FLabel("Bounds edge");
		PathColors(Vars::Colors::BoundHitboxEdge, Vars::Colors::BoundHitboxEdgeIgnoreZ);
		FLabel("Bounds face");
		PathColors(Vars::Colors::BoundHitboxFace, Vars::Colors::BoundHitboxFaceIgnoreZ);
		FSlider(Vars::Visuals::Hitbox::DrawDuration);
	} EndSection();
	if (Vars::Debug::Options.Value)
	{
		if (Section("Debug##Draw"))
		{
			if (DebugRow("Paths"))
			{
				FDropdown(Vars::Visuals::Prediction::RealPath, FDropdownEnum::None, -56);
				PathColors(Vars::Colors::RealPath, Vars::Colors::RealPathIgnoreZ);
				FSlider(Vars::Visuals::Path::SeparatorSpacing);
				FSlider(Vars::Visuals::Path::SeparatorLength);

				EndPopup();
			}
			if (DebugRow("Trajectory override"))
			{
				FToggle(Vars::Visuals::Trajectory::Override);
				bool bApply = FButton("Apply current");
				FSDropdown(Vars::Visuals::Trajectory::Type);
				FSlider(Vars::Visuals::Trajectory::OffsetX);
				FSlider(Vars::Visuals::Trajectory::OffsetY);
				FSlider(Vars::Visuals::Trajectory::OffsetZ);
				FSlider(Vars::Visuals::Trajectory::ForwardRedirect);
				FSlider(Vars::Visuals::Trajectory::ForwardCutoff);
				FSlider(Vars::Visuals::Trajectory::Hull);
				FSlider(Vars::Visuals::Trajectory::Speed);
				FSlider(Vars::Visuals::Trajectory::Gravity);
				FSlider(Vars::Visuals::Trajectory::LifeTime);
				FSlider(Vars::Visuals::Trajectory::UpVelocity);
				FSlider(Vars::Visuals::Trajectory::AngularVelocityX);
				FSlider(Vars::Visuals::Trajectory::AngularVelocityY);
				FSlider(Vars::Visuals::Trajectory::AngularVelocityZ);
				FSlider(Vars::Visuals::Trajectory::Drag);
				FSlider(Vars::Visuals::Trajectory::DragX);
				FSlider(Vars::Visuals::Trajectory::DragY);
				FSlider(Vars::Visuals::Trajectory::DragZ);
				FSlider(Vars::Visuals::Trajectory::AngularDragX);
				FSlider(Vars::Visuals::Trajectory::AngularDragY);
				FSlider(Vars::Visuals::Trajectory::AngularDragZ);
				FSlider(Vars::Visuals::Trajectory::MaxVelocity);
				FSlider(Vars::Visuals::Trajectory::MaxAngularVelocity);
				if (bApply)
					ApplyCurrentTrajectory();

				EndPopup();
			}
		} EndSection();
	}

	TableNextColumn();
	if (Section("Prediction"))
	{
		FDropdown(Vars::Visuals::Prediction::PlayerPath, FDropdownEnum::None, -56);
		PathColors(Vars::Colors::PlayerPath, Vars::Colors::PlayerPathIgnoreZ);
		FDropdown(Vars::Visuals::Prediction::ProjectilePath, FDropdownEnum::None, -56);
		PathColors(Vars::Colors::ProjectilePath, Vars::Colors::ProjectilePathIgnoreZ);
		FDropdown(Vars::Visuals::Prediction::BestPath, FDropdownEnum::None, -56);
		PathColors(Vars::Colors::BestPath, Vars::Colors::BestPathIgnoreZ);
		PushTransparent(!Vars::Visuals::Prediction::BestPath.Value);
		{
			FToggle(Vars::Visuals::Prediction::BestPathPreview);
		}
		PopTransparent();
		FToggle(Vars::Visuals::Prediction::SwingLines);
		FSlider(Vars::Visuals::Prediction::PlayerDrawDuration, FSliderEnum::None, !Vars::Visuals::Prediction::PlayerDrawDuration[DEFAULT_BIND] ? "timed" : "%g");
		FSlider(Vars::Visuals::Prediction::ProjectileDrawDuration, FSliderEnum::None, !Vars::Visuals::Prediction::ProjectileDrawDuration[DEFAULT_BIND] ? "timed" : "%g");

		Subheader("Aim position");
		FToggle(Vars::Visuals::Prediction::BestAimPos);
		FColorPicker(Vars::Colors::AimPosColor);
		PushTransparent(!Vars::Visuals::Prediction::BestAimPos.Value);
		{
			FDropdown(Vars::Visuals::Prediction::AimPosStyle);
			FSlider(Vars::Visuals::Prediction::AimPosSize);
			FSlider(Vars::Visuals::Prediction::AimPosThickness);
			FToggle(Vars::Visuals::Prediction::AimPosOutline);
		}
		PopTransparent();
	} EndSection();
	if (Section("Simulation"))
	{
		FDropdown(Vars::Visuals::Simulation::TrajectoryPath, FDropdownEnum::None, -56);
		PathColors(Vars::Colors::TrajectoryPath, Vars::Colors::TrajectoryPathIgnoreZ);
		FDropdown(Vars::Visuals::Simulation::ShotPath, FDropdownEnum::None, -56);
		PathColors(Vars::Colors::ShotPath, Vars::Colors::ShotPathIgnoreZ);
		FDropdown(Vars::Visuals::Simulation::SplashRadius, FDropdownEnum::None, -56);
		PathColors(Vars::Colors::SplashRadius, Vars::Colors::SplashRadiusIgnoreZ);
		FToggle(Vars::Visuals::Simulation::ProjectileCamera);
		FToggle(Vars::Visuals::Simulation::Box);
	} EndSection();

	EndTable();
}

static void VisualsView()
{
	if (!PageTable("VisualsView", 2))
		return;

	TableNextColumn();
	if (Section("Interface"))
	{
		FDropdown(Vars::Visuals::UI::StreamerMode);
		FDropdown(Vars::Visuals::UI::ChatTags, FDropdownEnum::None, -28);
		FColorPicker(Vars::Colors::Local);
		FToggle(Vars::Visuals::UI::RevealScoreboard);
		FToggle(Vars::Visuals::UI::ScoreboardUtility);
		FToggle(Vars::Visuals::UI::ScoreboardColors);
		FToggle(Vars::Visuals::UI::CleanScreenshots);
		FToggle(Vars::Visuals::AntiObs::Enabled);
		FToggle(Vars::Visuals::Other::KillstreakWeapons);
	} EndSection();
	if (Section("Camera"))
	{
		PushTransparent(!Vars::Visuals::UI::FieldOfView.Value);
		{
			FSlider(Vars::Visuals::UI::FieldOfView);
		}
		PopTransparent();
		PushTransparent(!Vars::Visuals::UI::ZoomFieldOfView.Value);
		{
			FSlider(Vars::Visuals::UI::ZoomFieldOfView);
		}
		PopTransparent();
		PushTransparent(!Vars::Visuals::UI::AspectRatio.Value);
		{
			FSlider(Vars::Visuals::UI::AspectRatio);
		}
		PopTransparent();
	} EndSection();

	TableNextColumn();
	if (Section("Thirdperson"))
	{
		FToggle(Vars::Visuals::Thirdperson::Enabled);
		FToggle(Vars::Visuals::Thirdperson::Crosshair);
		FToggle(Vars::Visuals::Thirdperson::Collision);
		FToggle(Vars::Visuals::Thirdperson::AutoZombie);
		FSlider(Vars::Visuals::Thirdperson::Distance);
		FSlider(Vars::Visuals::Thirdperson::Right);
		FSlider(Vars::Visuals::Thirdperson::Up);
		if (Vars::Debug::Options.Value)
			FToggle(Vars::Visuals::Thirdperson::Scale);
	} EndSection();
	if (Section("Viewmodel"))
	{
		FToggle(Vars::Visuals::Viewmodel::CrosshairAim);
		FToggle(Vars::Visuals::Viewmodel::ViewmodelAim);
		FSlider(Vars::Visuals::Viewmodel::OffsetX);
		FSlider(Vars::Visuals::Viewmodel::OffsetY);
		FSlider(Vars::Visuals::Viewmodel::OffsetZ);
		FSlider(Vars::Visuals::Viewmodel::Pitch);
		FSlider(Vars::Visuals::Viewmodel::Yaw);
		FSlider(Vars::Visuals::Viewmodel::Roll);
		PushTransparent(!Vars::Visuals::Viewmodel::SwayScale.Value || !Vars::Visuals::Viewmodel::SwayInterp.Value);
		{
			FSlider(Vars::Visuals::Viewmodel::SwayScale);
			FSlider(Vars::Visuals::Viewmodel::SwayInterp);
		}
		PopTransparent();
	} EndSection();

	EndTable();
}

static void VisualsWorld()
{
	if (!PageTable("VisualsWorld", 2))
		return;

	using namespace Vars::Visuals::World;
	auto fModulation = [](ConfigVar<Color_t>& tColor, int iFlag, int iFlags)
	{
		PushTransparent(!(Modulations.Value & iFlag));
		FColorPicker(tColor, iFlags);
		PopTransparent();
	};
	auto fTrigger = [](ConfigVar<Color_t>& tColor, int iFlag, int iFlags)
	{
		PushTransparent(!(ShowTriggers.Value & iFlag));
		FColorPicker(tColor, iFlags);
		PopTransparent();
	};

	TableNextColumn();
	if (Section("World"))
	{
		FSDropdown(WorldTexture);
		FSDropdown(SkyboxChanger);
		FToggle(NearPropFade);
		FToggle(NoPropFade);

		Subheader("Modulation");
		FDropdown(Modulations);
		fModulation(Vars::Colors::WorldModulation, ModulationsEnum::World, FColorPickerEnum::Left);
		fModulation(Vars::Colors::SkyModulation, ModulationsEnum::Sky, FColorPickerEnum::Right);
		fModulation(Vars::Colors::PropModulation, ModulationsEnum::Prop, FColorPickerEnum::Left);
		fModulation(Vars::Colors::ParticleModulation, ModulationsEnum::Particle, FColorPickerEnum::Right);
		fModulation(Vars::Colors::FogModulation, ModulationsEnum::Fog, FColorPickerEnum::Left);
	} EndSection();
	if (Section("Effects"))
	{
		FSDropdown(Vars::Visuals::Effects::BulletTracer);
		FSDropdown(Vars::Visuals::Effects::CritTracer);
		FSDropdown(Vars::Visuals::Effects::MedigunBeam);
		FSDropdown(Vars::Visuals::Effects::MedigunCharge);
		FSDropdown(Vars::Visuals::Effects::ProjectileTrail);
		FDropdown(Vars::Visuals::Effects::SpellFootsteps, FDropdownEnum::None, -28);
		FColorPicker(Vars::Colors::SpellFootstep, FColorPickerEnum::NoTooltip);
		FDropdown(Vars::Visuals::Effects::RagdollEffects);
		FToggle(Vars::Visuals::Effects::DrawIconsThroughWalls);
		FToggle(Vars::Visuals::Effects::DrawDamageNumbersThroughWalls);
	} EndSection();

	TableNextColumn();
	if (Section("Removals"))
	{
		FToggle(Vars::Visuals::Removals::Interpolation);
		PushTransparent(Vars::Visuals::Removals::Interpolation.Value);
		{
			FToggle(Vars::Visuals::Removals::Lerp);
		}
		PopTransparent();
		FToggle(Vars::Visuals::Removals::Disguises);
		FToggle(Vars::Visuals::Removals::Taunts);
		FToggle(Vars::Visuals::Removals::Scope);
		FToggle(Vars::Visuals::Removals::PostProcessing);
		FToggle(Vars::Visuals::Removals::ScreenOverlays);
		FToggle(Vars::Visuals::Removals::ScreenEffects);
		FToggle(Vars::Visuals::Removals::ViewPunch);
		FToggle(Vars::Visuals::Removals::AngleForcing);
		FToggle(Vars::Visuals::Removals::Ragdolls);
		FToggle(Vars::Visuals::Removals::Gibs);
		FToggle(Vars::Visuals::Removals::MOTD);
	} EndSection();
	if (Section("Triggers"))
	{
		FDropdown(ShowTriggers);
		fTrigger(Vars::Colors::HurtTrigger, ShowTriggersEnum::Hurt, FColorPickerEnum::Left);
		fTrigger(Vars::Colors::IgniteTrigger, ShowTriggersEnum::Ignite, FColorPickerEnum::Right);
		fTrigger(Vars::Colors::PushTrigger, ShowTriggersEnum::Push, FColorPickerEnum::Left);
		fTrigger(Vars::Colors::RegenerateTrigger, ShowTriggersEnum::Regenerate, FColorPickerEnum::Right);
		fTrigger(Vars::Colors::RespawnRoomTrigger, ShowTriggersEnum::RespawnRoom, FColorPickerEnum::Left);
		fTrigger(Vars::Colors::CaptureAreaTrigger, ShowTriggersEnum::CaptureArea, FColorPickerEnum::Right);
		fTrigger(Vars::Colors::CatapultTrigger, ShowTriggersEnum::Catapult, FColorPickerEnum::Left);
		fTrigger(Vars::Colors::ApplyImpulseTrigger, ShowTriggersEnum::ApplyImpulse, FColorPickerEnum::Right);
		fTrigger(Vars::Colors::TriggerAngle, ShowTriggersEnum::ShowAngles, FColorPickerEnum::Left);
		fTrigger(Vars::Colors::TriggerSurfaceCenter, ShowTriggersEnum::ShowSurfaceCenters, FColorPickerEnum::Right);
	} EndSection();

	EndTable();
}

static void VisualsMenu()
{
	if (!PageTable("VisualsMenu", 2))
		return;

	TableNextColumn();
	if (Section("Theme"))
	{
		FColorPicker(Vars::Menu::Theme::Accent, FColorPickerEnum::Full);
		FColorPicker(Vars::Menu::Theme::Background, FColorPickerEnum::Full);
		FColorPicker(Vars::Menu::Theme::Active, FColorPickerEnum::Full);
		FColorPicker(Vars::Menu::Theme::Inactive, FColorPickerEnum::Full);
		FSlider(Vars::Menu::Theme::Opacity);
	} EndSection();
	if (Section("Menu"))
	{
		FSDropdown(Vars::Menu::CheatTitle);
		FSDropdown(Vars::Menu::CheatTag);
		FSlider(Vars::Menu::Scale);
		FKeybind(Vars::Menu::PrimaryKey, FButtonEnum::None, { Vars::Menu::SecondaryKey[DEFAULT_BIND], VK_LBUTTON, VK_RBUTTON });
		FKeybind(Vars::Menu::SecondaryKey, FButtonEnum::None, { Vars::Menu::PrimaryKey[DEFAULT_BIND], VK_LBUTTON, VK_RBUTTON });
	} EndSection();

	TableNextColumn();
	if (Section("Indicators"))
	{
		FDropdown(Vars::Menu::Indicators);
		if (FSlider(Vars::Menu::IndicatorScale))
			H::Fonts.Reload();
		if (FToggle(Vars::Menu::CheapText))
			H::Fonts.Reload();
	} EndSection();
	if (Vars::Debug::Options.Value)
	{
		if (Section("Debug##Menu"))
		{
			FontPicker();
			if (DebugRow("Indicator colors"))
			{
				FColorPicker(Vars::Colors::IndicatorGood, FColorPickerEnum::Left);
				FColorPicker(Vars::Colors::IndicatorTextGood, FColorPickerEnum::Right);
				FColorPicker(Vars::Colors::IndicatorBad, FColorPickerEnum::Left);
				FColorPicker(Vars::Colors::IndicatorTextBad, FColorPickerEnum::Right);
				FColorPicker(Vars::Colors::IndicatorMid, FColorPickerEnum::Left);
				FColorPicker(Vars::Colors::IndicatorTextMid, FColorPickerEnum::Right);
				FColorPicker(Vars::Colors::IndicatorMisc, FColorPickerEnum::Left);
				FColorPicker(Vars::Colors::IndicatorTextMisc, FColorPickerEnum::Right);

				EndPopup();
			}
		} EndSection();
	}

	EndTable();
}

static void HvHTicks()
{
	if (!PageTable("HvHTicks", 2))
		return;

	TableNextColumn();
	if (Section("Doubletap"))
	{
		PushDisabled(F::AntiCheatCompatibility.Active());
		{
			FToggle(Vars::Doubletap::Doubletap);
			FToggle(Vars::Doubletap::Warp);
			FToggle(Vars::Doubletap::RechargeTicks);
			FToggle(Vars::Doubletap::AntiWarp);
			FSlider(Vars::Doubletap::TickLimit);
			FSlider(Vars::Doubletap::WarpRate);
			FSlider(Vars::Doubletap::RechargeLimit);
			FSlider(Vars::Doubletap::PassiveRecharge);
		}
		PopDisabled();
	} EndSection();
	if (Section("Auto peek"))
	{
		FToggle(Vars::AutoPeek::Enabled);
	} EndSection();

	TableNextColumn();
	if (Section("Fakelag"))
	{
		PushDisabled(F::AntiCheatCompatibility.Active());
		{
			FDropdown(Vars::Fakelag::Fakelag);
			FDropdown(Vars::Fakelag::Options);
			PushTransparent(Vars::Fakelag::Fakelag.Value != Vars::Fakelag::FakelagEnum::Plain);
			{
				FSlider(Vars::Fakelag::PlainTicks);
			}
			PopTransparent();
			PushTransparent(Vars::Fakelag::Fakelag.Value != Vars::Fakelag::FakelagEnum::Random);
			{
				FSlider(Vars::Fakelag::RandomTicks);
			}
			PopTransparent();
			FToggle(Vars::Fakelag::UnchokeOnAttack);
			FToggle(Vars::Fakelag::RetainBlastJump);
		}
		PopDisabled();
	} EndSection();

	EndTable();
}

static void HvHAngles()
{
	if (!PageTable("HvHAngles", 2))
		return;

	TableNextColumn();
	if (Section("Antiaim"))
	{
		using namespace Vars::AntiAim;
		const auto fUsesValue = [](int iYaw) { return iYaw == YawEnum::Edge || iYaw == YawEnum::Jitter; };
		const auto fSpins = [](int iYaw) { return iYaw == YawEnum::Spin || iYaw == YawEnum::Tornado || iYaw == YawEnum::Helix; };

		PushDisabled(F::AntiCheatCompatibility.Active());
		{
			FToggle(Enabled);

			Subheader("Real");
			FDropdown(PitchReal);
			FDropdown(YawReal);
			FDropdown(RealYawBase);
			FSlider(RealYawOffset);
			PushTransparent(!fUsesValue(YawReal.Value));
			{
				FSlider(RealYawValue);
			}
			PopTransparent();

			Subheader("Fake");
			FDropdown(PitchFake);
			FDropdown(YawFake);
			FDropdown(FakeYawBase);
			FSlider(FakeYawOffset);
			PushTransparent(!fUsesValue(YawFake.Value));
			{
				FSlider(FakeYawValue);
			}
			PopTransparent();

			Subheader("Other");
			PushTransparent(!fSpins(YawFake.Value) && !fSpins(YawReal.Value));
			{
				FSlider(SpinSpeed);
			}
			PopTransparent();
			FToggle(MinWalk);
			FToggle(AntiOverlap);
			FToggle(HidePitchOnShot);
		}
		PopDisabled();
		FToggle(TauntSpin);
		if (Vars::Debug::Options.Value)
			FToggle(AntiAimLines);
	} EndSection();

	TableNextColumn();
	if (Section("Resolver"))
	{
		FToggle(Vars::Resolver::Enabled);
		PushTransparent(!Vars::Resolver::Enabled.Value);
		{
			FToggle(Vars::Resolver::AutoResolve);
			PushTransparent(Transparent || !Vars::Resolver::AutoResolve.Value);
			{
				FToggle(Vars::Resolver::AutoResolveCheatersOnly);
				FToggle(Vars::Resolver::AutoResolveHeadshotOnly);
				PushTransparent(Transparent || !Vars::Resolver::AutoResolveYawAmount.Value);
				{
					FSlider(Vars::Resolver::AutoResolveYawAmount);
				}
				PopTransparent();
				PushTransparent(Transparent || !Vars::Resolver::AutoResolvePitchAmount.Value);
				{
					FSlider(Vars::Resolver::AutoResolvePitchAmount);
				}
				PopTransparent();
			}
			PopTransparent();

			Subheader("Cycle");
			FSlider(Vars::Resolver::CycleYaw);
			FSlider(Vars::Resolver::CyclePitch);
			FToggle(Vars::Resolver::CycleView);
			FToggle(Vars::Resolver::CycleMinwalk);
		}
		PopTransparent();
	} EndSection();

	EndTable();
}

static void MiscMain()
{
	if (!PageTable("MiscMain", 2))
		return;

	TableNextColumn();
	if (Section("Movement"))
	{
		using namespace Vars::Misc::Movement;
		FDropdown(AutoStrafe);
		PushTransparent(AutoStrafe.Value != AutoStrafeEnum::Directional);
		{
			FSlider(AutoStrafeTurnScale);
			FSlider(AutoStrafeMaxDelta);
		}
		PopTransparent();
		FDropdown(AutoEdgebug, FDropdownEnum::None, -28);
		FColorPicker(Vars::Colors::EdgebugPath);

		Subheader("Jumping");
		FToggle(Bunnyhop);
		FToggle(EdgeJump);
		FToggle(AutoJumpbug);
		FToggle(BreakJump);
		FToggle(AutoRocketJump);
		FToggle(AutoCTap);
		FToggle(AutoFaNJump);
		FToggle(AutoRevJump);

		Subheader("Control");
		FToggle(FastStop);
		PushDisabled(F::AntiCheatCompatibility.Active());
		{
			FToggle(FastAccelerate);
			FToggle(DuckSpeed);
		}
		PopDisabled();
		FToggle(MovementLock);
		FToggle(ShieldTurnRate);
		FToggle(NoPush);
	} EndSection();
	if (Vars::Debug::Options.Value)
	{
		if (Section("Debug##Movement"))
		{
			using namespace Vars::Misc::Movement;
			if (DebugRow("Rocket jump"))
			{
				FSlider(AutoRocketJumpChokeGrounded);
				FSlider(AutoRocketJumpChokeAir);
				FSlider(AutoRocketJumpSkipGround);
				FSlider(AutoRocketJumpSkipAir);
				FSlider(AutoRocketJumpTimingOffset);
				FSlider(AutoRocketJumpApplyAbove);

				EndPopup();
			}
			if (DebugRow("FaN jump"))
			{
				FSlider(AutoFaNJumpOnSolidTicks);
				FToggle(AutoFaNJumpCheckCeiling);

				EndPopup();
			}
			if (DebugRow("Edgebug"))
			{
				FSlider(AutoEdgebugStrafeSamples);
				FSlider(AutoEdgebugStrafeMaxDelta);
				FToggle(AutoEdgebugTryNegativeDir);
				FToggle(AutoEdgebugTryRandomMove);

				EndPopup();
			}
		} EndSection();
	}

	TableNextColumn();
	if (Section("Exploits"))
	{
		FToggle(Vars::Misc::Exploits::PureBypass);
		FToggle(Vars::Misc::Exploits::CheatsBypass);
		FToggle(Vars::Misc::Exploits::UnlockCVars);
		FToggle(Vars::Misc::Exploits::EquipRegionUnlock);
		FToggle(Vars::Misc::Exploits::BackpackExpander);
		FToggle(Vars::Misc::Exploits::BreakShootSound);
		FTooltip("breaks weapon shoot sound by switching weapons (soldier only)");
		FToggleSlider(Vars::Misc::Exploits::PingReducer, Vars::Misc::Exploits::PingTarget);
	} EndSection();
	if (Section("Game"))
	{
		FToggle(Vars::Misc::Game::NetworkFix);
		FToggle(Vars::Misc::Game::SetupBonesOptimization);
		FToggle(Vars::Misc::Game::InsecureBypass);
		FToggle(Vars::Misc::Game::AntiCheatCompatibility);
		if (Vars::Debug::Options.Value)
		{
			FToggle(Vars::Misc::Game::AntiCheatCritHack);
			FDropdown(Vars::Misc::TelemetryBlocker::Mode);
		}
	} EndSection();
	if (Section("Sound"))
	{
		FDropdown(Vars::Misc::Sound::Block);
		FToggle(Vars::Misc::Sound::HitsoundAlways);
		FToggle(Vars::Misc::Sound::RemoveDSP);
		FToggle(Vars::Misc::Sound::GiantWeaponSounds);
	} EndSection();

	EndTable();
}

static void MiscAutomation()
{
	if (!PageTable("MiscAutomation", 2))
		return;

	TableNextColumn();
	if (Section("Automation"))
	{
		using namespace Vars::Misc::Automation;
		FDropdown(AntiBackstab);
		FToggle(AcceptItemDrops);
		FToggle(AntiAFK);
		FToggle(AntiAutobalance);
		FToggleSlider(AutoRetry, AutoRetryHealth);
		FToggle(KartControl);
		FToggle(AutoReport);
		FDropdown(AutoDisguise);
		PushTransparent(AutoDisguise.Value != AutoDisguiseEnum::Random);
		{
			FDropdown(AutoDisguiseClasses);
		}
		PopTransparent();
	} EndSection();
	if (Section("Voting"))
	{
		using namespace Vars::Misc::Automation;
		FDropdown(AutoVote);
		PushTransparent(!(AutoVote.Value & AutoVoteEnum::Defend) && !(AutoVote.Value & AutoVoteEnum::Assist));
		{
			FToggle(AutoVoteDelay);
			PushTransparent(Transparent || !AutoVoteDelay.Value);
			{
				FSlider(AutoVoteDelayInterval);
			}
			PopTransparent();
		}
		PopTransparent();
		FToggleSlider(AutoVoteMap, AutoVoteMapOption);
	} EndSection();
	if (Section("Taunts"))
	{
		using namespace Vars::Misc::Automation;
		FToggle(TauntControl);
		FToggleSlider(AutoTaunt, AutoTauntChance);
		PushTransparent(!AutoTaunt.Value);
		{
			FSlider(AutoTauntSlot, FSliderEnum::None, AutoTauntSlot.Value == 0 ? "weapon" : "%i");
		}
		PopTransparent();
	} EndSection();

	TableNextColumn();
	if (Section("Chat"))
	{
		using namespace Vars::Misc::Automation;
		FToggleSlider(ChatSpam::Enable, ChatSpam::Interval, FToggleEnum::None, FSliderEnum::Clamp);
		PushTransparent(!ChatSpam::Enable.Value);
		{
			FToggle(ChatSpam::TeamChat);
			FToggle(ChatSpam::Randomize);
		}
		PopTransparent();
		FToggle(ChatSpam::AutoReply);
		FToggle(ChatSpam::VoteKickReply);
		FToggleSlider(KillSay::Enable, KillSay::Chance);
		PushTransparent(!KillSay::Enable.Value);
		{
			FToggle(KillSay::TeamChat);
		}
		PopTransparent();

		Subheader("Spam");
		FDropdown(VoiceCommandSpam);
		FToggle(Micspam);
		FToggle(NoiseSpam);
		FToggle(CallVoteSpam);
		FToggle(AchievementSpam);
		PushTransparent(!AchievementSpam.Value);
		{
			FDropdown(AchievementSpamID, GetAchievementSpamDropdownEntries(), GetAchievementSpamDropdownIDs());
		}
		PopTransparent();
	} EndSection();
	if (Section("Mann vs. Machine"))
	{
		using namespace Vars::Misc::MannVsMachine;
		FToggle(InstantRespawn);
		FToggle(InstantRevive);
		FToggle(AllowInspect);
		FToggle(AutoMvmReadyUp);
		FToggle(AutoAbandonMannUp);
		FToggleSlider(BuyBot, MaxCash);
		FTooltip("WARNING: Works only on Mann Up missions with enough starting cash (600$) before the 1st wave!\nRequirements:\n1. Be a Vaccinator Medic\n2. Ping must be below 80ms\n3. Walk to the upgrade station\nPerforms MVM upgrade station exploit for extra cash.");
		FToggle(BuyBotAutoClass);
		PushTransparent(!BuyBotAutoClass.Value);
		{
			FDropdown(BuyBotClass, { "Scout", "Soldier", "Pyro", "Demoman", "Heavy", "Engineer", "Sniper", "Spy" }, { 1, 3, 7, 4, 6, 9, 2, 8 });
		}
		PopTransparent();
		FDropdown(ChatCommands::Mode);
		FTooltip("Allows running cat_mvm_* commands through party or in-game chat.\nOff - disabled\nParty - party members can use them\nFriends - only friends can use them\nCustom tag - only players with the tag below can use them");
		PushTransparent(ChatCommands::Mode.Value != ChatCommands::ModeEnum::CustomTag);
		{
			std::vector<const char*> vEntries = { "None" };
			std::vector<int> vValues = { -1 };
			for (int i = 0; i < F::PlayerUtils.m_vTags.size(); i++)
			{
				if (!F::PlayerUtils.m_vTags[i].m_bAssignable)
					continue;

				vEntries.push_back(F::PlayerUtils.m_vTags[i].m_sName.c_str());
				vValues.push_back(i);
			}
			FDropdown(ChatCommands::Tag, vEntries, vValues);
		}
		PopTransparent();
	} EndSection();

	EndTable();
}

static void MiscBots()
{
	if (!PageTable("MiscBots", 2))
		return;

	TableNextColumn();
	if (Section("Nav engine"))
	{
		using namespace Vars::Misc::Movement;
		FToggle(NavEngine::Enabled);
		FToggle(NavEngine::PathInSetup);
		FToggle(NavBot::SmartJump);
		PushTransparent(!NavEngine::Enabled.Value);
		{
			FDropdown(NavEngine::Draw, FDropdownEnum::Multi);
			FColorPicker(Vars::Colors::NavbotPath, FColorPickerEnum::Full);
			FColorPicker(Vars::Colors::NavbotArea, FColorPickerEnum::Left);
			FColorPicker(Vars::Colors::NavbotBlacklist, FColorPickerEnum::Right);

			Subheader("Stuck");
			FSlider(NavEngine::StickyIgnoreTime);
			FSlider(NavEngine::StuckDetectTime);
			FSlider(NavEngine::StuckBlacklistTime);
			FSlider(NavEngine::StuckExpireTime);
			FSlider(NavEngine::StuckTime);

			Subheader("Visibility");
			FToggle(NavEngine::VischeckEnabled);
			FSlider(NavEngine::VischeckTime);
			FSlider(NavEngine::VischeckCacheTime);
			FToggle(NavEngine::HullCheck);
		}
		PopTransparent();
	} EndSection();
	if (Section("Navbot"))
	{
		using namespace Vars::Misc::Movement;
		PushTransparent(!NavEngine::Enabled.Value);
		{
			FToggle(NavBot::Enabled);
			PushTransparent(!NavBot::Enabled.Value || !NavEngine::Enabled.Value);
			{
				FDropdown(NavBot::RechargeDT);
				PushTransparent(Transparent || !NavBot::RechargeDT.Value);
				{
					FSlider(NavBot::RechargeDTDelay);
				}
				PopTransparent();
				FDropdown(NavBot::Preferences);
				FDropdown(NavBot::Personality);
				FDropdown(NavBot::Blacklist);
				PushTransparent(Transparent || !(NavBot::Blacklist.Value & NavBot::BlacklistEnum::NormalThreats));
				{
					FSlider(NavBot::BlacklistDelay);
				}
				PopTransparent();
				PushTransparent(Transparent || !(NavBot::Blacklist.Value & NavBot::BlacklistEnum::DormantThreats));
				{
					FSlider(NavBot::BlacklistDormantDelay);
				}
				PopTransparent();
				PushTransparent(Transparent || !(NavBot::Blacklist.Value & NavBot::BlacklistEnum::NormalThreats) && !(NavBot::Blacklist.Value & NavBot::BlacklistEnum::DormantThreats));
				{
					FSlider(NavBot::BlacklistSlightDangerLimit);
				}
				PopTransparent();
				FSlider(NavBot::MeleeTargetRange);
				FToggleSlider(NavBot::DangerOverlay, NavBot::DangerOverlayMaxDist);
			}
			PopTransparent();
		}
		PopTransparent();
	} EndSection();
	if (Vars::Debug::Options.Value)
	{
		if (Section("Debug##Bots"))
		{
			if (DebugRow("Bot utils"))
			{
				FToggle(Vars::Misc::Movement::BotUtils::AutoScopeUseCachedResults);
				FTooltip("should increase performance of autoscope by only checking every 2nd tick");

				EndPopup();
			}
			if (DebugRow("Navbot"))
			{
				FSlider(Vars::Misc::Movement::NavBot::StickyDangerRange);
				FSlider(Vars::Misc::Movement::NavBot::ProjectileDangerRange);
				FToggle(Vars::Misc::Movement::NavEngine::OffPathRepath);

				EndPopup();
			}
		} EndSection();
	}

	TableNextColumn();
	if (Section("Followbot"))
	{
		using namespace Vars::Misc::Movement::FollowBot;
		FToggle(Enabled);
		PushTransparent(!Vars::Misc::Movement::NavEngine::Enabled.Value);
		{
			FDropdown(UseNav);
			FTooltip("Use nav engine when unable to reach current target.\nNormal - runs in case the target is NOT dormant\nNormal + Dormant - runs regardless of dormancy\nNOTE:\n'+ Dormant' does not make the followbot target players across the whole map. \nIt only prevents followbot from losing current target in case it goes dormant");
		}
		PopTransparent();
		FDropdown(Targets);
		FDropdown(Prefer);
		FDropdown(LookAtPath);
		PushTransparent(!LookAtPath.Value);
		{
			FDropdown(LookAtPathMode);
			FTooltip("Look at path mode:\nPath - look at current path node.\nCopy - use saved target viewangles.\nCopy immediate - use current target viewangles.\nAt target - look at current target.");
			FToggle(LookAtPathNoSnap);
		}
		PopTransparent();

		Subheader("Distances");
		FSlider(MaxNodes);
		FTooltip("Allowed amount of path nodes.\nExceeding that abandons the target.");
		FSlider(MinPriority);
		FTooltip("Minimal priority at which followbot starts to target a player.\nOverrides playerlist follow priority minimum allowing to change which targets followbot selects depending on this config");
		FSlider(ActivationDistance);
		FTooltip("Distance at which followbot starts to follow.\nDoesn't affect anything with nav being active.");
		FSlider(FollowDistance);
		FTooltip("Distance to target which followbot tries to maintain.");
		FSlider(AbandonDistance);
		FTooltip("Distance at which followbot abandons the target (will try to use nav instead).");
		PushTransparent(!UseNav.Value);
		{
			FSlider(NavAbandonDistance);
			FTooltip("Distance at which followbot abandons the target completely.");
		}
		PopTransparent();
		FToggle(DrawPath);
		PathColors(Vars::Colors::FollowbotPathLine, Vars::Colors::FollowbotPathBox);
	} EndSection();
	if (Section("Bot utils"))
	{
		using namespace Vars::Misc::Movement;
		PushTransparent(!NavEngine::Enabled.Value);
		{
			FDropdown(NavEngine::LookAtPath);
		}
		PopTransparent();
		PushTransparent(!NavEngine::LookAtPath.Value && !FollowBot::LookAtPath.Value);
		{
			FSlider(BotUtils::LookAtPathSpeed);
			FTooltip("Specifies how smooth the viewangles will change when using 'Look at path' in nav engine or followbot");
			FToggle(BotUtils::LookAtPathDebug);
		}
		PopTransparent();
		FDropdown(Vars::Misc::Automation::ForceClass, { "Off", "Scout", "Soldier", "Pyro", "Demoman", "Heavy", "Engineer", "Medic", "Sniper", "Spy" }, { 0, 1, 3, 7, 4, 6, 9, 5, 2, 8 });
		FToggle(Vars::Misc::Automation::RandomClass);
		PushTransparent(!Vars::Misc::Automation::RandomClass.Value);
		{
			FDropdown(Vars::Misc::Automation::RandomClassExclude, FDropdownEnum::Multi);
			FSlider(Vars::Misc::Automation::RandomClassInterval);
		}
		PopTransparent();
		FDropdown(BotUtils::WeaponSlot);
		FDropdown(BotUtils::AutoScope);
		PushTransparent(!BotUtils::AutoScope.Value);
		{
			FSlider(BotUtils::AutoScopeCancelTime);
		}
		PopTransparent();
		FToggle(BotUtils::AutoHeatmakerFocus);
	} EndSection();
	if (Section("Auto item"))
	{
		using namespace Vars::Misc::Automation::AutoItem;
		FDropdown(Enable);
		FTooltip("Allows you to automatically rent and craft items, very useful for bots.");
		FSlider(Interval);
		FSDropdown(Primary);
		FSDropdown(Secondary);
		FSDropdown(Melee);
		FSDropdown(FirstHat);
		FSDropdown(SecondHat);
		FSDropdown(ThirdHat);
	} EndSection();

	EndTable();
}

static void MiscQueueing()
{
	if (!PageTable("MiscQueueing", 2))
		return;

	using namespace Vars::Misc::Queueing;
	TableNextColumn();
	if (Section("Matchmaking"))
	{
		FToggle(AutoCasualQueue);
		FToggle(AutoCasualJoin);
		FToggle(AutoCompetitiveQueue);
		FToggle(AutoMannUpQueue);
		FSlider(QueueDelay);
		F::MvmQueue.Refresh();
		PushTransparent(!AutoMannUpQueue.Value);
		{
			FDropdown(MannUpTourIndex, F::MvmQueue.m_tTourDropdown.m_vEntries, {}, FDropdownEnum::NoSanitization);
			FToggle(MannUpUncompleted);
		}
		PopTransparent();
		PushTransparent(!AutoBootCampQueue.Value || F::MvmQueue.m_tBootcampDropdown.m_vEntries.empty());
		{
			FDropdown(BootcampMissionBits, F::MvmQueue.m_tBootcampDropdown.m_vEntries, {}, FDropdownEnum::Multi | FDropdownEnum::NoSanitization);
		}
		PopTransparent();
		PushTransparent(!AutoMannUpQueue.Value);
		{
			FToggle(MannUpUncompleted);
		}
		PopTransparent();
	} EndSection();
	if (Section("Casual automation"))
	{
		PushTransparent(!AutoCasualQueue.Value);
		{
			FToggle(AutoAbandonIfNoNavmesh);
			FToggleSlider(AutoDumpProfiles, AutoDumpDelay);
		}
		PopTransparent();
	} EndSection();
	if (Section("Regions"))
	{
		dropdown_with_group_headers(ForceRegions, FDropdownEnum::None, 0,
		{
			{ 0, "North America", ForceRegionsEnum::ATL | ForceRegionsEnum::ORD | ForceRegionsEnum::DFW | ForceRegionsEnum::LAX | ForceRegionsEnum::SEA | ForceRegionsEnum::IAD },
			{ 6, "Europe", ForceRegionsEnum::AMS | ForceRegionsEnum::FSN | ForceRegionsEnum::FRA | ForceRegionsEnum::HEL | ForceRegionsEnum::LHR | ForceRegionsEnum::MAD | ForceRegionsEnum::PAR | ForceRegionsEnum::STO | ForceRegionsEnum::VIE | ForceRegionsEnum::WAW },
			{ 16, "South America", ForceRegionsEnum::EZE | ForceRegionsEnum::LIM | ForceRegionsEnum::SCL | ForceRegionsEnum::GRU },
			{ 20, "Asia", ForceRegionsEnum::MAA | ForceRegionsEnum::DXB | ForceRegionsEnum::HKG | ForceRegionsEnum::BOM | ForceRegionsEnum::SEO | ForceRegionsEnum::SGP | ForceRegionsEnum::TYO },
			{ 27, "Australia", ForceRegionsEnum::SYD },
			{ 28, "Africa", ForceRegionsEnum::JNB }
		});
		FDropdown(ExtendQueue);
	} EndSection();

	TableNextColumn();
	if (Section("Requeue"))
	{
		FToggle(RQif);
		PushTransparent(!RQif.Value);
		{
			FSlider(RQplt);
			FSlider(RQpgt);
			FToggle(RQkick);
			FToggle(RQnoAbandon);
			FToggle(RQIgnoreFriends);
			FToggle(RQLTM);
		}
		PopTransparent();
	} EndSection();
#ifdef MENU_PROFILE_STALKER
	if (Section("Profile stalker"))
	{
		FToggle(StalkerEnable);
		PushTransparent(!StalkerEnable.Value);
		{
			FSlider(StalkerInterval);
			for (auto& sLine : F::ProfileStalker.GetStatusLines())
				FLabel(sLine.c_str());
			if (FButton("Reload"))
				F::ProfileStalker.ForceReload();
		}
		PopTransparent();
	} EndSection();
#endif

	EndTable();
}

static void AnticheatDetection()
{
	if (!PageTable("AnticheatDetection", 2))
		return;

	using namespace Vars::CheatDetection;
	TableNextColumn();
	if (Section("Detection"))
	{
		FDropdown(Methods);
		PushTransparent(!DetectionsRequired.Value);
		{
			FSlider(DetectionsRequired);
		}
		PopTransparent();
		Subheader("Packet choking");
		PushTransparent(!(Methods.Value & MethodsEnum::PacketChoking));
		{
			FSlider(MinChoking);
		}
		PopTransparent();
		Subheader("Crit manipulation");
		PushTransparent(!(Methods.Value & MethodsEnum::CritManipulation));
		{
			FSlider(CritWindow);
			FSlider(CritThreshold);
		}
		PopTransparent();
	} EndSection();

	TableNextColumn();
	if (Section("Aim flicking"))
	{
		PushTransparent(!(Methods.Value & MethodsEnum::AimFlicking));
		{
			FSlider(MinFlick);
			FSlider(MaxNoise);
		}
		PopTransparent();
	} EndSection();
	if (Section("Lag compensation"))
	{
		PushTransparent(!(Methods.Value & MethodsEnum::LagCompAbuse));
		{
			FSlider(LagCompMinimumDelta);
			FSlider(LagCompBurstCount);
			FSlider(LagCompWindow);
		}
		PopTransparent();
	} EndSection();

	EndTable();
}

static void PlayersLogging()
{
	if (!PageTable("PlayersLogging", 2))
		return;

	using namespace Vars::Logging;
	TableNextColumn();
	if (Section("Logging"))
	{
		FDropdown(Logs);
		FDropdown(NotificationPosition);
		FSlider(NotificationTime);
		FSlider(MaxNotifications);
	} EndSection();

	TableNextColumn();
	if (Section("Destinations"))
	{
		auto fLogTo = [](auto& tVar, int iFlag)
		{
			PushTransparent(!(Logs.Value & iFlag));
			{
				FDropdown(tVar);
			}
			PopTransparent();
		};
		fLogTo(VoteStart::LogTo, LogsEnum::VoteStart);
		fLogTo(VoteCast::LogTo, LogsEnum::VoteCast);
		fLogTo(ClassChange::LogTo, LogsEnum::ClassChanges);
		fLogTo(Damage::LogTo, LogsEnum::Damage);
		fLogTo(CheatDetection::LogTo, LogsEnum::CheatDetection);
		fLogTo(Tags::LogTo, LogsEnum::Tags);
		fLogTo(Aliases::LogTo, LogsEnum::Aliases);
		fLogTo(Resolver::LogTo, LogsEnum::Resolver);
	} EndSection();

	EndTable();
}

bool Pages::Draw(int iPage, int iSub)
{
	switch (iPage)
	{
	case MenuPageAimbot:
		F::AntiCheatCompatibility.EnforceSettings();
		switch (iSub)
		{
		case 0: AimbotGeneral(); return true;
		case 1: AimbotWeapons(); return true;
		case 2: AimbotSupport(); return true;
		case 3: AimbotDraw(); return true;
		}
		break;
	case MenuPageVisuals:
		switch (iSub)
		{
		case 1: VisualsView(); return true;
		case 2: VisualsWorld(); return true;
		case 3: VisualsMenu(); return true;
		}
		break;
	case MenuPageHvH:
		F::AntiCheatCompatibility.EnforceSettings();
		switch (iSub)
		{
		case 0: HvHTicks(); return true;
		case 1: HvHAngles(); return true;
		}
		break;
	case MenuPageMisc:
		switch (iSub)
		{
		case 0: MiscMain(); return true;
		case 1: MiscAutomation(); return true;
		case 2: MiscBots(); return true;
		case 3: MiscQueueing(); return true;
		}
		break;
	case MenuPageAnticheat:
		if (iSub == 1)
		{
			AnticheatDetection();
			return true;
		}
		break;
	case MenuPagePlayers:
		if (iSub == 3)
		{
			PlayersLogging();
			return true;
		}
		break;
	}
	return false;
}

void Pages::Search(const std::string& sSearch)
{
	if (sSearch.empty())
		return;

	static std::vector<BaseVar*> vVars = {};
	static uint32_t uStaticHash = 0;
	if (const uint32_t uCurrHash = FNV1A::Hash32(sSearch.c_str()); uCurrHash != uStaticHash)
	{
		std::string sSearch2 = sSearch;
		std::transform(sSearch2.begin(), sSearch2.end(), sSearch2.begin(), ::tolower);

		vVars.clear();
		for (auto& pBase : G::Vars)
		{
			if (!Vars::Debug::Options[DEFAULT_BIND] && pBase->m_iFlags & DEBUGVAR)
				continue;

			std::vector<const char*> vSearch = { pBase->Name(), pBase->Section() };
			vSearch.insert(vSearch.end(), pBase->m_vNames.begin(), pBase->m_vNames.end());
			vSearch.insert(vSearch.end(), pBase->m_vValues.begin(), pBase->m_vValues.end());
			for (auto pSearch : vSearch)
			{
				std::string sSearch3 = pSearch;
				if (auto iFind = sSearch3.find("Vars::"); iFind != std::string::npos)
					sSearch3 = sSearch3.replace(iFind, strlen("Vars::"), "");
				if (auto iFind = sSearch3.find("##"); iFind != std::string::npos)
					sSearch3 = sSearch3.replace(iFind, strlen("##"), "");
				std::transform(sSearch3.begin(), sSearch3.end(), sSearch3.begin(), ::tolower);
				if (sSearch3.find(sSearch2) != std::string::npos)
				{
					vVars.push_back(pBase);
					break;
				}
			}
		}

		uStaticHash = uCurrHash;
	}

	if (vVars.empty())
	{
		const ImVec2 vTextSize = CalcTextSize("No results");
		GetWindowDrawList()->AddText(GetWindowPos() + ImVec2(floorf((GetWindowWidth() - vTextSize.x) * 0.5f), floorf(GetWindowHeight() * 0.4f)), FadeColor(F::Render.Inactive), "No results");
		return;
	}

	struct SearchGroup_t
	{
		std::string m_sLabel;
		std::vector<BaseVar*> m_vVars;
	};
	std::vector<SearchGroup_t> vGroups = {};
	for (auto pBase : vVars)
	{
		if (vGroups.empty() || FNV1A::Hash32(vGroups.back().m_vVars.front()->Section()) != FNV1A::Hash32(pBase->Section()))
			vGroups.push_back({ std::format("{}## {}", pBase->Section(), pBase->Name()), {} });
		vGroups.back().m_vVars.push_back(pBase);
	}

	std::vector<int> vColumns(vGroups.size());
	{
		float vHeights[2] = {};
		for (size_t i = 0; i < vGroups.size(); i++)
		{
			const int iColumn = vHeights[1] < vHeights[0] ? 1 : 0;
			vColumns[i] = iColumn;
			vHeights[iColumn] += GetSectionHeight(vGroups[i].m_sLabel.c_str(), H::Draw.Scale(36 + 34 * vGroups[i].m_vVars.size()));
		}
	}

	auto fDrawVar = [&](BaseVar* pBase)
	{
		if (auto pVar = pBase->As<bool>())
		{
			if (FToggle(*pVar) && FNV1A::Hash32(pVar->Name()) == FNV1A::Hash32Const("Vars::Debug::Options"))
				uStaticHash = 0;
		}
		else if (auto pVar = pBase->As<int>())
		{
			if (!pVar->m_vValues.empty() || FNV1A::Hash32(pVar->Name()) == FNV1A::Hash32Const("Vars::ESP::ActiveGroups"))
				FDropdown(*pVar);
			else if (pVar->m_sExtra)
			{
				const char* sFormat = nullptr;
				if (FNV1A::Hash32(pVar->Name()) == FNV1A::Hash32Const("Vars::Aimbot::Projectile::SplashAirCount") && !pVar->Map[DEFAULT_BIND])
					sFormat = "random";
				FSlider(*pVar, FSliderEnum::None, sFormat);
			}
			else
			{
				std::vector<int> vIgnore;
				switch (FNV1A::Hash32(pVar->Name()))
				{
				case FNV1A::Hash32Const("Vars::Menu::PrimaryKey"): vIgnore = { Vars::Menu::SecondaryKey[DEFAULT_BIND], VK_LBUTTON, VK_RBUTTON }; break;
				case FNV1A::Hash32Const("Vars::Menu::SecondaryKey"): vIgnore = { Vars::Menu::PrimaryKey[DEFAULT_BIND], VK_LBUTTON, VK_RBUTTON }; break;
				default: vIgnore = { Vars::Menu::PrimaryKey[DEFAULT_BIND], Vars::Menu::SecondaryKey[DEFAULT_BIND] };
				}
				FKeybind(pVar->m_vNames.front(), pVar->Map[DEFAULT_BIND], FKeybindEnum::None, vIgnore);
			}
		}
		else if (auto pVar = pBase->As<float>())
		{
			const char* sFormat = nullptr;
			switch (FNV1A::Hash32(pVar->Name()))
			{
			case FNV1A::Hash32Const("Vars::Visuals::Prediction::PlayerDrawDuration"):
			case FNV1A::Hash32Const("Vars::Visuals::Prediction::ProjectileDrawDuration"):
				if (!pVar->Map[DEFAULT_BIND])
					sFormat = "timed";
				break;
			case FNV1A::Hash32Const("Vars::Aimbot::Projectile::SplashRotateX"):
			case FNV1A::Hash32Const("Vars::Aimbot::Projectile::SplashRotateY"):
				if (pVar->Map[DEFAULT_BIND] < 0.f)
					sFormat = "random";
			}
			FSlider(*pVar, FSliderEnum::None, sFormat);
		}
		else if (auto pVar = pBase->As<IntRange_t>())
			FSlider(*pVar);
		else if (auto pVar = pBase->As<FloatRange_t>())
			FSlider(*pVar);
		else if (auto pVar = pBase->As<std::string>())
			FSDropdown(*pVar);
		else if (auto pVar = pBase->As<std::vector<std::pair<std::string, ChamsMaterial_t>>>())
			FMDropdown(*pVar);
		else if (auto pVar = pBase->As<Color_t>())
			FColorPicker(*pVar, FColorPickerEnum::Full);
		else if (auto pVar = pBase->As<Gradient_t>())
		{
			FColorPicker(*pVar, true, FColorPickerEnum::Left);
			FColorPicker(*pVar, false, FColorPickerEnum::Right);
		}
	};

	if (!PageTable("SearchTable", 2))
		return;

	const int iColumns = TableGetColumnCount();
	for (int iColumn = 0; iColumn < iColumns; iColumn++)
	{
		TableNextColumn();
		for (size_t i = 0; i < vGroups.size(); i++)
		{
			if (iColumns > 1 && vColumns[i] != iColumn)
				continue;

			if (Section(vGroups[i].m_sLabel.c_str()))
			{
				for (auto pBase : vGroups[i].m_vVars)
					fDrawVar(pBase);
			} EndSection();
		}
		if (iColumns == 1)
			break;
	}

	EndTable();
}

#endif
