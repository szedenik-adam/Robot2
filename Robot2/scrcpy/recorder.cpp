#include "recorder.h"

#include <cassert>
#include <stdexcept>
extern "C" {
#include <libavutil/time.h>
}
#include "config.h"
#include "compat.h"
#include "scrcpy.h"
#include "util/lock.h"
#include "util/log.h"

static const AVRational SCRCPY_TIME_BASE = { 1, 1000000 }; // timestamps in us

static const AVOutputFormat*
find_muxer(const char* name) {
#ifdef SCRCPY_LAVF_HAS_NEW_MUXER_ITERATOR_API
    void* opaque = NULL;
#endif
    const AVOutputFormat* oformat = NULL;
    do {
#ifdef SCRCPY_LAVF_HAS_NEW_MUXER_ITERATOR_API
        oformat = av_muxer_iterate(&opaque);
#else
        oformat = av_oformat_next(oformat);
#endif
        // until null or with name "mp4"
    } while (oformat && strcmp(oformat->name, name));
    return oformat;
}

/*static struct record_packet*
record_packet_new(const AVPacket* packet) {
    record_packet* rec = (record_packet*)SDL_malloc(sizeof(*rec));
    if (!rec) {
        return NULL;
    }

    // av_packet_ref() does not initialize all fields in old FFmpeg versions
    // See <https://github.com/Genymobile/scrcpy/issues/707>
    av_init_packet(&rec->packet);

    if (av_packet_ref(&rec->packet, packet)) {
        SDL_free(rec);
        return NULL;
    }
    return rec;
}*/

/*static void
record_packet_delete(struct record_packet* rec) {
    av_packet_unref(&rec->packet);
    SDL_free(rec);
}

static void
recorder_queue_clear(struct recorder_queue* queue) {
    while (!queue_is_empty(queue)) {
        struct record_packet* rec;
        queue_take(queue, next, &rec);
        record_packet_delete(rec);
    }
}*/

Recorder::Recorder(const char* filename, sc_record_format format, size declared_frame_size)
{
    this->filename = SDL_strdup(filename);
    if (!this->filename) {
        LOGE("Could not strdup filename");
        throw std::exception("Recorder strdup failed.");
    }

    this->mutex = SDL_CreateMutex();
    if (!this->mutex) {
        LOGC("Could not create mutex");
        SDL_free(this->filename);
        throw std::exception("Recorder mutex alloc failed.");
    }

    this->queue_cond = SDL_CreateCond();
    if (!this->queue_cond) {
        LOGC("Could not create cond");
        SDL_DestroyMutex(this->mutex);
        SDL_free(this->filename);
        throw std::exception("Recorder cond alloc failed.");
    }

    //queue_init(&this->queue);
    this->stopped = false;
    this->failed = false;
    this->format = format;
    this->declared_frame_size = declared_frame_size;
    this->header_written = false;
    this->previous = {0};
}


Recorder::~Recorder()
{
    SDL_DestroyCond(this->queue_cond);
    SDL_DestroyMutex(this->mutex);
    SDL_free(this->filename);
}

static const char*
recorder_get_format_name(enum sc_record_format format) {
    switch (format) {
    case SC_RECORD_FORMAT_MP4: return "mp4";
    case SC_RECORD_FORMAT_MKV: return "matroska";
    default: return NULL;
    }
}

bool Recorder::Open(const AVCodec* input_codec)
{
    const char* format_name = recorder_get_format_name(this->format);
    assert(format_name);
    const AVOutputFormat* format = find_muxer(format_name);
    if (!format) {
        LOGE("Could not find muxer");
        return false;
    }

    this->ctx = avformat_alloc_context();
    if (!this->ctx) {
        LOGE("Could not allocate output context");
        return false;
    }

    // contrary to the deprecated API (av_oformat_next()), av_muxer_iterate()
    // returns (on purpose) a pointer-to-const, but AVFormatContext.oformat
    // still expects a pointer-to-non-const (it has not be updated accordingly)
    // <https://github.com/FFmpeg/FFmpeg/commit/0694d8702421e7aff1340038559c438b61bb30dd>
    this->ctx->oformat = (AVOutputFormat*)format;

    av_dict_set(&this->ctx->metadata, "comment",
        "Recorded by scrcpy " SCRCPY_VERSION, 0);

    AVStream* ostream = avformat_new_stream(this->ctx, input_codec);
    if (!ostream) {
        avformat_free_context(this->ctx);
        return false;
    }

#ifdef SCRCPY_LAVF_HAS_NEW_CODEC_PARAMS_API
    ostream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    ostream->codecpar->codec_id = input_codec->id;
    ostream->codecpar->format = AV_PIX_FMT_YUV420P;
    ostream->codecpar->width = this->declared_frame_size.width;
    ostream->codecpar->height = this->declared_frame_size.height;
#else
    ostream->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    ostream->codec->codec_id = input_codec->id;
    ostream->codec->pix_fmt = AV_PIX_FMT_YUV420P;
    ostream->codec->width = this->declared_frame_size.width;
    ostream->codec->height = this->declared_frame_size.height;
#endif

    int ret = avio_open(&this->ctx->pb, this->filename,
        AVIO_FLAG_WRITE);
    if (ret < 0) {
        LOGE("Failed to open output file: %s", this->filename);
        // ostream will be cleaned up during context cleaning
        avformat_free_context(this->ctx);
        return false;
    }

    LOGI("Recording started to %s file: %s", format_name, this->filename);

    return true;
}

