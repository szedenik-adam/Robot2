#include "server.h"
#include "scrcpy.h"
#include "compat.h"
#include <cassert>
#include <cinttypes>
#include "util/lock.h"
#include "util/str_util.h"
#include <SDL2/SDL_timer.h>
//#include <SDL2/SDL_platform.h>

#define SOCKET_NAME "scrcpy"
#define SERVER_FILENAME "scrcpy-server"

#define DEFAULT_SERVER_PATH PREFIX "/share/scrcpy/" SERVER_FILENAME
#define DEVICE_SERVER_PATH "/data/local/tmp/scrcpy-server.jar"

static char*
get_server_path(void) {
#ifdef __WINDOWS__
    const wchar_t* server_path_env = _wgetenv(L"SCRCPY_SERVER_PATH");
#else
    const char* server_path_env = getenv("SCRCPY_SERVER_PATH");
#endif
    if (server_path_env) {
        // if the envvar is set, use it
#ifdef __WINDOWS__
        char* server_path = utf8_from_wide_char(server_path_env);
#else
        char* server_path = SDL_strdup(server_path_env);
#endif
        if (!server_path) {
            LOGE("Could not allocate memory");
            return NULL;
        }
        LOGD("Using SCRCPY_SERVER_PATH: %s", server_path);
        return server_path;
    }

#ifndef PORTABLE
    LOGD("Using server: " DEFAULT_SERVER_PATH);
    char* server_path = SDL_strdup(DEFAULT_SERVER_PATH);
    if (!server_path) {
        LOGE("Could not allocate memory");
        return NULL;
    }
    // the absolute path is hardcoded
    return server_path;
#else

    // use scrcpy-server in the same directory as the executable
    char* executable_path = get_executable_path();
    if (!executable_path) {
        LOGE("Could not get executable path, "
            "using " SERVER_FILENAME " from current directory -------------- removed return here!");
        // not found, use current directory
//        return SERVER_FILENAME;
    }
    //char* dir = dirname(executable_path);
    char dir[256], _drive[256], _fileName[256], _ext[50];
    _splitpath(executable_path, _drive, dir, _fileName, _ext);
    size_t dirlen = strlen(dir);

    // sizeof(SERVER_FILENAME) gives statically the size including the null byte
    size_t len = dirlen + 1 + sizeof(SERVER_FILENAME);
    char* server_path = (char*)SDL_malloc(len);
    /*if (!server_path) {
        LOGE("Could not alloc server path string, "
            "using " SERVER_FILENAME " from current directory"); // dont use it, because later a free won't work...
        SDL_free(executable_path);
        return SERVER_FILENAME;
    }*/

    memcpy(server_path, dir, dirlen);
    server_path[dirlen] = PATH_SEPARATOR;
    memcpy(&server_path[dirlen + 1], SERVER_FILENAME, sizeof(SERVER_FILENAME));
    // the final null byte has been copied with SERVER_FILENAME

    SDL_free(executable_path);

    LOGD("Using server (portable): %s", server_path);
    return server_path;
#endif
}

// static
bool Server::PushServer(const char* serial) {
    char* server_path = get_server_path();
    if (!server_path) {
        return false;
    }
    if (!is_regular_file(server_path)) {
        LOGE("'%s' does not exist or is not a regular file\n", server_path);
        SDL_free(server_path);
        return false;
    }
    process_t process = adb_push(serial, server_path, DEVICE_SERVER_PATH);
    SDL_free(server_path);
    return process_check_success(process, "adb push");
}

// static
bool Server::EnableTunnelReverse(const char* serial, uint16_t local_port) {
    process_t process = adb_reverse(serial, SOCKET_NAME, local_port);
    return process_check_success(process, "adb reverse");
}

// static
bool Server::DisableTunnelReverse(const char* serial) {
    process_t process = adb_reverse_remove(serial, SOCKET_NAME);
    return process_check_success(process, "adb reverse --remove");
}

// static
bool Server::EnableTunnelForward(const char* serial, uint16_t local_port) {
    process_t process = adb_forward(serial, local_port, SOCKET_NAME);
    return process_check_success(process, "adb forward");
}

// static
bool Server::DisableTunnelForward(const char* serial, uint16_t local_port) {
    process_t process = adb_forward_remove(serial, local_port);
    return process_check_success(process, "adb forward --remove");
}

bool Server::DisableTunnel()
{
    if (this->tunnel_forward) {
        return Server::DisableTunnelForward(this->serial, this->local_port);
    }
    return Server::DisableTunnelReverse(this->serial);
}

static socket_t
listen_on_port(uint16_t port) {
#define IPV4_LOCALHOST 0x7F000001
    return net_listen(IPV4_LOCALHOST, port, 1);
}

