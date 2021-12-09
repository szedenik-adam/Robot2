#pragma once

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "console/GLConsole.h"

class Environment;

class Window {
	inline static bool sdl_initialized = false;
	static SDL_Window* _CreateWindow();
	static void InitSDL();

protected:
	SDL_Window* window;
	GLConsole console;
	Environment* environment;
public:
	Window();
	virtual ~Window();

	bool ManageConsoleKey(SDL_Keycode keycode, SDL_Scancode scancode, uint16_t mod, bool isDown);

	void Render();

	void GetClientSize(int* w, int* h);

	//void SetEnvironment(Environment* env);

};