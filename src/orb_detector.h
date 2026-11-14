#pragma once
#include <opencv2/opencv.hpp>
#include <opencv2/features2d.hpp>
#include <string>
#include <vector>
#include "color_detector.h"  // Detection struct
#include "config.h"

// ORB ozellik eslestirme (feature matching) + RANSAC/homografi ile
// GEOMETRIK DOGRULAMA. Her sablonun AYIRT EDICI NOKTALARI (kose/desen)
// bir kere cikarilir, her karede sahnedeki noktalarla eslestirilir.
// Sadece "kac nokta eslesti" demekle YETINMIYORUZ - eslesen noktalarin
// GERCEKTEN sablonun kendi geometrisiyle tutarli (tek bir duzlemsel
// donusumle aciklanabilir) olup olmadigini RANSAC ile dogruluyoruz.
// Bu, birkac rastgele/aykiri eslesmenin kutuyu devasa/yanlis boyuta
// sismesini engelliyor - kutu, sablonun kendi kose noktalarinin sahneye
// projeksiyonundan cikiyor, "en uzak eslesen noktalar" degil.
class OrbDetector {
public:
    explicit OrbDetector(const Config& config);

    std::vector<Detection> Detect(const cv::Mat& frameBgr, const cv::Rect& searchRegion) const;

    bool HasTemplates() const { return !m_templates.empty(); }
    size_t TemplateCount() const { return m_templates.size(); }

private:
    struct Template {
        std::string name;
        std::string category;  // hangi templates_dirs klasorunden geldigi
        std::vector<cv::KeyPoint> keypoints;
        cv::Mat descriptors;
        cv::Size size;
    };

    static std::vector<Detection> NonMaxSuppression(std::vector<Detection> dets, double overlapThresh);

    std::vector<Template> m_templates;
    cv::Ptr<cv::ORB> m_orb;
    const Config& m_config;
};
