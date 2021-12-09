#include "controller.h"
#include <stdexcept>
#include "util/lock.h"

Controller::Controller(socket_t control_socket):receiver(control_socket)
{
    //cbuf_init(&this->queue);

    if (!(this->mutex = SDL_CreateMutex())) {
        throw std::exception("Controller mutex alloc failed.");
    }

    if (!(this->msg_cond = SDL_CreateCond())) {
        SDL_DestroyMutex(this->mutex);
        throw std::exception("Controller cond alloc failed.");
    }

    this->control_socket = control_socket;
    this->stopped = false;
}

Controller::~Controller()
{
    SDL_DestroyCond(this->msg_cond);
    SDL_DestroyMutex(this->mutex);

    //while (!this->queue.empty()) { this->queue.pop(); } // this is not needed
    /*struct control_msg msg;
    while (!cbuf_is_empty(&this->queue)) {
        cbuf_take(&this->queue, &msg);
        control_msg_destroy(&msg);
    }

    receiver_destroy(&controller->receiver);*/
}

bool Controller::ProcessMsg(const ControlMsg* msg) {
    static unsigned char serialized_msg[CONTROL_MSG_MAX_SIZE];
    int length = msg->Serialize(serialized_msg); //control_msg_serialize(msg, serialized_msg);
    if (!length) {
        return false;
    }
    int w = net_send_all(this->control_socket, serialized_msg, length);
    return w == length;
}
static int
run_controller(void* data) {
    Controller* controller = (Controller*)data;

    for (;;) {
        mutex_lock(controller->mutex);
        while (!controller->stopped && controller->queue.empty()) {
            cond_wait(controller->msg_cond, controller->mutex);
        }
        if (controller->stopped) {
            // stop immediately, do not process further msgs
            mutex_unlock(controller->mutex);
            break;
        }
        ControlMsg& msg = controller->queue.front();
        /*bool non_empty = !cbuf_is_empty(&controller->queue); cbuf_take(&controller->queue, &msg);
        assert(non_empty);
        (void)non_empty;*/
        mutex_unlock(controller->mutex);

        bool ok = controller->ProcessMsg(&msg);
        controller->queue.pop(); //control_msg_destroy(&msg);
        if (!ok) {
            LOGD("Could not write msg to socket");
            break;
        }
    }
    return 0;
}

bool Controller::Start()
{
    LOGD("Starting controller thread");

    this->thread = SDL_CreateThread(run_controller, "controller", this);
    if (!this->thread) {
        LOGC("Could not start controller thread");
        return false;
    }

    if (!this->receiver.Start()) {
        this->Stop();
        SDL_WaitThread(this->thread, NULL);
        return false;
    }

    return true;
}

void Controller::Stop()
{
    mutex_lock(this->mutex);
    this->stopped = true;
    cond_signal(this->msg_cond);
    mutex_unlock(this->mutex);
}

void Controller::Join()
{
    SDL_WaitThread(this->thread, NULL);
    this->receiver.Join();
}

bool Controller::PushMsg(ControlMsg&& msg)
{
    mutex_lock(this->mutex);
    bool was_empty = this->queue.empty(); // cbuf_is_empty(&controller->queue);
    this->queue.push(std::move(msg)); //bool res;  cbuf_push(&controller->queue, *msg, res);
    if (was_empty) {
        cond_signal(this->msg_cond);
    }
    mutex_unlock(this->mutex);
    return true;
}
