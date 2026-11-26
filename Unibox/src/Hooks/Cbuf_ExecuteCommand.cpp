#include "../SDK/SDK.h"

#include "../Features/Commands/Commands.h"
#include "../Features/AntiCheatCompatibility/AntiCheatCompatibility.h"
#ifndef TEXTMODE
#include "../Features/Visuals/SkinChanger/SkinChanger.h"
#endif
//#include "../Features/Debug/Debug.h"


enum cmd_source_t
{
	src_client,
	src_command
};

MAKE_HOOK(Cbuf_ExecuteCommand, S::Cbuf_ExecuteCommand(), void,
	CCommand& args, cmd_source_t source)
{
    DEBUG_RETURN(Cbuf_ExecuteCommand, args, source);

	//F::Debug.AddText(args.GetCommandString(), I::GlobalVars->curtime + 1.f);

	if (args.ArgC())
	{
#ifndef TEXTMODE
		if (source == src_command && !_stricmp(args[0], "taunt") && args.ArgC() == 2 && F::SkinChanger.HandleFakeTauntCommand(atoi(args[1])))
			return;
		if (source == src_command && F::SkinChanger.IsFakeTaunting()
			&& (!_stricmp(args[0], "stop_taunt") || !_stricmp(args[0], "+taunt") || !_stricmp(args[0], "-taunt")
				|| (args.ArgC() == 1 && (!_stricmp(args[0], "taunt") || !_stricmp(args[0], "weapon_taunt")))))
		{
			F::SkinChanger.HandleFakeTauntCommand(-1);
			return;
		}
#endif

		if (F::AntiCheatCompatibility.Active())
		{
			if (std::string_view(args.GetCommandString()).find("playerperf") != std::string_view::npos)
				return;

			if ((_stricmp(args[0], "say") == 0 || _stricmp(args[0], "say_team") == 0)
				&& (strchr(args.GetCommandString(), '\n') || strchr(args.GetCommandString(), '\r')))
			{
				std::string sCmd = args.GetCommandString();
				CAntiCheatCompatibility::SanitizeString(sCmd);
				I::EngineClient->ClientCmd_Unrestricted(sCmd.c_str());
				return;
			}
		}

        const char* sCommand = args[0];
		std::deque<const char*> vArgs;
		for (int i = 1; i < args.ArgC(); i++)
			vArgs.push_back(args[i]);

        if (F::Commands.Run(sCommand, vArgs))
            return;
	}

    CALL_ORIGINAL(args, source);
}