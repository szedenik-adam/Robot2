#include "Config.h"
#include "Worker.h"
#include <iostream>
#include <filesystem>
#include <ranges>
#include <chrono>
#include <opencv2/imgcodecs.hpp>
#include <SDL2/SDL_timer.h>
#include "Random.h"


void Config::InitStates(libconfig::Setting& setting)
{
    this->stateNameToIndex.clear();
    this->states.clear();
    for (libconfig::SettingIterator actionIt = setting.begin(); actionIt != setting.end(); ++actionIt)
    {
        for (const std::string stateParam : {"state", "state_blacklist"})
        {
            if (actionIt->exists(stateParam))
            {
                libconfig::Setting& stateSetting = actionIt->lookup(stateParam);
                for (libconfig::SettingIterator stateIt = stateSetting.begin(); stateIt != stateSetting.end(); ++stateIt)
                {
                    if (!this->stateNameToIndex.contains(*stateIt))
                    {
                        this->states.emplace_back(*stateIt, this->objects.size());
                        this->stateNameToIndex[*stateIt] = this->states.size() - 1;
                    }
                }
            }
        }
    }
}

void Config::ExtractStatesIntoAction(libconfig::Setting& setting, const std::string& settingName, std::list<int>& stateList)
{
    libconfig::Setting* stateSetting = nullptr;
    if (setting.exists(settingName)) { stateSetting = &setting.lookup(settingName); }
    else if (setting.exists(settingName+"s")) { stateSetting = &setting.lookup(settingName+"s"); }
    if (!stateSetting) return;
    if (stateSetting->isString()) { stateList.push_back(this->GetStateInd(*stateSetting));}

    for (libconfig::SettingIterator stateIt = stateSetting->begin(); stateIt != stateSetting->end(); ++stateIt)
    {
        if (stateIt->isString()) { stateList.push_back(this->GetStateInd(*stateIt)); }
    }
}

int Config::GetStateInd(const std::string& stateName)
{
    for (int i = 0; i < this->states.size(); i++)
    {
        if (this->states[i].name == stateName) { return i; }
    }
    this->states.emplace_back(stateName, this->objects.size());
    return this->states.size()-1;
}

void Config::ExtractRequirementsIntoAction(libconfig::Setting& setting, Action& action)
{
    if (setting.exists("timeout")) {
        int timeoutMs = Config::TimeParse(setting.lookup("timeout"));

        action.requirements.emplace_back(new Config::TimeoutRequirement(timeoutMs));
    }
    if (setting.exists("object"))
    {
        libconfig::Setting& objSetting = setting.lookup("object");
        this->ExtractObjectRequirementIntoAction(objSetting, action);
    }
    if (setting.exists("objects"))
    {
        libconfig::Setting& objSetting = setting.lookup("objects");
        for (libconfig::SettingIterator objIt = objSetting.begin(); objIt != objSetting.end(); ++objIt)
            this->ExtractObjectRequirementIntoAction(*objIt, action);
    }
}

void Config::ExtractObjectRequirementIntoAction(libconfig::Setting& setting, Action& action)
{
    bool notFoundCheck = false;
    libconfig::Setting* objNameSetting = nullptr;
    if (setting.exists("found")) { objNameSetting = &setting.lookup("found"); }
    if (setting.exists("not_found")) { objNameSetting = &setting.lookup("not_found"); notFoundCheck = true; }
    std::string objName = *objNameSetting;
    int objIndex;
    try {
        objIndex = this->objNameToIndex.at(objName);
    }
    catch (std::out_of_range& err)
    {
        std::cout << "Object "<< std::quoted(objName)<<" not found!\n";
        return;
    }
    cv::Rect2f screenMarginMul(Config::MarginMultiplierParse(setting));
    
    //action.objReqs.emplace_back(objIndex, notFoundCheck, screenMarginMul);
    action.requirements.emplace_back(new Config::ObjectRequirement(objIndex, notFoundCheck, screenMarginMul, this->minDetectionQuality));
}

void Config::ExtractTasksIntoAction(libconfig::Setting& setting, Action& action)
{
    if (setting.isList() || setting.isArray())
    {
        for (libconfig::SettingIterator taskIt = setting.begin(); taskIt != setting.end(); ++taskIt)
        {
            this->ExtractSingleTaskIntoAction(*taskIt, action);
        }
    }
    else {
        this->ExtractSingleTaskIntoAction(setting, action);
    }
}

