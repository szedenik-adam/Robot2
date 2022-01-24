#include "ObjDetect.h"
#include "../Benchmark.h"
#include <opencv2/xfeatures2d.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp> // imwrite

cv::Mat ObjDetect::PreprocessImage(const cv::Mat& image, const std::string& channel)
{
    cv::Mat result;
    ObjDetect::PreprocessImage(image, result, channel);
    return result;
}

void ObjDetect::PreprocessImageInplace(cv::Mat& image, const std::string& channel)
{
    ObjDetect::PreprocessImage(image, image, channel);
}

void ObjDetect::PreprocessImage(const cv::Mat& inImage, cv::Mat& outImage, const std::string& channel)
{
    BenchmarkT<"PreprocessImage"> _b;

    int channelNum = -1;
    if (channel == "R") channelNum = 2;
    else if (channel == "G") channelNum = 1;
    else if (channel == "B") channelNum = 0;

    if(channelNum!=-1) {
        cv::extractChannel(inImage, outImage, channelNum);
        return;
    }
    if (channel == "Grayscale" || channel == "grayscale") {
        cv::cvtColor(inImage, outImage, cv::COLOR_BGR2GRAY);
        return;
    }

    if (channel == "H")  channelNum = 0;
    else if (channel == "S") channelNum = 1;
    else if (channel == "V") channelNum = 2;
    if (channelNum != -1) {
        cv::Mat hsvImg;
        cv::cvtColor(inImage, hsvImg, cv::COLOR_BGR2HSV);
        cv::extractChannel(hsvImg, outImage, channelNum);
        return;
    }
}

ObjDetect::DetectorHolder ObjDetect::GetDetector(enum Detector id)
{
    BenchmarkT<"GetDetector"> _b;
    if (ObjDetect::detectors.size() > (int)id) {
        DetectorHolder dh = ObjDetect::detectors[(int)id];
        if (!dh.detectAlgo.empty()) return dh;
    }
    DetectorHolder result;
    switch (id)
    {
    case Detector::AKAZE_DESCRIPTOR_MLDB:
        result.detectAlgo = cv::AKAZE::create(cv::AKAZE::DescriptorType::DESCRIPTOR_MLDB, 0, 3, 0.001f, 4, 4, cv::KAZE::DiffusivityType::DIFF_PM_G2);
        break;
    case Detector::AKAZE_DESCRIPTOR_KAZE_UPRIGHT:
        result.detectAlgo = cv::AKAZE::create(cv::AKAZE::DESCRIPTOR_KAZE_UPRIGHT);
        break;
    case Detector::ORB_BEBLID:
        result.computeAlgo = cv::xfeatures2d::BEBLID::create(1.0f, cv::xfeatures2d::BEBLID::BeblidSize::SIZE_512_BITS);
        // not breaking.
    case Detector::ORB:
    {
        const int edgeThreshold = 8;
        const int patchSize = 24; //std::min(img2.cols, img2.rows) - edgeThreshold * 2 - 5;
        //result.detectAlgo = cv::ORB::create(100000, 1.2f, 8, edgeThreshold, 0, 2, cv::ORB::ScoreType::HARRIS_SCORE, patchSize, 20);
        result.detectAlgo = cv::ORB::create(100000, 1.1f, 16, edgeThreshold, 0, 2, cv::ORB::ScoreType::HARRIS_SCORE, patchSize, 20);
    }
        break;
    case Detector::BRISK_BEBLID:
        result.computeAlgo = cv::xfeatures2d::BEBLID::create(5.f, cv::xfeatures2d::BEBLID::BeblidSize::SIZE_512_BITS);
        // not breaking.
    case Detector::BRISK:
        result.detectAlgo = cv::BRISK::create(60, 6, 1.0f);
        break;
    case Detector::SURF_BEBLID:
        result.computeAlgo = cv::xfeatures2d::BEBLID::create(6.25f, cv::xfeatures2d::BEBLID::BeblidSize::SIZE_512_BITS);
        // not breaking.
    case Detector::SURF:
        result.detectAlgo = cv::xfeatures2d::SURF::create(
            100.0,		// threshold (default = 100.0)
            4,			// number of octaves (default=4)
            3,			// number of octave layers within each octave (default=3)
            false,		// true=use 128 element descriptors, false=use 64 element (def=false)
            false);		// true=don't compute orientation, false=compute orientation (def=false)
        break;
    case Detector::SIFT_BEBLID:
        result.computeAlgo = cv::xfeatures2d::BEBLID::create(6.75f, cv::xfeatures2d::BEBLID::BeblidSize::SIZE_512_BITS);
        // not breaking.
    case Detector::SIFT:
        result.detectAlgo = cv::SIFT::create();
        break;
    default:
        printf("ObjDetect::GetDetector called with invalid Detector!\n");
        return result;
    }
    ObjDetect::detectors.resize((int)id + 1);
    ObjDetect::detectors[(int)id] = result;
    return result;
}

