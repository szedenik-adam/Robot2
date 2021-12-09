#include <GL/glew.h>
#pragma comment(lib, "glew32.lib")
#pragma comment(lib, "opengl32.lib")

#include "screen.h"
#include "util/log.h"
#include "util/lock.h"
#include "scrcpy.h"
#include "video_buffer.h"
#include "events.h"
extern "C" {
    #include "util/tiny_xpm.h"
    #include "util/icon.xpm"
}

#include "../console/GLConsole.h"
#include "../Worker.h"

#define DISPLAY_MARGINS 96

#define BUFFER_OFFSET(i) ((char *)NULL + (i))

/*
  if( theConsole.IsOpen() )
  {
    //pass all key strokes to the console
    theConsole.SpecialFunc( key );
  }
  
      if( theConsole.IsOpen() ) {
        //send keystroke to console
        theConsole.KeyboardFunc( key );
      }


    case GLCONSOLE_KEY: //~ key opens console on US keyboards.
                                  //On UK keyboards it is the ` key (the one above the Tab and left of the 1 keys)
      theConsole.ToggleConsole();
      break;
  */

// static
struct size Screen::get_rotated_size(struct size size, int rotation)
{
    struct size rotated_size;
    if (rotation & 1) {
        rotated_size.width = size.height;
        rotated_size.height = size.width;
    }
    else {
        rotated_size.width = size.width;
        rotated_size.height = size.height;
    }
    return rotated_size;
}
// get the window size in a struct size
struct size Screen::get_window_size() {
    int width;
    int height;
    SDL_GetWindowSize(this->window, &width, &height);

    struct size size;
    size.width = width;
    size.height = height;
    return size;
}

// set the window size to be applied when fullscreen is disabled
void Screen::set_window_size(struct size new_size) {
    assert(!this->fullscreen);
    assert(!this->maximized);
    SDL_SetWindowSize(this->window, new_size.width, new_size.height);
}

// get the preferred display bounds (i.e. the screen bounds with some margins)
// static
bool Screen::get_preferred_display_bounds(struct size* bounds) {
    SDL_Rect rect;
#ifdef SCRCPY_SDL_HAS_GET_DISPLAY_USABLE_BOUNDS
# define GET_DISPLAY_BOUNDS(i, r) SDL_GetDisplayUsableBounds((i), (r))
#else
# define GET_DISPLAY_BOUNDS(i, r) SDL_GetDisplayBounds((i), (r))
#endif
    if (GET_DISPLAY_BOUNDS(0, &rect)) {
        LOGW("Could not get display usable bounds: %s", SDL_GetError());
        return false;
    }

    bounds->width = MAX(0, rect.w - DISPLAY_MARGINS);
    bounds->height = MAX(0, rect.h - DISPLAY_MARGINS);
    return true;
}

//static
bool Screen::is_optimal_size(struct size current_size, struct size content_size) {
    // The size is optimal if we can recompute one dimension of the current
    // size from the other
    return current_size.height == current_size.width * content_size.height
        / content_size.width
        || current_size.width == current_size.height * content_size.width
        / content_size.height;
}

// return the optimal size of the window, with the following constraints:
//  - it attempts to keep at least one dimension of the current_size (i.e. it
//    crops the black borders)
//  - it keeps the aspect ratio
//  - it scales down to make it fit in the display_size
// static
struct size Screen::get_optimal_size(struct size current_size, struct size content_size) {
    if (content_size.width == 0 || content_size.height == 0) {
        // avoid division by 0
        return current_size;
    }

    struct size window_size;

    struct size display_size;
    if (!Screen::get_preferred_display_bounds(&display_size)) {
        // could not get display bounds, do not constraint the size
        window_size.width = current_size.width;
        window_size.height = current_size.height;
    }
    else {
        window_size.width = MIN(current_size.width, display_size.width);
        window_size.height = MIN(current_size.height, display_size.height);
    }

    if (Screen::is_optimal_size(window_size, content_size)) {
        return window_size;
    }

    bool keep_width = content_size.width * window_size.height
                    > content_size.height * window_size.width;
    if (keep_width) {
        // remove black borders on top and bottom
        window_size.height = content_size.height * window_size.width
            / content_size.width;
    }
    else {
        // remove black borders on left and right (or none at all if it already
        // fits)
        window_size.width = content_size.width * window_size.height
            / content_size.height;
    }

