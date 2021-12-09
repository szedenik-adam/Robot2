#include "input_manager.h"

#include <cassert>
#include <SDL2/SDL_keycode.h>

#include "config.h"
#include "event_converter.h"
#include "util/lock.h"
#include "util/log.h"
#include "scrcpy.h"

static const int ACTION_DOWN = 1;
static const int ACTION_UP = 1 << 1;

#define SC_SDL_SHORTCUT_MODS_MASK (KMOD_CTRL | KMOD_ALT | KMOD_GUI)

static inline uint16_t
to_sdl_mod(unsigned mod) {
    uint16_t sdl_mod = 0;
    if (mod & SC_MOD_LCTRL) {
        sdl_mod |= KMOD_LCTRL;
    }
    if (mod & SC_MOD_RCTRL) {
        sdl_mod |= KMOD_RCTRL;
    }
    if (mod & SC_MOD_LALT) {
        sdl_mod |= KMOD_LALT;
    }
    if (mod & SC_MOD_RALT) {
        sdl_mod |= KMOD_RALT;
    }
    if (mod & SC_MOD_LSUPER) {
        sdl_mod |= KMOD_LGUI;
    }
    if (mod & SC_MOD_RSUPER) {
        sdl_mod |= KMOD_RGUI;
    }
    return sdl_mod;
}

bool InputManager::IsShortcutMod(uint16_t sdl_mod) {
    // keep only the relevant modifier keys
    sdl_mod &= SC_SDL_SHORTCUT_MODS_MASK;

    assert(this->sdl_shortcut_mods.count);
    assert(this->sdl_shortcut_mods.count < SC_MAX_SHORTCUT_MODS);
    for (unsigned i = 0; i < this->sdl_shortcut_mods.count; ++i) {
        if (this->sdl_shortcut_mods.data[i] == sdl_mod) {
            return true;
        }
    }

    return false;
}

InputManager::InputManager(const scrcpyOptions* options, Controller* controller, VideoBuffer* video_buff, Screen* screen)
    : controller(controller), video_buffer(video_buff), screen(screen),
    repeat(0), prefer_text(false), sdl_shortcut_mods({ .data = {0}, .count = 0 }),
    vfinger_down(false), allowAutomatedInputs(true), onScreenshot(nullptr)
{
    this->control = options->control;
    this->forward_key_repeat = options->forward_key_repeat;
    this->prefer_text = options->prefer_text;
    this->forward_all_clicks = options->forward_all_clicks;
    this->legacy_paste = options->legacy_paste;

    const struct sc_shortcut_mods* shortcut_mods = &options->shortcut_mods;
    assert(shortcut_mods->count);
    assert(shortcut_mods->count < SC_MAX_SHORTCUT_MODS);
    for (unsigned i = 0; i < shortcut_mods->count; ++i) {
        uint16_t sdl_mod = to_sdl_mod(shortcut_mods->data[i]);
        assert(sdl_mod);
        this->sdl_shortcut_mods.data[i] = sdl_mod;
    }
    this->sdl_shortcut_mods.count = shortcut_mods->count;

    this->vfinger_down = false;
}

static void
send_keycode(Controller *controller, enum android_keycode keycode,
             int actions, const char *name) {
    // send DOWN event
    ControlMsg msg;
    msg.type = CONTROL_MSG_TYPE_INJECT_KEYCODE;
    msg.inject_keycode.keycode = keycode;
    msg.inject_keycode.metastate = (android_metastate)0;
    msg.inject_keycode.repeat = 0;

    if (actions & ACTION_DOWN) {
        msg.inject_keycode.action = AKEY_EVENT_ACTION_DOWN;
        if (!controller->PushMsg(std::move(msg))) {
            LOGW("Could not request 'inject %s (DOWN)'", name);
            return;
        }
    }

    if (actions & ACTION_UP) {
        msg.inject_keycode.action = AKEY_EVENT_ACTION_UP;
        if (!controller->PushMsg(std::move(msg))) {
            LOGW("Could not request 'inject %s (UP)'", name);
        }
    }
}

