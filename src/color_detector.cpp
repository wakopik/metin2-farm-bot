#include "color_detector.h"

ColorDetector::ColorDetector(const Config& config) : m_config(config) {
    for (const auto& p : config.color_profiles) {
        Profile prof;
        prof.name = p.name;
        prof.lower = cv::Scalar(p.hsv_lower[0], p.hsv_lower[1], p.hsv_lower[2]);
        prof.upper = cv::Scalar(p.hsv_upper[0], p.hsv_upper[1], p.hsv_upper[2]);
        m_profiles.push_back(prof);
    }
}

std::vector<Detection> ColorDetector::Detect(const cv::Mat& frameBgr) const {
    std::vector<Detection> detections;
    if (frameBgr.empty() || m_profiles.empty()) return detections;

    cv::Mat hsv;
    cv::cvtColor(frameBgr, hsv, cv::COLOR_BGR2HSV);
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));

    for (const auto& profile : m_profiles) {
        cv::Mat mask;
        cv::inRange(hsv, profile.lower, profile.upper, mask);
        cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
        cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        for (const auto& c : contours) {
            double area = cv::contourArea(c);
            if (area < m_config.color_min_area || area > m_config.color_max_area) continue;

            cv::Rect box = cv::boundingRect(c);
            if (box.width > m_config.color_max_width || box.height > m_config.color_max_height) continue;

            double fillRatio = area / std::max(1.0, (double)(box.width * box.height));
            if (fillRatio < m_config.color_min_score) continue;

            Detection d;
            d.x = box.x;
            d.y = box.y;
            d.w = box.width;
            d.h = box.height;
            d.score = fillRatio;
            d.name = profile.name;
            detections.push_back(d);
        }
    }

    return detections;
}