    return window_size;
}

// same as get_optimal_size(), but read the current size from the window
struct size Screen::get_optimal_window_size(struct size content_size) {
    struct size window_size = this->get_window_size();
    return get_optimal_size(window_size, content_size);
}

// initially, there is no current size, so use the frame size as current size
// req_width and req_height, if not 0, are the sizes requested by the user
//static
struct size Screen::get_initial_optimal_size(struct size content_size, uint16_t req_width, uint16_t req_height) {
    struct size window_size;
    if (!req_width && !req_height) {
        window_size = Screen::get_optimal_size(content_size, content_size);
    }
    else {
        if (req_width) {
            window_size.width = req_width;
        }
        else {
            // compute from the requested height
            window_size.width = (uint32_t)req_height * content_size.width
                / content_size.height;
        }
        if (req_height) {
            window_size.height = req_height;
        }
        else {
            // compute from the requested width
            window_size.height = (uint32_t)req_width * content_size.height
                / content_size.width;
        }
    }
    return window_size;
}

void Screen::update_content_rect() {
    int dw;
    int dh;
    SDL_GL_GetDrawableSize(this->window, &dw, &dh);

    struct size content_size = this->content_size;
    // The drawable size is the window size * the HiDPI scale
    struct size drawable_size = { dw, dh };

    SDL_Rect* rect = &this->rect;

    if (Screen::is_optimal_size(drawable_size, content_size)) {
        rect->x = 0;
        rect->y = 0;
        rect->w = drawable_size.width;
        rect->h = drawable_size.height;
        return;
    }

    bool keep_width = content_size.width * drawable_size.height
                    > content_size.height * drawable_size.width;
    if (keep_width) {
        rect->x = 0;
        rect->w = drawable_size.width;
        rect->h = drawable_size.width * content_size.height
            / content_size.width;
        rect->y = (drawable_size.height - rect->h) / 2;
    }
    else {
        rect->y = 0;
        rect->h = drawable_size.height;
        rect->w = drawable_size.height * content_size.width
            / content_size.height;
        rect->x = (drawable_size.width - rect->w) / 2;
    }
}


GLuint Screen::create_texture()
{
    /*SDL_Renderer* renderer = this->renderer;
    struct size size = this->frame_size;
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32, //SDL_PIXELFORMAT_YV12,
        SDL_TEXTUREACCESS_STREAMING,
        size.width, size.height);
    if (!texture) {
        return NULL;
    }

    if (this->mipmaps) {
        sc_opengl* gl = this->gl.get();

        SDL_GL_BindTexture(texture, NULL, NULL);

        // Enable trilinear filtering for downscaling
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
            GL_LINEAR_MIPMAP_LINEAR);
        gl->TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, -1.f);

        SDL_GL_UnbindTexture(texture);
    }

    return texture;*/
    GLenum  err;
    if (this->vao == -1) {
        glGenVertexArrays(1, &this->vao);
        glBindVertexArray(this->vao);
        glGenBuffers(1, &this->vert_buf);

        glBindBuffer(GL_ARRAY_BUFFER, this->vert_buf);
        float quad[20] = {
            -1.0f,  1.0f, 0.0f, 0.0f, 0.0f,
            -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,
             1.0f, -1.0f, 0.0f, 1.0f, 1.0f,
             1.0f,  1.0f, 0.0f, 1.0f, 0.0f
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        err = glGetError();
        //glVertexAttribPointer(this->vert_attrib, 3, GL_FLOAT, GL_FALSE, 20, BUFFER_OFFSET(0));
        glVertexPointer(3, GL_FLOAT, 20, BUFFER_OFFSET(0));
        err = glGetError();
        //glEnableVertexAttribArray(this->vert_attrib);
        glEnableClientState(GL_VERTEX_ARRAY);
        err = glGetError();
        //glVertexAttribPointer(this->tex_attrib, 2, GL_FLOAT, GL_FALSE, 20, BUFFER_OFFSET(12));
        glTexCoordPointer(2, GL_FLOAT, 20, BUFFER_OFFSET(12));
        err = glGetError();
        //glEnableVertexAttribArray(this->tex_attrib);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        err = glGetError();
        glGenBuffers(1, &this->elem_buf);
        err = glGetError();
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->elem_buf);
        err = glGetError();
        unsigned char elem[6] = {
            0, 1, 2,
            0, 2, 3
        };
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(elem), elem, GL_STATIC_DRAW);
        err = glGetError();
        glBindVertexArray(0);
        err = glGetError();
    }

    GLuint resultTex;
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &resultTex);
    glBindTexture(GL_TEXTURE_2D, resultTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, this->frame_size.width, this->frame_size.height,
        0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);


    uint64_t psize = 4 * ((uint64_t)this->frame_size.width) * this->frame_size.height;
    uint8_t* pixels = (uint8_t*)malloc(psize + 100);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, this->frame_size.width,
        this->frame_size.height, GL_BGRA, GL_UNSIGNED_BYTE,
        pixels);
    if (this->mipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    free(pixels);
    return resultTex;
}

