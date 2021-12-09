#ifndef DECODER_H
#define DECODER_H

extern "C" {
#include <libavformat/avformat.h>
}
#include "config.h"

class VideoBuffer;

class Decoder {
public:
    VideoBuffer& video_buffer;
    AVCodecContext* codec_ctx;

    Decoder(VideoBuffer& video_buffer);
    bool Open(const AVCodec* codec);
    void Close();
    bool Push(const AVPacket* packet);
    void Interrupt();

    void PushFrame();
};

#endif
