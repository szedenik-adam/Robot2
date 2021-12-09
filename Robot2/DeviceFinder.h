#pragma once

#include <list>
#include <string>

class Device
{
	void* mutex;
	std::string devId;
public:
	static std::list<std::string> List();

	static void* LockDevice(const std::string& devId);

	static std::string LockFirstUnusedDeviceId(void** mutexPtr = nullptr);

	Device();
	~Device();
	const char* GetDeviceId();
};