uint32_t Screen::RefreshTimerCallback(uint32_t interval, void* param)
{
    Screen* screen = (Screen*)param;
    if (!screen->window || screen->frame_tex == -1) { return interval; }
    uint32_t now = SDL_GetTicks();
    if ((int)now - (int)screen->lastRender > (1000 / 25)) {

        static SDL_Event timer_update_event = {
            .type = EVENT_REFRESH,
        };
        SDL_PushEvent(&timer_update_event);
        screen->lastRender = now;
    }
    return interval;
}

Screen::Screen(const Window& window) : Window(window), frame_tex(-1), vao(-1), vert_buf(-1), elem_buf(-1), tex_attrib(-1), vert_attrib(-1), /*gl(nullptr),*/ glcontext(nullptr),
frame_size{ .width = 0,.height = 0 }, content_size{ .width = 0,.height = 0 }, resize_pending(false),
windowed_content_size{ .width = 0,.height = 0 }, rotation(0), rect{.x=0,.y=0,.w=0,.h=0},
has_frame(false),fullscreen(false),maximized(false),no_window(false),mipmaps(false), swsCtx(nullptr), lastRender(0), pixels{0}, worker(nullptr)
{
    this->refreshTimer = SDL_AddTimer(1000/25, RefreshTimerCallback, this);
    SDL_ShowWindow(this->window);
}

// virtual 
Screen::~Screen()
{
    if (this->refreshTimer) {
        SDL_RemoveTimer(this->refreshTimer);
    }
    if (this->glcontext) {
        if(this->frame_tex != -1) {
            SDL_GL_MakeCurrent(this->window, this->glcontext);
            glDeleteTextures(1, &this->frame_tex);
            //TODO: destroy other objects.
        }
        SDL_GL_DeleteContext(this->glcontext);
    }
    if (this->window) {
        SDL_DestroyWindow(this->window);
    }
    if (this->swsCtx) {
        sws_freeContext(this->swsCtx);
        this->swsCtx = NULL;
    }
    if (this->pixels[0]) {
        free(std::min(std::min(this->pixels[0], this->pixels[1]), this->pixels[2]));
        this->pixels[0] = NULL;
        this->pixels[1] = NULL;
        this->pixels[2] = NULL;
    }
    /*if (this->detection.tapi) {
        DestroyDetection(&this->detection);
    }*/
}

