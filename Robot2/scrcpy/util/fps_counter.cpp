#include "fps_counter.h"
#include <stdexcept>
#include "lock.h"
#include "log.h"
extern "C" {
#include <SDL2/SDL_timer.h>
}
#include <cassert>

#define FPS_COUNTER_INTERVAL_MS 1000

FrameCounter::FrameCounter() : started(false)
{
    this->mutex = SDL_CreateMutex();
    if (!this->mutex) {
        throw std::exception("FrameCounter init failed.");
    }

    this->state_cond = SDL_CreateCond();
    if (!this->state_cond) {
        SDL_DestroyMutex(this->mutex);
        throw std::exception("FrameCounter init failed.");
    }

    this->thread = NULL;
    //atomic_init(&this->started, 0);
    // no need to initialize the other fields, they are unused until started
}

FrameCounter::~FrameCounter()
{
    SDL_DestroyCond(this->state_cond);
    SDL_DestroyMutex(this->mutex);
}
void FrameCounter::SetStarted(bool started) {
    this->started.store(started, std::memory_order_release);
    //atomic_store_explicit(&this->started, started, memory_order_release);
}

// must be called with mutex locked
void FrameCounter::DisplayFps() {
    unsigned rendered_per_second =
        this->nr_rendered * 1000 / FPS_COUNTER_INTERVAL_MS;
    if (this->nr_skipped) {
        LOGI("%u fps (+%u frames skipped)", rendered_per_second,
            this->nr_skipped);
    }
    else {
        LOGI("%u fps", rendered_per_second);
    }
}

// must be called with mutex locked
void FrameCounter::CheckIntervalExpired(uint32_t now) {
    if (now < this->next_timestamp) {
        return;
    }

    this->DisplayFps();
    this->nr_rendered = 0;
    this->nr_skipped = 0;
    // add a multiple of the interval
    uint32_t elapsed_slices =
        (now - this->next_timestamp) / FPS_COUNTER_INTERVAL_MS + 1;
    this->next_timestamp += FPS_COUNTER_INTERVAL_MS * elapsed_slices;
}

static int run_fps_counter(void* data) {
    FrameCounter* counter = static_cast<FrameCounter*>(data);

    mutex_lock(counter->mutex);
    while (!counter->interrupted) {
        while (!counter->interrupted && !counter->IsStarted()) {
            cond_wait(counter->state_cond, counter->mutex);
        }
        while (!counter->interrupted && counter->IsStarted()) {
            uint32_t now = SDL_GetTicks();
            counter->CheckIntervalExpired(now);

            assert(counter->next_timestamp > now);
            uint32_t remaining = counter->next_timestamp - now;

            // ignore the reason (timeout or signaled), we just loop anyway
            cond_wait_timeout(counter->state_cond, counter->mutex, remaining);
        }
    }
    mutex_unlock(counter->mutex);
    return 0;
}

bool FrameCounter::Start()
{
    mutex_lock(this->mutex);
    this->next_timestamp = SDL_GetTicks() + FPS_COUNTER_INTERVAL_MS;
    this->nr_rendered = 0;
    this->nr_skipped = 0;
    mutex_unlock(this->mutex);

    this->SetStarted(true);
    cond_signal(this->state_cond);

    // this->thread is always accessed from the same thread, no need to lock
    if (!this->thread) {
        this->thread =
            SDL_CreateThread(run_fps_counter, "fps counter", this);
        if (!this->thread) {
            LOGE("Could not start FPS counter thread");
            return false;
        }
    }

    return true;
}

void FrameCounter::Stop()
{
    this->SetStarted(false);
    cond_signal(this->state_cond);
}

bool FrameCounter::IsStarted()
{
    return this->started.load(std::memory_order_acquire);
    //return atomic_load_explicit(&this->started, memory_order_acquire);
}

void FrameCounter::Interrupt()
{
    if (!this->thread) {
        return;
    }

    mutex_lock(this->mutex);
    this->interrupted = true;
    mutex_unlock(this->mutex);
    // wake up blocking wait
    cond_signal(this->state_cond);
}

void FrameCounter::Join()
{
    if (this->thread) {
        SDL_WaitThread(this->thread, NULL);
    }
}

void FrameCounter::AddRenderedFrame()
{
    if (!this->IsStarted()) {
        return;
    }

    mutex_lock(this->mutex);
    uint32_t now = SDL_GetTicks();
    this->CheckIntervalExpired(now);
    ++this->nr_rendered;
    mutex_unlock(this->mutex);
}

void FrameCounter::AddSkippedFrame()
{
    if (!this->IsStarted()) {
        return;
    }

    mutex_lock(this->mutex);
    uint32_t now = SDL_GetTicks();
    this->CheckIntervalExpired(now);
    ++this->nr_skipped;
    mutex_unlock(this->mutex);
}
