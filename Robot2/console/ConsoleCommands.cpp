#pragma once

#include "ConsoleCommands.h"
#include "../Environment.h"
#include <filesystem>

ConsoleCommands::ConsoleCommands(): env(nullptr)
{
}

bool ConsoleCommands::Execute(const std::string& function, const std::string& params)
{
    if (!this->env) {
        printf("Env not set for ConsoleCommands!\n");
        return false;
    }

    if (function == "touch") {
        bool enable = !(params=="0" || params == "disable" || params == "off");
        this->env->SetAutomatedInputsEnabled(enable);
        return true;
    }
    else if (function == "load") {
        for (const std::string& ext : { "", ".cfg", ".txt", "config.txt" })
        {
            std::string fileName = params + ext;
            if (std::filesystem::exists(fileName))
            {
                env->LoadConfig(fileName);
                return true;
            }
        }
    }
    else if (function == "play") {
        this->env->EnableWorker(true);
        return true;
    }
    else if (function == "pause") {
        this->env->EnableWorker(false);
        return true;
    }
    return false;
}
