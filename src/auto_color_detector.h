#pragma once
#include <opencv2/opencv.hpp>
#include <optional>
#include "color_detector.h"  // Detection struct
#include "config.h"

// Renk profilini MANUEL secim yerine, templates/ klasorundeki gorsellerin
// KENDI PIKSEL RENKLERINDEN otomatik cikarir (alfa varsa sadece opak
// pikseller, yoksa merkez %60'lik bolge kullanilir). Ayrica sablonlarin
// ortalama boyutunu da ogrenip, o araliga UYMAYAN lekeleri (ornegin bir
// dag/tepe gibi cok daha buyuk ayni renkteki seyler) otomatik eler.
class AutoColorDetector {
public:
    explicit AutoColorDetector(const Config& config);

    std::vector<Detection> Detect(const cv::Mat& frameBgr, const cv::Rect& searchRegion) const;

    // YEREL RENK IYILESTIRMESI: verilen KUCUK/hedefli pencere icinde
    // (ör. bir isim etiketinin hemen alti) ogrenilen HSV renk araligina
    // uyan EN BUYUK lekeyi bulup MERKEZINI dondurur - bkz.
    // name_label_detector.h/bot.cpp DetectInFarmZone. IsReady()/global
    // "gurultulu renk" korumasindan (bkz. .cpp'deki devre-disi-birakma
    // yorumu) BAGIMSIZ calisir - burada arama alani zaten o kadar kucuk/
    // hedefli ki genis/gurultulu bir HSV araligi bile yanlis pozitife
    // yol acmaz (ekranin geri kalani hic taranmiyor).
    // excludeRect: kare koordinatlarinda, maskeden CIKARILACAK bolge -
    // pencere artik isim yazisinin ETRAFINI (ustu dahil) taradigi icin
    // (bkz. bot.cpp DetectInFarmZone) yazinin KENDISI de pencerenin icinde
    // kaliyor; yazi pikselleri cimenden cok farkli oldugundan, disarida
    // birakilmazsa "en buyuk leke" olarak YAZININ kendisi secilebilirdi.
    // outMask/outCrop SADECE TANI (debug_preview.exe) icindir - nullptr
    // birakilirsa (bot.cpp'nin yaptigi gibi) hicbir ek is yapilmaz.
    std::optional<cv::Point2d> FindColorCenterIn(const cv::Mat& frameBgr, cv::Rect window,
                                                  cv::Rect excludeRect = cv::Rect(),
                                                  cv::Mat* outMask = nullptr, cv::Mat* outCrop = nullptr) const;

    bool IsReady() const { return m_ready; }

private:
    cv::Scalar m_lower{0, 0, 0};
    cv::Scalar m_upper{179, 255, 255};
    int m_minW = 10, m_maxW = 500, m_minH = 10, m_maxH = 500;
    bool m_ready = false;
    const Config& m_config;
};
