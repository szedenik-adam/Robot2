#include "video_buffer.h"
extern "C" {
#include <libavutil/avutil.h>
#include <libavformat/avformat.h>
}
#include <cassert>
#include <stdexcept>
#include "util/lock.h"

struct VideoBufferException : public std::exception
{
    const char* what() const throw ()
    {
        return "VideoBufferException";
    }
};

VideoBuffer::VideoBuffer(FrameCounter* fps_counter, bool render_expired_frames)
{
    this->fps_counter = fps_counter;

    if (!(this->decoding_frame = av_frame_alloc())) {
        goto error_0;
    }

    if (!(this->rendering_frame = av_frame_alloc())) {
        goto error_1;
    }

    if (!(this->mutex = SDL_CreateMutex())) {
        goto error_2;
    }

    this->render_expired_frames = render_expired_frames;
    if (render_expired_frames) {
        if (!(this->rendering_frame_consumed_cond = SDL_CreateCond())) {
            SDL_DestroyMutex(this->mutex);
            goto error_2;
        }
        // interrupted is not used if expired frames are not rendered
        // since offering a frame will never block
        this->interrupted = false;
    }

    // there is initially no rendering frame, so consider it has already been
    // consumed
    this->rendering_frame_consumed = true;
    return;

error_2:
    av_frame_free(&this->rendering_frame);
error_1:
    av_frame_free(&this->decoding_frame);
error_0:
    throw VideoBufferException();
}

VideoBuffer::~VideoBuffer()
{
    if (this->render_expired_frames) {
        SDL_DestroyCond(this->rendering_frame_consumed_cond);
    }
    SDL_DestroyMutex(this->mutex);
    av_frame_free(&this->rendering_frame);
    av_frame_free(&this->decoding_frame);
}

void VideoBuffer::swap_frames() {
    std::swap(this->decoding_frame, this->rendering_frame);
}

void VideoBuffer::offer_decoded_frame(bool* previous_frame_skipped)
{
    mutex_lock(this->mutex);
    if (this->render_expired_frames) {
        // wait for the current (expired) frame to be consumed
        while (!this->rendering_frame_consumed && !this->interrupted) {
            cond_wait(this->rendering_frame_consumed_cond, this->mutex);
        }
    }
    else if (!this->rendering_frame_consumed) {
        this->fps_counter->AddSkippedFrame();
    }

    this->swap_frames();

    *previous_frame_skipped = !this->rendering_frame_consumed;
    this->rendering_frame_consumed = false;

    mutex_unlock(this->mutex);
}

const AVFrame* VideoBuffer::consume_rendered_frame()
{
    assert(!this->rendering_frame_consumed);
    this->rendering_frame_consumed = true;
    this->fps_counter->AddRenderedFrame();
    if (this->render_expired_frames) {
        // unblock video_buffer_offer_decoded_frame()
        cond_signal(this->rendering_frame_consumed_cond);
    }
    return this->rendering_frame;
}

void VideoBuffer::video_buffer_interrupt()
{
    if (this->render_expired_frames) {
        mutex_lock(this->mutex);
        this->interrupted = true;
        mutex_unlock(this->mutex);
        // wake up blocking wait
        cond_signal(this->rendering_frame_consumed_cond);
    }
}