bool Screen::init_rendering(const char* window_title, size frame_size, bool always_on_top, int16_t window_x, int16_t window_y, uint16_t window_width, uint16_t window_height, bool window_borderless, uint8_t rotation, bool mipmaps)
{
    this->frame_size = frame_size;
    this->rotation = rotation;
    if (rotation) {
        LOGI("Initial display rotation set to %u", rotation);
    }
    struct size content_size = Screen::get_rotated_size(frame_size, this->rotation);
    this->content_size = content_size;

    struct size window_size =
        get_initial_optimal_size(content_size, window_width, window_height);
    uint32_t window_flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE;
#ifdef HIDPI_SUPPORT
    window_flags |= SDL_WINDOW_ALLOW_HIGHDPI;
#endif
    if (always_on_top) {
#ifdef SCRCPY_SDL_HAS_WINDOW_ALWAYS_ON_TOP
        window_flags |= SDL_WINDOW_ALWAYS_ON_TOP;
#else
        LOGW("The 'always on top' flag is not available "
            "(compile with SDL >= 2.0.5 to enable it)");
#endif
    }
    if (window_borderless) {
        window_flags |= SDL_WINDOW_BORDERLESS;
    }

    int x = window_x != SC_WINDOW_POSITION_UNDEFINED
        ? window_x : (int)SDL_WINDOWPOS_UNDEFINED;
    int y = window_y != SC_WINDOW_POSITION_UNDEFINED
        ? window_y : (int)SDL_WINDOWPOS_UNDEFINED;
    if (!this->window) {
        this->window = SDL_CreateWindow(window_title, x, y,
            window_size.width, window_size.height,
            window_flags | SDL_WINDOW_OPENGL);
        if (!this->window) {
            LOGC("Could not create window: %s", SDL_GetError());
            return false;
        }
    }
    else { // Modify an existing window.
        SDL_SetWindowSize(this->window, window_size.width, window_size.height);
        SDL_SetWindowPosition(this->window, x, y);
        SDL_SetWindowTitle(this->window, window_title);
    }
    this->glcontext = SDL_GL_CreateContext(this->window);
    if (!this->glcontext) {
        LOGC("Could not create GL context: %s", SDL_GetError());
        return false;
    }
    if (glewInit() != GLEW_OK) // Load Extension Loader Library (glad) here if needed. (gladLoadGL(glfwGetProcAddress);)
    {
        fprintf(stderr, "GLFW failed to init, CRITICAL ERROR!");
        return -3;//Have to back out due to severity of error
    }

    /*this->renderer = SDL_CreateRenderer(this->window, -1,
        SDL_RENDERER_ACCELERATED);
    if (!this->renderer) {
        LOGC("Could not create renderer: %s", SDL_GetError());
        //screen_destroy(screen); // ??
        return false;
    }

    SDL_RendererInfo renderer_info;
    int r = SDL_GetRendererInfo(this->renderer, &renderer_info);
    const char* renderer_name = r ? NULL : renderer_info.name;
    LOGI("Renderer: %s", renderer_name ? renderer_name : "(unknown)");*/

    // starts with "opengl"
    //this->use_opengl = renderer_name && !strncmp(renderer_name, "opengl", 6);
    /*if (this->use_opengl)*/ {
        //struct sc_opengl* gl = &screen->gl;
        //sc_opengl_init(gl);
        //this->gl = std::unique_ptr<sc_opengl>(new sc_opengl());

        //LOGI("OpenGL version: %s", gl->version);

        if (mipmaps) {
            bool supports_mipmaps = true;
               // this->gl->sc_opengl_version_at_least(3, 0, /* OpenGL 3.0+ */
               //     2, 0  /* OpenGL ES 2.0+ */);
            if (supports_mipmaps) {
                LOGI("Trilinear filtering enabled");
                this->mipmaps = true;
            }
            else {
                LOGW("Trilinear filtering disabled "
                    "(OpenGL 3.0+ or ES 2.0+ required)");
            }
        }
        else {
            LOGI("Trilinear filtering disabled");
        }
    }
   /* else {
        LOGD("Trilinear filtering disabled (not an OpenGL renderer)");
    }*/

    SDL_Surface* icon = read_xpm(icon_xpm);
    if (icon) {
        SDL_SetWindowIcon(this->window, icon);
        SDL_FreeSurface(icon);
    }
    else {
        LOGW("Could not load icon");
    }

    LOGI("Initial texture: %" PRIu16 "x%" PRIu16, frame_size.width,
        frame_size.height);
    this->frame_tex = this->create_texture();
    if (!this->frame_tex) {
        LOGC("Could not create texture: %s", SDL_GetError());
        //screen_destroy(screen); // ??
        return false;
    }

    // Reset the window size to trigger a SIZE_CHANGED event, to workaround
    // HiDPI issues with some SDL renderers when several displays having
    // different HiDPI scaling are connected
    SDL_SetWindowSize(this->window, window_size.width, window_size.height);

    this->update_content_rect();

    /*if (!this->console.get()) {
        this->console = std::make_unique<GLConsole>(*this->window, this->environment);
    }
    this->console->Init();*/
    //this->console->OpenConsole();

    return true;
}

void Screen::show_window()
{
    SDL_ShowWindow(this->window);
}

