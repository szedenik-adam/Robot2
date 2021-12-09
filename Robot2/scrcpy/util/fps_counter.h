#ifndef FPSCOUNTER_H
#define FPSCOUNTER_H

#include <atomic>
#include <stdint.h>
extern "C" {
#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_thread.h>
}

#include "../config.h"

class FrameCounter {
public:
    SDL_Thread* thread;
    SDL_mutex* mutex;
    SDL_cond* state_cond;

    // atomic so that we can check without locking the mutex
    // if the FPS counter is disabled, we don't want to lock unnecessarily
    std::atomic_bool started;

    // the following fields are protected by the mutex
    bool interrupted;
    unsigned nr_rendered;
    unsigned nr_skipped;
    uint32_t next_timestamp;

    FrameCounter();
    ~FrameCounter();

    bool Start();
    void Stop();
    bool IsStarted();

    // request to stop the thread (on quit)
    // must be called before fps_counter_join()
    void Interrupt();
    void Join();
    void AddRenderedFrame();
    void AddSkippedFrame();

    void SetStarted(bool started);
    void CheckIntervalExpired(uint32_t now);
    void DisplayFps();
};

#endif
