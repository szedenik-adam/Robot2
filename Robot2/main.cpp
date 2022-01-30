// Define CV_IGNORE_DEBUG_BUILD_GUARD when using release OpenCV with debug build.
// icon source: https://www.iconfinder.com/icons/2730368/animal_character_inkcontober_psyduck_screech_yellow_icon

/*
vcpkg install opencv[ffmpeg,contrib,nonfree,openmp,cuda]:x64-windows
*/

#ifdef _WIN32

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include <conio.h>

#endif

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/calib3d.hpp>
#include <vector>
#include <iostream>
#include <filesystem>
#include <opencv2/xfeatures2d.hpp>

#ifdef _DEBUG
#pragma comment(lib, "opencv_cored.lib")
#pragma comment(lib, "opencv_features2dd.lib")
#pragma comment(lib, "opencv_xfeatures2dd.lib")
#pragma comment(lib, "opencv_imgprocd.lib")
#pragma comment(lib, "opencv_imgcodecsd.lib")
#pragma comment(lib, "opencv_highguid.lib")
#pragma comment(lib, "opencv_calib3dd.lib")
#pragma comment(lib, "opencv_flannd.lib")
//#pragma comment(lib, "opencv_world.lib")
#pragma comment(lib, "libconfig++.lib")
#else
#pragma comment(lib, "opencv_core.lib")
#pragma comment(lib, "opencv_features2d.lib")
#pragma comment(lib, "opencv_xfeatures2d.lib")
#pragma comment(lib, "opencv_imgproc.lib")
#pragma comment(lib, "opencv_imgcodecs.lib")
#pragma comment(lib, "opencv_highgui.lib")
#pragma comment(lib, "opencv_calib3d.lib")
#pragma comment(lib, "opencv_flann.lib")
//#pragma comment(lib, "opencv_world.lib")
#pragma comment(lib, "libconfig++.lib")
#endif

#include "Benchmark.h"
#include "detect/ObjDetect.h"
#include "detect/Tests.h"
#include "Config.h"
#include "Worker.h"
#include "Environment.h"
#include "Window.h"

#include "scrcpy/scrcpy.h"

#include "DeviceFinder.h"

using namespace std;
using namespace cv;
namespace fs = std::filesystem;

Config config;

void atexit_launched_without_console()
{
    std::cout << "Press any key to continue...\n";
    _getch(); // from conio.h, windows only
}
bool IsRunningFromCommandLine(char** envp)
{
    bool result = false;
    if (IsDebuggerPresent()) { result = true; }
    else
    {
        const char* prompt_text = "PROMPT=";
        size_t prompt_len = strlen(prompt_text);
        while (*envp)
        {
            if (strncmp(*envp, prompt_text, prompt_len) == 0)
            {
                result = true;
                break;
            }
            envp++;
        }
    }
    return result;
}

int main(int argc, char* argv[], char** envp)
{
#ifdef _WIN32
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF); // Show memory leak on program exit.
    //_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    bool runsFromCmd = IsRunningFromCommandLine(envp);
    if (!runsFromCmd) std::atexit(atexit_launched_without_console);

    Window window;
    bool testOnlyConfig = true;
    if (argc > 1) {
        if (fs::exists(argv[1])) {
            config.LoadConfig(argv[1]); // Load config file given as command line parameter.
        }
        else {
            printf("File %s doesn't exists!\n", argv[1]);
            return 1;
        }
    }
    else {
        // Load configs
        bool foundConfig = false;
        if (fs::exists("default.txt")) {
            std::ifstream t("default.txt");
            std::stringstream buffer;
            buffer << t.rdbuf();
            const std::string& defaultConfigFileName = buffer.str();
            std::cout << "Loading file \"" << defaultConfigFileName << "\" given in default.txt\n";
            if (fs::exists(defaultConfigFileName)) {
                testOnlyConfig = config.LoadConfig(defaultConfigFileName);
            }
            else {
                std::cout << "File \""<< defaultConfigFileName <<"\" doesn't exists!\n";
            }
        }
        else if (fs::exists("default.cfg")) {
            std::cout << "Loading default.cfg\n";
            testOnlyConfig = config.LoadConfig("default.cfg");
        }
        else {
            for (const fs::directory_entry& entry : fs::directory_iterator("."))
            {
                if (!entry.is_regular_file()) continue;
                std::string fileName = entry.path().string();
                if (fileName.ends_with(".cfg") || fileName.ends_with("Config.txt") || fileName.ends_with("config.txt")) {
                    testOnlyConfig = config.LoadConfig(fileName);
                    foundConfig = true;
                    if (!testOnlyConfig) { break; } // Only load more configs when the config files only contain tests.
                }
            }
            if (!foundConfig) {
                printf("No configs found!\n");
                return 1;
            }
        }
    }
    if (testOnlyConfig)
    {
        for (ObjDetectTest& test : config.Tests())
        {
            test.RunTest();
        }
        ObjDetectTest::DumpGlobalStats();
    }
    Worker worker(config);

    Device d;
    std::cout << "Using device id: " << d.GetDeviceId() << std::endl;

    Environment env(window, config, d.GetDeviceId());
    env.Run();
    return 1;
}
