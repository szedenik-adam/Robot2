#ifndef SCRCPY_H
#define SCRCPY_H

//#include <stdbool.h>
//#include <stddef.h>
//#include <stdint.h>
#include <memory>
#include <functional>

#include "util/fps_counter.h"
#include "server.h"
#include "screen.h"
#include "video_buffer.h"
#include "file_handler.h"
#include "decoder.h"
#include "recorder.h"
#include "controller.h"
#include "input_manager.h"
#include "stream.h"

#include "config.h"

enum sc_log_level {
    SC_LOG_LEVEL_DEBUG,
    SC_LOG_LEVEL_INFO,
    SC_LOG_LEVEL_WARN,
    SC_LOG_LEVEL_ERROR,
};

enum sc_record_format {
    SC_RECORD_FORMAT_AUTO,
    SC_RECORD_FORMAT_MP4,
    SC_RECORD_FORMAT_MKV,
};


enum sc_shortcut_mod {
    SC_MOD_LCTRL = 1 << 0,
    SC_MOD_RCTRL = 1 << 1,
    SC_MOD_LALT = 1 << 2,
    SC_MOD_RALT = 1 << 3,
    SC_MOD_LSUPER = 1 << 4,
    SC_MOD_RSUPER = 1 << 5,
};

enum event_result {
    EVENT_RESULT_CONTINUE,
    EVENT_RESULT_STOPPED_BY_USER,
    EVENT_RESULT_STOPPED_BY_EOS,
};

struct sc_shortcut_mods {
    unsigned data[SC_MAX_SHORTCUT_MODS];
    unsigned count;
};

#define SC_WINDOW_POSITION_UNDEFINED (-0x8000)

class scrcpyOptions {
public:
    Environment* env;
    const char* serial;
    const char* crop;
    const char* record_filename;
    const char* window_title;
    const char* push_target;
    const char* render_driver;
    const char* codec_options;
    const char* encoder_name;
    enum sc_log_level log_level;
    enum sc_record_format record_format;
    struct port_range port_range;
    struct sc_shortcut_mods shortcut_mods;
    uint16_t max_size;
    uint32_t bit_rate;
    uint16_t max_fps;
    int8_t lock_video_orientation;
    uint8_t rotation;
    int16_t window_x; // SC_WINDOW_POSITION_UNDEFINED for "auto"
    int16_t window_y; // SC_WINDOW_POSITION_UNDEFINED for "auto"
    uint16_t window_width;
    uint16_t window_height;
    uint16_t display_id;
    bool show_touches;
    bool fullscreen;
    bool always_on_top;
    bool control;
    bool display;
    bool turn_screen_off;
    bool render_expired_frames;
    bool prefer_text;
    bool window_borderless;
    bool mipmaps;
    bool stay_awake;
    bool force_adb_forward;
    bool disable_screensaver;
    bool forward_key_repeat;
    bool forward_all_clicks;
    bool legacy_paste;

    std::unique_ptr<Server> server;
    std::unique_ptr<Screen> screen;
    std::unique_ptr<FrameCounter> fps_counter;
    std::unique_ptr<VideoBuffer> video_buff;
    std::unique_ptr<Stream> stream;
    std::unique_ptr<Decoder> decoder;
    std::unique_ptr<Recorder> recorder;
    std::unique_ptr <Controller> controller;
    std::unique_ptr<FileHandler> file_handler;
    std::unique_ptr<InputManager> input_manager;

    std::function<void(Screen*, struct size)> onWindowCreation;
    std::function<void(InputManager*)> onInputManCreation;

    scrcpyOptions(Environment* env, const char* serial=nullptr) :env(env), serial(serial), crop(nullptr), record_filename(nullptr), push_target(nullptr), render_driver(nullptr), codec_options(nullptr), encoder_name(nullptr),
        log_level(SC_LOG_LEVEL_INFO), record_format(SC_RECORD_FORMAT_AUTO),
        port_range({.first=DEFAULT_LOCAL_PORT_RANGE_FIRST,.last=DEFAULT_LOCAL_PORT_RANGE_LAST}),
        shortcut_mods({.data={SC_MOD_LALT, SC_MOD_LSUPER},.count=2}),
        max_size(DEFAULT_MAX_SIZE),
        bit_rate(DEFAULT_BIT_RATE),
        max_fps(0),
        lock_video_orientation(DEFAULT_LOCK_VIDEO_ORIENTATION),
        rotation(0),
        window_x(SC_WINDOW_POSITION_UNDEFINED),
        window_y(SC_WINDOW_POSITION_UNDEFINED),
        window_width(0),
        window_height(0),
        display_id(0),
        show_touches(false),
        fullscreen(false),
        always_on_top(false),
        control(true),
        display(true),
        turn_screen_off(false),
        render_expired_frames(false),
        prefer_text(false),
        window_borderless(false),
        mipmaps(true),
        stay_awake(false),
        force_adb_forward(false),
        disable_screensaver(false),
        forward_key_repeat(true),
        forward_all_clicks(false),
        legacy_paste(false),
        server(nullptr), screen(nullptr), fps_counter(nullptr), video_buff(nullptr), stream(nullptr), decoder(nullptr), recorder(nullptr), controller(nullptr), file_handler(nullptr), input_manager(nullptr),
        onWindowCreation(nullptr), onInputManCreation(nullptr)
        {}

    bool Run();

    enum event_result HandleEvent(SDL_Event* event);
    bool EventLoop();

    void OnWindowCreation(const std::function<void(Screen*, struct size)>& f) { this->onWindowCreation = f; }
    void OnInputManCreation(const std::function<void(InputManager*)>& f) { this->onInputManCreation = f; }

    static bool sdl_init_and_configure(bool display, const char* render_driver, bool disable_screensaver);
};

#endif
