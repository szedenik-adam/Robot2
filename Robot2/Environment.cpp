#include "Environment.h"
#pragma once

Environment::Environment(Window& window, Config& config, const char* serial):
	window(&window), config(&config), serial(serial), worker(config), scrcpy(this, this->serial), screen(nullptr), inputManager(nullptr)
{
    Worker& worker = this->worker;

    scrcpy.OnWindowCreation([&](Screen* s, struct size si) {
        this->screen = s;
        s->SetWorker(&worker);
        s->SetEnvironment(this);
        worker.UpdateResolution(si.width, si.height);
        worker.SetGrabImageFunct(s->GetGrabImageFunc());
        worker.Start();
        });
    scrcpy.OnInputManCreation([&worker, this](InputManager* im) {
        this->inputManager = im;
        worker.SetTouchFunct(im->GetTouchFunc());
        im->onScreenshot = [&worker](int keycode) {worker.TakeScreenshot(); };
        });
}

void Environment::SetAutomatedInputsEnabled(bool enabled)
{
    if (this->inputManager) { this->inputManager->SetAutomatedInputsEnabled(enabled); }
}

void Environment::LoadConfig(const std::string& configFile)
{
    std::unique_ptr<Config> newConfig = std::make_unique<Config>();
    newConfig->LoadConfig(configFile);
    this->UpdateConfig(*newConfig.get());
    std::swap(this->loadedConfig, newConfig);
}

void Environment::EnableWorker(bool enabled)
{
    if (enabled) this->worker.Start();
    else this->worker.Stop(false);
}

void Environment::Run()
{
    scrcpy.Run();
}

void Environment::UpdateConfig(Config& config)
{
    this->config = &config;
    if(this->screen) this->screen->SetWorker(nullptr);
    this->worker.SetGrabImageFunct(nullptr);
    if (this->inputManager) this->inputManager->onScreenshot = nullptr;

    this->worker.~Worker();
    Worker* w = new (&this->worker) Worker(config);
    Worker& worker = this->worker;

    if (this->screen) {
        this->screen->SetWorker(&worker);
        worker.UpdateResolution(this->screen->frame_size.width, this->screen->frame_size.height);
        worker.SetGrabImageFunct(this->screen->GetGrabImageFunc());
        worker.Start();
    }
    if (this->inputManager) {
        worker.SetTouchFunct(this->inputManager->GetTouchFunc());
        this->inputManager->onScreenshot = [&worker](int keycode) {worker.TakeScreenshot(); };
    }
}
