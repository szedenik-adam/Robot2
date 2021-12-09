#include "Tests.h"
#include "ObjDetect.h"
#include "../Benchmark.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <numeric>
#include <algorithm>
#include "../termcolor.hpp"

cv::Mat LoadImage(const std::string& imagePath)
{
	return cv::imread(imagePath);
}

/*ObjDetectTest::ObjDetectTest(const std::string& imagePath, const std::string& objectPath, const std::list<cv::Rect>& objPlacesInImage)
	:ObjDetectTest(LoadImage(imagePath), LoadImage(objectPath), objPlacesInImage)
{
}
ObjDetectTest::ObjDetectTest(cv::Mat& image, cv::Mat& object, const std::list<cv::Rect>& objPlacesInImage)
	:image(image), object(object), objPlacesInImage(objPlacesInImage)
{
}
ObjDetectTest::ObjDetectTest(cv::Mat&& image, cv::Mat&& object, const std::list<cv::Rect>& objPlacesInImage)
	: image(image), object(object), objPlacesInImage(objPlacesInImage)
{
}*/
ObjDetectTest::ObjDetectTest(const std::string& imagePath, const std::string& objectPath, const std::list<cv::Rect>& objPlacesInImage)
	:imagePath(imagePath), objectPath(objectPath), objPlacesInImage(objPlacesInImage)
{
}
ObjDetectTest::ObjDetectTest(cv::Mat& image, cv::Mat& object, const std::list<cv::Rect>& objPlacesInImage)
	: image(image), object(object), objPlacesInImage(objPlacesInImage)
{
}
ObjDetectTest::ObjDetectTest(cv::Mat&& image, cv::Mat&& object, const std::list<cv::Rect>& objPlacesInImage)
	: image(image), object(object), objPlacesInImage(objPlacesInImage)
{
}

ObjDetectTest::~ObjDetectTest()
{
}