static inline void
action_home(Controller *controller, int actions) {
    send_keycode(controller, AKEYCODE_HOME, actions, "HOME");
}

static inline void
action_back(Controller *controller, int actions) {
    send_keycode(controller, AKEYCODE_BACK, actions, "BACK");
}

static inline void
action_app_switch(Controller *controller, int actions) {
    send_keycode(controller, AKEYCODE_APP_SWITCH, actions, "APP_SWITCH");
}

static inline void
action_power(Controller *controller, int actions) {
    send_keycode(controller, AKEYCODE_POWER, actions, "POWER");
}

static inline void
action_volume_up(Controller *controller, int actions) {
    send_keycode(controller, AKEYCODE_VOLUME_UP, actions, "VOLUME_UP");
}

static inline void
action_volume_down(Controller *controller, int actions) {
    send_keycode(controller, AKEYCODE_VOLUME_DOWN, actions, "VOLUME_DOWN");
}

static inline void
action_menu(Controller *controller, int actions) {
    send_keycode(controller, AKEYCODE_MENU, actions, "MENU");
}

static inline void
action_copy(Controller *controller, int actions) {
    send_keycode(controller, AKEYCODE_COPY, actions, "COPY");
}

static inline void
action_cut(Controller *controller, int actions) {
    send_keycode(controller, AKEYCODE_CUT, actions, "CUT");
}

// turn the screen on if it was off, press BACK otherwise
static void
press_back_or_turn_screen_on(Controller *controller) {
    ControlMsg msg;
    msg.type = CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON;

    if (!controller->PushMsg(std::move(msg))) {
        LOGW("Could not request 'press back or turn screen on'");
    }
}

static void
expand_notification_panel(Controller *controller) {
    ControlMsg msg;
    msg.type = CONTROL_MSG_TYPE_EXPAND_NOTIFICATION_PANEL;

    if (!controller->PushMsg(std::move(msg))) {
        LOGW("Could not request 'expand notification panel'");
    }
}

static void
collapse_notification_panel(Controller *controller) {
    ControlMsg msg;
    msg.type = CONTROL_MSG_TYPE_COLLAPSE_NOTIFICATION_PANEL;

    if (!controller->PushMsg(std::move(msg))) {
        LOGW("Could not request 'collapse notification panel'");
    }
}

static void
set_device_clipboard(Controller *controller, bool paste) {
    char *text = SDL_GetClipboardText();
    if (!text) {
        LOGW("Could not get clipboard text: %s", SDL_GetError());
        return;
    }
    if (!*text) {
        // empty text
        SDL_free(text);
        return;
    }

    ControlMsg msg;
    msg.type = CONTROL_MSG_TYPE_SET_CLIPBOARD;
    msg.set_clipboard.text = text;
    msg.set_clipboard.paste = paste;

    if (!controller->PushMsg(std::move(msg))) {
        SDL_free(text);
        LOGW("Could not request 'set device clipboard'");
    }
}

static void
set_screen_power_mode(Controller *controller,
                      enum screen_power_mode mode) {
    ControlMsg msg;
    msg.type = CONTROL_MSG_TYPE_SET_SCREEN_POWER_MODE;
    msg.set_screen_power_mode.mode = mode;

    if (!controller->PushMsg(std::move(msg))) {
        LOGW("Could not request 'set screen power mode'");
    }
}

static void
switch_fps_counter_state(FrameCounter* fps_counter) {
    // the started state can only be written from the current thread, so there
    // is no ToCToU issue
    if (fps_counter->IsStarted()) {
        fps_counter->Stop();
        LOGI("FPS counter stopped");
    } else {
        if (fps_counter->Start()) {
            LOGI("FPS counter started");
        } else {
            LOGE("FPS counter starting failed");
        }
    }
}

