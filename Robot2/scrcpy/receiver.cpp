#include "receiver.h"

#include <cassert>
#include <SDL2/SDL_clipboard.h>
#include <stdexcept>

#include "device_msg.h"
#include "util/log.h"
//#include "util/lock.h"

Receiver::Receiver(socket_t control_socket)
{
    if (!(this->mutex = SDL_CreateMutex())) {
        throw std::exception("Receiver mutex alloc failed.");
    }
    this->control_socket = control_socket;
}

Receiver::~Receiver()
{
    SDL_DestroyMutex(this->mutex);
}

static void
process_msg(DeviceMsg* msg) {
    switch (msg->type) {
    case DEVICE_MSG_TYPE_CLIPBOARD: {
        char* current = SDL_GetClipboardText();
        bool same = current && !strcmp(current, msg->clipboard.text);
        SDL_free(current);
        if (same) {
            LOGD("Computer clipboard unchanged");
            return;
        }

        LOGI("Device clipboard copied");
        SDL_SetClipboardText(msg->clipboard.text);
        break;
    }
    }
}

static ssize_t
process_msgs(const unsigned char* buf, size_t len) {
    size_t head = 0;
    for (;;) {
        DeviceMsg msg;
        ssize_t r = msg.Deserialize(&buf[head], len - head, &msg);
        if (r == -1) {
            return -1;
        }
        if (r == 0) {
            return head;
        }

        process_msg(&msg);
        //device_msg_destroy(&msg); // Destroyed at the end of the for-loop.

        head += r;
        assert(head <= len);
        if (head == len) {
            return head;
        }
    }
}

static int
run_receiver(void* data) {
    Receiver* receiver = (Receiver*)data;

    static unsigned char buf[DEVICE_MSG_MAX_SIZE];
    size_t head = 0;

    for (;;) {
        assert(head < DEVICE_MSG_MAX_SIZE);
        ssize_t r = net_recv(receiver->control_socket, buf + head,
            DEVICE_MSG_MAX_SIZE - head);
        if (r <= 0) {
            LOGD("Receiver stopped");
            break;
        }

        head += r;
        ssize_t consumed = process_msgs(buf, head);
        if (consumed == -1) {
            // an error occurred
            break;
        }

        if (consumed) {
            head -= consumed;
            // shift the remaining data in the buffer
            memmove(buf, &buf[consumed], head);
        }
    }

    return 0;
}

bool Receiver::Start()
{
    LOGD("Starting receiver thread");

    this->thread = SDL_CreateThread(run_receiver, "receiver", this);
    if (!this->thread) {
        LOGC("Could not start receiver thread");
        return false;
    }

    return true;
}

void Receiver::Join()
{
    SDL_WaitThread(this->thread, NULL);
}
