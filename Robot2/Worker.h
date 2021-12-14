#pragma once
#include "Config.h"
#include "detect/ObjDetect.h"
#include "Estimator.h"
#include "ThreadSafeBuffer.h"
#include "WorkerHelper.h"
#include <opencv2/core.hpp>
#include <memory>
#include <vector>
#include <functional>
#include <mutex>
#include <thread>

class Worker
{
	Config& config;
	std::unique_ptr<FixedResolutionConfig> frConfig;
	Estimator estimator;
	ThreadSafeBuffer<WorkerInfo> workerInfos;
	std::function<uint8_t*()> grabImageFunc;
	std::function<void(int, int, bool)> touchFunc;
	bool isExiting, isOnceStopped;
	std::thread thread;
	const Config::State* currentState;
	std::vector<std::vector<RectProb>> lastDetection;
	//std::vector<int> lastDetectionFirstValidRect;
	uint32_t lastActionMs, nextScanMs, lastDetectionMs, nowMs;
	bool takeScreenshot;

	void Run(); // Thread method.
public:
	Worker(Config& config);

	~Worker();

	void UpdateResolution(int width, int height);

	void Start(); // Starts a background thread executing the Run method.
	void Stop(bool waitForThread, bool once=false);

	void SetGrabImageFunct(const std::function<uint8_t*()>& f) { this->grabImageFunc = f; }
	void SetTouchFunct(const std::function<void(int, int, bool)>& f) { this->touchFunc = f; }

	const std::vector<std::vector<RectProb>>& GetLastDetection() const { return this->lastDetection; }
	std::vector<std::vector<RectProb>>& GetLastDetection() { return this->lastDetection; }

	const FixedResolutionConfig& GetFixResConfig() const { return *this->frConfig; }
	const Config& GetConfig() const { return this->config; }
	Estimator& GetEstimator() { return this->estimator; }
	const Config::State* GetState() { return this->currentState; }
	void SetState(int stateInd);
	//std::vector<int>& GetFirstValidRect() { return this->lastDetectionFirstValidRect; }
	void SendTouchEvent(const cv::Point& p, bool isDown, const cv::Rect* r = nullptr);
	uint32_t GetLastAction() const { return this->lastActionMs; }
	uint32_t GetNow() const { return this->nowMs; }
	uint32_t UpdateNow();
	ThreadSafeBuffer<WorkerInfo>& GetInfos() { return this->workerInfos; }
	void TakeScreenshot() { this->takeScreenshot = true; }
	
};
