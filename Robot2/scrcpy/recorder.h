#ifndef RECORDER_H
#define RECORDER_H

extern "C" {
#include <libavformat/avformat.h>
}
#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_thread.h>
#include "config.h"
#include "common.h"
//#include "util/queue.h"
#include <queue>

/*struct record_packet {
    AVPacket packet;
    struct record_packet* next;
};*/

//struct recorder_queue QUEUE(struct record_packet);

class Recorder {
public:
    char* filename;
    enum sc_record_format format;
    AVFormatContext* ctx;
    struct size declared_frame_size;
    bool header_written;

    SDL_Thread* thread;
    SDL_mutex* mutex;
    SDL_cond* queue_cond;
    bool stopped; // set on recorder_stop() by the stream reader
    bool failed; // set on packet write failure
    std::queue<AVPacket> queue;//struct recorder_queue queue;

    // we can write a packet only once we received the next one so that we can
    // set its duration (next_pts - current_pts)
    // "previous" is only accessed from the recorder thread, so it does not
    // need to be protected by the mutex
    AVPacket previous;

    Recorder(const char* filename, enum sc_record_format format, struct size declared_frame_size);
    ~Recorder();
    bool Open(const AVCodec* input_codec);
    void Close();
    bool Start();
    void Stop();
    void Join();
    bool Push(AVPacket* packet); 

    bool WriteHeader(const AVPacket* packet);
    void RescalePacket(AVPacket* packet);
    bool Write(AVPacket* packet);
};

#endif
