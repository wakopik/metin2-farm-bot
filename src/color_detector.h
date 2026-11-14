#pragma once
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include "config.h"

// Tek bir tespiti temsil eder - Python tarafindaki detector.py -> Detection
// sinifinin karsiligi. "name" alani, hangi renk PROFILINE (metin turune)
// ait oldugunu tasir (ornegin "Savas Metni").
struct Detection {
    int x = 0, y = 0, w = 0, h = 0;
    double score = 0.0;
    std::string name;
    // Hangi metin turu klasorunden geldigi (ornegin "dovus"/"savas") - AYNI
    // ANDA birden fazla tur secildiginde (bkz. config.h templates_dirs
    // yorumu), OrbDetector::NonMaxSuppression bu alani kullanarak FARKLI
    // turlerin birbirini bastirmasini (yanlislikla daha guclu eslesen bir
    // turun, o konumdaki BASKA turden gecerli bir tespidi silmesini) engeller.
    std::string category;

    // Bazi dedektorler (ornegin OrbDetector), dikdortgenin GEOMETRIK
    // MERKEZI yerine, nesnenin uzerinde GERCEKTEN eslesen noktalarin
    // ortalamasini daha dogru bir tiklama noktasi olarak verebilir -
    // dikdortgen nesnenin sekline tam oturmuyorsa (yuvarlak/duzensiz tas
    // gibi), bu nokta arka plana degil nesnenin uzerine duser.
    bool hasPrecisePoint = false;
    cv::Point2d precisePoint{};

    cv::Point2d Center() const {
        if (hasPrecisePoint) return precisePoint;
        return cv::Point2d(x + w / 2.0, y + h / 2.0);
    }
};

// Renk (HSV) tabanli tespit - Python tarafindaki src/color_detector.py'nin
// C++ karsiligi. Birden fazla isimli profil destekler (config.json ->
// color_profiles) - her profil ayri taranir, bulunan her tespit hangi
// profilden geldigini Detection::name'de tasir.
class ColorDetector {
public:
    explicit ColorDetector(const Config& config);

    std::vector<Detection> Detect(const cv::Mat& frameBgr) const;

private:
    struct Profile {
        std::string name;
        cv::Scalar lower;
        cv::Scalar upper;
    };

    std::vector<Profile> m_profiles;
    const Config& m_config;
};
