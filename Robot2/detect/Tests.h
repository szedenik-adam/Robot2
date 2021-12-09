#pragma once

#include <map>
#include <list>
#include <string>
#include <opencv2/core.hpp>

class ObjDetectTest
{
	cv::Mat image;
	cv::Mat object;
	const std::list<cv::Rect> objPlacesInImage;
	std::string imagePath, objectPath;
public:
	ObjDetectTest(const std::string& imagePath, const std::string& objectPath, const std::list<cv::Rect>& objPlacesInImage);
	ObjDetectTest(cv::Mat& image, cv::Mat& object, const std::list<cv::Rect>& objPlacesInImage);
	ObjDetectTest(cv::Mat&& image, cv::Mat&& object, const std::list<cv::Rect>& objPlacesInImage);
	~ObjDetectTest();

	void RunTest();

	void RunDownsampled(double scale);

	inline static float totalPoints = 0;
	inline static std::map<std::string, std::tuple<float, float, int>> imageChannelPoints{};
	inline static std::map<std::string, std::tuple<float, float, int>> detectorPoints{};
	inline static std::map<std::string, std::tuple<float, float, int>> matcherPoints{};
	inline static std::map<std::string, std::tuple<float, float, int>> uniqueConfigPoints{};

	static void DumpGlobalStats();
private:
	static void DumpStatMap(const std::string& name, const std::map<std::string, std::tuple<float, float, int>>& container, int limit=100);
	static void AddPoint(const std::string& imageChannel, const std::string& detector, const std::string& matcher, float point, float timeMs);
	static void AddPoint(const std::string& name, std::map<std::string, std::tuple<float, float, int>>& container, float point, float timeMs);
};

