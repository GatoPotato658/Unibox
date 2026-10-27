#pragma once
#include "../../SDK/SDK.h"

#include <functional>

using CommandCallback = const std::function<void(std::deque<const char*>&)>;

class CCommands
{
public:
    bool Run(const char* sCmd, std::deque<const char*>& vArgs);
    void RunChat(const std::string& sMsg, uint32_t uAccountID, bool bPartyChat);
};

ADD_FEATURE(CCommands, Commands);