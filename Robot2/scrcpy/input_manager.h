#ifndef INPUTMANAGER_H
#define INPUTMANAGER_H

#include <SDL2/SDL.h>
#include <functional>

#include "config.h"
#include "common.h"
#include "controller.h"
#include "util/fps_counter.h"
#include "screen.h"
#include "video_buffer.h"

class scrcpyOptions;

class InputManager {
public:
    Controller* controller;
    VideoBuffer* video_buffer;
    Screen* screen;

    // SDL reports repeated events as a boolean, but Android expects the actual
    // number of repetitions. This variable keeps track of the count.
    unsigned repeat;

    bool control;
    bool forward_key_repeat;
    bool prefer_text;
    bool forward_all_clicks;
    bool legacy_paste;

    struct {
        unsigned data[SC_MAX_SHORTCUT_MODS];
        unsigned count;
    } sdl_shortcut_mods;

    bool vfinger_down;
    bool allowAutomatedInputs;
    std::function<void(int)> onScreenshot;

    InputManager(const scrcpyOptions* options, Controller* controller, VideoBuffer* video_buff, Screen* screen);
    void ProcessTextInput(const SDL_TextInputEvent* event);
    void ProcessKey(const SDL_KeyboardEvent* event);
    void ProcessMouseMotion(const SDL_MouseMotionEvent* event);
    void ProcessTouch(const SDL_TouchFingerEvent* event);
    void ProcessMouseButton(const SDL_MouseButtonEvent* event);
    void ProcessMouseWheel(const SDL_MouseWheelEvent* event);

    bool IsShortcutMod(uint16_t sdl_mod);
    bool SimulateVirtualFinger(enum android_motionevent_action action, struct point point);

    std::function<void(int, int, bool)> GetTouchFunc();
    void SetAutomatedInputsEnabled(bool enabled);
};


#endif