static void
clipboard_paste(Controller *controller) {
    char *text = SDL_GetClipboardText();
    if (!text) {
        LOGW("Could not get clipboard text: %s", SDL_GetError());
        return;
    }
    if (!*text) {
        // empty text
        SDL_free(text);
        return;
    }

    ControlMsg msg;
    msg.type = CONTROL_MSG_TYPE_INJECT_TEXT;
    msg.inject_text.text = text;
    if (!controller->PushMsg(std::move(msg))) {
        SDL_free(text);
        LOGW("Could not request 'paste clipboard'");
    }
}

static void
rotate_device(Controller *controller) {
    ControlMsg msg;
    msg.type = CONTROL_MSG_TYPE_ROTATE_DEVICE;

    if (!controller->PushMsg(std::move(msg))) {
        LOGW("Could not request device rotation");
    }
}

static void
rotate_client_left(Screen* screen) {
    unsigned new_rotation = (screen->rotation + 1) % 4;
    screen->set_rotation(new_rotation);
}

static void
rotate_client_right(Screen* screen) {
    unsigned new_rotation = (screen->rotation + 3) % 4;
    screen->set_rotation(new_rotation);
}

void InputManager::ProcessTextInput(const SDL_TextInputEvent *event) {
    if (this->IsShortcutMod(SDL_GetModState())) {
        // A shortcut must never generate text events
        return;
    }
    if (!this->prefer_text) {
        char c = event->text[0];
        if (isalpha(c) || c == ' ') {
            assert(event->text[1] == '\0');
            // letters and space are handled as raw key event
            return;
        }
    }

    ControlMsg msg;
    msg.type = CONTROL_MSG_TYPE_INJECT_TEXT;
    msg.inject_text.text = SDL_strdup(event->text);
    if (!msg.inject_text.text) {
        LOGW("Could not strdup input text");
        return;
    }
    if (!this->controller->PushMsg(std::move(msg))) {
        SDL_free(msg.inject_text.text);
        LOGW("Could not request 'inject text'");
    }
}

bool InputManager::SimulateVirtualFinger(enum android_motionevent_action action, struct point point) {
    bool up = action == AMOTION_EVENT_ACTION_UP;

    ControlMsg msg;
    msg.type = CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT;
    msg.inject_touch_event.action = action;
    msg.inject_touch_event.position.screen_size = this->screen->frame_size;
    msg.inject_touch_event.position.point = point;
    msg.inject_touch_event.pointer_id = POINTER_ID_VIRTUAL_FINGER;
    msg.inject_touch_event.pressure = up ? 0.0f : 1.0f;
    msg.inject_touch_event.buttons = (android_motionevent_buttons)0;

    if (!this->controller->PushMsg(std::move(msg))) {
        LOGW("Could not request 'inject virtual finger event'");
        return false;
    }

    return true;
}

static struct point
inverse_point(struct point point, struct size size) {
    point.x = size.width - point.x;
    point.y = size.height - point.y;
    return point;
}

static bool
convert_input_key(const SDL_KeyboardEvent *from, ControlMsg *to,
                  bool prefer_text, uint32_t repeat) {
    to->type = CONTROL_MSG_TYPE_INJECT_KEYCODE;

    if (!convert_keycode_action((SDL_EventType)from->type, &to->inject_keycode.action)) {
        return false;
    }

    uint16_t mod = from->keysym.mod;
    if (!convert_keycode(from->keysym.sym, &to->inject_keycode.keycode, mod,
                         prefer_text)) {
        return false;
    }

    to->inject_keycode.repeat = repeat;
    to->inject_keycode.metastate = convert_meta_state((SDL_Keymod)mod);

    return true;
}

