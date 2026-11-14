#pragma once
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include "color_detector.h"  // Detection struct
#include "config.h"

// Metin tasinin USTUNDE/yaninda YUZEN isim etiketini (ornegin "Dovus Metni"
// yazisi) arayan EK/opsiyonel bir tespit yontemi. ORB'un aksine bu yazi
// kameraya HER ZAMAN duz/okunakli goruntulenir (billboard) - yani hangi
// aciدan bakilirsa bakilsin GORUNUMU DEGISMEZ, sadece MESAFEYE gore
// boyutu degisir. Bu yuzden TEK bir referans gorselle (template_picker.exe
// 'n' modu, <klasor>/_name.png) genis bir mesafe araliginda calisabilir -
// ORB'un onlarca/yuzlerce farkli acidan sablon gerektirmesinin aksine.
//
// Referans yoksa (HasTemplates()==false) bu yontem TAMAMEN sessiz kalir -
// ORB/AutoColor tespitini hicbir sekilde etkilemez, sadece EK bir aday
// kaynagi olarak ayni Detection havuzuna katilir (bkz. bot.cpp
// DetectInFarmZone).
class NameLabelDetector {
public:
    explicit NameLabelDetector(const Config& config);

    std::vector<Detection> Detect(const cv::Mat& frameBgr, const cv::Rect& searchRegion) const;

    bool HasTemplates() const { return !m_templates.empty(); }

private:
    struct Template {
        std::string category;  // hangi templates_dirs klasorunden geldigi
        cv::Mat gray;
        // KENAR HARITASI (Canny): renkten BAGIMSIZ ikinci bir sinyal -
        // yazinin koyu dis hatti HANGI zemin rengi arkasinda olursa olsun
        // GUCLU bir kenar birakir - bkz. Detect().
        cv::Mat edges;
        // ARKA PLAN MASKESI (YENI): template_picker.exe 'n' modu artik
        // yazinin etrafindaki zemini otomatik CIKARIP (bkz.
        // ExtractTextForeground) _name.png'yi SEFFAF ARKA PLANLI (RGBA)
        // kaydediyor - bu alan, alfa kanalindan turetilen "SADECE yazi
        // pikselleri" maskesidir. Doluysa Detect() maskeli (arka plandan
        // TAMAMEN etkilenmeyen) bir UCUNCU eslesme gecisi de yapar - bkz.
        // Detect(). ESKI (arka plansiz, duz gri) sablonlarla GERIYE DONUK
        // UYUMLU: mask bossa bu gecis atlanir, sadece gray/edges kullanilir.
        cv::Mat mask;
    };
    std::vector<Template> m_templates;
    const Config& m_config;
};
