#include "NoSpreadHitscan.h"

#include "../SpreadMath.h"
#include "../../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include "../../ImGui/IndicatorPanel.h"
#include <regex>
#include <numeric>

MAKE_SIGNATURE(CTFWeaponBaseGun_GetWeaponSpread, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 4C 63 91", 0x0);

static_assert(offsetof(IMatchGroupDescription, m_bFixedWeaponSpread) == 0x70);

void CNoSpreadHitscan::Reset()
{
	m_bWaitingForPlayerPerf = false;
	m_flServerTime = 0.f;
	m_vTimeDeltas.clear();
	m_dTimeDelta = 0.0;

	m_iSeed = 0;
	m_flMantissaStep = 0.f;

	m_bSynced = false;
}

bool CNoSpreadHitscan::ShouldRun(CTFWeaponBase* pWeapon)
{
	if (pWeapon
		? G::PrimaryWeaponType != EWeaponType::HITSCAN || pWeapon->GetWeaponSpread() <= 0.f
		: !Vars::Aimbot::General::NoSpread.Value || !I::EngineClient->IsInGame())
		return false;

	if (F::AntiCheatCompatibility.Active())
		return false;

	return true;
}

int CNoSpreadHitscan::GetSeed(CUserCmd* pCmd)
{
	static auto sv_usercmd_custom_random_seed = H::ConVars.FindVar("sv_usercmd_custom_random_seed");
	if (sv_usercmd_custom_random_seed && !sv_usercmd_custom_random_seed->GetBool())
		return pCmd->random_seed & 255;

	double dFloatTime = SDK::PlatFloatTime() + m_dTimeDelta;
	float flTime = float(dFloatTime * 1000.0);
	return *reinterpret_cast<int*>((char*)&flTime) & 255;
}

float CNoSpreadHitscan::CalcMantissaStep(float flV)
{
	float flMs = float(double(flV) * 1000.0);
	return std::nextafter(flMs, std::numeric_limits<float>::infinity()) - flMs;
}

bool CNoSpreadHitscan::IsFixedSpread(CTFWeaponBase* pWeapon)
{
	if (pWeapon->GetBulletsPerShot(false) <= 1 || !(pWeapon->GetDamageType() & DMG_BUCKSHOT))
		return false;

	static auto tf_use_fixed_weaponspreads = H::ConVars.FindVar("tf_use_fixed_weaponspreads");
	bool bFixed = tf_use_fixed_weaponspreads && tf_use_fixed_weaponspreads->GetBool();
	if (auto pGameRules = I::TFGameRules())
	{
		if (auto pMatchDesc = pGameRules->GetMatchGroupDescription())
			bFixed = pMatchDesc->m_bFixedWeaponSpread;
	}
	return bFixed || int(SDK::AttribHookValue(0.f, "fixed_shot_pattern", pWeapon));
}

std::string CNoSpreadHitscan::GetFormat(int iServerTime)
{
	int iDays = iServerTime / 86400;
	int iHours = iServerTime / 3600 % 24;
	int iMinutes = iServerTime / 60 % 60;
	int iSeconds = iServerTime % 60;

	if (iDays)
		return std::format("{}d {}h", iDays, iHours);
	else if (iHours)
		return std::format("{}h {}m", iHours, iMinutes);
	else
		return std::format("{}m {}s", iMinutes, iSeconds);
}

void CNoSpreadHitscan::AskForPlayerPerf()
{
	if (!ShouldRun())
		return Reset();

	if (G::Choking)
		return;

	static Timer tTimer = {};
	if (!m_bWaitingForPlayerPerf ? tTimer.Run(Vars::Aimbot::General::NoSpreadInterval.Value) : tTimer.Run(Vars::Aimbot::General::NoSpreadBackupInterval.Value))
	{
		I::ClientState->SendStringCmd("playerperf");
		m_bWaitingForPlayerPerf = true;
		m_dRequestTime = SDK::PlatFloatTime();
	}
}

bool CNoSpreadHitscan::ParsePlayerPerf(const std::string& sMsg)
{
	if (!ShouldRun())
		return false;

	std::smatch tMatch; std::regex_match(sMsg, tMatch, std::regex(R"((\d+.\d+)\s\d+\s\d+\s\d+.\d+\s\d+.\d+\svel\s\d+.\d+)"));

	if (tMatch.size() == 2)
	{
		m_bWaitingForPlayerPerf = false;

		float flNewServerTime = std::stof(tMatch[1].str());
		if (flNewServerTime < m_flServerTime)
			return true;

		bool bLoopback = SDK::IsLoopback();
		m_flServerTime = flNewServerTime;

		if (bLoopback)
			m_dTimeDelta = 0.f;
		else
		{
			m_vTimeDeltas.push_back(m_flServerTime - m_dRequestTime + TICKS_TO_TIME(1));
			while (!m_vTimeDeltas.empty() && m_vTimeDeltas.size() > Vars::Aimbot::General::NoSpreadAverage.Value)
				m_vTimeDeltas.pop_front();
			m_dTimeDelta = std::reduce(m_vTimeDeltas.begin(), m_vTimeDeltas.end()) / m_vTimeDeltas.size();
		}
		m_dTimeDelta += TICKS_TO_TIME(Vars::Aimbot::General::NoSpreadOffset.Value);

		float flMantissaStep = CalcMantissaStep(m_flServerTime);
		m_bSynced = flMantissaStep >= 1.f || bLoopback;

		if (flMantissaStep > m_flMantissaStep && (m_bSynced || !m_flMantissaStep))
		{
			SDK::Output("Seed Prediction", m_bSynced ? std::format("Synced ({})", m_dTimeDelta).c_str() : "Not synced, step too low", Vars::Menu::Theme::Accent.Value);
			SDK::Output("Seed Prediction", std::format("Age {}; Step {}", GetFormat(m_flServerTime), flMantissaStep).c_str(), Vars::Menu::Theme::Accent.Value);
		}
		m_flMantissaStep = flMantissaStep;

		return true;
	}

	return std::regex_match(sMsg, std::regex(R"(\d+.\d+\s\d+\s\d+)"));
}

void CNoSpreadHitscan::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	m_iPredictionBullet = -1;
	if (!ShouldRun(pWeapon))
		return;

	m_iSeed = GetSeed(pCmd);
#ifdef SEEDPRED_DEBUG
	SDK::Output("CNoSpreadHitscan::Run", std::format("{}: {}", SDK::PlatFloatTime() + m_dTimeDelta, m_iSeed).c_str(), { 255, 0, 0 });
#endif

	const int iBullets = pWeapon->GetBulletsPerShot();
	if (iBullets <= 0)
		return;

	const bool bFixed = IsFixedSpread(pWeapon);
	if (!bFixed && !m_bSynced)
		return;

	SpreadMath::Shot_t tShot = { m_iSeed, pWeapon->GetWeaponSpread(), iBullets, bFixed };
	if (!bFixed && TICKS_TO_TIME(pLocal->m_nTickBase()) - pWeapon->m_flLastFireTime() > (iBullets > 1 ? 0.25f : 1.25f))
	{
		tShot.m_flFirstRange = SDK::AttribHookValue(0.f, "mult_spread_scale_first_shot", pWeapon);
		tShot.m_bFirstPerfect = !tShot.m_flFirstRange;
	}

	const Vec3 vPunch = pLocal->m_vecPunchAngle();
	const Vec3 vShotAngles = pCmd->viewangles + vPunch;

	auto vPellets = SpreadMath::GetPellets(tShot);
	m_iPredictionBullet = SpreadMath::ChoosePellet(vPellets, vShotAngles, tShot.m_bFirstPerfect);

	const auto& tPellet = vPellets[m_iPredictionBullet];
	if (!tPellet.m_flRight && !tPellet.m_flUp)
		return;

	Vec3 vForward; Math::AngleVectors(vShotAngles, &vForward);
	Vec3 vAngles = SpreadMath::SolveAngles(vForward, tPellet, vShotAngles.z, vShotAngles.x) - vPunch;
	SDK::FixMovement(pCmd, vAngles);
	pCmd->viewangles = vAngles;
	Math::ClampAngles(pCmd->viewangles);

	G::SilentAngles = true;
}

