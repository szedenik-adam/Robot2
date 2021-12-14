#pragma once

#include <cstdio>
#include <cstdarg>

template <int Size>
class PrintfBuf
{
	char buf[Size];
	char* bufStart = buf;
	int len = Size-1;
public:
	void write(const char* format, ...)
	{
		int written;
		va_list ap;
		va_start(ap, format);
		written = vsnprintf(bufStart, len, format, ap);
		bufStart += written;
		len -= written;
		va_end(ap);
	}
	char* str() { return buf; }
	void reset() { buf[0] = '\0'; bufStart = buf; len = Size; }
};

// End of PrintfBuf

#include <stdint.h>
#include <string>

class Estimator
{
	uint32_t lastEventTime;
	uint8_t eventCount;
	uint32_t eventTimes[8];
	uint32_t eventTimesStart, eventTimesCount;
	uint32_t counterLimit;
	uint32_t lastBufUpdateTime; char lastUsedSeparator;
	std::string eventName;
	PrintfBuf<100> buf;

public:
	Estimator(const std::string& eventName, uint32_t counterLimit);

	void Add(uint32_t now, int inc);

	void SetCounterLimit(uint32_t counterLimit) { this->counterLimit = counterLimit; }

	void Display(uint32_t now);

	const char* GetString(uint32_t now, char separator = ' ');
};

#define eventTimesLength (sizeof(this->eventTimes) / sizeof(this->eventTimes[0]))