std::tuple<std::vector<cv::KeyPoint>, cv::Mat> ObjDetect::FindKeypoints(const cv::Mat& image, enum Detector detector)
{
    /*cv::Mat bigMask = cv::Mat();
    if (image.rows > 1000 && image.cols > 1000) {
        bigMask = cv::Mat::zeros(image.rows, image.cols, CV_8U);
        cv::rectangle(bigMask, cv::Rect(0, 0, 1000, 1000), cv::Scalar_<uint8_t>(255), cv::LineTypes::FILLED);
    }*/

    BenchmarkT<"FindKeypoints"> _b;
    DetectorHolder holder = ObjDetect::GetDetector(detector);
    std::vector<cv::KeyPoint> keyImg;
    holder.detectAlgo->detect(image, keyImg, cv::Mat());//bigMask);//);

    /*auto orb = std::dynamic_pointer_cast<cv::ORB>(holder.detectAlgo);
    if (orb)
    {
        orb->setPatchSize()
    }*/

    cv::Mat descImg;
    const cv::Ptr<cv::FeatureDetector>& computeAlgo = holder.computeAlgo.empty() ? holder.detectAlgo : holder.computeAlgo;
    computeAlgo->compute(image, keyImg, descImg);

    return std::tuple<std::vector<cv::KeyPoint>, cv::Mat>(keyImg, descImg);
}

std::vector<cv::DMatch> ObjDetect::MatchDescriptors(const cv::Mat& descImg1, const cv::Mat& descImg2, cv::DescriptorMatcher::MatcherType matcherId)
{
    BenchmarkT<"MatchDescriptors"> _b;
    std::vector<cv::DMatch> result, matches;

    if (descImg1.rows == 0 || descImg2.rows == 0)
    {
        printf("ObjDetect::MatchDescriptors: Descriptor image has no rows!\n");
        return result;
    }
    cv::Ptr<cv::DescriptorMatcher> matcher = cv::DescriptorMatcher::create(matcherId);
#if 1 == 0
    matcher->match(descImg1, descImg2, matches, cv::Mat());
    cv::Mat index;
    int nbMatch = int(matches.size());
    if (nbMatch == 0)
    {
        printf("No matches found!\n");
        return result;
    }
    const int bestMatchLimit = std::min<size_t>(std::max<size_t>(100, nbMatch / 5), 1000);
    if (nbMatch <= bestMatchLimit) { return matches; }

    cv::Mat tab(nbMatch, 1, CV_32F);
    for (int i = 0; i < nbMatch; i++)
    {
        tab.at<float>(i, 0) = matches[i].distance;
    }
    sortIdx(tab, index, cv::SORT_EVERY_COLUMN + cv::SORT_ASCENDING);
    //std::vector<Point2f> points1, points2;
    for (int i = 0; i < bestMatchLimit; i++)
    {
        cv::DMatch& match = matches[index.at<int>(i, 0)];
        result.push_back(match);
        //points1.push_back(keyImg1[match.queryIdx].pt);
        //points2.push_back(keyImg2[match.trainIdx].pt);
    }
    return result;
#else 
    //cv::FlannBasedMatcher matcher2(new cv::flann::LshIndexParams(20, 10, 2)); // nem jó és lassú
    std::vector<std::vector<cv::DMatch>> knnMatches;
    matcher->knnMatch(descImg1, descImg2, knnMatches, 2);
    // Apply ratio test: best match should be much better than 2nd best match. // source: http://cs-courses.mines.edu/csci508/labs/05/doeval.cpp
    // Form a list of matches that survive this test.
    //   matches[i].trainIdx is the index of the ith match, in keypoints1
    //	 matches[i].queryIdx is the index of the ith match, in keypoints2
    const float minRatio = 0.7f;
    for (size_t i = 0; i < knnMatches.size(); i++) {
        if (knnMatches[i].size() < 2) continue;
        const cv::DMatch& bestMatch = knnMatches[i][0];
        const cv::DMatch& betterMatch = knnMatches[i][1];

        // To avoid NaN's when match has zero distance use inverse ratio. 
        float inverseRatio = bestMatch.distance / betterMatch.distance;

        // Test for distinctiveness: pass only matches where the inverse
        // ratio of the distance between nearest matches is < than minimum.
        if (inverseRatio < minRatio)
            result.push_back(bestMatch);
    }
    return result;
#endif
}

