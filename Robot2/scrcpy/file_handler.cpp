#include "file_handler.h"
#include <stdexcept>
#include <cassert>
#include "util/log.h"
#include "util/lock.h"

#define DEFAULT_PUSH_TARGET "/sdcard/"

static void file_handler_request_destroy(struct file_handler_request* req)
{
	SDL_free(req->file);
}

static process_t
install_apk(const char* serial, const char* file) {
    return adb_install(serial, file);
}

static process_t
push_file(const char* serial, const char* file, const char* push_target) {
    return adb_push(serial, file, push_target);
}

static int run_file_handler(void* data)
{
    FileHandler* file_handler = (FileHandler*)data;

    for (;;) {
        mutex_lock(file_handler->mutex);
        file_handler->current_process = PROCESS_NONE;
        while (!file_handler->stopped && cbuf_is_empty(&file_handler->queue)) {
            cond_wait(file_handler->event_cond, file_handler->mutex);
        }
        if (file_handler->stopped) {
            // stop immediately, do not process further events
            mutex_unlock(file_handler->mutex);
            break;
        }
        struct file_handler_request req;
        bool non_empty = !cbuf_is_empty(&file_handler->queue); cbuf_take(&file_handler->queue, &req);
        assert(non_empty);
        (void)non_empty;

        process_t process;
        if (req.action == ACTION_INSTALL_APK) {
            LOGI("Installing %s...", req.file);
            process = install_apk(file_handler->serial, req.file);
        }
        else {
            LOGI("Pushing %s...", req.file);
            process = push_file(file_handler->serial, req.file,
                file_handler->push_target);
        }
        file_handler->current_process = process;
        mutex_unlock(file_handler->mutex);

        if (req.action == ACTION_INSTALL_APK) {
            if (process_check_success(process, "adb install")) {
                LOGI("%s successfully installed", req.file);
            }
            else {
                LOGE("Failed to install %s", req.file);
            }
        }
        else {
            if (process_check_success(process, "adb push")) {
                LOGI("%s successfully pushed to %s", req.file,
                    file_handler->push_target);
            }
            else {
                LOGE("Failed to push %s to %s", req.file,
                    file_handler->push_target);
            }
        }

        file_handler_request_destroy(&req);
    }
    return 0;
}


FileHandler::FileHandler(const char* serial, const char* push_target)
{
    cbuf_init(&this->queue);

    if (!(this->mutex = SDL_CreateMutex())) {
        throw std::exception("FileHandler mutex alloc failed.");
    }

    if (!(this->event_cond = SDL_CreateCond())) {
        SDL_DestroyMutex(this->mutex);
        throw std::exception("FileHandler cond alloc failed.");
    }

    if (serial) {
        this->serial = SDL_strdup(serial);
        if (!this->serial) {
            LOGW("Could not strdup serial");
            SDL_DestroyCond(this->event_cond);
            SDL_DestroyMutex(this->mutex);
            throw std::exception("FileHandler strdup serial failed.");
        }
    }
    else {
        this->serial = NULL;
    }

    // lazy initialization
    this->initialized = false;

    this->stopped = false;
    this->current_process = PROCESS_NONE;

    this->push_target = push_target ? push_target : DEFAULT_PUSH_TARGET;
}

FileHandler::~FileHandler()
{
    SDL_DestroyCond(this->event_cond);
    SDL_DestroyMutex(this->mutex);
    SDL_free(this->serial);

    struct file_handler_request req;
    while (!cbuf_is_empty(&this->queue)) {
        cbuf_take(&this->queue, &req)
            file_handler_request_destroy(&req);
    }
}


bool FileHandler::Start()
{
    LOGD("Starting file_handler thread");

    this->thread = SDL_CreateThread(run_file_handler, "file_handler", this);
    if (!this->thread) {
        LOGC("Could not start file_handler thread");
        return false;
    }

    return true;
}

void FileHandler::Stop()
{
    mutex_lock(this->mutex);
    this->stopped = true;
    cond_signal(this->event_cond);
    if (this->current_process != PROCESS_NONE) {
        if (!cmd_terminate(this->current_process)) {
            LOGW("Could not terminate install process");
        }
        cmd_simple_wait(this->current_process, NULL);
        this->current_process = PROCESS_NONE;
    }
    mutex_unlock(this->mutex);
}

void FileHandler::Join()
{
    SDL_WaitThread(this->thread, NULL);
}

bool FileHandler::Request(file_handler_action_t action, char* file)
{
    // start file_handler if it's used for the first time
    if (!this->initialized) {
        if (!this->Start()) {
            return false;
        }
        this->initialized = true;
    }

    LOGI("Request to %s %s", action == ACTION_INSTALL_APK ? "install" : "push",
        file);
    struct file_handler_request req = {
        .action = action,
        .file = file,
    };

    mutex_lock(this->mutex);
    bool was_empty = cbuf_is_empty(&this->queue);
    bool res; cbuf_push(&this->queue, req, res);
    if (was_empty) {
        cond_signal(this->event_cond);
    }
    mutex_unlock(this->mutex);
    return res;
}