bool Screen::update_frame(VideoBuffer& vb)
{
    mutex_lock(vb.mutex);
    const AVFrame* frame = vb.consume_rendered_frame();
    struct size new_frame_size = { frame->width, frame->height };
    if (!this->prepare_for_frame(new_frame_size)) {
        mutex_unlock(vb.mutex);
        return false;
    }
    this->convert_frame(frame);
    this->update_texture();
    /*if (this->pixels_mutex.try_lock()) {
        std::swap(this->pixels[0], this->pixels[1]);
        this->pixels_mutex.unlock();
    }*/
    mutex_unlock(vb.mutex);

    this->render(false);
    return true;
}

void Screen::render(bool update_content_rect)
{
    this->lastRender = SDL_GetTicks();

    if (update_content_rect) {
        this->update_content_rect();
    }

    /*SDL_RenderClear(this->renderer);
    if (this->rotation == 0) {
        SDL_RenderCopy(this->renderer, this->texture, NULL, &this->rect);
    }
    else {
        // rotation in RenderCopyEx() is clockwise, while screen->rotation is
        // counterclockwise (to be consistent with --lock-video-orientation)
        int cw_rotation = (4 - this->rotation) % 4;
        double angle = 90 * cw_rotation;

        SDL_Rect* dstrect = NULL;
        SDL_Rect rect;
        if (this->rotation & 1) {
            rect.x = this->rect.x + (this->rect.w - this->rect.h) / 2;
            rect.y = this->rect.y + (this->rect.h - this->rect.w) / 2;
            rect.w = this->rect.h;
            rect.h = this->rect.w;
            dstrect = &rect;
        }
        else {
            assert(this->rotation == 2);
            dstrect = &this->rect;
        }

        SDL_RenderCopyEx(this->renderer, this->texture, NULL, dstrect,
            angle, NULL, SDL_FLIP_NONE);
    }
    //SDL_RenderPresent(this->renderer);*/
    SDL_GL_MakeCurrent(window, this->glcontext);

    glClearColor(((SDL_GetTicks() / 16) % 256) / 256.0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_TEXTURE_2D);
    //glEnable(GL_BLEND);
    ////glBlendFunc(GL_ONE_MINUS_SRC_COLOR, GL_SRC_COLOR); // white is transparent
    //glBlendFunc(GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR); // black is transparent

    //glUseProgram(this->program);
    glBindTexture(GL_TEXTURE_2D, this->frame_tex);
    glBindVertexArray(this->vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, BUFFER_OFFSET(0));
    glBindVertexArray(0);

 //   glDisable(GL_TEXTURE_2D);
    /*glColor4f(1, 0, 1, 0.1);
    glRectf(-1, -1 , 1, 1);
    glColor4f(1, 1, 0, 0.1);
    glRectf(0, 0, 100, 100);*/

/*    if (this->console.get()) {
        //if (!this->console->IsOpen()) { this->console->OpenConsole(); }
        this->console->RenderConsole(); 
    }*/
    Window::Render();

    if (this->worker) {
        //int window_width, window_height;
        //SDL_GetWindowSize(this->window, &window_width, &window_height);

        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0, this->frame_size.width, 0, this->frame_size.height, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        LockedBuffer<WorkerInfo> lb = this->worker->GetInfos().GetLast();
        WorkerInfo& wi = lb.Get();
        int oind = -1;
        for (const std::vector<RectProb>& ds : wi.detections) {
            oind++;
            for (const RectProb& r : ds) {
                if (r.p < 0.01) glColor4f(0, 0, 0, 0.1); // black
                else if (r.isExcluded) glColor4f(0.47, 0.22, 0.44, 0.15); // purple
                else if (r.p < 0.05) glColor4f(1, 0, 0, 0.2); // red
                else glColor4f(1, 0, 0, 0.5); // red
                glRectf(r.x, this->frame_size.height-r.y, r.x+r.width, this->frame_size.height-(r.y+r.height));
                { 
                    glColor4f(0, 0, 0, 1);
                    const std::string objName = this->worker->GetConfig().GetObjectName(oind);
                    this->console.GetFont().glPrintfFast(r.x, (this->frame_size.height - r.y)+2, 
                        std::format("{:.1f}%% {}",r.p*100, objName));
                }
            }
        }
        if (!wi.clickRect.empty()) {
            uint32_t clickAge = SDL_GetTicks() - wi.lastClickTime;
            if (clickAge < 5000) {
                glColor4f(0, 0, 1, (5000-clickAge)/(float)5000); // blue
                glRectf(wi.clickRect.x, this->frame_size.height - wi.clickRect.y, wi.clickRect.x + wi.clickRect.width, this->frame_size.height - (wi.clickRect.y + wi.clickRect.height));
            }
        }

        //glRectf(0, 0, 100, 100); // rectangle at bottom left.

        {
            // Switch to window coordinates from device coordinates.
            glMatrixMode(GL_PROJECTION);
            glPushMatrix();
            glLoadIdentity();
            size windowSize = this->get_window_size();
            glOrtho(0, windowSize.width, 0, windowSize.height, -1, 1);

            // Text color := inverse of the background.
            glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
            glColor4f(1, 1, 1, 1);

            // Write text.
            this->console.GetFont().Print(0, 10, this->worker->GetEstimator().GetString(SDL_GetTicks(), '\n'));

            // Restore original blending.
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            // Return to device coordinates.
            glPopMatrix();
        }

        GLfloat verts[] = { 0.0f, 0,
                            (GLfloat)200, 0,
                            (GLfloat)200, (GLfloat)200,
                            0.0f, (GLfloat)200 };
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, verts);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glDisableClientState(GL_VERTEX_ARRAY);

        //glRecti(1,1,2,2);
        //restore old matrices and properties...
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glColor4f(1, 1, 1, 1);
    }

    //glClearColor(1, 1, 0, 0);
    //glClear(GL_COLOR_BUFFER_BIT);
    /*glBegin(GL_QUADS);
    glColor3f(1, 1, 1);
    glVertex2f(-1, -1); glTexCoord2f(0, 0);
    glVertex2f(-1, 1); glTexCoord2f(1, 0);
    glVertex2f(1, 1); glTexCoord2f(1, 1);
    glVertex2f(1, -1); glTexCoord2f(0, 1);
    glEnd();
    glFlush();*/
    SDL_GL_SwapWindow(window);  // Swap the window/buffer to display the result.
}

