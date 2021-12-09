#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>
#include <atomic>
#include <SDL2/SDL_thread.h>

#include "config.h"
#include "command.h"
#include "common.h"
//#include "scrcpy.h"
#include "util/log.h"
extern "C" {
#include "util/net.h"
}

class Server {
public:
    char* serial;
    process_t process;
    SDL_Thread* wait_server_thread;
    std::atomic_flag server_socket_closed;

    SDL_mutex* mutex;
    SDL_cond* process_terminated_cond;
    bool process_terminated;

    socket_t server_socket; // only used if !tunnel_forward
    socket_t video_socket;
    socket_t control_socket;
    struct port_range port_range;
    uint16_t local_port; // selected from port_range
    bool tunnel_enabled;
    bool tunnel_forward; // use "adb forward" instead of "adb reverse"

    // init default values
    Server();
    // close and release sockets
    ~Server();

    // push, enable tunnel et start the server
    bool Start(const char* serial, const struct server_params* params);

    // block until the communication with the server is established
    bool ConnectTo();

    // disconnect and kill the server process
    void Stop();

    bool DisableTunnel();
    bool EnableTunnelReverseAnyPort(struct port_range port_range);
    bool EnableTunnelForwardAnyPort(struct port_range port_range);
    bool EnableTunnelAnyPort(struct port_range port_range, bool force_adb_forward);

    static bool PushServer(const char* serial);
    static bool EnableTunnelReverse(const char* serial, uint16_t local_port);
    static bool DisableTunnelReverse(const char* serial);
    static bool EnableTunnelForward(const char* serial, uint16_t local_port);
    static bool DisableTunnelForward(const char* serial, uint16_t local_port);

    process_t ExecuteServer(const struct server_params* params);
};

struct server_params {
    enum sc_log_level log_level;
    const char* crop;
    const char* codec_options;
    const char* encoder_name;
    struct port_range port_range;
    uint16_t max_size;
    uint32_t bit_rate;
    uint16_t max_fps;
    int8_t lock_video_orientation;
    bool control;
    uint16_t display_id;
    bool show_touches;
    bool stay_awake;
    bool force_adb_forward;
};

#endif