void InputManager::ProcessKey(const SDL_KeyboardEvent* event) {
    // control: indicates the state of the command-line option --no-control
    bool control = this->control;

    bool smod = this->IsShortcutMod(event->keysym.mod);

    Controller* controller = this->controller;

    SDL_Keycode keycode = event->keysym.sym;
    bool down = event->type == SDL_KEYDOWN;
    bool ctrl = event->keysym.mod & KMOD_CTRL;
    bool shift = event->keysym.mod & KMOD_SHIFT;
    bool repeat = event->repeat;

    if (this->screen->ManageConsoleKey(keycode, event->keysym.scancode, event->keysym.mod, down)) return;

    // The shortcut modifier is pressed
    if (smod) {
        int action = down ? ACTION_DOWN : ACTION_UP;
        switch (keycode) {
            case SDLK_h:
                if (control && !shift && !repeat) {
                    action_home(controller, action);
                }
                return;
            case SDLK_b: // fall-through
            case SDLK_BACKSPACE:
                if (control && !shift && !repeat) {
                    action_back(controller, action);
                }
                return;
            case SDLK_s:
                if (control && !shift && !repeat) {
                    //action_app_switch(controller, action);
                    if (this->onScreenshot) { this->onScreenshot(keycode); }
                }
                return;
            case SDLK_m:
                if (control && !shift && !repeat) {
                    action_menu(controller, action);
                }
                return;
            case SDLK_p:
                if (control && !shift && !repeat) {
                    action_power(controller, action);
                }
                return;
            case SDLK_o:
                if (control && !repeat && down) {
                    enum screen_power_mode mode = shift
                                                ? SCREEN_POWER_MODE_NORMAL
                                                : SCREEN_POWER_MODE_OFF;
                    set_screen_power_mode(controller, mode);
                }
                return;
            case SDLK_DOWN:
                if (control && !shift) {
                    // forward repeated events
                    action_volume_down(controller, action);
                }
                return;
            case SDLK_UP:
                if (control && !shift) {
                    // forward repeated events
                    action_volume_up(controller, action);
                }
                return;
            case SDLK_LEFT:
                if (!shift && !repeat && down) {
                    rotate_client_left(this->screen);
                }
                return;
            case SDLK_RIGHT:
                if (!shift && !repeat && down) {
                    rotate_client_right(this->screen);
                }
                return;
            case SDLK_c:
                if (control && !shift && !repeat) {
                    action_copy(controller, action);
                }
                return;
            case SDLK_x:
                if (control && !shift && !repeat) {
                    action_cut(controller, action);
                }
                return;
            case SDLK_v:
                if (control && !repeat && down) {
                    if (shift || this->legacy_paste) {
                        // inject the text as input events
                        clipboard_paste(controller);
                    } else {
                        // store the text in the device clipboard and paste
                        set_device_clipboard(controller, true);
                    }
                }
                return;
            case SDLK_f:
                if (!shift && !repeat && down) {
                    this->screen->switch_fullscreen();
                }
                return;
            case SDLK_w:
                if (!shift && !repeat && down) {
                    this->screen->resize_to_fit();
                }
                return;
            case SDLK_g:
                if (!shift && !repeat && down) {
                    this->screen->resize_to_pixel_perfect();
                }
                return;
            case SDLK_i:
                if (!shift && !repeat && down) {
                    FrameCounter* fps_counter = this->video_buffer->fps_counter;
                    switch_fps_counter_state(fps_counter);
                }
                return;
            case SDLK_n:
                if (control && !repeat && down) {
                    if (shift) {
                        collapse_notification_panel(controller);
                    } else {
                        expand_notification_panel(controller);
                    }
                }
                return;
            case SDLK_r:
                if (control && !shift && !repeat && down) {
                    rotate_device(controller);
                }
                return;
        }

        return;
    }

    if (!control) {
        return;
    }

    switch (keycode) {
    case SDLK_F1:
        action_power(controller, down ? ACTION_DOWN : ACTION_UP);
        return;
    case SDLK_F2:
        set_screen_power_mode(controller, SCREEN_POWER_MODE_OFF);
        return;
    case SDLK_F3:
        set_screen_power_mode(controller, SCREEN_POWER_MODE_NORMAL);
        return;
    }

    if (event->repeat) {
        if (!this->forward_key_repeat) {
            return;
        }
        ++this->repeat;
    } else {
        this->repeat = 0;
    }

    if (ctrl && !shift && keycode == SDLK_v && down && !repeat) {
        if (this->legacy_paste) {
            // inject the text as input events
            clipboard_paste(controller);
            return;
        }
        // Synchronize the computer clipboard to the device clipboard before
        // sending Ctrl+v, to allow seamless copy-paste.
        set_device_clipboard(controller, false);
    }

    ControlMsg msg;
    if (convert_input_key(event, &msg, this->prefer_text, this->repeat)) {
        if (!controller->PushMsg(std::move(msg))) {
            LOGW("Could not request 'inject keycode'");
        }
    }
}