std::tuple<std::vector<cv::Point2f>, std::vector<cv::Point2f>> ObjDetect::GetMatchedPoints(const std::vector<cv::DMatch>& matches, const std::vector<cv::KeyPoint>& keyImg1, const std::vector<cv::KeyPoint>& keyImg2)
{
    BenchmarkT<"GetMatchedPoints"> _b;
    std::vector<cv::Point2f> points1, points2;
    for (const cv::DMatch& match : matches)
    {
        points1.push_back(keyImg1[match.queryIdx].pt);
        points2.push_back(keyImg2[match.trainIdx].pt);
    }
    return std::tuple<std::vector<cv::Point2f>, std::vector<cv::Point2f>>(points1, points2);
}

std::tuple<cv::Mat, cv::Mat> ObjDetect::GetTransformationMatrix(const std::tuple<std::vector<cv::Point2f>, std::vector<cv::Point2f>>& points)
{
    BenchmarkT<"GetTransformationMatrix"> _b;
    cv::Mat inliers;
    cv::Mat h = cv::estimateAffine2D(std::get<1>(points), std::get<0>(points), inliers, cv::RANSAC, 3, 1000, 0.95);
    cv::Mat row = cv::Mat::zeros(1, 3, CV_64F);
    row.at<double>(0, 2) = 1;
    h.push_back(row);

    /*float tx = h.at<double>(0, 2);
    float ty = h.at<double>(1, 2);
    float sx = copysign(sqrt(pow(h.at<double>(0, 0), 2) + pow(h.at<double>(0, 1), 2)), 1);
    float sy = copysign(sqrt(pow(h.at<double>(1, 0), 2) + pow(h.at<double>(1, 1), 2)), h.at<double>(0, 0));
    h.at<double>(0, 1) = 0; h.at<double>(1, 0) = 0; // Remove rotation from matrix.
    h.at<double>(0, 0) = sx; h.at<double>(1, 1) = sy;*/
    return std::tuple<cv::Mat, cv::Mat>(h, inliers);
}

bool ObjDetect::ValidateTransformationMatrix(const cv::Mat& h, cv::Size srcSize)
{
    BenchmarkT<"ValidateTransformationMatrix"> _b;
    if (h.cols < 3 || h.rows < 2) {
        printf("Transformation Matrix is incomplete.\n");
        return false;
    }
    float translateX = h.at<double>(0, 2);
    float translateY = h.at<double>(1, 2);
    float xx_scale = h.at<double>(0, 0);
    float yy_scale = h.at<double>(1, 1);
    if (xx_scale <= 0 || yy_scale <= 0) { return false; } // Non-positive scaling on X or Y axis.
    float scaleX = sqrt(pow(xx_scale, 2) + pow(h.at<double>(0, 1), 2));
    float scaleY = sqrt(pow(h.at<double>(1, 0), 2) + pow(yy_scale, 2));

    const float degree = 180 / CV_PI;
    const float radian = CV_PI / 180;
    float sign = atan(-1*h.at<double>(1, 0) / h.at<double>(0, 0));
    float rad = acos(h.at<double>(0, 0) / scaleX);
    float deg = rad * degree;
    float rotation;

    if (deg > 90 && sign > 0)
    {
        rotation = (360 - deg) * radian;
    }
    else if (deg < 90 && sign < 0)
    {
        rotation = (360 - deg) * radian;
    }
    else
    {
        rotation = rad;
    }
    float rotationInDegree = rotation * degree;

    //printf("Scaling: x: %f y: %f \n", scaleX, scaleY);
    if (abs(scaleY/scaleX - 1) > 0.1) return false; // X-Y scaling distortion >10%.
    if (scaleX > 10 || scaleY > 10 || scaleX < 0.1 || scaleY < 0.1) return false; // Extreme scaling values.
    if (translateX + 0.25f * scaleX < 0) return false; // Clips left more than 25%.
    if (translateY + 0.25f * scaleY < 0) return false; // Clips top more than 25%.
    if (translateX + 0.75f * scaleX > srcSize.width) return false; // Clips right more than 25%.
    if (translateY + 0.75f * scaleY > srcSize.height) return false; // Clips bottom more than 25%.
    if (rotationInDegree > 45 && rotationInDegree < 360 - 45) {
        printf("Too much rotation: %f, %f, %f\n", rad, deg, rotationInDegree);
        return false;
    }
    std::cout << " scaleX: " << scaleX << ", scaleY: " << scaleY<<", sign: "<<sign << " deg: "<<deg <<" rot: "<<rotation<< " rotDegree: " << rotationInDegree<<";\n";
    //std::cout << " H: " << h;
    return true;
}

