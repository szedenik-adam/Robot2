#pragma once

#include <tuple>
#include <vector>
#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>

class RectProb : public cv::Rect {
public:
	float p;
	bool isExcluded;
	RectProb() :cv::Rect(), p(0), isExcluded(false) {}
	RectProb(const cv::Rect& r, float p = 0) :cv::Rect(r), p(p), isExcluded(false) {}

	RectProb operator | (const RectProb& r) // OR operator needed for merging rects.
	{
		return RectProb(((const cv::Rect&)*this) | (cv::Rect)r, this->p+r.p); //std::max(this->p+r.p)
	}
	RectProb& operator |= (const RectProb& r)
	{
		((cv::Rect&)*this) |= (const cv::Rect&)r;
		this->p += r.p; //this->p = std::max(this->p, r.p);
		this->isExcluded |= r.isExcluded;
		return *this;
	}
	RectProb& operator=(const RectProb& other)
	{
		if (this == &other) return *this; // Guard self assignment
		((cv::Rect&)*this) = (const cv::Rect&)other;
		this->p = other.p;
		this->isExcluded = other.isExcluded;
		return *this;
	}
};

class ObjDetect
{
public:
	enum class Detector {
		AKAZE_DESCRIPTOR_KAZE_UPRIGHT = 0,
		AKAZE_DESCRIPTOR_MLDB,
		ORB,
		ORB_BEBLID,
		BRISK,
		BRISK_BEBLID,
		SURF,
		SURF_BEBLID,
		SIFT,
		SIFT_BEBLID,
	};
	class ImageFeatures {
	public:
		// Found keypoints and descriptors.
		std::vector<cv::KeyPoint> keypoints;
		cv::Mat descriptors;
		cv::Size size;

		ImageFeatures(const cv::Mat& img, enum Detector detector);
	};

	static cv::Mat PreprocessImage(const cv::Mat& image, const std::string& channel = "R"); // Converts RGB to single channel image.
	static void PreprocessImageInplace(cv::Mat& image, const std::string& channel = "R");

	static std::tuple <std::vector<cv::KeyPoint>, cv::Mat> FindKeypoints(const cv::Mat& image, enum Detector detector = Detector::ORB_BEBLID); // Detects keypoints and calculates descriptor on a single channel image. This is used by the keypoint matcher.
	static std::vector<cv::DMatch> MatchDescriptors(const cv::Mat& descImg1, const cv::Mat& descImg2, cv::DescriptorMatcher::MatcherType matcher = cv::DescriptorMatcher::MatcherType::BRUTEFORCE_HAMMING);
	static std::tuple<std::vector<cv::Point2f>, std::vector<cv::Point2f>> GetMatchedPoints(const std::vector<cv::DMatch>& matches, const std::vector<cv::KeyPoint>& keyImg1, const std::vector<cv::KeyPoint>& keyImg2);
	static std::tuple<cv::Mat, cv::Mat> GetTransformationMatrix(const std::tuple<std::vector<cv::Point2f>, std::vector<cv::Point2f>>& points);
	static bool ValidateTransformationMatrix(const cv::Mat& h, cv::Size srcSize);
	static int RemoveMatchedPoints(std::tuple<std::vector<cv::Point2f>, std::vector<cv::Point2f>>& points, const cv::Mat& inliers);
	static cv::Rect GetSubImageRect(const cv::Mat& objImg, const cv::Mat& h);
	static cv::Rect GetSubImageRect(const cv::Size& objImgSize, const cv::Mat& h);
	static void AddRectangleOrMerge(std::vector<RectProb>& rects, RectProb& rect);
	static std::vector<RectProb> FindRectanglesFromMatchedPoints(std::tuple<std::vector<cv::Point2f>, std::vector<cv::Point2f>>& points, cv::Size objSize, cv::Size srcSize, size_t objKeypointCount, std::list<cv::Mat>* hList = nullptr);

	static std::vector<RectProb> FindObject(const cv::Mat& srcImg, const cv::Mat& objImg, enum Detector detector = Detector::ORB_BEBLID, cv::DescriptorMatcher::MatcherType matcher = cv::DescriptorMatcher::MatcherType::BRUTEFORCE_HAMMING, cv::Mat* debugImage = nullptr);

	ObjDetect(enum Detector detector = Detector::ORB_BEBLID, cv::DescriptorMatcher::MatcherType matcher = cv::DescriptorMatcher::MatcherType::BRUTEFORCE_HAMMING, const std::string& channel = "R");
	int AddObject(const cv::Mat& objImg);
	void UpdateBaseImage(cv::Mat&& srcImg);
	std::vector < std::vector<RectProb> > FindObjects(const std::vector<bool>* objectMask = nullptr);

	void SaveBaseImage(const std::string& filename);

private:
	enum Detector detector;
	cv::DescriptorMatcher::MatcherType matcher;
	std::string channel;
	cv::Mat srcImg;
	std::list<ImageFeatures> objects;

	static void PreprocessImage(const cv::Mat& inImage, cv::Mat& outImage, const std::string& channel = "R");

	struct DetectorHolder {
		cv::Ptr<cv::FeatureDetector> detectAlgo;
		cv::Ptr<cv::FeatureDetector> computeAlgo;
	};
	inline static std::vector<DetectorHolder> detectors;
	static DetectorHolder GetDetector(enum Detector id);

	inline static cv::Mat srcTemp;

};