void Config::ExtractSingleTaskIntoAction(libconfig::Setting& setting, Action& action)
{
    if (setting.exists("click")) {
        libconfig::Setting& task = setting.lookup("click");
        int delayMs = Config::TimeParse(task.lookup("delay"));
        int randomDelayMs = Config::TimeParse(task.lookup("random_delay"));
        std::string targetName; task.lookupValue("on", targetName);
        int objInd = (targetName == "screen") ? -1 : this->objNameToIndex.at(targetName);
        cv::Rect2f marginMul = Config::MarginMultiplierParse(task);
        char selectMode = 0; std::string selectStr;
        if (task.lookupValue("select", selectStr)) {
            if (selectStr.find("left") != std::string::npos) { selectMode |= CLICKTASK_SELECT_LEFT; }
            if (selectStr.find("right") != std::string::npos) { selectMode |= CLICKTASK_SELECT_RIGHT; }
            if (selectStr.find("top") != std::string::npos) { selectMode |= CLICKTASK_SELECT_TOP; }
            if (selectStr.find("bottom") != std::string::npos) { selectMode |= CLICKTASK_SELECT_BOTTOM; }
        }
        action.tasks.emplace_back(new ClickTask(objInd, delayMs, randomDelayMs, marginMul, selectMode));
    }
    if (setting.exists("wait")) {
        libconfig::Setting& task = setting.lookup("wait");
        int delayMs = Config::TimeParse(task.lookup("delay"));
        int randomDelayMs = Config::TimeParse(task.lookup("random_delay"));
        action.tasks.emplace_back(new WaitTask(delayMs, randomDelayMs));
    }
    if (setting.exists("counter_add")) {
        int increment=1; setting.lookupValue("counter_add", increment);
        std::string stateName; setting.lookupValue("if_state", stateName);
        int stateInd = this->stateNameToIndex.at(stateName);
        const State* state = &this->states[stateInd];
        action.tasks.emplace_back(new CounterIncrementTask(increment, state));
    }
    if (setting.exists("state")) {
        std::string stateName; setting.lookupValue("state", stateName);
        int stateInd = this->stateNameToIndex.at(stateName);
        action.tasks.emplace_back(new SetStateTask(stateInd));
    }
}

int Config::TimeParse(libconfig::Setting& setting)
{
    int result = 0;
    if (setting.isString())
    {
        std::string text = setting;
        if (text.ends_with("ms")) { result = std::stoi(text.substr(0, text.length() - 2)); }
        else if (text.ends_with("us")) { result = std::stoull(text.substr(0, text.length() - 2)) / 1000; }
        else if (text.ends_with("ns")) { result = std::stoull(text.substr(0, text.length() - 2)) / 1000000; }
        else if (text.ends_with("s")) { result = std::stoull(text.substr(0, text.length())) * 1000; }
    }
    if (setting.isNumber()) { result = setting; }
    return result;
}

void Config::MarginMultiplierParse(std::string text, cv::Rect2f& marginMul, bool xMasked, bool yMasked)
{
    size_t pos = 0;
    constexpr std::string_view delimiter{ "," };
    int partInd = -1;
    do {
        partInd++;
        pos = text.find(delimiter);
        std::string part(text.substr(0, pos));

        float* target = nullptr; bool center = false;
        if (part.starts_with("bottom")) { target = &marginMul.y; part.erase(0, ((std::string)"bottom").size()); }
        if (part.starts_with("right")) { target = &marginMul.x; part.erase(0, ((std::string)"right").size()); }
        if (part.starts_with("top")) { target = &marginMul.height; part.erase(0, ((std::string)"top").size()); }
        if (part.starts_with("left")) { target = &marginMul.width; part.erase(0, ((std::string)"left").size()); }
        if (part.starts_with("center")) { center = true; part.erase(0, ((std::string)"center").size()); }

        float value = 0;
        if (part.ends_with("%")) {
            part.erase(part.size() - 1);
            value = std::stof(part) / 100;
        }
        else {
            value = std::stof(part);
        }
        //if value > 1 throw error
        value = 1 - value;

        if (target) { *target = value; }
        else if (center) {
            if (partInd == 0 && text.find(',') != std::string::npos) { yMasked = true; }
            if (partInd == 1) { xMasked = true; }
            value /= 2;
            if (!xMasked) { marginMul.x = value; marginMul.width = value;}
            if (!yMasked) { marginMul.y = value; marginMul.height = value;}
        }

        text.erase(0, pos + delimiter.length());
    } while (pos != std::string::npos);
}