void Screen::switch_fullscreen()
{
    uint32_t new_mode = this->fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (SDL_SetWindowFullscreen(this->window, new_mode)) {
        LOGW("Could not switch fullscreen mode: %s", SDL_GetError());
        return;
    }

    this->fullscreen = !this->fullscreen;
    if (!this->fullscreen && !this->maximized) {
        this->apply_pending_resize();
    }

    LOGD("Switched to %s mode", this->fullscreen ? "fullscreen" : "windowed");
    this->render(true);
}

void Screen::resize_to_fit()
{
    if (this->fullscreen || this->maximized) {
        return;
    }

    struct size optimal_size = this->get_optimal_window_size(this->content_size);
    SDL_SetWindowSize(this->window, optimal_size.width, optimal_size.height);
    LOGD("Resized to optimal size: %ux%u", optimal_size.width, optimal_size.height);
}

void Screen::resize_to_pixel_perfect()
{
    if (this->fullscreen) { return; }

    if (this->maximized) {
        SDL_RestoreWindow(this->window);
        this->maximized = false;
    }

    struct size content_size = this->content_size;
    SDL_SetWindowSize(this->window, content_size.width, content_size.height);
    LOGD("Resized to pixel-perfect: %ux%u", content_size.width, content_size.height);
}

void Screen::resize_for_content(struct size old_content_size, struct size new_content_size)
{
    struct size window_size = this->get_window_size();
    struct size target_size = {
        .width = (uint32_t)window_size.width * new_content_size.width
                / old_content_size.width,
        .height = (uint32_t)window_size.height * new_content_size.height
                / old_content_size.height,
    };
    target_size = get_optimal_size(target_size, new_content_size);
    this->set_window_size(target_size);
}

void Screen::set_content_size(struct size new_content_size)
{
    if (!this->fullscreen && !this->maximized) {
        this->resize_for_content(this->content_size, new_content_size);
    }
    else if (!this->resize_pending) {
        // Store the windowed size to be able to compute the optimal size once
        // fullscreen and maximized are disabled
        this->windowed_content_size = this->content_size;
        this->resize_pending = true;
    }

    this->content_size = new_content_size;
}

void Screen::apply_pending_resize()
{
    assert(!this->fullscreen);
    assert(!this->maximized);
    if (this->resize_pending) {
        this->resize_for_content(this->windowed_content_size, this->content_size);
        this->resize_pending = false;
    }
}

