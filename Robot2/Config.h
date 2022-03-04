#pragma once

#include <list>
#include <string>
#include <libconfig.h++>
#include "detect/Tests.h"
#include "magic_enum.hpp"
#include "detect/ObjDetect.h"

// https://stackoverflow.com/questions/29263090/ffmpeg-avframe-to-opencv-mat-conversion/29265787
// http://www.partow.net/programming/exprtk/
// https://project-awesome.org/fffaraz/awesome-cpp

class Worker;

class Config
{
public:
	class State
	{
	public:
		std::string name;
		std::list<int> actionInds; // Actions which can be fired from this state.
		std::vector<bool> objectsToDetect;
		bool hasObjectToDetect;
		State(const std::string& name, int objCount) :name(name), objectsToDetect(objCount,false), hasObjectToDetect(false){}
		void SetObjectToDetect(int objInd) {
			this->objectsToDetect[objInd] = true;
			this->hasObjectToDetect = true;
		}
	};
	class Requirement {
	public:
		Requirement() {}
		virtual ~Requirement() {}
		virtual bool IsSatisfied(Worker* worker) const = 0;
		virtual void UpdateScreenSize(const cv::Size screenSize) {}
	};
	class TimeoutRequirement : public Requirement {
		int timeoutMs;
	public:
		TimeoutRequirement(int timeoutMs) : timeoutMs(timeoutMs) {}
		virtual ~TimeoutRequirement() {}
		virtual bool IsSatisfied(Worker* worker) const;
	};
	class ObjectRequirement : public Requirement {
		int objIndex;
		bool notFoundCheck;
		cv::Rect2f screenMarginMul; // note: width and height are treated as right and bottom margins.
		cv::Rect scanRegion;
		float minDetectQuality;
	public:
		ObjectRequirement(int objIndex, bool notFoundCheck, cv::Rect2f screenMarginMul, float minDetectQuality) :objIndex(objIndex), notFoundCheck(notFoundCheck), screenMarginMul(screenMarginMul), minDetectQuality(minDetectQuality){}
		virtual ~ObjectRequirement() {}
		virtual bool IsSatisfied(Worker* worker) const;
		virtual void UpdateScreenSize(const cv::Size screenSize);
		cv::Rect CalculateScanRect(const cv::Size screenSize) const;
		int GetObjIndex() const { return objIndex; }
	};
	class Task {
	public:
		virtual void Execute(Worker& worker) {}
	};
	class ClickTask : public Task {
#define CLICKTASK_SELECT_FIRST 0
#define CLICKTASK_SELECT_LEFT 1
#define CLICKTASK_SELECT_RIGHT 2
#define CLICKTASK_SELECT_TOP 4
#define CLICKTASK_SELECT_BOTTOM 8
		int objInd, delayMs, randomDelayMs; cv::Rect2f marginMul; char selectMode;
	public:
		ClickTask(int objInd, int delayMs, int randomDelayMs, cv::Rect2f marginMul, char selectMode) :objInd(objInd), delayMs(delayMs), randomDelayMs(randomDelayMs), marginMul(marginMul), selectMode(selectMode){}
		virtual void Execute(Worker& worker);
	};
	class WaitTask : public Task {
		int delayMs, randomDelayMs;
	public:
		WaitTask(int delayMs, int randomDelayMs) :delayMs(delayMs), randomDelayMs(randomDelayMs) {}
		virtual void Execute(Worker& worker);
	};
	class CounterIncrementTask : public Task {
		int increment;
		const State* curState;
	public:
		CounterIncrementTask(int increment, const State* curState) :increment(increment), curState(curState){}
		virtual void Execute(Worker& worker);
	};
	class SetStateTask : public Task {
		int newState;
	public:
		SetStateTask(int newState) :newState(newState){}
		virtual void Execute(Worker& worker);
	};
	class Action
	{
	public:
		std::list<int> states; // The states which the action can be called from.
		std::list<int> state_blacklist; // The states which the action cannot be called from. After loading the config file the states are updated based on this list (states := all state - blacklisted states).
		std::list<std::unique_ptr<Requirement>> requirements; // Requirements to fire the action.
		//std::list<ObjectRequirement> objReqs; // Object detection requirements.
		std::list<std::unique_ptr<Task>> tasks; // Tasks to execute when the action is fired.

		Action() {}
		~Action() {}
		Action(Action&& o) noexcept : states(std::move(o.states)), state_blacklist(std::move(o.state_blacklist)), requirements(std::move(o.requirements)), tasks(std::move(o.tasks)) {}
		//Action(Action&& o) noexcept { *this = std::move(o); }
		/*Action& operator=(Action&& other) noexcept
		{
			if(this != &other) *this = std::move(other);
			return *this;
		}*/
	};
private:
	std::list<ObjDetectTest> tests;
	std::vector<std::pair<std::string, std::string>> objects;
	std::vector<State> states;
	std::vector<Action> actions;
	std::map<std::string, int> objNameToIndex;
	std::map<std::string, int> stateNameToIndex;

	int scanWaitMs, scanWaitRandomMs, counter_limit, estimator_history, initialState, threadCount;
	float minDetectionQuality;
	std::string image_channel, source;
	ObjDetect::Detector detector;
	cv::DescriptorMatcher::MatcherType matcher;

	std::string name;
	
	template<typename T, typename defT>
	void LoadSetting(libconfig::Config& setting, const std::string& name, T& value, const defT& defaultValue)
	{
		bool found = setting.lookupValue(name, value);
		if (!found) value = defaultValue;
	}
	void InitStates(libconfig::Setting& setting);
	void ExtractStatesIntoAction(libconfig::Setting& setting, const std::string& settingName, std::list<int>& stateList);
	int GetStateInd(const std::string& stateName);
	void ExtractRequirementsIntoAction(libconfig::Setting& setting, Action& action);
	void ExtractObjectRequirementIntoAction(libconfig::Setting& setting, Action& action);
	void ExtractTasksIntoAction(libconfig::Setting& setting, Action& action);
	void ExtractSingleTaskIntoAction(libconfig::Setting& setting, Action& action);

	static int TimeParse(libconfig::Setting& setting);
	static void MarginMultiplierParse(std::string text, cv::Rect2f& marginMul, bool xMasked=false, bool yMasked=false);
	static cv::Rect2f MarginMultiplierParse(libconfig::Setting& setting);
	static float RangeParamParse(libconfig::Setting& setting, bool isXAxis);
	static float PercentParse(std::string text);
	static void RangeParse(libconfig::Setting& setting, cv::Rect2f& marginMul);
public:
	bool LoadConfig(const std::string& filePath);
	std::list<ObjDetectTest>& Tests() { return tests; }
	const std::vector<State>& GetStates() const { return states; }
	const std::vector<Action>& GetActions() const { return actions; }
	const std::vector<std::pair<std::string, std::string>>& GetObjects() const { return objects; }
	ObjDetect CreateDetector();
	int GetThreadCount() const { return this->threadCount; }
	const State* GetInitialState() const { return &this->states[this->initialState]; }
	int GetScanWaitMs() const;
	int GetCounterLimit() const { return this->counter_limit; }
	int GetObjectCount() const { return this->objects.size(); }
	const std::string& GetObjectName(int ind) const { return std::get<1>(this->objects[ind]); }
	const std::string& GetName() const { return this->name; }

	static cv::Rect ApplyMarginMulToRectangle(const cv::Rect2f& marginMul, const cv::Rect& r);
	static cv::Rect ApplyMarginMulToSize(const cv::Rect2f& marginMul, const cv::Size& s);
};