cv::Rect2f Config::MarginMultiplierParse(libconfig::Setting& setting)
{
    std::string regionStr;
    cv::Rect2f result{ 0,0,0,0 };
    if (setting.exists("at"))
    {
        setting.lookupValue("at", regionStr);
        Config::MarginMultiplierParse(regionStr, result, false, false);
    }
    if (setting.exists("x"))
    {
        setting.lookupValue("x", regionStr);
        Config::MarginMultiplierParse(regionStr, result, false, true);
    }
    if (setting.exists("y"))
    {
        setting.lookupValue("y", regionStr);
        Config::MarginMultiplierParse(regionStr, result, true, false);
    }
    if (setting.exists("x_range"))
    {
        libconfig::Setting& rangeSetting = setting.lookup("x_range");
        Config::RangeParse(rangeSetting, result);
    }
    if (setting.exists("y_range"))
    {
        libconfig::Setting& rangeSetting = setting.lookup("y_range");
        Config::RangeParse(rangeSetting, result);
    }
    return result;
}

float Config::RangeParamParse(libconfig::Setting& setting, bool isXAxis)
{
    if (setting.isNumber()) {
        return static_cast<float>(setting);
    }
    else if (setting.isString()) {
        bool endSide = false;
        std::string str = setting;
        if (isXAxis) {
            if (str.starts_with("left")){ str.erase(0, ((std::string)"left").size()); }
            else if (str.starts_with("right")) { endSide = true; str.erase(0, ((std::string)"right").size()); }
            else {}//throw error
        }
        else {
            if (str.starts_with("top")) { str.erase(0, ((std::string)"top").size()); }
            else if (str.starts_with("bottom")) { endSide = true; str.erase(0, ((std::string)"bottom").size()); }
            else {}//throw error
        }
        float value = Config::PercentParse(str);
        if (endSide) { value = 1 - value; }
        return value;
    }
    else {} // throw error.
    return 0;
}

float Config::PercentParse(std::string text)
{
    float result = 0;
    if (text.ends_with("%")) {
        text.erase(text.size() - 1);
        result = std::stof(text) / 100;
    }
    else {
        result = std::stof(text);
    }
    return result;
}

void Config::RangeParse(libconfig::Setting& setting, cv::Rect2f& marginMul)
{
    bool isXAxis = false;
    char settingNameStartLetter = setting.getName()[0];
    if (settingNameStartLetter == 'x') { isXAxis = true; }
    else if (settingNameStartLetter == 'y') {}
    else {} // throw error.
    float beginVal = Config::RangeParamParse(setting[0], isXAxis);
    float endVal = Config::RangeParamParse(setting[1], isXAxis);
    if (beginVal >= endVal) {} // throw error.
    if (isXAxis) {
        marginMul.x = beginVal;
        marginMul.width = 1 - endVal;
    }
    else {
        marginMul.y = beginVal;
        marginMul.height = 1 - endVal;
    }
}

