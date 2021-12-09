#pragma once
#include <mutex>

template <typename BufferType>
class LockedBuffer {
	std::unique_lock<std::mutex> lock;
	BufferType& ref;
public:
	LockedBuffer(BufferType& wi, std::unique_lock<std::mutex>&& lock) : ref(wi), lock(std::move(lock)) {}
	BufferType& Get() { return ref; }
};

template <typename BufferType>
class ThreadSafeBuffer
{
	BufferType buffer[3];
	BufferType* live, *last, *waitingToBeLast;
	bool waitingToBeLastValid, isLiveDirty;
	std::mutex lastMutex, waitingMutex;
public:
	ThreadSafeBuffer(): buffer(), live(buffer+0), last(buffer+1), waitingToBeLast(buffer+2), waitingToBeLastValid(false), isLiveDirty(false){}
	
	BufferType& GetLive() { isLiveDirty = true; return *live; }
	
	LockedBuffer<BufferType> GetLast()
	{
		if (waitingToBeLastValid) {
			std::lock_guard<std::mutex> lock(waitingMutex);
			if (waitingToBeLastValid) {
				std::swap(waitingToBeLast, last);
				waitingToBeLastValid = false;
			}
		}
		std::unique_lock<std::mutex> lock(this->lastMutex);
		LockedBuffer<BufferType> result(*last, std::move(lock));
		return result;
	}
	void Commit()
	{
		if (!isLiveDirty) return;

		if (lastMutex.try_lock())
		{
			//std::swap(live, last);
			*last = *live; // copy
			waitingToBeLastValid = false;
			lastMutex.unlock();
		}
		else
		{
			std::lock_guard<std::mutex> lock(waitingMutex);
			//std::swap(live, waitingToBeLast);
			*waitingToBeLast = *live;
			waitingToBeLastValid = true;
		}
		isLiveDirty = false;
	}
};