void CNoSpreadHitscan::CacheDrawInfo(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::SeedPrediction)
		|| !pLocal || !ShouldRun() || !pLocal->IsAlive())
	{
		m_tDrawCache.Set({});
		return;
	}

	m_tDrawCache.Set({
		std::format("Uptime {}", GetFormat(m_flServerTime)),
		std::format("Mantissa step {}", m_flMantissaStep),
		std::format("Delta {:.3f}", m_dTimeDelta),
		m_bSynced ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value,
		true
	});
}

void CNoSpreadHitscan::Draw()
{
	static IndicatorAnim_t tAnim;
	static DrawCache_t s_tLastValidCache;

	const DrawCache_t tCache = m_tDrawCache.Get();
	if (tCache.m_bValid)
		s_tLastValidCache = tCache;

	const bool bEnabled = (Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::SeedPrediction) != 0;
	const bool bVisible = bEnabled && I::EngineClient->IsInGame() && tCache.m_bValid;

	bool bOpening = true;
	const float flAnim = tAnim.Update(bVisible, bOpening);
	if (!tAnim.ShouldDraw(bVisible))
		return;

	const auto& tDraw = tCache.m_bValid ? tCache : s_tLastValidCache;

	int x = Vars::Menu::SeedPredictionDisplay.Value.x;
	int y = Vars::Menu::SeedPredictionDisplay.Value.y + 8;
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = fFont.m_nTall + H::Draw.Scale(1);
	ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();

	EAlign align = ALIGN_TOP;
	if (x <= 100 + H::Draw.Scale(50, Scale_Round))
	{
		x -= H::Draw.Scale(42, Scale_Round);
		align = ALIGN_TOPLEFT;
	}
	else if (x >= H::Draw.m_nScreenW - 100 - H::Draw.Scale(50, Scale_Round))
	{
		x += H::Draw.Scale(42, Scale_Round);
		align = ALIGN_TOPRIGHT;
	}

	const int iVtxStart = pDrawList->VtxBuffer.Size;
	const int iCmdStart = pDrawList->CmdBuffer.Size;

	DrawIndicatorText(pDrawList, x, y, tDraw.m_tColor, Vars::Menu::Theme::Background.Value, align, tDraw.m_sUptime);
	DrawIndicatorText(pDrawList, x, y += nTall, tDraw.m_tColor, Vars::Menu::Theme::Background.Value, align, tDraw.m_sMantissaStep);
	if (Vars::Debug::Info.Value)
		DrawIndicatorText(pDrawList, x, y += nTall, tDraw.m_tColor, Vars::Menu::Theme::Background.Value, align, tDraw.m_sDelta);

	TransformIndicatorVertices(pDrawList, iVtxStart, pDrawList->VtxBuffer.Size, iCmdStart, pDrawList->CmdBuffer.Size, flAnim, bOpening, ImGui::GetIO().DisplaySize);
}
