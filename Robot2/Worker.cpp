#include "Worker.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <chrono>
#include <SDL2/SDL_timer.h>
#include <iostream>

FixedResolutionConfig::FixedResolutionConfig(const Config& config, int width, int height):width(width), height(height)
{
	this->screenMasksPerState.resize(config.GetStates().size());
	int stateInd = -1;
	for (const Config::State& state : config.GetStates())
	{
		stateInd++;
		cv::Mat& mask = this->screenMasksPerState[stateInd];
		mask = cv::Mat::zeros(height, width, CV_8U);

		for (int actionInd : state.actionInds)
		{
			const Config::Action& action = config.GetActions()[actionInd];
			
			for (const std::unique_ptr<Config::Requirement>& requirement : action.requirements)
			{
				Config::Requirement* reqPtr = requirement.get();
				Config::ObjectRequirement* objReq = dynamic_cast<Config::ObjectRequirement*>(reqPtr);
				if (objReq) {
					cv::Rect rect(objReq->CalculateScanRect(cv::Size(width, height)));
					// add region to mask.
					cv::rectangle(mask, rect, cv::Scalar_<uint8_t>(255), cv::LineTypes::FILLED);
					char n = mask.at<char>(0, 0);
					char p = mask.at<char>(90, 90);
					int a = 0;
				}
			}
			
			//cv::imwrite(std::format("mask_{}.png", state.name), mask);
		}
	}
}

void Worker::Run()
{
	using namespace std::chrono_literals;

	ObjDetect od = config.CreateDetector();
	try {
	while (!isExiting)
	{
		if (!grabImageFunc || !this->frConfig.get()) {
			std::this_thread::sleep_for(100ms);
			continue;
		}

		{
			//printf("screen proc<<");
			//std::tuple<uint8_t*, std::unique_lock<std::mutex>> imageBuf = grabImageFunc();
			//uint8_t* imageRawPtr = std::get<0>(imageBuf);
			uint8_t* imageRawPtr = grabImageFunc();
			if (!imageRawPtr) continue;

			// Convert image to single channel.
			// Object detection based on current state and config +mask.
			if (this->currentState->hasObjectToDetect) {
				this->lastDetectionMs = SDL_GetTicks();
				od.UpdateBaseImage(cv::Mat(this->frConfig->GetHeight(), this->frConfig->GetWidth(), CV_8UC4, imageRawPtr));
				this->lastDetection = od.FindObjects(&this->currentState->objectsToDetect);
				if (this->takeScreenshot) {
					cv::imwrite("screenshot.png", cv::Mat(this->frConfig->GetHeight(), this->frConfig->GetWidth(), CV_8UC4, imageRawPtr));
					od.SaveBaseImage("screenshot-1ch.png");
					this->takeScreenshot = false;
					std::cout << "Taking screenshot.\n";
				}
				this->workerInfos.GetLive().SetDetections(this->lastDetection);
				this->workerInfos.Commit();
				std::cout << "State: " << this->currentState->name <<" d:[";
				for (int i = 0; i < this->lastDetection.size(); i++) {
					if(this->currentState->objectsToDetect[i])
						std::cout << this->config.GetObjects()[i].second << ':' << this->lastDetection[i].size() << ", ";
				}
				std::cout << "]\n";
				od.UpdateBaseImage(cv::Mat());
			}
			//printf("screen processing done\n");
		}
		this->nowMs = SDL_GetTicks();
		// Evaluate actions.
		for (int aInd : this->currentState->actionInds)
		{
			const Config::Action& action = this->config.GetActions()[aInd];
			bool allReqGood = true;
			for (const std::unique_ptr<Config::Requirement>& req : action.requirements)
			{
				if (!req->IsSatisfied(this)) { 
					allReqGood = false;
					break; 
				}
			}
			if (!allReqGood) continue;
			this->lastActionMs = nowMs;
			// Do the action's tasks.
			for (const std::unique_ptr<Config::Task>& task : action.tasks)
			{
				task->Execute(*this);
			}

		}
		this->GetInfos().Commit();

		uint32_t timeSinceLastDetectionMs = SDL_GetTicks() - this->lastDetectionMs;
		if (timeSinceLastDetectionMs < config.GetScanWaitMs()) {
			int sleepTimeMs = config.GetScanWaitMs() - timeSinceLastDetectionMs;
			printf("Sleeping for %d ms\n", sleepTimeMs);
			std::this_thread::sleep_for(std::chrono::milliseconds(sleepTimeMs));
		}
	}

	}
	catch (const std::exception& ex) {
		printf("std::exception\n");
	}
	catch (...) {
		printf("??? exception\n");
	}
	printf("Thread exiting\n");
}

Worker::Worker(Config& config) : config(config), estimator(config.GetName(), config.GetCounterLimit()), frConfig(nullptr), grabImageFunc(nullptr), isExiting(false), isOnceStopped(false), currentState(config.GetInitialState()),
lastDetection(), /*lastDetectionFirstValidRect(config.GetObjectCount(),0),*/ lastActionMs(0), nextScanMs(0), lastDetectionMs(0)
{
}

Worker::~Worker()
{
	this->Stop(true);
}

void Worker::UpdateResolution(int width, int height)
{
	if (!this->frConfig.get() || this->frConfig->GetWidth() != width || this->frConfig->GetHeight() != height) {
		this->frConfig = std::make_unique<FixedResolutionConfig>(config, width, height);
	}
}

void Worker::Start()
{
	this->isExiting = false;
	if (this->thread.joinable()) { return; } // Already started.
	this->currentState = config.GetInitialState();
	this->lastActionMs = SDL_GetTicks();
	this->thread = std::thread(&Worker::Run, this);
}

void Worker::Stop(bool waitForThread, bool once)
{
	if (once && this->isOnceStopped) { return; }

	if (this->thread.joinable())
	{
		this->isExiting = true;
		if (waitForThread) { this->thread.join(); }
		else { this->thread.detach(); }

		if (once) { this->isOnceStopped = true; }
	}
}

void Worker::SetState(int stateInd)
{
	this->currentState = &config.GetStates()[stateInd];
}

void Worker::SendTouchEvent(const cv::Point& p, bool isDown, const cv::Rect* r)
{
	this->workerInfos.GetLive().SetClickTime(SDL_GetTicks(), p.x, p.y, r);
	if (touchFunc) { touchFunc(p.x, p.y, isDown); }
}
uint32_t Worker::UpdateNow()
{
	this->nowMs = SDL_GetTicks();
	return this->nowMs;
}
