#include "scrcpy.h"
#include "util/log.h"
#include "device.h"
#include "events.h"

#include "../Environment.h"

#include <SDL2/SDL.h>
#ifdef _WIN32
// not needed here, but winsock2.h must never be included AFTER windows.h
# include <winsock2.h>
# include <windows.h>
#endif

#ifdef _WIN32
BOOL WINAPI windows_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT) {
        SDL_Event event;
        event.type = SDL_QUIT;
        SDL_PushEvent(&event);
        return TRUE;
    }
    return FALSE;
}
#endif // _WIN32

// init SDL and set appropriate hints
// static
bool scrcpyOptions::sdl_init_and_configure(bool display, const char* render_driver, bool disable_screensaver)
{
    static bool sdl_init_done = false;
    if (sdl_init_done) return true;
    sdl_init_done = true;

    uint32_t flags = display ? SDL_INIT_VIDEO : SDL_INIT_EVENTS;
    if (SDL_Init(flags)) {
        LOGC("Could not initialize SDL: %s", SDL_GetError());
        return false;
    }

    atexit(SDL_Quit);


#ifdef _WIN32
    // Clean up properly on Ctrl+C on Windows
    bool ok = SetConsoleCtrlHandler(windows_ctrl_handler, TRUE);
    if (!ok) {
        LOGW("Could not set Ctrl+C handler");
    }
#endif // _WIN32

    if (!display) {
        return true;
    }

    if (render_driver && !SDL_SetHint(SDL_HINT_RENDER_DRIVER, render_driver)) {
        LOGW("Could not set render driver");
    }

    // Linear filtering
    if (!SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1")) {
        LOGW("Could not enable linear filtering");
    }

#ifdef SCRCPY_SDL_HAS_HINT_MOUSE_FOCUS_CLICKTHROUGH
    // Handle a click to gain focus as any other click
    if (!SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1")) {
        LOGW("Could not enable mouse focus clickthrough");
    }
#endif

#ifdef SCRCPY_SDL_HAS_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR
    // Disable compositor bypassing on X11
    if (!SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0")) {
        LOGW("Could not disable X11 compositor bypass");
    }
#endif

    // Do not minimize on focus loss
    if (!SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0")) {
        LOGW("Could not disable minimize on focus loss");
    }

    if (disable_screensaver) {
        LOGD("Screensaver disabled");
        SDL_DisableScreenSaver();
    }
    else {
        LOGD("Screensaver enabled");
        SDL_EnableScreenSaver();
    }

    return true;
}

static SDL_LogPriority
sdl_priority_from_av_level(int level) {
    switch (level) {
    case AV_LOG_PANIC:
    case AV_LOG_FATAL:
        return SDL_LOG_PRIORITY_CRITICAL;
    case AV_LOG_ERROR:
        return SDL_LOG_PRIORITY_ERROR;
    case AV_LOG_WARNING:
        return SDL_LOG_PRIORITY_WARN;
    case AV_LOG_INFO:
        return SDL_LOG_PRIORITY_INFO;
    }
    // do not forward others, which are too verbose
    return (SDL_LogPriority)0;
}

static void
av_log_callback(void* avcl, int level, const char* fmt, va_list vl) {
    (void)avcl;
    SDL_LogPriority priority = sdl_priority_from_av_level(level);
    if (priority == 0) {
        return;
    }
    char* local_fmt = (char*)SDL_malloc(strlen(fmt) + 10);
    if (!local_fmt) {
        LOGC("Could not allocate string");
        return;
    }
    // strcpy is safe here, the destination is large enough
    strcpy(local_fmt, "[FFmpeg] ");
    strcpy(local_fmt + 9, fmt);
    SDL_LogMessageV(SDL_LOG_CATEGORY_VIDEO, priority, local_fmt, vl);
    SDL_free(local_fmt);
}

