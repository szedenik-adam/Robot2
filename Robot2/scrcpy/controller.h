#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_thread.h>

#include "config.h"
#include "control_msg.h"
#include "receiver.h"
#include <queue>//#include "util/cbuf.h"
extern "C" {
#include "util/net.h"
}
//struct control_msg_queue CBUF(struct control_msg, 64);

class Controller {
public:
    socket_t control_socket;
    SDL_Thread* thread;
    SDL_mutex* mutex;
    SDL_cond* msg_cond;
    bool stopped;
    std::queue<ControlMsg> queue;//struct control_msg_queue queue;
    Receiver receiver;

    Controller(socket_t control_socket);
    ~Controller();
    bool Start();
    void Stop();
    void Join();
    bool PushMsg(ControlMsg&& msg);

    bool ProcessMsg(const ControlMsg* msg);
};

#endif