void Screen::set_rotation(unsigned rotation)
{
    assert(rotation < 4);
    if (rotation == this->rotation) {
        return;
    }

    struct size new_content_size = get_rotated_size(this->frame_size, rotation);

    this->set_content_size(new_content_size);

    this->rotation = rotation;
    LOGI("Display rotation set to %u", rotation);

    this->render(true);
}

// recreate the texture and resize the window if the frame size has changed
bool Screen::prepare_for_frame(struct size new_frame_size) {
    if (this->frame_size.width != new_frame_size.width
        || this->frame_size.height != new_frame_size.height) {
        // frame dimension changed, destroy texture
        //SDL_DestroyTexture(this->texture);
        SDL_GL_MakeCurrent(this->window, this->glcontext);
        glDeleteTextures(1, &this->frame_tex);

        this->frame_size = new_frame_size;

        struct size new_content_size = get_rotated_size(new_frame_size, this->rotation);
        this->set_content_size(new_content_size);

        this->update_content_rect();

        LOGI("New texture: %" PRIu16 "x%" PRIu16,
            this->frame_size.width, this->frame_size.height);
        this->frame_tex = this->create_texture();
        if (this->frame_tex == -1) {
            LOGC("Could not create texture: %s", SDL_GetError());
            return false;
        }

        if (this->swsCtx) { sws_freeContext(this->swsCtx); this->swsCtx = NULL; }
        if (this->pixels[0]) {
            free(std::min(std::min(this->pixels[0], this->pixels[1]), this->pixels[2]));
            this->pixels[0] = NULL;
            this->pixels[1] = NULL;
            this->pixels[2] = NULL;
        }

        //goto INIT_SWS;
    }
    else if (!this->swsCtx) {
        /*INIT_SWS:
                this->swsCtx = sws_getContext(new_frame_size.width-32, new_frame_size.height, AV_PIX_FMT_YUV420P,
                    new_frame_size.width-32, new_frame_size.height, AV_PIX_FMT_RGB24,
                    0, 0, 0, 0);
                this->pixels = malloc(3 * ((uint64_t)new_frame_size.width) * new_frame_size.height);
                for (int i = 0; i < 3 * ((uint64_t)new_frame_size.width) * new_frame_size.height; i++)this->pixels[i] = i;*/
    }

    return true;
}


// write the frame into the texture
void Screen::update_texture() { 

    /*uint32_t now = SDL_GetTicks();
    screen->robot->now = now;
    if (now > this->detection.nextDetection)
    {
        // TODO: do detection on different thread!
        detect(&this->detection, this->pixels, this->frame_size.width, this->frame_size.height, 4, rgb32_stride[0]);
        RobotControl(this->robot);
        if (now > screen->detection.nextDetection)
        {
            now = SDL_GetTicks();
            screen->detection.nextDetection = now + 1000; // Next detection in 1 second if the RobotControl didn't set it.
        }
        EstDisplayTradeEstimation(&screen->robot->est, now);
    }
    RobotProcessCommandQueue(screen->robot);
    AlarmUpdate(now, screen->robot->lastCommandTime, screen->robot->lastConfirmedTime);*/


    //SDL_Rect rect = { 0,0,this->frame_size.width , this->frame_size.height };
    //SDL_UpdateTexture(this->texture, &rect, this->pixels, this->frame_size.width * 4);

    SDL_GL_MakeCurrent(window, this->glcontext);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, this->frame_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, this->frame_size.width,
        this->frame_size.height, GL_BGRA, GL_UNSIGNED_BYTE,
        this->pixels[0]);

    if (this->mipmaps) {
        //assert(this->use_opengl);
        //glBindTexture(GL_TEXTURE_2D, this->frame_tex); //SDL_GL_BindTexture(this->frame_tex, NULL, NULL);
        glGenerateMipmap(GL_TEXTURE_2D);
        //SDL_GL_UnbindTexture(this->frame_tex);
    }

    std::lock_guard<std::mutex> lock(this->pixels_mutex);
    std::swap(pixels[0], pixels[1]);
}