bool scrcpyOptions::Run()
{
    if (!this->server.get()) {
        this->server = std::make_unique<Server>();
        //return false;
    }

    //InitRobot(&robot, &screen, &input_manager);
    //EstSetDeviceId(&robot.est, options->serial);

    bool ret = false;

    bool server_started = false;
    bool fps_counter_initialized = false;
    bool video_buffer_initialized = false;
    bool file_handler_initialized = false;
    bool recorder_initialized = false;
    bool stream_started = false;
    bool controller_initialized = false;
    bool controller_started = false;

    bool record = !!this->record_filename;
    struct server_params params = {
        .log_level = this->log_level,
        .crop = this->crop,
        .codec_options = this->codec_options,
        .encoder_name = this->encoder_name,
        .port_range = this->port_range,
        .max_size = this->max_size,
        .bit_rate = this->bit_rate,
        .max_fps = this->max_fps,
        .lock_video_orientation = this->lock_video_orientation,
        .control = this->control,
        .display_id = this->display_id,
        .show_touches = this->show_touches,
        .stay_awake = this->stay_awake,
        .force_adb_forward = this->force_adb_forward,
    };
    if (!this->server->Start(this->serial, &params)) {
        goto end;
    }

    server_started = true;

    if (!sdl_init_and_configure(this->display, this->render_driver, this->disable_screensaver)) {
        goto end;
    }

    if (!this->server->ConnectTo()) {
        goto end;
    }

    char device_name[DEVICE_NAME_FIELD_LENGTH];
    struct size frame_size;

    // screenrecord does not send frames when the screen content does not
    // change therefore, we transmit the screen size before the video stream,
    // to be able to init the window immediately
    if (!device_read_info(server->video_socket, device_name, &frame_size)) {
        goto end;
    }

    //struct decoder* dec = NULL;
    if (this->display) {
        if (!this->fps_counter) {
            this->fps_counter = std::make_unique<FrameCounter>();
            //goto end;
        }
        fps_counter_initialized = true;

        if (!this->video_buff){
            this->video_buff = std::make_unique<VideoBuffer>(fps_counter.get(), this->render_expired_frames);
            //goto end;
        }
        video_buffer_initialized = true;

        if (this->control) {
            if (!this->file_handler.get()){// file_handler_init(&file_handler, server->serial, this->push_target)) {
                this->file_handler = std::make_unique<FileHandler>(server->serial, this->push_target);
                //goto end;
            }
            file_handler_initialized = true;
        }

        this->decoder = std::make_unique<Decoder>(*this->video_buff); //decoder_init(&decoder, &video_buff);
        //dec = &decoder;
    }

    //struct recorder* rec = NULL;
    if (record) {
        if (!this->recorder.get()) { // !recorder_init(&recorder, this->record_filename, this->record_format, frame_size)) {
            this->recorder = std::make_unique<Recorder>(this->record_filename, this->record_format, frame_size);
            //goto end;
        }
        //rec = &recorder;
        recorder_initialized = true;
    }

    av_log_set_callback(av_log_callback);

    this->stream = std::make_unique<Stream>(server->video_socket, this->decoder.get(), this->recorder.get()); //stream_init(&stream, server->video_socket, dec, rec);

    // now we consumed the header values, the socket receives the video stream
    // start the stream
    if (!this->stream->Start()) {
        goto end;
    }
    stream_started = true;

    if (this->display) {
        if (this->control) {
            if (!this->controller.get()){ //controller_init(&controller, server->control_socket)) {
                this->controller = std::make_unique<Controller>(server->control_socket);
                //goto end;
            }
            controller_initialized = true;

            if (!controller->Start()) {
                goto end;
            }
            controller_started = true;
        }

        const char* window_title = this->window_title ? this->window_title : device_name;

        if (!this->screen.get()) {
            this->screen = std::make_unique<Screen>(*this->env->GetWindow());
        }

        if (!this->screen->init_rendering(window_title, frame_size,
            this->always_on_top, this->window_x,
            this->window_y, this->window_width,
            this->window_height,
            this->window_borderless,
            this->rotation, this->mipmaps)) {
            goto end;
        }
        if (this->onWindowCreation) {
            this->onWindowCreation(this->screen.get(), frame_size);
        }

        if (this->turn_screen_off) {
            ControlMsg msg;
            msg.type = CONTROL_MSG_TYPE_SET_SCREEN_POWER_MODE;
            msg.set_screen_power_mode.mode = SCREEN_POWER_MODE_OFF;

            if (!controller->PushMsg(std::move(msg))) {
                LOGW("Could not request 'set screen power mode'");
            }
        }

        if (this->fullscreen) {
            this->screen->switch_fullscreen();
        }
    }
    
    this->input_manager = std::make_unique<InputManager>(this, controller.get(), video_buff.get(), screen.get()); //input_manager_init(&input_manager, options);
    if (this->onInputManCreation) { this->onInputManCreation(this->input_manager.get()); }

    ret = this->EventLoop(); //options
    LOGD("quit...");

    this->screen.reset();

end:
    // stop stream and controller so that they don't continue once their socket
    // is shutdown
    if (stream_started) {
        this->stream->Stop();
    }
    if (this->controller.get()) {
        this->controller->Stop();
    }
    if (this->file_handler.get()) {
        this->file_handler->Stop();
    }
    if (this->fps_counter.get()) {
        this->fps_counter->Interrupt();
    }

    if (server_started) {
        // shutdown the sockets and kill the server
        this->server->Stop();
    }

    // now that the sockets are shutdown, the stream and controller are
    // interrupted, we can join them
    if (stream_started) {
        this->stream->Join();
    }
    if (controller_started) {
        this->controller->Join();
    }
    if (this->controller.get()) {
        this->controller.reset();
    }

    if (this->recorder.get()) {
        this->recorder.reset();
    }

    if (this->file_handler.get()) {
        this->file_handler->Join();
        this->file_handler.reset();
    }

    if (this->video_buff.get()) {
        this->video_buff.reset();
    }

    if (this->fps_counter.get()) {
        this->fps_counter->Join();
        this->fps_counter.reset();// Calls the FrameCounter's destructor.
    }

    this->server.reset(); //server_destroy(&server);

    return ret;
}

