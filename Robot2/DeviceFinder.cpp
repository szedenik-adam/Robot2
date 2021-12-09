#include "DeviceFinder.h"
#include "scrcpy/command.h"

#include <reproc/reproc.h>
//#include <reproc/drain.h>
#include <iostream>
#pragma comment(lib,"reproc.lib")
//#pragma comment(lib,"reproc++.lib")

// static
std::list<std::string> Device::List()
{
	std::list<std::string> result;

	char devIdBuf[1024];
	const char* cmd[] = { get_adb_command(), "devices", NULL };
	int r = 0;
	reproc_t* process = reproc_new();
	r = reproc_start(process, cmd, { .redirect = {.err = {.type = REPROC_REDIRECT_STDOUT} },
		.stop = {
					.first = { REPROC_STOP_WAIT, REPROC_DEADLINE},
					.second = { REPROC_STOP_TERMINATE, 500 },
					.third = { REPROC_STOP_KILL, 5000 },
				},
		.deadline = 1000,
		});
	r = reproc_close(process, REPROC_STREAM_IN);

	char* out = NULL;
	//r = reproc_drain(process, reproc_sink_string(&out), REPROC_SINK_NULL);

	r = reproc_read(process, REPROC_STREAM_OUT, (uint8_t*)devIdBuf, sizeof(devIdBuf));

	char* lineStart = devIdBuf, * lineEnd = devIdBuf;
	while (lineEnd) {
		lineEnd = strchr(lineStart, '\n');
		if (lineEnd) {
			*lineEnd = '\0';
			if (lineEnd > lineStart && lineEnd[-1] == '\r') lineEnd[-1] = '\0';
			char* tdevice = strstr(lineStart, "\tdevice");
			if (tdevice) {
				*tdevice = '\0';
				result.emplace_back(std::string(lineStart));
			}
		}
		//printf("line: %s\n", lineStart);
		lineStart = lineEnd + 1;
	}
	if (result.empty()) printf("No ADB device found."); else printf("ADB devices: ");
	for (const std::string& devId : result) std::cout << devId << ' '; 
	putchar('\n');

	//r = reproc_wait(process, 1000);
	reproc_destroy(process);
	return result;
}

// static
void* Device::LockDevice(const std::string& devId)
{
	char name[64];
	strcpy(name, "Local\\amp_");
	strcpy(name + strlen(name), devId.c_str());
	HANDLE mutex = CreateMutexA(0, FALSE, name); // try to create a named mutex
	if (GetLastError() == ERROR_ALREADY_EXISTS) // did the mutex already exist?
		return 0; // quit; mutex is released automatically
	if (mutex)
		return mutex;
	printf("MUTEX unknown error!\n");
	return 0;
}

// static
std::string Device::LockFirstUnusedDeviceId(void** mutexPtr)
{
	std::list<std::string> devIds = Device::List();
	for (const std::string& devId : devIds)
	{
		void* mutex;
		if (mutex = Device::LockDevice(devId)) {
			if (mutexPtr) { *mutexPtr = mutex; }
			return devId;
		}
	}
	return std::string(); // Empty string.
}

Device::Device()
{
	this->devId = Device::LockFirstUnusedDeviceId(&this->mutex);
}

Device::~Device()
{
	if (this->mutex) { CloseHandle(this->mutex); }
}

const char* Device::GetDeviceId()
{
	return this->devId.c_str();
}
