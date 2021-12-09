#include "../../command.h"

#include <sys/stat.h>

#include "../../config.h"
#include "../../util/log.h"
#include "../../util/str_util.h"

static int
build_cmd(char *cmd, size_t len, const char *const argv[]) {
    // Windows command-line parsing is WTF:
    // <http://daviddeley.com/autohotkey/parameters/parameters.htm#WINPASS>
    // only make it work for this very specific program
    // (don't handle escaping nor quotes)
    size_t ret = xstrjoin(cmd, argv, ' ', len);
    if (ret >= len) {
        LOGE("Command too long (%" PRIsizet " chars)", len - 1);
        return -1;
    }
    return 0;
}

enum process_result
cmd_execute(const char *const argv[], HANDLE *handle) {
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);

    char cmd[256];
    if (build_cmd(cmd, sizeof(cmd), argv)) {
        *handle = NULL;
        return PROCESS_ERROR_GENERIC;
    }

    wchar_t *wide = utf8_to_wide_char(cmd);
    if (!wide) {
        LOGC("Could not allocate wide char string");
        return PROCESS_ERROR_GENERIC;
    }

    if (!CreateProcessW(NULL, wide, NULL, NULL, FALSE, 0, NULL, NULL, &si,
                        &pi)) {
        SDL_free(wide);
        *handle = NULL;
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            return PROCESS_ERROR_MISSING_BINARY;
        }
        return PROCESS_ERROR_GENERIC;
    }

    SDL_free(wide);
    *handle = pi.hProcess;
    return PROCESS_SUCCESS;
}

bool
cmd_terminate(HANDLE handle) {
    return TerminateProcess(handle, 1);
}

bool
cmd_simple_wait(HANDLE handle, DWORD *exit_code) {
    DWORD code;
    if (WaitForSingleObject(handle, INFINITE) != WAIT_OBJECT_0
            || !GetExitCodeProcess(handle, &code)) {
        // could not wait or retrieve the exit code
        code = -1; // max value, it's unsigned
    }
    if (exit_code) {
        *exit_code = code;
    }
    CloseHandle(handle);
    return !code;
}

char *
get_executable_path(void) {
    HMODULE hModule = GetModuleHandleW(NULL);
    if (!hModule) {
        return NULL;
    }
    WCHAR buf[MAX_PATH + 1]; // +1 for the null byte
    int len = GetModuleFileNameW(hModule, buf, MAX_PATH);
    if (!len) {
        return NULL;
    }
    buf[len] = '\0';
    return utf8_from_wide_char(buf);
}

#include <Windows.h>

bool
is_regular_file(const char *path) {
    wchar_t *wide_path = utf8_to_wide_char(path);
    if (!wide_path) {
        LOGC("Could not allocate wide char string");
        return false;
    }

    struct _stat path_stat;
    int r = _wstat(wide_path, &path_stat);
    SDL_free(wide_path);

    if (r) {
        perror("stat");
        return false;
    }
    WIN32_FIND_DATAA finddata;
    HANDLE findHandle = FindFirstFileA(path, &finddata);
    bool result = !!findHandle;
    if (findHandle) FindClose(findHandle);
    return result;

    //return S_ISREG(path_stat.st_mode);
}