#if defined(__APPLE__) || defined(__WINDOWS__)
# define CONTINUOUS_RESIZING_WORKAROUND
#endif

#ifdef CONTINUOUS_RESIZING_WORKAROUND
static int
event_watcher(void* data, SDL_Event* event) {
    scrcpyOptions* options = (scrcpyOptions*)data;
    if (event->type == SDL_WINDOWEVENT
        && event->window.event == SDL_WINDOWEVENT_RESIZED) {
        // In practice, it seems to always be called from the same thread in
        // that specific case. Anyway, it's just a workaround.
        options->screen->render(true);
    }
    return 0;
}
#endif

static bool
is_apk(const char* file) {
    const char* ext = strrchr(file, '.');
    return ext && !strcmp(ext, ".apk");
}

enum event_result scrcpyOptions::HandleEvent(SDL_Event* event) {
    switch (event->type) {
    case EVENT_REFRESH:
        this->screen->render(false);
        break; 
    case EVENT_STREAM_STOPPED:
        LOGD("Video stream stopped");
        return EVENT_RESULT_STOPPED_BY_EOS;
    case SDL_QUIT:
        LOGD("User requested to quit");
        return EVENT_RESULT_STOPPED_BY_USER;
    case EVENT_NEW_FRAME:
        if (!this->screen->has_frame) {
            screen->has_frame = true;
            // this is the very first frame, show the window
            screen->show_window();
        }
        if (!this->screen->update_frame(*this->video_buff)) {
            return EVENT_RESULT_CONTINUE;
        }
        // TODO: send frame to 
        break;
    case SDL_WINDOWEVENT:
        this->screen->handle_window_event(&event->window);
        break;
    case SDL_TEXTINPUT:
        if (!this->control) {
            break;
        }
        this->input_manager->ProcessTextInput(&event->text);
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        // some key events do not interact with the device, so process the
        // event even if control is disabled
        this->input_manager->ProcessKey(&event->key);
        break;
    case SDL_MOUSEMOTION:
        if (!this->control) {
            break;
        }
        this->input_manager->ProcessMouseMotion(&event->motion);
        break;
    case SDL_MOUSEWHEEL:
        if (!this->control) {
            break;
        }
        this->input_manager->ProcessMouseWheel(&event->wheel);
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        // some mouse events do not interact with the device, so process
        // the event even if control is disabled
        this->input_manager->ProcessMouseButton(&event->button);
        break;
    case SDL_FINGERMOTION:
    case SDL_FINGERDOWN:
    case SDL_FINGERUP:
        this->input_manager->ProcessTouch(&event->tfinger);
        break;
    case SDL_DROPFILE: {
        if (!this->control) {
            break;
        }
        file_handler_action_t action;
        if (is_apk(event->drop.file)) {
            action = ACTION_INSTALL_APK;
        }
        else {
            action = ACTION_PUSH_FILE;
        }
        this->file_handler->Request(action, event->drop.file);
        break;
    }
}
    return EVENT_RESULT_CONTINUE;
}

bool scrcpyOptions::EventLoop() {
#ifdef CONTINUOUS_RESIZING_WORKAROUND
    if (this->display) {
        SDL_AddEventWatch(event_watcher, this);
    }
#endif
    SDL_Event event;
    while (SDL_WaitEvent(&event)) {
        enum event_result result = this->HandleEvent(&event);
        switch (result) {
        case EVENT_RESULT_STOPPED_BY_USER:
            return true;
        case EVENT_RESULT_STOPPED_BY_EOS:
            LOGW("Device disconnected");
            return false;
        case EVENT_RESULT_CONTINUE:
            break;
        }
    }
    return false;
}