int ObjDetect::RemoveMatchedPoints(std::tuple<std::vector<cv::Point2f>, std::vector<cv::Point2f>>& points, const cv::Mat& inliers)
{
    BenchmarkT<"RemoveMatchedPoints"> _b;
    std::vector<cv::Point2f>& points1 = std::get<0>(points);
    std::vector<cv::Point2f>& points2 = std::get<1>(points);

    int count = 0;
    auto r = std::remove_if(points1.begin(), points1.end(), [&count, &inliers](const cv::Point2f& p) {int result = inliers.at<char>(count, 0); count++; return result; });
    points1.erase(r, points1.end());

    count = 0;
    int inlier_count = 0;
    r = std::remove_if(points2.begin(), points2.end(), [&count, &inliers, &inlier_count](const cv::Point2f& p) {int result = inliers.at<char>(count, 0); count++; if (result) { inlier_count++; } return result; });
    points2.erase(r, points2.end());

    return inlier_count;
}

cv::Rect ObjDetect::GetSubImageRect(const cv::Mat& objImg, const cv::Mat& h)
{
    return ObjDetect::GetSubImageRect(cv::Size{ objImg.cols, objImg.rows}, h);
}
cv::Rect ObjDetect::GetSubImageRect(const cv::Size& objImgSize, const cv::Mat& h)
{
    std::vector<cv::Point2f> obj_corners(4);
    obj_corners[0] = cv::Point2f(0, 0);
    obj_corners[1] = cv::Point2f((float)objImgSize.width, 0);
    obj_corners[2] = cv::Point2f((float)objImgSize.width, (float)objImgSize.height);
    obj_corners[3] = cv::Point2f(0, (float)objImgSize.height);
    std::vector<cv::Point2f> scene_corners(4);
    perspectiveTransform(obj_corners, scene_corners, h);
    //return cv::Rect(scene_corners[0], scene_corners[2] - scene_corners[0]);
    float corners[4] = { (scene_corners[0].x + scene_corners[3].x) / 2, (scene_corners[0].y + scene_corners[1].y) / 2,  (scene_corners[1].x + scene_corners[2].x) / 2,  (scene_corners[2].y + scene_corners[3].y) / 2 };
    return cv::Rect(corners[0], corners[1], corners[2] - corners[0], corners[3] - corners[1]);
}

void ObjDetect::AddRectangleOrMerge(std::vector<RectProb>& rects, RectProb& rect)
{
    int firstmergedRect = -1;
    std::list<int> otherMergedRects;
    for (const RectProb& r : rects)
    {
        // The rectangles intersecting region is larger than the 75 % of the smaller rectangle's area.
        if ((r & rect).area() > std::min(r.area(), rect.area()) * 3 / 4)
        {
            if (firstmergedRect == -1) { firstmergedRect = &r - &*rects.begin(); }
            else {
                otherMergedRects.push_back(&r - &*rects.begin());
            }
        }
    }
    // No merge => add rectangle to the result rectangles.
    if (firstmergedRect == -1) { rects.push_back(rect); }
    else {
        // Update rect with the union of the first merged rectangle.
        rect |= rects[firstmergedRect]; 
        if (!otherMergedRects.empty())
        {
            int removeIfInd = 0;
            auto r = std::remove_if(rects.begin(), rects.end(), [&removeIfInd, &otherMergedRects, &rect](const RectProb& r) {
                if (otherMergedRects.empty()) return false;
                bool result = otherMergedRects.front() == removeIfInd;
                if (result) {
                    otherMergedRects.pop_front();
                    rect |= r; // Update rect with a merged rectangle.
                }
                removeIfInd++;
                return result;
                });
            rects.erase(r, rects.end()); // Remove the merged rectangles (except the first).
        }
        // Replace the first merged rectangle in the list with the new rectangle.
        rects[firstmergedRect] = rect; 
    }
}

