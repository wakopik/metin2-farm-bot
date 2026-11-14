#include "template_detector.h"
#include <filesystem>
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;

TemplateDetector::TemplateDetector(const Config& config) : m_config(config) {
    std::error_code ec;
    for (const auto& dir : config.templates_dirs) {
        if (!fs::exists(dir, ec)) {
            std::cerr << "[template_detector] '" << dir
                       << "' klasoru bulunamadi - template_picker.exe ile sablon ekleyin.\n";
            continue;
        }

        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext != ".png") continue;
            if (entry.path().filename().string() == "_panel.png") continue;

            cv::Mat img = cv::imread(entry.path().string(), cv::IMREAD_UNCHANGED);
            if (img.empty()) continue;

            Template t;
            t.name = entry.path().stem().string();

            cv::Mat bgr;
            if (img.channels() == 4) {
                std::vector<cv::Mat> ch(4);
                cv::split(img, ch);
                cv::merge(std::vector<cv::Mat>{ch[0], ch[1], ch[2]}, bgr);
                t.mask = ch[3];
                cv::erode(t.mask, t.mask, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
            } else if (img.channels() == 3) {
                bgr = img;
            } else {
                cv::cvtColor(img, bgr, cv::COLOR_GRAY2BGR);
            }

            cv::cvtColor(bgr, t.gray, cv::COLOR_BGR2GRAY);
            m_templates.push_back(std::move(t));
        }
    }

    std::cout << "[template_detector] " << m_templates.size() << " sablon yuklendi.\n";
}

std::vector<Detection> TemplateDetector::Detect(const cv::Mat& frameBgr, const cv::Rect& searchRegionIn) const {
    std::vector<Detection> all;
    if (frameBgr.empty() || m_templates.empty()) return all;

    cv::Rect frameBounds(0, 0, frameBgr.cols, frameBgr.rows);
    cv::Rect searchRegion = searchRegionIn & frameBounds;
    if (searchRegion.width <= 4 || searchRegion.height <= 4) return all;

    cv::Mat region = frameBgr(searchRegion);
    cv::Mat gray;
    cv::cvtColor(region, gray, cv::COLOR_BGR2GRAY);
    cv::Mat edges;
    cv::Canny(gray, edges, m_config.canny_low, m_config.canny_high);

    std::vector<double> scales = {1.0};
    if (m_config.template_multi_scale) {
        scales.clear();
        int steps = std::max(2, m_config.template_scale_steps);
        for (int i = 0; i < steps; ++i) {
            double t = (double)i / (steps - 1);
            scales.push_back(m_config.template_scale_min + t * (m_config.template_scale_max - m_config.template_scale_min));
        }
    }

    for (const auto& tmpl : m_templates) {
        for (double scale : scales) {
            int sw = (int)std::lround(tmpl.gray.cols * scale);
            int sh = (int)std::lround(tmpl.gray.rows * scale);
            if (sw < 4 || sh < 4 || sw >= edges.cols || sh >= edges.rows) continue;

            cv::Mat scaledGray;
            cv::resize(tmpl.gray, scaledGray, cv::Size(sw, sh), 0, 0, cv::INTER_AREA);
            cv::Mat templEdges;
            cv::Canny(scaledGray, templEdges, m_config.canny_low, m_config.canny_high);

            cv::Mat result;
            if (!tmpl.mask.empty()) {
                cv::Mat scaledMask;
                cv::resize(tmpl.mask, scaledMask, cv::Size(sw, sh), 0, 0, cv::INTER_NEAREST);
                cv::matchTemplate(edges, templEdges, result, cv::TM_CCORR_NORMED, scaledMask);
            } else {
                cv::matchTemplate(edges, templEdges, result, cv::TM_CCORR_NORMED);
            }

            // Duz/kenarsiz bolgelerde normalizasyon paydasi sifira yaklasip
            // inf/NaN uretebiliyor - guvenli sekilde temizle.
            cv::patchNaNs(result, 0.0);
            cv::threshold(result, result, 1.0, 0.0, cv::THRESH_TOZERO_INV);

            for (int y = 0; y < result.rows; ++y) {
                const float* rowPtr = result.ptr<float>(y);
                for (int x = 0; x < result.cols; ++x) {
                    double score = rowPtr[x];
                    if (score < m_config.template_match_threshold) continue;

                    Detection d;
                    d.x = searchRegion.x + x;
                    d.y = searchRegion.y + y;
                    d.w = sw;
                    d.h = sh;
                    d.score = score;
                    d.name = tmpl.name;
                    all.push_back(d);
                }
            }
        }
    }

    return NonMaxSuppression(std::move(all), 0.3);
}

std::vector<Detection> TemplateDetector::NonMaxSuppression(std::vector<Detection> dets, double overlapThresh) {
    if (dets.size() <= 1) return dets;
    std::sort(dets.begin(), dets.end(), [](const Detection& a, const Detection& b) { return a.score > b.score; });

    std::vector<Detection> kept;
    std::vector<bool> suppressed(dets.size(), false);

    for (size_t i = 0; i < dets.size(); ++i) {
        if (suppressed[i]) continue;
        kept.push_back(dets[i]);
        cv::Rect a(dets[i].x, dets[i].y, dets[i].w, dets[i].h);
        for (size_t j = i + 1; j < dets.size(); ++j) {
            if (suppressed[j]) continue;
            cv::Rect b(dets[j].x, dets[j].y, dets[j].w, dets[j].h);
            double interArea = (a & b).area();
            double unionArea = a.area() + b.area() - interArea;
            if (unionArea > 0 && (interArea / unionArea) > overlapThresh) suppressed[j] = true;
        }
    }
    return kept;
}
