#pragma once
#include <opencv2/opencv.hpp>
#include <string>
#include "config.h"

// Metin secilince ekranin UST-ORTASINDA beliren HEDEF HP CUBUGUNU arar
// (config.hp_bar_dir/_hpbar.png referansiyla, template_picker.exe'de 'h' ile
// alinir). YONTEM: PHANTOM-Metin2-AI-Bot (kullanicinin referans aldigi
// acik-kaynak bot) ile BIREBIR AYNI, kaynak kodu incelenerek birebir
// uygulandi - dar/SABIT bir bolgede (ekranin ust %2-%7'si, orta %30-%70'i,
// bkz. Read()) DOGRUDAN (sliding-window ARAMA YOK) gri-tonlama VE kenar
// (Canny) korelasyonunun ikisi birden denenir, 4 olcekte (1.0/0.75/0.5/
// 0.25) en iyi skor alinir. Genis bir alanda "arama" yapmamak (PHANTOM'un
// da yapmadigi sey) arazi/golge gibi dogal dokularla yanlislikla eslesme
// riskini ortadan kaldirir - cunku bolge zaten SADECE panelin gercekten
// cikacagi dar yeri kapsar.
class StatusPanelDetector {
public:
    struct Reading {
        bool hasTemplate = false;  // _hpbar.png yuklendi mi (yoksa cift dogrulama devre disi)
        bool visible = false;      // HP cubugu ekranda bulundu mu
        cv::Rect matchRect;        // debug_preview.exe icin - arandigi bolge
        double bestScore = 0.0;    // TANI: en iyi skor (esigi gecmese bile)
    };

    explicit StatusPanelDetector(const Config& config);

    Reading Read(const cv::Mat& frameBgr) const;

    bool HasTemplate() const { return !m_templateGray.empty(); }

private:
    struct ScaledVariant {
        cv::Mat gray;
        cv::Mat edges;
    };
    const ScaledVariant& GetScaledTemplate(double scale) const;

    // Genis bant aramasi (YAVAS) - sadece konum bilinmiyorken calisir.
    Reading SearchBand(const cv::Mat& frameBgr) const;

    const Config& m_config;
    cv::Mat m_templateGray;
    cv::Mat m_templateEdges;
    mutable std::vector<std::pair<double, ScaledVariant>> m_scaleCache;

    // ============ OGRENILEN KONUM (PERFORMANS - KRITIK) ============
    // HP cubugu HER ZAMAN ayni yerde belirir (sabit boyutlu, capali bir
    // arayuz ogesi). Genis bandi HER KAREDE taramanin maliyeti OLCULDU:
    // 37-57 ms. Bu fonksiyon bot dongusunde HER KAREDE calistigi icin botu
    // felcediyordu ("inanilmaz yavas"). Cozum: bandi yalnizca konum
    // BILINMIYORKEN tara; bulununca konumu ve olcegi HATIRLA, sonraki tum
    // karelerde SADECE o kucuk bolgeye bak - eski kod kadar ucuz. Panel
    // ekranda yokken de ayni kucuk bolgeye bakilir, yani "yok" cevabi da
    // ucuza gelir.
    mutable cv::Rect m_knownRect;       // bos = konum henuz bilinmiyor
    mutable double m_knownScale = 1.0;
    mutable cv::Size m_knownFrameSize;  // hangi kare boyutunda ogrenildi
    mutable int m_missStreak = 0;       // ust uste kac karedir bulunamadi
};
