#pragma once

class FixedResolutionConfig {
	int width, height;
	std::vector<cv::Mat> screenMasksPerState;
public:
	FixedResolutionConfig(const Config& config, int width, int height);
	int GetWidth() const { return width; };
	int GetHeight() const { return height; };
	cv::Size GetSize() const { return cv::Size(width, height); }
};

class WorkerInfo {
public:
	std::vector<std::vector<RectProb>> detections;
	cv::Rect clickRect;
	uint32_t lastClickTime;
	int lastClickX, lastClickY;
	void SetClickTime(uint32_t time, int x, int y, const cv::Rect* r)
	{
		lastClickTime = time;
		lastClickX = x;
		lastClickY = y;
		if (r) { clickRect = *r; }
	}
	void SetDetections(const std::vector<std::vector<RectProb>>& d)
	{
		this->detections.resize(d.size());
		for (int i = 0; i < d.size(); i++) {
			this->detections[i].resize(d[i].size());
			for (int j = 0; j < d[i].size(); j++) {
				this->detections[i][j] = d[i][j];
			}
		}
	}
};
