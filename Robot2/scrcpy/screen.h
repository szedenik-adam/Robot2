#pragma once

#include <mutex>
#include <memory>
#include <functional>
#include <SDL2/SDL.h>
extern "C" {
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
}
#include "config.h"
#include "common.h"
//#include "sc_opengl.h"
#include "../console/GLConsole.h"
#include "../Window.h"

class VideoBuffer; class Worker; class Environment;

class Screen : public Window {
public:
    //SDL_Window* window;
    //std::unique_ptr<GLConsole> console;
    // 
    //SDL_Texture* texture;
    GLuint frame_tex, vao, vert_buf, elem_buf, tex_attrib, vert_attrib;
    //bool use_opengl;
    //std::unique_ptr<sc_opengl> gl;
    SDL_GLContext glcontext;
    struct size frame_size;
    struct size content_size; // rotated frame_size

    bool resize_pending; // resize requested while fullscreen or maximized
    // The content size the last time the window was not maximized or
    // fullscreen (meaningful only when resize_pending is true)
    struct size windowed_content_size;

    // client rotation: 0, 1, 2 or 3 (x90 degrees counterclockwise)
    unsigned rotation;
    // rectangle of the content (excluding black borders)
    struct SDL_Rect rect;
    bool has_frame;
    bool fullscreen;
    bool maximized;
    bool no_window;
    bool mipmaps;

    struct SwsContext* swsCtx;
    uint8_t* pixels[3];
    std::mutex pixels_mutex; // Locks the usage of pixels[1].

    uint32_t lastRender;
    SDL_TimerID refreshTimer;

    Worker* worker;
    Environment* environment;
    /*Detection detection;
    Robot* robot;*/


    // initialize default values
    Screen(const Window& window);

    // destroy window, renderer and texture (if any)
    virtual ~Screen();

    // initialize screen, create window, renderer and texture (window is hidden)
    // window_x and window_y accept SC_WINDOW_POSITION_UNDEFINED
    bool
        init_rendering(const char* window_title,
            struct size frame_size, bool always_on_top,
            int16_t window_x, int16_t window_y, uint16_t window_width,
            uint16_t window_height, bool window_borderless,
            uint8_t rotation, bool mipmaps);

    // show the window
    void show_window();

    // resize if necessary and write the rendered frame into the texture
    bool update_frame(VideoBuffer& vb);

    // render the texture to the renderer
    //
    // Set the update_content_rect flag if the window or content size may have
    // changed, so that the content rectangle is recomputed
    void render(bool update_content_rect);

    // switch the fullscreen mode
    void switch_fullscreen();

    // resize window to optimal size (remove black borders)
    void resize_to_fit();

    // resize window to 1:1 (pixel-perfect)
    void resize_to_pixel_perfect();

    // set the display rotation (0, 1, 2 or 3, x90 degrees counterclockwise)
    void set_rotation(unsigned rotation);

    // react to window events
    void handle_window_event(const SDL_WindowEvent* event);

    // convert point from window coordinates to frame coordinates
    // x and y are expressed in pixels
    struct point convert_window_to_frame_coords(int32_t x, int32_t y);

    // convert point from drawable coordinates to frame coordinates
    // x and y are expressed in pixels
    struct point convert_drawable_to_frame_coords(int32_t x, int32_t y);

    // Convert coordinates from window to drawable.
    // Events are expressed in window coordinates, but content is expressed in
    // drawable coordinates. They are the same if HiDPI scaling is 1, but differ
    // otherwise.
    void hidpi_scale_coords(int32_t* x, int32_t* y);

    static struct size get_rotated_size(struct size size, int rotation);

    // get the window size in a struct size
    struct size get_window_size();

    // set the window size to be applied when fullscreen is disabled
    void set_window_size(struct size new_size);

    // get the preferred display bounds (i.e. the screen bounds with some margins)
    static bool get_preferred_display_bounds(struct size* bounds);

    static bool is_optimal_size(struct size current_size, struct size content_size);

    // return the optimal size of the window, with the following constraints:
    //  - it attempts to keep at least one dimension of the current_size (i.e. it
    //    crops the black borders)
    //  - it keeps the aspect ratio
    //  - it scales down to make it fit in the display_size
    static struct size get_optimal_size(struct size current_size, struct size content_size);

    // same as get_optimal_size(), but read the current size from the window
    struct size get_optimal_window_size(struct size content_size);

    // initially, there is no current size, so use the frame size as current size
    // req_width and req_height, if not 0, are the sizes requested by the user
    static struct size get_initial_optimal_size(struct size content_size, uint16_t req_width, uint16_t req_height);

    void update_content_rect();

    GLuint create_texture();

    void resize_for_content(struct size old_content_size, struct size new_content_size);
    void set_content_size(struct size new_content_size);
    void apply_pending_resize();

    bool prepare_for_frame(struct size new_frame_size);
    void update_texture();
    void convert_frame(const AVFrame* frame);

    // Update window when not receiving video frames for some time.
    static uint32_t RefreshTimerCallback(uint32_t interval, void* param);

    //std::function<std::tuple<uint8_t*, std::unique_lock<std::mutex>>()> GetGrabImageFunc();
    //static std::tuple<uint8_t*, std::unique_lock<std::mutex>> GetLastImageFrame(Screen* screen);
    std::function<uint8_t*()> GetGrabImageFunc();
    static uint8_t* GetLastImageFrame(Screen* screen);
    void SetWorker(Worker* worker);
    void SetEnvironment(Environment* environment);
};