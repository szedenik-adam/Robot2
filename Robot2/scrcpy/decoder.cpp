#include "decoder.h"
#include "video_buffer.h"
#include "util/log.h"
#include <SDL2/SDL_events.h>
#include "compat.h"
#include "events.h"

Decoder::Decoder(VideoBuffer& video_buffer): video_buffer(video_buffer)
{
}

bool Decoder::Open(const AVCodec* codec)
{
    this->codec_ctx = avcodec_alloc_context3(codec);
    if (!this->codec_ctx) {
        LOGC("Could not allocate decoder context");
        return false;
    }

    if (avcodec_open2(this->codec_ctx, codec, 0) < 0) {
        LOGE("Could not open codec");
        avcodec_free_context(&this->codec_ctx);
        return false;
    }

    return true;
}

void Decoder::Close()
{
    avcodec_close(this->codec_ctx);
    avcodec_free_context(&this->codec_ctx);
}

bool Decoder::Push(const AVPacket* packet)
{
    // the new decoding/encoding API has been introduced by:
    // <http://git.videolan.org/?p=ffmpeg.git;a=commitdiff;h=7fc329e2dd6226dfecaa4a1d7adf353bf2773726>
#ifdef SCRCPY_LAVF_HAS_NEW_ENCODING_DECODING_API
    int ret;
    if ((ret = avcodec_send_packet(this->codec_ctx, packet)) < 0) {
        LOGE("Could not send video packet: %d", ret);
        return false;
    }

    ret = avcodec_receive_frame(this->codec_ctx, this->video_buffer.decoding_frame);
    if (!ret) {
        // a frame was received
        this->PushFrame();
    }
    else if (ret != AVERROR(EAGAIN)) {
        LOGE("Could not receive video frame: %d", ret);
        return false;
    }
#else
    int got_picture;
    int len = avcodec_decode_video2(this->codec_ctx,
        this->video_buffer.decoding_frame,
        &got_picture,
        packet);
    if (len < 0) {
        LOGE("Could not decode video packet: %d", len);
        return false;
    }
    if (got_picture) {
        this->PushFrame();
    }
#endif
    return true;
}

void Decoder::Interrupt()
{
}

void Decoder::PushFrame()
{
    bool previous_frame_skipped;
    this->video_buffer.offer_decoded_frame(&previous_frame_skipped);
    if (previous_frame_skipped) {
        // the previous EVENT_NEW_FRAME will consume this frame
        return;
    }
    static SDL_Event new_frame_event = {
        .type = EVENT_NEW_FRAME,
    };
    SDL_PushEvent(&new_frame_event);
}
