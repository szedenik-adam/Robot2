#ifndef CONFIG_H
#define CONFIG_H
#define ssize_t int
#define DEFAULT_LOCAL_PORT 27183
#define DEFAULT_MAX_SIZE 0
#define DEFAULT_BIT_RATE 8000000
#define SCRCPY_VERSION "1.21"
#define WINDOWS_NOCONSOLE
#define DEFAULT_LOCK_VIDEO_ORIENTATION  -1
#define DEFAULT_LOCAL_PORT_RANGE_FIRST 27183
#define DEFAULT_LOCAL_PORT_RANGE_LAST 27199
#define PREFIX ""
#define PORTABLE

typedef unsigned char byte;
#pragma comment(lib,"SDL2.lib")
#pragma comment(lib,"SDL2main.lib")
//#pragma comment(lib,"SDL2test.lib")
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avdevice.lib")
#pragma comment(lib,"avfilter.lib")
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avutil.lib")
//#pragma comment(lib,"postproc.lib")
#pragma comment(lib,"swresample.lib")
#pragma comment(lib,"swscale.lib")

#pragma comment(lib, "ws2_32.lib")
#endif
