#ifndef STREAM_H
#define STREAM_H

#include <stdint.h>
extern "C" {
#include <libavformat/avformat.h>
#include "util/net.h"
}
#include <SDL2/SDL_atomic.h>
#include <SDL2/SDL_thread.h>

#include "config.h"

class Decoder; class Recorder;

class Stream {
public:
    socket_t socket;
    SDL_Thread *thread;
    Decoder* decoder;
    Recorder* recorder;
    AVCodecContext *codec_ctx;
    AVCodecParserContext *parser;
    // successive packets may need to be concatenated, until a non-config
    // packet is available
    bool has_pending;
    AVPacket pending;

    Stream(socket_t socket, Decoder* decoder, Recorder* recorder);
    bool Start();
    void Stop();
    void Join();

};

#endif