void Recorder::Close()
{
    if (this->header_written) {
        int ret = av_write_trailer(this->ctx);
        if (ret < 0) {
            LOGE("Failed to write trailer to %s", this->filename);
            this->failed = true;
        }
    }
    else {
        // the recorded file is empty
        this->failed = true;
    }
    avio_close(this->ctx->pb);
    avformat_free_context(this->ctx);

    if (this->failed) {
        LOGE("Recording failed to %s", this->filename);
    }
    else {
        const char* format_name = recorder_get_format_name(this->format);
        LOGI("Recording complete to %s file: %s", format_name, this->filename);
    }
}

bool Recorder::WriteHeader(const AVPacket* packet) {
    AVStream* ostream = this->ctx->streams[0];

    uint8_t* extradata = (uint8_t*)av_malloc(packet->size * sizeof(uint8_t));
    if (!extradata) {
        LOGC("Could not allocate extradata");
        return false;
    }

    // copy the first packet to the extra data
    memcpy(extradata, packet->data, packet->size);

#ifdef SCRCPY_LAVF_HAS_NEW_CODEC_PARAMS_API
    ostream->codecpar->extradata = extradata;
    ostream->codecpar->extradata_size = packet->size;
#else
    ostream->codec->extradata = extradata;
    ostream->codec->extradata_size = packet->size;
#endif

    int ret = avformat_write_header(this->ctx, NULL);
    if (ret < 0) {
        LOGE("Failed to write header to %s", this->filename);
        return false;
    }

    return true;
}

void Recorder::RescalePacket(AVPacket* packet) {
    AVStream* ostream = this->ctx->streams[0];
    av_packet_rescale_ts(packet, SCRCPY_TIME_BASE, ostream->time_base);
}

bool Recorder::Write(AVPacket* packet) {
    if (!this->header_written) {
        if (packet->pts != AV_NOPTS_VALUE) {
            LOGE("The first packet is not a config packet");
            return false;
        }
        bool ok = this->WriteHeader(packet);
        if (!ok) {
            return false;
        }
        this->header_written = true;
        return true;
    }

    if (packet->pts == AV_NOPTS_VALUE) {
        // ignore config packets
        return true;
    }

    this->RescalePacket(packet);
    return av_write_frame(this->ctx, packet) >= 0;
}

static int run_recorder(void* data) {
    Recorder* recorder = (Recorder*)data;

    for (;;) {
        mutex_lock(recorder->mutex);

        while (!recorder->stopped && recorder->queue.empty()) {
            cond_wait(recorder->queue_cond, recorder->mutex);
        }

        // if stopped is set, continue to process the remaining events (to
        // finish the recording) before actually stopping

        if (recorder->stopped && recorder->queue.empty()) {
            mutex_unlock(recorder->mutex);
            AVPacket* lastPacket = &recorder->previous;

            // assign an arbitrary duration to the last packet
            lastPacket->duration = 100000;
            bool ok = recorder->Write(lastPacket);
            if (!ok) {
                // failing to write the last frame is not very serious, no
                // future frame may depend on it, so the resulting file
                // will still be valid
                LOGW("Could not record last packet");
            }
            av_packet_unref(lastPacket); //record_packet_delete(last);
            break;
        }

        AVPacket recPacket = recorder->queue.front(); recorder->queue.pop(); //queue_take(&recorder->queue, next, &rec);

        mutex_unlock(recorder->mutex);

        // recorder->previous is only written from this thread, no need to lock
        AVPacket previous = recorder->previous; //struct record_packet* previous = recorder->previous;
        recorder->previous = recPacket;

        /*if (!previous) {
            // we just received the first packet
            continue;
        }*/

        // config packets have no PTS, we must ignore them
        if (recPacket.pts != AV_NOPTS_VALUE
            && previous.pts != AV_NOPTS_VALUE) {
            // we now know the duration of the previous packet
            previous.duration = recPacket.pts - previous.pts;
        }

        bool ok = recorder->Write(&previous);
        av_packet_unref(&previous); //record_packet_delete(previous);
        if (!ok) {
            LOGE("Could not record packet");

            mutex_lock(recorder->mutex);
            recorder->failed = true;
            // discard pending packets
            while (!recorder->queue.empty()) {//recorder_queue_clear(&recorder->queue);
                AVPacket& p = recorder->queue.front();
                av_packet_unref(&p);
                recorder->queue.pop();
            }
            mutex_unlock(recorder->mutex);
            break;
        }

    }

    LOGD("Recorder thread ended");

    return 0;
}

bool Recorder::Start()
{
    LOGD("Starting recorder thread");

    this->thread = SDL_CreateThread(run_recorder, "recorder", this);
    if (!this->thread) {
        LOGC("Could not start recorder thread");
        return false;
    }

    return true;
}

void Recorder::Stop()
{
    mutex_lock(this->mutex);
    this->stopped = true;
    cond_signal(this->queue_cond);
    mutex_unlock(this->mutex);
}

void Recorder::Join()
{
    SDL_WaitThread(this->thread, NULL);
}

bool Recorder::Push(AVPacket* packet)
{
    mutex_lock(this->mutex);
    assert(!this->stopped);

    if (this->failed) {
        // reject any new packet (this will stop the stream)
        mutex_unlock(this->mutex);
        return false;
    }

    /*struct record_packet* rec = record_packet_new(packet);
    if (!rec) {
        LOGC("Could not allocate record packet");
        mutex_unlock(this->mutex);
        return false;
    }*/
    
    this->queue.emplace();
    AVPacket& addedPacket = this->queue.back();
    av_init_packet(&addedPacket);
    av_packet_ref(&addedPacket, packet);
    //this->queue.push(p); //queue_push(&this->queue, next, rec);
    cond_signal(this->queue_cond);

    mutex_unlock(this->mutex);
    return true;
}