static bool
convert_mouse_motion(const SDL_MouseMotionEvent *from, Screen* screen,
                     ControlMsg *to) {
    to->type = CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT;
    to->inject_touch_event.action = AMOTION_EVENT_ACTION_MOVE;
    to->inject_touch_event.pointer_id = POINTER_ID_MOUSE;
    to->inject_touch_event.position.screen_size = screen->frame_size;
    to->inject_touch_event.position.point =
        screen->convert_window_to_frame_coords(from->x, from->y);
    to->inject_touch_event.pressure = 1.f;
    to->inject_touch_event.buttons = convert_mouse_buttons(from->state);

    return true;
}

void InputManager::ProcessMouseMotion(const SDL_MouseMotionEvent *event) {
    if (!event->state) {
        // do not send motion events when no button is pressed
        return;
    }
    if (event->which == SDL_TOUCH_MOUSEID) {
        // simulated from touch events, so it's a duplicate
        return;
    }
    ControlMsg msg;
    if (!convert_mouse_motion(event, this->screen, &msg)) {
        return;
    }

    if (!this->controller->PushMsg(std::move(msg))) {
        LOGW("Could not request 'inject mouse motion event'");
    }

    if (this->vfinger_down) {
        struct point mouse = msg.inject_touch_event.position.point;
        struct point vfinger = inverse_point(mouse, this->screen->frame_size);
        this->SimulateVirtualFinger(AMOTION_EVENT_ACTION_MOVE, vfinger);
    }
}

static bool
convert_touch(const SDL_TouchFingerEvent *from, Screen* screen,
              ControlMsg *to) {
    to->type = CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT;

    if (!convert_touch_action((SDL_EventType)from->type, &to->inject_touch_event.action)) {
        return false;
    }

    to->inject_touch_event.pointer_id = from->fingerId;
    to->inject_touch_event.position.screen_size = screen->frame_size;

    int dw;
    int dh;
    screen->GetClientSize(&dw, &dh);

    // SDL touch event coordinates are normalized in the range [0; 1]
    int32_t x = from->x * dw;
    int32_t y = from->y * dh;
    to->inject_touch_event.position.point =
        screen->convert_drawable_to_frame_coords(x, y);

    to->inject_touch_event.pressure = from->pressure;
    to->inject_touch_event.buttons = (android_motionevent_buttons)0;
    return true;
}

void InputManager::ProcessTouch(const SDL_TouchFingerEvent *event) {
    ControlMsg msg;
    if (convert_touch(event, this->screen, &msg)) {
        if (!this->controller->PushMsg(std::move(msg))) {
            LOGW("Could not request 'inject touch event'");
        }
    }
}

static bool
convert_mouse_button(const SDL_MouseButtonEvent *from, Screen *screen,
                     ControlMsg *to) {
    to->type = CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT;

    if (!convert_mouse_action((SDL_EventType)from->type, &to->inject_touch_event.action)) {
        return false;
    }

    to->inject_touch_event.pointer_id = POINTER_ID_MOUSE;
    to->inject_touch_event.position.screen_size = screen->frame_size;
    to->inject_touch_event.position.point =
        screen->convert_window_to_frame_coords(from->x, from->y);
    to->inject_touch_event.pressure =
        from->type == SDL_MOUSEBUTTONDOWN ? 1.f : 0.f;
    to->inject_touch_event.buttons =
        convert_mouse_buttons(SDL_BUTTON(from->button));

    return true;
}

