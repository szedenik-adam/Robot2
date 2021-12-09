#include "Estimator.h"
#include <cstdio>
#include <algorithm>
#include <iostream>
#include "termcolor.hpp"

Estimator::Estimator(uint32_t counterLimit): lastEventTime(0), eventCount(0), eventTimes{ 0 }, eventTimesStart(0), eventTimesCount(0), counterLimit(counterLimit), lastBufUpdateTime(0), lastUsedSeparator('\0')
{
}

void Estimator::Add(uint32_t now, int inc)
{
	if (this->eventTimesCount < eventTimesLength) {
		this->eventTimes[(this->eventTimesStart + this->eventTimesCount++) % eventTimesLength] = now;
	}
	else {
		this->eventTimes[(this->eventTimesStart + this->eventTimesCount) % eventTimesLength] = now;
		this->eventTimesStart = (this->eventTimesStart + 1) % eventTimesLength;
	}

	this->eventCount += inc;
}

#ifdef min
#undef min
#endif

void Estimator::Display(uint32_t now)
{
	std::cout << termcolor::bright_yellow << this->GetString(now) << std::endl << termcolor::reset;
}

const char* Estimator::GetString(uint32_t now, char separator)
{
	if (now - lastBufUpdateTime > 1000 || separator!=lastUsedSeparator) // Limit text buffer update to 1 per seconds.
	{
		lastBufUpdateTime = now;
		lastUsedSeparator = separator;
		buf.reset();
		uint32_t nowSec = now / 1000;
		uint32_t nowMin = nowSec / 60; nowSec -= nowMin * 60;
		uint32_t nowHour = nowMin / 60; nowMin -= nowHour * 60;
		if (nowHour) buf.write("Done %u events in %02u:%02u:%02u.", this->eventCount, nowHour, nowMin, nowSec);
		else buf.write("Done %u events in %02u:%02u.", this->eventCount, nowMin, nowSec);

		uint32_t lasteventDelta = now - (this->eventTimesCount ? this->eventTimes[(this->eventTimesStart + this->eventTimesCount - 1) % eventTimesLength] : 0);
		uint32_t deltaSec = lasteventDelta / 1000;
		uint32_t deltaMin = deltaSec / 60; deltaSec -= deltaMin * 60;
		buf.write("%cTime since last event: %u:%02u.", separator, deltaMin, deltaSec);

		if (this->eventTimesCount) {
			uint32_t avgeventTime;
			if (this->eventTimesCount < eventTimesLength)
				avgeventTime = this->eventTimes[(this->eventTimesStart + this->eventTimesCount - 1) % eventTimesLength] / this->eventTimesCount;
			else
				avgeventTime = (this->eventTimes[(this->eventTimesStart + this->eventTimesCount - 1) % eventTimesLength] - this->eventTimes[this->eventTimesStart]) / (this->eventTimesCount - 1);

			uint32_t remainingTime = avgeventTime * (this->counterLimit - this->eventCount) - std::min(lasteventDelta, avgeventTime);
			uint32_t remSec = remainingTime / 1000;
			uint32_t remMin = remSec / 60; remSec -= remMin * 60;
			buf.write("%cRemaining time: %u:%02u.", separator, remMin, remSec);
		}
	}
	return buf.str();
}