bool Config::LoadConfig(const std::string& filePath)
{
    bool testOnlyConfig = true;
    libconfig::Config config;
    try {
        std::cerr << "Loading config " << filePath << ".\n";
        config.readFile(filePath);
    }
    catch (const libconfig::FileIOException& fioex)
    {
        std::cerr << "I/O error while reading file.\n";
        return testOnlyConfig;
    }
    catch (const libconfig::ParseException& pex)
    {
        std::cerr << "Parse error at " << pex.getFile() << ":" << pex.getLine()
            << " - " << pex.getError() << std::endl;
        return testOnlyConfig;
    }
    if (config.exists("tests"))
    {
        libconfig::Setting& testsSetting = config.lookup("tests");
        if (!testsSetting.isList())
        {
            std::cerr << "tests config entry is not a list, use \"()\" braces!\n";
            return testOnlyConfig;
        }
        for (libconfig::SettingIterator testIt = testsSetting.begin(); testIt != testsSetting.end(); ++testIt)
        {
            std::string baseImagePath, templateImagePath;
            testIt->lookupValue("image", baseImagePath);
            testIt->lookupValue("template", templateImagePath);
            std::list<cv::Rect> objPlacesInImage;
            libconfig::Setting& testRes = testIt->lookup("result");
            for (libconfig::SettingIterator resIt = testRes.begin(); resIt != testRes.end(); ++resIt)
            {
                cv::Rect rect;
                resIt->lookupValue("x", rect.x); resIt->lookupValue("y", rect.y); resIt->lookupValue("w", rect.width); resIt->lookupValue("h", rect.height);
                objPlacesInImage.push_back(rect);
            }
            // Add test.
            tests.emplace_back(baseImagePath, templateImagePath, objPlacesInImage);
        }
    }
    if (config.exists("actions"))
    {
        testOnlyConfig = false;
        if (config.exists("objects"))
        {
            libconfig::Setting& objSetting = config.lookup("objects");
            for (libconfig::SettingIterator objIt = objSetting.begin(); objIt != objSetting.end(); ++objIt)
            {
                std::string image, name;
                objIt->lookupValue("image", image);
                objIt->lookupValue("name", name);
                this->objects.emplace_back(image, name);
                this->objNameToIndex.insert(std::pair(name, this->objects.size()-1));
            }
        }
        std::string strDetector, strMatcher;
        this->LoadSetting(config, "scan_wait_ms", this->scanWaitMs, 500);
        this->LoadSetting(config, "scan_wait_random_ms", this->scanWaitRandomMs, 0);
        this->LoadSetting(config, "image_channel", this->image_channel, "Grayscale");
        this->LoadSetting(config, "detector", strDetector, "ORB_BEBLID");
        this->LoadSetting(config, "matcher", strMatcher, "BRUTEFORCE_HAMMING");
        this->LoadSetting(config, "source", this->source, "android");
        this->LoadSetting(config, "counter_limit", this->counter_limit, 100);
        this->LoadSetting(config, "estimator_history", this->estimator_history, 8);
        this->LoadSetting(config, "min_detect_quality", this->minDetectionQuality, 0.1f);

        std::optional<ObjDetect::Detector> detector = magic_enum::enum_cast<ObjDetect::Detector>(strDetector);
        if (detector.has_value()) { this->detector = detector.value(); }
        else { this->detector = ObjDetect::Detector::ORB_BEBLID; }

        std::optional<cv::DescriptorMatcher::MatcherType> matcher = magic_enum::enum_cast<cv::DescriptorMatcher::MatcherType>(strMatcher);
        if (matcher.has_value()) { this->matcher = matcher.value(); }
        else { this->matcher = cv::DescriptorMatcher::MatcherType::BRUTEFORCE_HAMMING; }

        libconfig::Setting& actionSetting = config.lookup("actions");
        this->InitStates(actionSetting);
        for (libconfig::SettingIterator actionIt = actionSetting.begin(); actionIt != actionSetting.end(); ++actionIt)
        {
            if (!actionIt->exists("on") || !actionIt->exists("do")) continue;
            Action& action = this->actions.emplace_back();
            this->ExtractStatesIntoAction(*actionIt, "state", action.states);
            this->ExtractStatesIntoAction(*actionIt, "state_blacklist", action.state_blacklist);
            this->ExtractRequirementsIntoAction(actionIt->lookup("on"), action);
            this->ExtractTasksIntoAction(actionIt->lookup("do"), action);
        }

        // Update the actions' state array based on the state_blacklist array.
        int actionInd = -1;
        for (Action& action : this->actions)
        {
            actionInd++;
            if (!action.state_blacklist.empty()) {
                for (int stateInd = 0; stateInd < this->states.size(); stateInd++)
                {
                    if (std::find(action.state_blacklist.begin(), action.state_blacklist.end(), stateInd) != action.state_blacklist.end()) continue;
                    if (std::find(action.states.begin(), action.states.end(), stateInd) != action.states.end()) continue;
                    action.states.push_back(stateInd);
                }
                action.state_blacklist.clear();
            }
            // Update the states' action list.
            for (int stateInd : action.states)
            {
                std::list<int>& sAcList = this->states[stateInd].actionInds;
                if (std::find(sAcList.begin(), sAcList.end(), actionInd) == sAcList.end())
                {
                    // Add action to state.
                    sAcList.push_back(actionInd);
                    // Add objects to state.
                    //for (const ObjectRequirement& objr : action.objReqs) { this->states[stateInd].SetObjectToDetect(objr.GetObjIndex()); }
                    for (const std::unique_ptr<Requirement>& req : action.requirements) {
                        const ObjectRequirement* objReq = dynamic_cast<const ObjectRequirement*>(&*req);
                        if (objReq) {
                            this->states[stateInd].SetObjectToDetect(objReq->GetObjIndex());
                        }
                    }
                }
            }
        }

        // Set inital state.
        if (config.exists("init_state")) {
            std::string stateStr = config.lookup("init_state");
            this->initialState = this->stateNameToIndex[stateStr];
        }
        else {
            for (const std::string& key : { "init","begin","start","unknown"}) {
                auto search = this->stateNameToIndex.find(key);
                if (search == this->stateNameToIndex.end()) continue;
                this->initialState = search->second;
            }
        }
    }
    size_t lastSlashPos = filePath.find_last_of("/\\");
    std::string name = (lastSlashPos == std::string::npos) ? filePath : filePath.substr(lastSlashPos +1);
    if (name.ends_with("config.txt") || name.ends_with("Config.txt")) { name = name.substr(0, name.length() - strlen("config.txt")); }
    else {
        size_t lastDotPos = filePath.find_last_of('.');
        if (lastDotPos != std::string::npos) name = name.substr(0, lastDotPos);
    }
    std::for_each(name.begin(), name.end(), [](char& c) { c = ::tolower(c); });
    this->name = name;

    return testOnlyConfig;
}