void ObjDetectTest::RunTest()
{
	if (this->image.empty()) { this->image = LoadImage(imagePath); }
	if (this->object.empty()) { this->object = LoadImage(objectPath); }

	float sumDetectPoints = 0, sumTimingPoints = 0;
	cv::Mat hsvImg, hsvObj;
	cv::cvtColor(image, hsvImg, cv::COLOR_BGR2HSV);
	cv::cvtColor(object, hsvObj, cv::COLOR_BGR2HSV);

	for (int channel = 0; channel < image.channels() + 1 + 3; channel++)
	//for (int channel = 2; channel <=2; channel++) // Red channel only
	{
		cv::Mat image1ch, obj1ch; std::string imageChannel;
		if (channel == image.channels()) { // Calculate grayscale.
			cv::cvtColor(image, image1ch, cv::COLOR_BGR2GRAY);
			cv::cvtColor(object, obj1ch, cv::COLOR_BGR2GRAY);
			imageChannel = "Grayscale";
		}
		else if (channel > image.channels()) {
			int hsvChannel = channel - image.channels() - 1;
			cv::extractChannel(hsvImg, image1ch, hsvChannel);
			cv::extractChannel(hsvObj, obj1ch, hsvChannel);
			imageChannel = std::string(1, "HSV"[hsvChannel]) + "-channel";
		}
		else {
			cv::extractChannel(image, image1ch, channel);
			cv::extractChannel(object, obj1ch, channel);
			imageChannel = std::string(1, "BGR"[channel])+"-channel";
		}
		// Other color spaces: https://docs.opencv.org/4.5.3/d8/d01/group__imgproc__color__conversions.html

		std::vector<std::pair<int, const char*>> detectors{ {(int)ObjDetect::Detector::ORB,"ORB"}, {(int)ObjDetect::Detector::ORB_BEBLID,"ORB-BEBLID"},{(int)ObjDetect::Detector::BRISK,"BRISK"},{(int)ObjDetect::Detector::BRISK_BEBLID,"BRISK-BEBLID"},{(int)ObjDetect::Detector::SURF,"SURF"},{(int)ObjDetect::Detector::SURF_BEBLID,"SURF-BEBLID"},{(int)ObjDetect::Detector::SIFT,"SIFT"},{(int)ObjDetect::Detector::SIFT_BEBLID,"SIFT-BEBLID"} };
		std::vector<std::pair<int, const char*>> binary_matchers{ {cv::DescriptorMatcher::MatcherType::BRUTEFORCE_HAMMING,"BruteForceHamming"}, {cv::DescriptorMatcher::MatcherType::BRUTEFORCE_HAMMINGLUT,"BruteForceHammingLUT"} };
		std::vector<std::pair<int, const char*>> float_matchers{ {cv::DescriptorMatcher::MatcherType::BRUTEFORCE,"BruteForce"}, {cv::DescriptorMatcher::MatcherType::BRUTEFORCE_L1,"BruteForceL1"}, {cv::DescriptorMatcher::MatcherType::FLANNBASED,"FLANN"} };
		// Loop through Detector algorithms.
		for (const std::pair<int, const char*> detector : detectors)
		{

			// Loop through Matcher functions.
			const auto& matchers = (detector.second == "SURF" || detector.second == "SIFT") ? float_matchers : binary_matchers;
			for (const std::pair<int, const char*> matcher : matchers)
			{
				float points = 0;
				std::cout << "Test " << detector.second << " - " << matcher.second << " on " << imageChannel << '\n';
				BenchmarkTCollector::Reset();
				ObjDetect od;
				std::vector<RectProb> result = od.FindObject(image1ch, obj1ch, (ObjDetect::Detector)detector.first, (cv::DescriptorMatcher::MatcherType)matcher.first);
				
				// Evaluate found rectangles.
				std::list<int> testRectFoundPercent;
				int foundAreas = 0, testAreas = 0, falseArea = 0, foundRects = 0, totalRects = 0;
				falseArea = std::accumulate(result.begin(), result.end(), 0, [](size_t sum, const cv::Rect& r) { return sum + r.area(); });
				for (const RectProb& testRect : this->objPlacesInImage)
				{
					std::vector<RectProb>::iterator maxCoveredRectIt = result.end();
					int maxIntersectionArea = 0;
					for (auto resRectIt = result.begin(); resRectIt != result.end(); ++resRectIt)
					{
						cv::Rect intersection = (*resRectIt & testRect);
						int intersectionArea = intersection.area();
						if (intersectionArea > maxIntersectionArea) {
							maxCoveredRectIt = resRectIt;
							maxIntersectionArea = intersectionArea;
						}
					}
					int testRectArea = testRect.area();
					testRectFoundPercent.push_back(maxIntersectionArea *100 / testRectArea);
					foundAreas += maxIntersectionArea;
					testAreas += testRectArea;
					falseArea -= maxIntersectionArea;
					if (maxIntersectionArea) {
						foundRects++;
					}
					totalRects++;
				}
				const auto foundObjColor = (foundRects == totalRects) ? termcolor::bright_green : foundRects == 0 ? termcolor::bright_red : termcolor::bright_yellow;
				std::cout << "Found objects: " << foundObjColor << foundRects << '/' << totalRects << termcolor::reset << ".\n";
				if (testAreas) {
					int foundAreaPercent = foundAreas * 100 / testAreas;
					const auto foundAreaColor = (foundAreaPercent > 95) ? termcolor::bright_green : foundAreaPercent < 75 ? termcolor::bright_red : termcolor::bright_yellow;
					std::cout << "Found object area: " << foundAreaColor << foundAreaPercent << " %" << termcolor::reset << ".\n";
					int falseDetectPercent = falseArea * 100 / testAreas;
					const auto falseDetectColor = (falseDetectPercent < 5) ? termcolor::bright_green : foundAreaPercent > 10 ? termcolor::bright_red : termcolor::bright_yellow;
					std::cout << "False detection area / test: " << falseDetectColor << falseDetectPercent << " %" << termcolor::reset <<".\n";
					points += std::max<float>(0, foundRects * 5. / totalRects + foundAreas * 5. / testAreas - falseDetectPercent/10); // All object found: 5p, None: 0p. 100% found by area: 5p, 0%: 0p. 10% false detection by area: -1p, 0%: 0p. Minimum is 0p.
				}
				else {
					float falseDetectPercent = falseArea * 100 / (image.rows * image.cols);
					const auto falseDetectColor = (falseDetectPercent == 0) ? termcolor::bright_green : falseDetectPercent > 5 ? termcolor::bright_red : termcolor::bright_yellow;
					std::cout << "False detection area / image: " << falseDetectColor << falseDetectPercent << termcolor::reset << " %.\n";
					points += std::max<float>(0, 5 - result.size() +(5*!result.size()) - falseArea * 100 / (image.rows * image.cols)); // 5p - 1p for each detected rectangle, +5p if nothing detected, 100% detected by image area: -100p, 0%: 0p. Minimum is 0p.
				}
				if (!testRectFoundPercent.empty()) {
					std::cout << "Found rectangles: "; auto testRectFoundIt = testRectFoundPercent.begin();
					std::cout << *testRectFoundIt++; 
					for (; testRectFoundIt != testRectFoundPercent.end(); ++testRectFoundIt) std::cout << " %, " << *testRectFoundIt;
					std::cout << " %.\n";
				}
				sumDetectPoints += points;
				const auto detectPointColor = (points >= 9.5) ? termcolor::bright_green : points <= 7 ? termcolor::bright_red : termcolor::bright_yellow;
				std::cout << "Detection points: " << detectPointColor << std::setprecision(2) << points << 'p' <<termcolor::reset<< ".\n";

				// Get Benchmark.
				std::map<std::string, std::tuple<long long, size_t>> bms = BenchmarkTCollector::Results();
				int64_t totalTimeMs = std::get<0>(bms["FindObject"]) / 1000;
				const auto totalTimeColor = (totalTimeMs < 100) ? termcolor::bright_green : (totalTimeMs < 200) ? termcolor::bright_white : termcolor::bright_yellow;
				float timingPoints = (totalTimeMs < 10) ? 10 : (totalTimeMs < 200) ? ((200-totalTimeMs)*5./100) : 0;
				std::cout << "Processing time: " << totalTimeColor << totalTimeMs << " ms " <<  std::setprecision(2) << timingPoints << 'p' << termcolor::reset
					<<" (Image Keypoints: [Base: "<< std::get<0>(bms["FindKeypointsSrc"]) / 1000 <<" ms, Object: " << std::get<0>(bms["FindKeypointsObj"]) / 1000 <<" ms], Other: "<< (std::get<0>(bms["FindObject"]) - std::get<0>(bms["FindKeypointsSrc"]) - std::get<0>(bms["FindKeypointsObj"]))/1000 <<" ms).\n";
				points += timingPoints;
				sumTimingPoints += timingPoints;
				std::cout << "Total points: " << std::setprecision(2) << points << 'p' << termcolor::reset << ".\n";
				//BenchmarkTCollector::Print();

				// Save calculated points.
				ObjDetectTest::totalPoints += points;
				ObjDetectTest::AddPoint(imageChannel, detector.second, matcher.second, points- timingPoints, totalTimeMs);
			}
		}
	}
	std::cout << "Total points: " << std::fixed << std::setprecision(2) << (sumDetectPoints+sumTimingPoints) << " (Detect: " << std::setprecision(2) << sumDetectPoints <<", Timing: " << std::setprecision(2) << sumTimingPoints<< ").\n";
}

