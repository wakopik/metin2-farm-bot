#pragma once
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include "color_detector.h"  // Detection struct
#include "config.h"

// Gorsel (template) tabanli tespit - templates/ klasorundeki PNG sablonlarla
// Canny kenar haritasi uzerinde eslesme yapar (renk/parlaklik degil sekil
// benzerligine bakar - toprak yol/golge gibi seyleri elemek icin).
//
// KRITIK PERFORMANS: Detect() bir "searchRegion" alir ve SADECE o
// dikdortgen icinde arar - tum kareyi degil. Cagiran taraf (bot.cpp) bunu
// farm alaniyla sinirlar, boylece arama alani kucuk kalir ve oyunu
// yavaslatmaz.
class TemplateDetector {
public:
    explicit TemplateDetector(const Config& config);

    std::vector<Detection> Detect(const cv::Mat& frameBgr, const cv::Rect& searchRegion) const;

    bool HasTemplates() const { return !m_templates.empty(); }
    size_t TemplateCount() const { return m_templates.size(); }

private:
    struct Template {
        std::string name;
        cv::Mat gray;  // grayscale sablon
        cv::Mat mask;  // alfa kanalindan gelen maske (varsa), yoksa bos
    };

    static std::vector<Detection> NonMaxSuppression(std::vector<Detection> dets, double overlapThresh);

    std::vector<Template> m_templates;
    const Config& m_config;
};