ObjDetect Config::CreateDetector()
{
    ObjDetect result(this->detector, this->matcher, this->image_channel);
    for (const std::pair<std::string, std::string>& object : this->objects)
    {
        const std::string& imagePath = object.first;
        cv::Mat objImage = cv::imread(imagePath);
        result.AddObject(objImage);
    }
    return result;
}

int Config::GetScanWaitMs() const
{
    return this->scanWaitMs + Random(scanWaitRandomMs);
}

cv::Rect Config::ApplyMarginMulToRectangle(const cv::Rect2f& marginMul, const cv::Rect& r)
{
    cv::Rect result(r.x + std::round(marginMul.x * r.width),
        r.y + std::round(marginMul.y * r.height),
        r.width - std::round((marginMul.x + marginMul.width) * r.width),
        r.height - std::round((marginMul.y + marginMul.height) * r.height));
    return result;
}

cv::Rect Config::ApplyMarginMulToSize(const cv::Rect2f& marginMul, const cv::Size& s)
{
    return Config::ApplyMarginMulToRectangle(marginMul, cv::Rect(cv::Point(0, 0), s));
}

bool Config::TimeoutRequirement::IsSatisfied(Worker* worker) const
{
    return bool(worker->GetNow() - worker->GetLastAction() > this->timeoutMs);
}

bool Config::ObjectRequirement::IsSatisfied(Worker* worker) const //const std::vector<std::vector<cv::Rect>>& detections, int width, int height) const
{
    bool found = false;
    cv::Rect scanRegion = this->CalculateScanRect(worker->GetFixResConfig().GetSize());

    std::vector<RectProb>& objDetection = worker->GetLastDetection()[this->objIndex];
    for (RectProb& rect : objDetection)
    {
        if (rect.p >= this->minDetectQuality && (rect & scanRegion).area())
        {
            //worker->GetFirstValidRect()[this->objIndex] = &rect - (&*objDetection.begin());
            //return !this->notFoundCheck;
            found = true;
        }
        else {
            rect.isExcluded = true;
        }
    }
    return this->notFoundCheck ^ found;
}

void Config::ObjectRequirement::UpdateScreenSize(const cv::Size screenSize)
{

}

cv::Rect Config::ObjectRequirement::CalculateScanRect(const cv::Size screenSize) const
{
    return Config::ApplyMarginMulToSize(this->screenMarginMul, screenSize);
}