void ObjDetectTest::RunDownsampled(double scale)
{
	if (scale > 1) { std::cout << "RunDownsampled not upscaling.\n";  return; }
	if (scale == 1) { this->RunTest(); }
	else {
		cv::Mat dImg, dObj;
		cv::resize(this->image, dImg, cv::Size(this->image.cols * scale, this->image.rows * scale), scale, scale, cv::INTER_NEAREST);
		cv::resize(this->object, dObj, cv::Size(this->object.cols * scale, this->object.rows * scale), scale, scale, cv::INTER_NEAREST);
		std::list<cv::Rect> dObjPlaces;
		for (const cv::Rect& objPlace : this->objPlacesInImage) { dObjPlaces.emplace_back(objPlace.x * scale, objPlace.y * scale, objPlace.width * scale, objPlace.height * scale); }
		ObjDetectTest dTest(dImg, dObj, dObjPlaces);
		dTest.RunTest();
	}
}

// static
void ObjDetectTest::DumpGlobalStats()
{
	ObjDetectTest::DumpStatMap("Image Channels", ObjDetectTest::imageChannelPoints);
	ObjDetectTest::DumpStatMap("Detectors", ObjDetectTest::detectorPoints);
	ObjDetectTest::DumpStatMap("Matchers", ObjDetectTest::matcherPoints);
	ObjDetectTest::DumpStatMap("Methods", uniqueConfigPoints, 10);

	std::cout << "Global points: " << ObjDetectTest::totalPoints << '\n';
}

// static
void ObjDetectTest::DumpStatMap(const std::string& name, const std::map<std::string, std::tuple<float, float, int>>& container, int limit)
{
	std::vector<std::tuple<std::string, float, float>> vec;
	for (const auto& [key, val] : container)
	{
		vec.emplace_back(key, 100 * std::get<0>(val) / std::get<2>(val), std::get<1>(val) / std::get<2>(val));
	}
	std::sort(vec.begin(), vec.end(), [](const std::tuple<std::string, float, float>& a, const std::tuple<std::string, float, float>& b) {return std::get<1>(a) > std::get<1>(b); });

	bool firstEntry = true;
	std::cout << name << ":\n";
	
	for (const auto& elem : vec) {
		if (firstEntry) { std::cout << termcolor::bright_white; }
		std::cout << "  " << std::get<0>(elem) << ": " << std::setprecision(2) << std::get<1>(elem) <<" ("<< (int)std::round(std::get<2>(elem)) << " ms)\n";
		if (firstEntry) {
			std::cout << termcolor::reset; firstEntry = false;
		}
		if (!--limit) { break; }
	}
}

// static
void ObjDetectTest::AddPoint(const std::string& imageChannel, const std::string& detector, const std::string& matcher, float point, float timeMs)
{
	ObjDetectTest::AddPoint(imageChannel, ObjDetectTest::imageChannelPoints, point, timeMs);
	ObjDetectTest::AddPoint(detector, ObjDetectTest::detectorPoints, point, timeMs);
	ObjDetectTest::AddPoint(matcher, ObjDetectTest::matcherPoints, point, timeMs);
	ObjDetectTest::AddPoint(imageChannel + " / " + detector + " / " + matcher, ObjDetectTest::uniqueConfigPoints, point, timeMs);
}
// static
void ObjDetectTest::AddPoint(const std::string& name, std::map<std::string, std::tuple<float, float, int>>& container, float point, float timeMs)
{
	std::pair<decltype(container.begin()), bool> empRet = container.try_emplace(name, point, timeMs, 1);
	if (!empRet.second) {
		std::get<0>((*empRet.first).second) += point;
		std::get<1>((*empRet.first).second) += timeMs;
		std::get<2>((*empRet.first).second) += 1;
	}
}
