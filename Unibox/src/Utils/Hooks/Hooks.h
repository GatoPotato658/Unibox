#pragma once
#include "../Macros/Macros.h"
#include <MinHook/MinHook.h>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace HookActivity
{
	inline std::atomic<int> s_iActive = 0;

	inline void Enter()
	{
		s_iActive.fetch_add(1, std::memory_order_acq_rel);
	}

	inline void Exit()
	{
		s_iActive.fetch_sub(1, std::memory_order_acq_rel);
	}

	struct Scope_t
	{
		Scope_t() { Enter(); }
		~Scope_t() { Exit(); }
	};

	inline void Wait()
	{
		while (s_iActive.load(std::memory_order_acquire))
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

#define HOOK_ACTIVITY_CONCAT_IMPL(a, b) a##b
#define HOOK_ACTIVITY_CONCAT(a, b) HOOK_ACTIVITY_CONCAT_IMPL(a, b)
#define TRACK_HOOK HookActivity::Scope_t HOOK_ACTIVITY_CONCAT(tHookActivityScope, __COUNTER__)

class CHook
{
public:
	std::string m_sName;
	void* m_pOriginal = nullptr;
	void* m_pInitFunc = nullptr;

public:
	CHook(const std::string& sName, void* pInitFunc);

	inline MH_STATUS Create(void* pSrc, void* pDst)
	{
		m_pOriginal = nullptr;
		return MH_CreateHook(pSrc, pDst, &m_pOriginal);
	}

	template <typename T>
	inline T As() const
	{
		return reinterpret_cast<T>(m_pOriginal);
	}
};

#ifndef DEBUG_HOOKS
	#define MAKE_HOOK(name, address, type, ...) \
	namespace Hooks \
	{ \
		namespace name \
		{ \
			bool Init(); \
			inline CHook Hook(#name, Init); \
			using FN = type(__fastcall*)(__VA_ARGS__); \
			type __fastcall Func(__VA_ARGS__); \
		} \
	} \
	bool Hooks::name::Init() { if (const auto pAddress = reinterpret_cast<void*>(address); pAddress && Hook.Create(pAddress, Func) == MH_OK && Hook.m_pOriginal) return true; SDK::Output("unibox", std::format("Failed to initialize hook: {}", #name).c_str(), { 255, 150, 175, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG); return false;} \
	type __fastcall Hooks::name::Func(__VA_ARGS__)

	#define DEBUG_RETURN(...) TRACK_HOOK
#else
	#define DEBUG_VAR(name) \
	namespace Vars { \
		namespace Hooks { \
			inline ConfigVar<bool> name = { true, { #name }, "Vars::Hooks::"#name##"_", "Hooks", NOSAVE | DEBUGVAR }; \
		} \
	}

	#define MAKE_HOOK(name, address, type, ...) \
	DEBUG_VAR(name) \
	namespace Hooks \
	{ \
		namespace name \
		{ \
			bool Init(); \
			inline CHook Hook(#name, Init); \
			using FN = type(__fastcall*)(__VA_ARGS__); \
			type __fastcall Func(__VA_ARGS__); \
		} \
	} \
	bool Hooks::name::Init() { if (const auto pAddress = reinterpret_cast<void*>(address); pAddress && Hook.Create(pAddress, Func) == MH_OK && Hook.m_pOriginal) return true; SDK::Output("unibox", std::format("Failed to initialize hook: {}", #name).c_str(), { 255, 150, 175, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG); return false;} \
	type __fastcall Hooks::name::Func(__VA_ARGS__)

	#define DEBUG_RETURN(hook, ...) \
		TRACK_HOOK; \
		if (!Vars::Hooks::hook[DEFAULT_BIND]) \
			return CALL_ORIGINAL(__VA_ARGS__);
#endif

#define CALL_ORIGINAL Hook.As<FN>()

class CHooks
{
private:
	bool m_bFailed = false;
	bool m_bInitialized = false;

public:
	bool Initialize();
	bool Unload();
	bool IsInitialized() const { return m_bInitialized; }
	CHook* Get(const std::string& sName) const;
};

ADD_FEATURE_CUSTOM(CHooks, Hooks, U);