// static
bool Server::EnableTunnelReverseAnyPort(struct port_range port_range) {
    uint16_t port = port_range.first;
    for (;;) {
        if (!Server::EnableTunnelReverse(this->serial, port)) {
            // the command itself failed, it will fail on any port
            return false;
        }

        // At the application level, the device part is "the server" because it
        // serves video stream and control. However, at the network level, the
        // client listens and the server connects to the client. That way, the
        // client can listen before starting the server app, so there is no
        // need to try to connect until the server socket is listening on the
        // device.
        this->server_socket = listen_on_port(port);
        if (this->server_socket != INVALID_SOCKET) {
            // success
            this->local_port = port;
            return true;
        }

        // failure, disable tunnel and try another port
        if (!Server::DisableTunnelReverse(this->serial)) {
            LOGW("Could not remove reverse tunnel on port %" PRIu16, port);
        }

        // check before incrementing to avoid overflow on port 65535
        if (port < port_range.last) {
            LOGW("Could not listen on port %" PRIu16", retrying on %" PRIu16,
                port, (uint16_t)(port + 1));
            port++;
            continue;
        }

        if (port_range.first == port_range.last) {
            LOGE("Could not listen on port %" PRIu16, port_range.first);
        }
        else {
            LOGE("Could not listen on any port in range %" PRIu16 ":%" PRIu16,
                port_range.first, port_range.last);
        }
        return false;
    }
}

bool Server::EnableTunnelForwardAnyPort(struct port_range port_range) {
    this->tunnel_forward = true;
    uint16_t port = port_range.first;
    for (;;) {
        if (Server::EnableTunnelForward(this->serial, port)) {
            // success
            this->local_port = port;
            return true;
        }

        if (port < port_range.last) {
            LOGW("Could not forward port %" PRIu16", retrying on %" PRIu16,
                port, (uint16_t)(port + 1));
            port++;
            continue;
        }

        if (port_range.first == port_range.last) {
            LOGE("Could not forward port %" PRIu16, port_range.first);
        }
        else {
            LOGE("Could not forward any port in range %" PRIu16 ":%" PRIu16,
                port_range.first, port_range.last);
        }
        return false;
    }
}

bool Server::EnableTunnelAnyPort(struct port_range port_range, bool force_adb_forward) {
    if (!force_adb_forward) {
        // Attempt to use "adb reverse"
        if (this->EnableTunnelReverseAnyPort(port_range)) {
            return true;
        }

        // if "adb reverse" does not work (e.g. over "adb connect"), it
        // fallbacks to "adb forward", so the app socket is the client

        LOGW("'adb reverse' failed, fallback to 'adb forward'");
    }

    return this->EnableTunnelForwardAnyPort(port_range);
}

static const char*
log_level_to_server_string(enum sc_log_level level) {
    switch (level) {
    case SC_LOG_LEVEL_DEBUG:
        return "debug";
    case SC_LOG_LEVEL_INFO:
        return "info";
    case SC_LOG_LEVEL_WARN:
        return "warn";
    case SC_LOG_LEVEL_ERROR:
        return "error";
    default:
        assert(!"unexpected log level");
        return "(unknown)";
    }
}