void Config::WaitTask::Execute(Worker& worker)
{
    worker.GetInfos().Commit();
    int sleepMs = this->delayMs + Random(this->randomDelayMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    worker.UpdateNow();
}

void Config::SetStateTask::Execute(Worker& worker)
{
    worker.SetState(this->newState);
    std::cout << std::format("Entering new state: {}\n", worker.GetState()->name);
}

void Config::CounterIncrementTask::Execute(Worker& worker)
{
    if (worker.GetState() == this->curState) {
        bool isLimitReached = worker.GetEstimator().Add(worker.GetNow(), this->increment);
        if (isLimitReached) { worker.Stop(false, true); }
        std::cout << std::format("Counter incremented (+{})\n", this->increment);
        worker.GetEstimator().Display(worker.GetNow());
    }
}

void Config::ClickTask::Execute(Worker& worker)
{
    const RectProb* selectedObjRect;
    RectProb screenRect;
    if (this->objInd == -1) {
        // Set region to the whole screen.
        screenRect = cv::Rect{ 0, 0, worker.GetFixResConfig().GetWidth(), worker.GetFixResConfig().GetHeight() };
        selectedObjRect = &screenRect;
    }
    else {
        // Use the first valid found object's rectangle (some found object might be outside of the ObjectRequirement's area).
        //int rectInd = worker.GetFirstValidRect()[this->objInd];
        selectedObjRect = nullptr;//&worker.GetLastDetection()[this->objInd][rectInd++];
        auto calculateDirScore = [&worker](const RectProb* objRect, int selectMode)->float {
            float dirScore = 0;
            if (std::popcount((unsigned int)selectMode) == 2) {
                switch (selectMode) {
                case CLICKTASK_SELECT_TOP | CLICKTASK_SELECT_LEFT: dirScore = std::pow(objRect->x, 2) + std::pow(objRect->y, 2); break;
                case CLICKTASK_SELECT_TOP | CLICKTASK_SELECT_RIGHT: dirScore = std::pow(worker.GetFixResConfig().GetWidth() - (objRect->x + objRect->width), 2) + std::pow(objRect->y, 2); break;
                case CLICKTASK_SELECT_BOTTOM | CLICKTASK_SELECT_RIGHT: dirScore = std::pow(worker.GetFixResConfig().GetWidth() - (objRect->x + objRect->width), 2) + std::pow(worker.GetFixResConfig().GetHeight() - (objRect->y + objRect->height), 2); break;
                case CLICKTASK_SELECT_BOTTOM | CLICKTASK_SELECT_LEFT: dirScore = std::pow(objRect->x, 2) + std::pow(worker.GetFixResConfig().GetHeight() - (objRect->y + objRect->height), 2); break;
                default: dirScore = 0;
                }
            }
            else {
                switch (selectMode) {
                case CLICKTASK_SELECT_LEFT: dirScore = objRect->x; break;
                case CLICKTASK_SELECT_RIGHT: dirScore = worker.GetFixResConfig().GetWidth() - objRect->x; break;
                case CLICKTASK_SELECT_TOP: dirScore = objRect->y; break;
                case CLICKTASK_SELECT_BOTTOM: dirScore = worker.GetFixResConfig().GetHeight() - objRect->y; break;
                default:
                    // TODO: error handling.
                    break;
                }
            }
            dirScore *= objRect->p;
            return dirScore;
        };

        float selectedDirScore = std::numeric_limits<float>::max();

        for (const RectProb& objRect : worker.GetLastDetection()[this->objInd])
        {
            if (objRect.isExcluded) continue;
            if (this->selectMode == CLICKTASK_SELECT_FIRST) {
                selectedObjRect = &objRect;
                break;
            }
            float dirScore = calculateDirScore(&objRect, this->selectMode);
            if (dirScore < selectedDirScore) {
                selectedDirScore = dirScore;
                selectedObjRect = &objRect;
            }
        }
    }
    if (selectedObjRect == nullptr) {
        printf("No valid rectangle for ClickTask.\n");
        return;
    }

    // Apply margin.
    cv::Rect shrinkedObjRect = Config::ApplyMarginMulToRectangle(this->marginMul, *selectedObjRect);
    // Get random point.
    cv::Point p(shrinkedObjRect.x + Random(shrinkedObjRect.width), shrinkedObjRect.y + Random(shrinkedObjRect.height));

    std::cout << std::format("Clicking at ({}, {})\n", p.x, p.y);
    worker.GetInfos().GetLive().SetClickTime(worker.GetNow(), p.x, p.y, &shrinkedObjRect);
    // Send event.
    worker.SendTouchEvent(p, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(this->delayMs + Random(this->randomDelayMs)));
    worker.SendTouchEvent(p, false);
}