void Screen::convert_frame(const AVFrame* frame)
{
    if (!this->swsCtx) {
        std::lock_guard<std::mutex> lock(this->pixels_mutex);
        this->swsCtx = sws_getContext(frame->width,
            frame->height, AV_PIX_FMT_YUV420P,
            frame->width, frame->height, AV_PIX_FMT_RGB32,
            SWS_POINT, NULL, NULL, NULL);
        uint64_t psize = 4 * ((uint64_t)frame->width) * frame->height;
        this->pixels[0] = (uint8_t*)malloc(psize*3 /*+100*/);
        this->pixels[1] = ((uint8_t*)this->pixels[0]) + psize;
        this->pixels[2] = ((uint8_t*)this->pixels[0]) + 2*psize;
    }

    uint8_t* rgb32[1] = { this->pixels[0] };
    int rgb32_stride[1] = { 4 * this->frame_size.width };
    sws_scale(this->swsCtx, frame->data, frame->linesize, 0, this->frame_size.height, rgb32, rgb32_stride);
}

/*std::function<std::tuple<uint8_t*, std::unique_lock<std::mutex>>()> Screen::GetGrabImageFunc()
{
    std::function<std::tuple<uint8_t*, std::unique_lock<std::mutex>>()> result = std::bind(Screen::GetLastImageFrame, this);
    return result;
}

std::tuple<uint8_t*, std::unique_lock<std::mutex>> Screen::GetLastImageFrame(Screen* screen)
{
    std::unique_lock<std::mutex> lock(screen->pixels_mutex);
    std::tuple<uint8_t*, std::unique_lock<std::mutex>> result{ screen->pixels[0vagy1], std::move(lock) };
    return result;
}*/
std::function<uint8_t*()> Screen::GetGrabImageFunc()
{
    std::function<uint8_t*()> result = std::bind(Screen::GetLastImageFrame, this);
    return result;
}
uint8_t* Screen::GetLastImageFrame(Screen* screen)
{
    //printf("Reading Last Screen Frame\n");
    std::lock_guard<std::mutex> lock(screen->pixels_mutex);
    std::swap(screen->pixels[1], screen->pixels[2]);
    return screen->pixels[2];
}

void Screen::SetWorker(Worker* worker)
{
    this->worker = worker;
}

void Screen::SetEnvironment(Environment* environment)
{
    this->environment = environment;
}


void Screen::handle_window_event(const SDL_WindowEvent* event)
{
    switch (event->event) {
    case SDL_WINDOWEVENT_EXPOSED:
        this->render(true);
        break;
    case SDL_WINDOWEVENT_SIZE_CHANGED:
        this->render(true);
        break;
    case SDL_WINDOWEVENT_MAXIMIZED:
        this->maximized = true;
        break;
    case SDL_WINDOWEVENT_RESTORED:
        if (this->fullscreen) {
            // On Windows, in maximized+fullscreen, disabling fullscreen
            // mode unexpectedly triggers the "restored" then "maximized"
            // events, leaving the window in a weird state (maximized
            // according to the events, but not maximized visually).
            break;
        }
        this->maximized = false;
        this->apply_pending_resize();
        break;
    }
}

point Screen::convert_window_to_frame_coords(int32_t x, int32_t y)
{
    this->hidpi_scale_coords(&x, &y);
    return this->convert_drawable_to_frame_coords(x, y);
}

point Screen::convert_drawable_to_frame_coords(int32_t x, int32_t y)
{
    unsigned rotation = this->rotation;
    assert(rotation < 4);

    int32_t w = this->content_size.width;
    int32_t h = this->content_size.height;


    x = (int64_t)(x - this->rect.x) * w / this->rect.w;
    y = (int64_t)(y - this->rect.y) * h / this->rect.h;

    // rotate
    struct point result;
    switch (rotation) {
    case 0:
        result.x = x;
        result.y = y;
        break;
    case 1:
        result.x = h - y;
        result.y = x;
        break;
    case 2:
        result.x = w - x;
        result.y = h - y;
        break;
    default:
        assert(rotation == 3);
        result.x = y;
        result.y = w - x;
        break;
    }
    return result;
}

void Screen::hidpi_scale_coords(int32_t* x, int32_t* y)
{
    // take the HiDPI scaling (dw/ww and dh/wh) into account
    int ww, wh, dw, dh;
    SDL_GetWindowSize(this->window, &ww, &wh);
    SDL_GL_GetDrawableSize(this->window, &dw, &dh);

    // scale for HiDPI (64 bits for intermediate multiplications)
    *x = (int64_t)*x * dw / ww;
    *y = (int64_t)*y * dh / wh;
}