process_t Server::ExecuteServer(const struct server_params* params)
{
    process_t pid = 0;//SC_PROCESS_NONE;

    const char* cmd[128];
    unsigned count = 0;
    cmd[count++] = "shell";
    cmd[count++] = "CLASSPATH=" DEVICE_SERVER_PATH;
    cmd[count++] = "app_process";

#ifdef SERVER_DEBUGGER
# define SERVER_DEBUGGER_PORT "5005"
    cmd[count++] =
# ifdef SERVER_DEBUGGER_METHOD_NEW
        /* Android 9 and above */
        "-XjdwpProvider:internal -XjdwpOptions:transport=dt_socket,suspend=y,"
        "server=y,address="
# else
        /* Android 8 and below */
        "-agentlib:jdwp=transport=dt_socket,suspend=y,server=y,address="
# endif
        SERVER_DEBUGGER_PORT;
#endif
    cmd[count++] = "/"; // unused
    cmd[count++] = "com.genymobile.scrcpy.Server";
    cmd[count++] = SCRCPY_VERSION;

    unsigned dyn_idx = count; // from there, the strings are allocated
#define ADD_PARAM(fmt, ...) { \
        char *p = (char *) &cmd[count]; \
        if (asprintf(&p, fmt, ## __VA_ARGS__) == -1) { \
            goto end; \
        } \
        cmd[count++] = p; \
    }
#define STRBOOL(v) (v ? "true" : "false")

    ADD_PARAM("log_level=%s", log_level_to_server_string(params->log_level));
    ADD_PARAM("bit_rate=%" PRIu32, params->bit_rate);

    if (params->max_size) {
        ADD_PARAM("max_size=%" PRIu16, params->max_size);
    }
    if (params->max_fps) {
        ADD_PARAM("max_fps=%" PRIu16, params->max_fps);
    }
    if (params->lock_video_orientation != DEFAULT_LOCK_VIDEO_ORIENTATION) {
        ADD_PARAM("lock_video_orientation=%" PRIi8,
            params->lock_video_orientation);
    }
    if (this->tunnel_forward) {
        ADD_PARAM("tunnel_forward=%s", STRBOOL(this->tunnel_forward));
    }
    if (params->crop) {
        ADD_PARAM("crop=%s", params->crop);
    }
    if (!params->control) {
        // By default, control is true
        ADD_PARAM("control=%s", STRBOOL(params->control));
    }
    if (params->display_id) {
        ADD_PARAM("display_id=%" PRIu32, params->display_id);
    }
    if (params->show_touches) {
        ADD_PARAM("show_touches=%s", STRBOOL(params->show_touches));
    }
    if (params->stay_awake) {
        ADD_PARAM("stay_awake=%s", STRBOOL(params->stay_awake));
    }
    if (params->codec_options) {
        ADD_PARAM("codec_options=%s", params->codec_options);
    }
    if (params->encoder_name) {
        ADD_PARAM("encoder_name=%s", params->encoder_name);
    }
    /*if (params->power_off_on_close) {
        ADD_PARAM("power_off_on_close=%s", STRBOOL(params->power_off_on_close));
    }
    if (!params->clipboard_autosync) {
        // By defaut, clipboard_autosync is true
        ADD_PARAM("clipboard_autosync=%s", STRBOOL(params->clipboard_autosync));
    }*/

#undef ADD_PARAM
#undef STRBOOL

#ifdef SERVER_DEBUGGER
    LOGI("Server debugger waiting for a client on device port "
        SERVER_DEBUGGER_PORT "...");
    // From the computer, run
    //     adb forward tcp:5005 tcp:5005
    // Then, from Android Studio: Run > Debug > Edit configurations...
    // On the left, click on '+', "Remote", with:
    //     Host: localhost
    //     Port: 5005
    // Then click on "Debug"
#endif
    // Inherit both stdout and stderr (all server logs are printed to stdout)
    pid = adb_execute(this->serial, cmd, count);

end:
    for (unsigned i = dyn_idx; i < count; ++i) {
        free((char*)cmd[i]);
    }

    return pid;
}