void InputManager::ProcessMouseButton(const SDL_MouseButtonEvent *event) {
    bool control = this->control;
    //AlarmHandleMouseClick();

    if (event->which == SDL_TOUCH_MOUSEID) {
        // simulated from touch events, so it's a duplicate
        return;
    }

    bool down = event->type == SDL_MOUSEBUTTONDOWN;
    if (!this->forward_all_clicks && down) {
        if (control && event->button == SDL_BUTTON_RIGHT) {
            press_back_or_turn_screen_on(this->controller);
            return;
        }
        if (control && event->button == SDL_BUTTON_MIDDLE) {
            action_home(this->controller, ACTION_DOWN | ACTION_UP);
            return;
        }

        // double-click on black borders resize to fit the device screen
        if (event->button == SDL_BUTTON_LEFT && event->clicks == 2) {
            int32_t x = event->x;
            int32_t y = event->y;
            screen->hidpi_scale_coords(&x, &y);
            SDL_Rect *r = &this->screen->rect;
            bool outside = x < r->x || x >= r->x + r->w
                        || y < r->y || y >= r->y + r->h;
            if (outside) {
                screen->resize_to_fit();
                return;
            }
        }
        // otherwise, send the click event to the device
    }

    if (!control) {
        return;
    }

    ControlMsg msg;
    if (!convert_mouse_button(event, this->screen, &msg)) {
        return;
    }

    if (!this->controller->PushMsg(std::move(msg))) {
        LOGW("Could not request 'inject mouse button event'");
        return;
    }

    // Pinch-to-zoom simulation.
    //
    // If Ctrl is hold when the left-click button is pressed, then
    // pinch-to-zoom mode is enabled: on every mouse event until the left-click
    // button is released, an additional "virtual finger" event is generated,
    // having a position inverted through the center of the screen.
    //
    // In other words, the center of the rotation/scaling is the center of the
    // screen.
#define CTRL_PRESSED (SDL_GetModState() & (KMOD_LCTRL | KMOD_RCTRL))
    if ((down && !this->vfinger_down && CTRL_PRESSED)
            || (!down && this->vfinger_down)) {
        struct point mouse = msg.inject_touch_event.position.point;
        struct point vfinger = inverse_point(mouse, this->screen->frame_size);
        enum android_motionevent_action action = down
                                               ? AMOTION_EVENT_ACTION_DOWN
                                               : AMOTION_EVENT_ACTION_UP;
        if (!this->SimulateVirtualFinger(action, vfinger)) {
            return;
        }
        this->vfinger_down = down;
    }
}

static bool
convert_mouse_wheel(const SDL_MouseWheelEvent *from, Screen *screen,
                    ControlMsg *to) {

    // mouse_x and mouse_y are expressed in pixels relative to the window
    int mouse_x;
    int mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);

    struct position position = {
        .screen_size = screen->frame_size,
        .point = screen->convert_window_to_frame_coords(mouse_x, mouse_y),
    };

    to->type = CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT;

    to->inject_scroll_event.position = position;
    to->inject_scroll_event.hscroll = from->x;
    to->inject_scroll_event.vscroll = from->y;

    return true;
}

void InputManager::ProcessMouseWheel(const SDL_MouseWheelEvent *event) {
    ControlMsg msg;
    if (convert_mouse_wheel(event, this->screen, &msg)) {
        if (!this->controller->PushMsg(std::move(msg))) {
            LOGW("Could not request 'inject mouse wheel event'");
        }
    }
}

std::function<void(int, int, bool)> InputManager::GetTouchFunc()
{
    std::function<void(int, int, bool)> result = [&](int x, int y, bool isDown) {
        if (this->allowAutomatedInputs) {
            this->SimulateVirtualFinger(isDown ? AMOTION_EVENT_ACTION_DOWN : AMOTION_EVENT_ACTION_UP, point{ x, y });
        }
    };
    return result;
}

void InputManager::SetAutomatedInputsEnabled(bool enabled)
{
    this->allowAutomatedInputs = enabled;
}