std::vector<RectProb> ObjDetect::FindRectanglesFromMatchedPoints(std::tuple<std::vector<cv::Point2f>, std::vector<cv::Point2f>>& points, cv::Size objSize, cv::Size srcSize, size_t objKeypointCount, std::list<cv::Mat>* hList)
{
    //int invalid_rects = 8, rect_iter = 12;
    //int invalid_rects = 68, rect_iter = 72;
    int invalid_rects = 8, rect_iter = 12;
    std::vector<RectProb> rects;
    while (true)
    {
        if (std::get<0>(points).size() < 5) { break; }
        std::tuple<cv::Mat, cv::Mat> h_inl = GetTransformationMatrix(points);
        cv::Mat& h = std::get<0>(h_inl);
        cv::Mat& inliers = std::get<1>(h_inl);

        bool isValid = ValidateTransformationMatrix(h, srcSize);
        int inlier_count = RemoveMatchedPoints(points, inliers);
        if (rect_iter < 12) {
            if(isValid)
                putchar('+');
        }
        if (isValid) {
            RectProb rect(GetSubImageRect(objSize, h), inlier_count/(float)objKeypointCount);
            AddRectangleOrMerge(rects, rect);
            if (hList) {
                hList->push_back(h);
            }
        }
        else {
            if (!--invalid_rects) { break; }
        }
        if (!--rect_iter) { break; }
    }
    return rects;
}

void drawCorners(cv::Mat& result, const cv::Mat& objImg, const cv::Mat& h, const cv::Scalar& color = cv::Scalar(0, 255, 0), int thickness = 2)
{
    std::vector<cv::Point2f> obj_corners(4);
    obj_corners[0] = cv::Point2f(0, 0);
    obj_corners[1] = cv::Point2f((float)objImg.cols, 0);
    obj_corners[2] = cv::Point2f((float)objImg.cols, (float)objImg.rows);
    obj_corners[3] = cv::Point2f(0, (float)objImg.rows);
    std::vector<cv::Point2f> scene_corners(4);
    perspectiveTransform(obj_corners, scene_corners, h);
    //-- Draw lines between the corners (the mapped object in the scene).
    line(result, scene_corners[0], scene_corners[1], color, thickness);
    line(result, scene_corners[1], scene_corners[2], color, thickness);
    line(result, scene_corners[2], scene_corners[3], color, thickness);
    line(result, scene_corners[3], scene_corners[0], color, thickness);
    cv::circle(result, scene_corners[0], 5, color, -1);
}

