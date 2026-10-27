#include "../SDK/SDK.h"

MAKE_SIGNATURE(CTFParty_GetMemberActivity, "client.dll", "48 8B C1 85 D2 78 ? 3B 51", 0x0);
MAKE_SIGNATURE(CTFPartyClient_GetPartyMemberStatus_call, "client.dll", "E8 ? ? ? ? 8B 93 ? ? ? ? 48 8B 5C 24", 0x0);
//MAKE_SIGNATURE(CSOTFPartyMember_CSOTFPartyMember, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 48 8D 05 ? ? ? ? 8B FA 48 89 01 48 8B D9 48 3B 0D ? ? ? ? 74 ? 48 8B 49 ? 48 85 C9 74 ? 48 8B 01 BA ? ? ? ? FF 10 48 8B 4B ? 48 85 C9 74 ? 48 8B 01 BA ? ? ? ? FF 10 48 8D 4B ? E8 ? ? ? ? 48 8B CB E8 ? ? ? ? 40 F6 C7 ? 74 ? BA ? ? ? ? 48 8B CB E8 ? ? ? ? 48 8B C3 48 8B 5C 24 ? 48 83 C4 ? 5F C3 CC CC CC CC CC CC 48 89 5C 24 ? 57 48 83 EC ? 48 8D 05 ? ? ? ? 48 8B F9 48 89 01 8B DA 48 83 C1", 0x0);
//MAKE_SIGNATURE(CSOTFPartyMember_Activity_CSOTFPartyMember_Activity, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 48 8D 05 ? ? ? ? 48 8B F9 48 89 01 8B DA 48 83 C1 ? E8 ? ? ? ? 48 8B CF E8 ? ? ? ? F6 C3 ? 74 ? BA ? ? ? ? 48 8B CF E8 ? ? ? ? 48 8B 5C 24 ? 48 8B C7 48 83 C4 ? 5F C3 CC CC CC CC CC CC 48 89 5C 24 ? 57 48 83 EC ? 48 8D 05 ? ? ? ? 48 8B F9 48 89 01 8B DA 48 83 C1 ? E8 ? ? ? ? 48 8B CF E8 ? ? ? ? F6 C3 ? 74 ? BA ? ? ? ? 48 8B CF E8 ? ? ? ? 48 8B 5C 24 ? 48 8B C7 48 83 C4 ? 5F C3 CC CC CC CC CC CC 48 89 5C 24 ? 57 48 83 EC ? 48 8D 05 ? ? ? ? 48 8B F9 48 89 01 8B DA 48 83 C1 ? E8 ? ? ? ? 48 8B CF E8 ? ? ? ? F6 C3 ? 74 ? BA ? ? ? ? 48 8B CF E8 ? ? ? ? 48 8B 5C 24 ? 48 8B C7 48 83 C4 ? 5F C3 CC CC CC CC CC CC 48 89 5C 24 ? 57 48 83 EC ? 48 8D 05 ? ? ? ? 48 8B F9 48 89 01 8B DA 48 83 C1 ? E8 ? ? ? ? 48 8B CF E8 ? ? ? ? F6 C3 ? 74 ? BA ? ? ? ? 48 8B CF E8 ? ? ? ? 48 8B 5C 24 ? 48 8B C7 48 83 C4 ? 5F C3 CC CC CC CC CC CC 48 89 5C 24 ? 57 48 83 EC ? 48 8D 05 ? ? ? ? 48 8B F9 48 89 01 8B DA 48 83 C1 ? E8 ? ? ? ? 48 8B CF E8 ? ? ? ? F6 C3 ? 74 ? BA ? ? ? ? 48 8B CF E8 ? ? ? ? 48 8B 5C 24 ? 48 8B C7 48 83 C4 ? 5F C3 CC CC CC CC CC CC 48 89 5C 24 ? 57 48 83 EC ? 48 8D 05 ? ? ? ? 48 8B F9 48 89 01 8B DA 48 8B 49 ? E8 ? ? ? ? 48 8D 4F ? E8 ? ? ? ? 48 8B CF E8 ? ? ? ? F6 C3 ? 74 ? BA ? ? ? ? 48 8B CF E8 ? ? ? ? 48 8B 5C 24 ? 48 8B C7 48 83 C4 ? 5F C3 CC CC CC CC CC CC CC CC CC CC CC CC CC 48 89 5C 24 ? 48 89 74 24", 0x0);

/*
class CSOTFPartyMember
{
	byte pad0[124];
};

class CSOTFPartyMember_activity
{
	byte pad0[135];
};
*/

MAKE_HOOK(CTFParty_GetMemberActivity, S::CTFParty_GetMemberActivity(), void*,
	void* rcx, int idx)
{
	void* pPartyMemberActivity = CALL_ORIGINAL( rcx, idx );
	if ( pPartyMemberActivity && reinterpret_cast< uintptr_t >( _ReturnAddress( ) ) != S::CTFPartyClient_GetPartyMemberStatus_call() + 5 )
	{
		//CSOTFPartyMember* pPartyMember = nullptr;
		//S::CSOTFPartyMember_CSOTFPartyMember.Call<void*>(pPartyMember, 1);
		//CSOTFPartyMember_activity* pPartyMemberActivity = nullptr;
		//S::CSOTFPartyMember_Activity_CSOTFPartyMember_Activity.Call<void*>(pPartyMemberActivity, 1);

		//*reinterpret_cast< byte* >( ( uintptr_t )pPartyMemberActivity + 36 ) = 1;
		*reinterpret_cast< byte* >( ( uintptr_t )pPartyMemberActivity + 37 ) = 1;

		//return pPartyMemberActivity;
	}
	return pPartyMemberActivity;
}
