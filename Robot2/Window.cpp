#include "Window.h"
#include "scrcpy/scrcpy.h"

Window::Window() :
    window(Window::_CreateWindow()),
    console(*window)
{
    console.Init();
}

Window::~Window()
{
}

bool Window::ManageConsoleKey(SDL_Keycode keycode, SDL_Scancode scancode, uint16_t mod, bool isDown)
{
    if (isDown) {
        if (keycode == SDLK_TAB) {
            this->console.ToggleConsole();
            //printf("Console is "); printf(this->screen->console->IsOpen() ? "opened.\n" : "closed.\n");
            return true;
        }
        else if (this->console.IsOpen())
        {
            if (keycode < 256) {
                this->console.KeyboardFunc(keycode, mod);
            }
            else {
                this->console.SpecialFunc(scancode, mod);
            }
            return true;
        }
    }
    return false; // Keyboard event is not for the console.
}
//#include <Windows.h>
//#include <GL/glew.h>
//#include <GL/GL.h>
void Window::Render()
{
    glDisable(GL_TEXTURE_2D);
    this->console.RenderConsole();
}

void Window::GetClientSize(int* w, int* h)
{
    SDL_GL_GetDrawableSize(this->window, w, h);
}

SDL_Window* Window::_CreateWindow()
{
    Window::InitSDL();
    int x = SDL_WINDOWPOS_UNDEFINED;
    int y = SDL_WINDOWPOS_UNDEFINED;
    SDL_Window* w = SDL_CreateWindow("Main window", x, y, 600, 800, SDL_WINDOW_OPENGL);
    return w;
}

void Window::InitSDL()
{
    if (Window::sdl_initialized) return;
    scrcpyOptions::sdl_init_and_configure(true, nullptr, false);
    Window::sdl_initialized = true;
}
