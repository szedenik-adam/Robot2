#ifndef RECEIVER_H
#define RECEIVER_H

#include <stdbool.h>
#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_thread.h>

#include "config.h"
extern "C"{
#include "util/net.h"
}
// receive events from the device
// managed by the controller
class Receiver {
public:
    socket_t control_socket;
    SDL_Thread* thread;
    SDL_mutex* mutex;

    Receiver(socket_t control_socket);
    ~Receiver();
    bool Start();
    // no receiver_stop(), it will automatically stop on control_socket shutdown
    void Join();
};

#endif