std::vector<RectProb> ObjDetect::FindObject(const cv::Mat& srcImg, const cv::Mat& objImg, enum Detector detector, cv::DescriptorMatcher::MatcherType matcher, cv::Mat* debugImage)
{
    BenchmarkT<"FindObject"> _b;
    const cv::Mat& srci = (srcImg.channels() > 1) ? PreprocessImage(srcImg) : srcImg;
    const cv::Mat& obji = (objImg.channels() > 1) ? PreprocessImage(objImg) : objImg;

    std::tuple<std::vector<cv::KeyPoint>, cv::Mat> srcKeyT, objKeyT;
    { BenchmarkT<"FindKeypointsSrc"> _b2; srcKeyT = FindKeypoints(srci, detector); }
    { BenchmarkT<"FindKeypointsObj"> _b2; objKeyT = FindKeypoints(obji, detector); }

    const std::vector<cv::KeyPoint>& srcKey = std::get<0>(srcKeyT);
    const std::vector<cv::KeyPoint>& objKey = std::get<0>(objKeyT);
    const cv::Mat& srcDesc = std::get<1>(srcKeyT);
    const cv::Mat& objDesc = std::get<1>(objKeyT);

    std::vector<cv::DMatch> matches = MatchDescriptors(srcDesc, objDesc, matcher);
    std::tuple<std::vector<cv::Point2f>, std::vector<cv::Point2f>> points = GetMatchedPoints(matches, srcKey, objKey);
    if (matches.empty()) { return std::vector<RectProb>{}; }

    std::unique_ptr<std::list<cv::Mat>> hList = debugImage ? std::make_unique<std::list<cv::Mat>>() : nullptr;

    std::vector<RectProb> rects = FindRectanglesFromMatchedPoints(points, cv::Size{ objImg.cols, objImg.rows }, cv::Size{ srcImg.cols, srcImg.rows }, objKey.size(), hList.get());

    if (debugImage)
    {
        _b.Stop();
        drawMatches(srcImg, srcKey, objImg, objKey, matches, *debugImage);
        for (const cv::Mat& h : *hList)
        {
            drawCorners(*debugImage, objImg, h, cv::Scalar(0, 150, 255), 2);
        }
        for (const RectProb& rect : rects)
        {
            cv::rectangle(*debugImage, rect, cv::Scalar(0, 255, 0), 4);
        }
    }
    return rects;
}

ObjDetect::ImageFeatures::ImageFeatures(const cv::Mat& img, enum Detector detector)
    : size(img.cols, img.rows)
{
    std::tuple<std::vector<cv::KeyPoint>, cv::Mat> findKpRes = ObjDetect::FindKeypoints(img, detector);
    this->keypoints = std::get<0>(findKpRes);
    this->descriptors = std::get<1>(findKpRes);
}

ObjDetect::ObjDetect(enum Detector detector, cv::DescriptorMatcher::MatcherType matcher, const std::string& channel)
    :detector(detector), matcher(matcher), channel(channel)
{
}


int ObjDetect::AddObject(const cv::Mat& objImg)
{
    cv::Mat objImg1ch;
    const cv::Mat* objImgPtr;
    if (objImg.channels() == 1) { objImgPtr = &objImg; }
    else{
        ObjDetect::PreprocessImage(objImg, objImg1ch, this->channel);
        objImgPtr = &objImg1ch;
    } 

    this->objects.emplace_back(*objImgPtr, this->detector);
    return this->objects.size() - 1;
}

void ObjDetect::UpdateBaseImage(cv::Mat&& srcImg)
{
    this->srcImg = std::move(srcImg);
    //this->srcImg = this->srcImg(cv::Rect(0, 0, this->srcImg.cols, std::min(1000, this->srcImg.rows)));
    //printf("Updated Base Image: %d x %d \n", this->srcImg.cols, this->srcImg.rows);
}

std::vector<std::vector<RectProb>> ObjDetect::FindObjects(const std::vector<bool>* objectMask)
{
    if (this->srcImg.channels() > 1) {
        ObjDetect::PreprocessImageInplace(this->srcImg, this->channel);
    }

    std::tuple<std::vector<cv::KeyPoint>, cv::Mat> srcKeyT = FindKeypoints(this->srcImg, this->detector);
    const std::vector<cv::KeyPoint>& srcKey = std::get<0>(srcKeyT);
    const cv::Mat& srcDesc = std::get<1>(srcKeyT);

    int objInd = 0;
    std::vector<std::vector<RectProb>> result(this->objects.size());

    for (const ImageFeatures& object : this->objects)
    {
        if (objectMask && !(*objectMask)[objInd]) { objInd++; continue; } // Ignore masked object.

        std::vector<cv::DMatch> matches = MatchDescriptors(srcDesc, object.descriptors, this->matcher);
        std::tuple<std::vector<cv::Point2f>, std::vector<cv::Point2f>> points = GetMatchedPoints(matches, srcKey, object.keypoints);

        if (matches.empty()) { objInd++; continue; }

        std::vector<RectProb> objRects = FindRectanglesFromMatchedPoints(points, object.size, this->srcImg.size(), object.keypoints.size(), nullptr);
        result[objInd++] = objRects;
    }
    return result;
}

void ObjDetect::SaveBaseImage(const std::string& filename)
{
    cv::imwrite(filename, this->srcImg);
}
