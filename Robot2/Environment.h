#pragma once

#include "Window.h"
#include "Config.h"
#include "Worker.h"
#include "scrcpy/scrcpy.h"

class Environment {
    Window* window;
    Config* config;
    const char* serial;
    std::unique_ptr<Config> loadedConfig;

    Worker worker;
    scrcpyOptions scrcpy;

    Screen* screen;
    InputManager* inputManager;
public:
    Environment(Window& window, Config& config, const char* serial);
    void Run();
    void UpdateConfig(Config& config);
    Window* GetWindow() { return window; }

    void SetAutomatedInputsEnabled(bool enabled);
    void LoadConfig(const std::string& configFile);
};