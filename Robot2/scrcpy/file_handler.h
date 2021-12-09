#ifndef FILE_HANDLER_H
#define FILE_HANDLER_H

#include <stdbool.h>
#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_thread.h>

#include "config.h"
#include "command.h"
#include "util/cbuf.h"

typedef enum {
    ACTION_INSTALL_APK,
    ACTION_PUSH_FILE,
} file_handler_action_t;

struct file_handler_request {
    file_handler_action_t action;
    char* file;
};

struct file_handler_request_queue CBUF(struct file_handler_request, 16);

class FileHandler {
public:
    char* serial;
    const char* push_target;
    SDL_Thread* thread;
    SDL_mutex* mutex;
    SDL_cond* event_cond;
    bool stopped;
    bool initialized;
    process_t current_process;
    struct file_handler_request_queue queue;

    FileHandler(const char* serial, const char* push_target);
    ~FileHandler();
    bool Start();
    void Stop();
    void Join();
    // take ownership of file, and will SDL_free() it
    bool Request(file_handler_action_t action, char* file);
};

#endif