static socket_t
connect_and_read_byte(uint16_t port) {
    socket_t socket = net_connect(IPV4_LOCALHOST, port);
    if (socket == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    char byte;
    // the connection may succeed even if the server behind the "adb tunnel"
    // is not listening, so read one byte to detect a working connection
    if (net_recv(socket, &byte, 1) != 1) {
        // the server is not listening yet behind the adb tunnel
        net_close(socket);
        return INVALID_SOCKET;
    }
    return socket;
}

static socket_t
connect_to_server(uint16_t port, uint32_t attempts, uint32_t delay) {
    do {
        LOGD("Remaining connection attempts: %d", (int)attempts);
        socket_t socket = connect_and_read_byte(port);
        if (socket != INVALID_SOCKET) {
            // it worked!
            return socket;
        }
        if (attempts) {
            SDL_Delay(delay);
        }
    } while (--attempts > 0);
    return INVALID_SOCKET;
}

static void
close_socket(socket_t socket) {
    assert(socket != INVALID_SOCKET);
    net_shutdown(socket, SHUT_RDWR);
    if (!net_close(socket)) {
        LOGW("Could not close socket");
    }
}

Server::Server()
{
    this->serial = NULL;
    this->process = PROCESS_NONE;
    this->wait_server_thread = NULL;
    this->server_socket_closed.clear(std::memory_order_relaxed);

    this->mutex = SDL_CreateMutex();
    if (!this->mutex) {
        throw std::exception("Server mutex alloc error.");
    }

    this->process_terminated_cond = SDL_CreateCond();
    if (!this->process_terminated_cond) {
        SDL_DestroyMutex(this->mutex);
        throw std::exception("Server cond alloc error.");
    }

    this->process_terminated = false;

    this->server_socket = INVALID_SOCKET;
    this->video_socket = INVALID_SOCKET;
    this->control_socket = INVALID_SOCKET;

    this->port_range.first = 0;
    this->port_range.last = 0;
    this->local_port = 0;

    this->tunnel_enabled = false;
    this->tunnel_forward = false;
}

Server::~Server()
{
    if (this->serial) {
        SDL_free(this->serial);
    }
    SDL_DestroyCond(this->process_terminated_cond);
    SDL_DestroyMutex(this->mutex);
}

static int
run_wait_server(void* data) {
    Server* server = (Server*)data;
    cmd_simple_wait(server->process, NULL); // ignore exit code

    mutex_lock(server->mutex);
    server->process_terminated = true;
    cond_signal(server->process_terminated_cond);
    mutex_unlock(server->mutex);

    // no need for synchronization, server_socket is initialized before this
    // thread was created
    if (server->server_socket != INVALID_SOCKET
        && !atomic_flag_test_and_set(&server->server_socket_closed)) {
        // On Linux, accept() is unblocked by shutdown(), but on Windows, it is
        // unblocked by closesocket(). Therefore, call both (close_socket()).
        close_socket(server->server_socket);
    }
    LOGD("Server terminated");
    return 0;
}

bool Server::Start(const char* serial, const server_params* params)
{
    net_init(); // Only initializes once (internally checks it).

    this->port_range = params->port_range;

    if (serial) {
        this->serial = SDL_strdup(serial);
        if (!this->serial) {
            return false;
        }
    }

    if (!Server::PushServer(serial)) {
        goto error1;
    }

    if (!this->EnableTunnelAnyPort(params->port_range, params->force_adb_forward)) {
        goto error1;
    }

    // server will connect to our server socket
    this->process = this->ExecuteServer(params);
    if (this->process == PROCESS_NONE) {
        goto error2;
    }

    // If the server process dies before connecting to the server socket, then
    // the client will be stuck forever on accept(). To avoid the problem, we
    // must be able to wake up the accept() call when the server dies. To keep
    // things simple and multiplatform, just spawn a new thread waiting for the
    // server process and calling shutdown()/close() on the server socket if
    // necessary to wake up any accept() blocking call.
    this->wait_server_thread =
        SDL_CreateThread(run_wait_server, "wait-server", this);
    if (!this->wait_server_thread) {
        cmd_terminate(this->process);
        cmd_simple_wait(this->process, NULL); // ignore exit code
        goto error2;
    }

    this->tunnel_enabled = true;

    return true;

error2:
    if (!this->tunnel_forward) {
        bool was_closed =
            atomic_flag_test_and_set(&this->server_socket_closed);
        // the thread is not started, the flag could not be already set
        assert(!was_closed);
        (void)was_closed;
        close_socket(this->server_socket);
    }
    this->DisableTunnel();
error1:
    if (this->serial) {
        SDL_free(this->serial);
        this->serial = nullptr;
    }
    return false;
}

bool Server::ConnectTo()
{
    if (!this->tunnel_forward) {
        this->video_socket = net_accept(this->server_socket);
        if (this->video_socket == INVALID_SOCKET) {
            return false;
        }

        this->control_socket = net_accept(this->server_socket);
        if (this->control_socket == INVALID_SOCKET) {
            // the video_socket will be cleaned up on destroy
            return false;
        }

        // we don't need the server socket anymore
        if (!atomic_flag_test_and_set(&this->server_socket_closed)) {
            // close it from here
            close_socket(this->server_socket);
            // otherwise, it is closed by run_wait_server()
        }
    }
    else {
        uint32_t attempts = 100;
        uint32_t delay = 100; // ms
        this->video_socket =
            connect_to_server(this->local_port, attempts, delay);
        if (this->video_socket == INVALID_SOCKET) {
            return false;
        }

        // we know that the device is listening, we don't need several attempts
        this->control_socket =
            net_connect(IPV4_LOCALHOST, this->local_port);
        if (this->control_socket == INVALID_SOCKET) {
            return false;
        }
    }

    // we don't need the adb tunnel anymore
    this->DisableTunnel(); // ignore failure
    this->tunnel_enabled = false;

    return true;
}

void Server::Stop()
{
    if (this->server_socket != INVALID_SOCKET
        && !atomic_flag_test_and_set(&this->server_socket_closed)) {
        close_socket(this->server_socket);
    }
    if (this->video_socket != INVALID_SOCKET) {
        close_socket(this->video_socket);
    }
    if (this->control_socket != INVALID_SOCKET) {
        close_socket(this->control_socket);
    }

    assert(this->process != PROCESS_NONE);

    if (this->tunnel_enabled) {
        // ignore failure
        this->DisableTunnel();
    }

    // Give some delay for the server to terminate properly
    mutex_lock(this->mutex);
    int r = 0;
    if (!this->process_terminated) {
#define WATCHDOG_DELAY_MS 1000
        r = cond_wait_timeout(this->process_terminated_cond,
            this->mutex,
            WATCHDOG_DELAY_MS);
    }
    mutex_unlock(this->mutex);

    // After this delay, kill the server if it's not dead already.
    // On some devices, closing the sockets is not sufficient to wake up the
    // blocking calls while the device is asleep.
    if (r == SDL_MUTEX_TIMEDOUT) {
        // FIXME There is a race condition here: there is a small chance that
        // the process is already terminated, and the PID assigned to a new
        // process.
        LOGW("Killing the server...");
        cmd_terminate(this->process);
    }

    SDL_WaitThread(this->wait_server_thread, NULL);
}
