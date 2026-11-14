#include "status_panel_detector.h"
#include "diag_log.h"
#include <cmath>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace {
// PHANTOM-Metin2-AI-Bot (src/phantom/app/main.py) ile BIREBIR AYNI olcekler.
// Esik degeri (orada HP_TEMPLATE_MIN_SCORE=0.70) ise config.json ->
// status_panel_name_match_threshold'dan (varsayilani AYNI sekilde 0.70'e
// cekildi) okunuyor - bot.cpp'deki near-miss hesaplarinin AYNI kaynaga
// gore calismasi icin TEK bir yerden yonetiliyor.
constexpr double kScales[] = {1.0, 0.75, 0.50, 0.25};
}  // namespace

StatusPanelDetector::StatusPanelDetector(const Config& config) : m_config(config) {
    std::string hpBarPath = (fs::path(config.hp_bar_dir) / "_hpbar.png").string();
    cv::Mat gray = cv::imread(hpBarPath, cv::IMREAD_GRAYSCALE);
    if (!gray.empty()) {
        m_templateGray = gray;
        cv::Canny(gray, m_templateEdges, 50, 150);
        std::cout << "[status_panel] '" << hpBarPath << "' HP cubugu referansi yuklendi (" << gray.cols << "x"
                   << gray.rows << "px) - cift dogrulama AKTIF (PHANTOM yontemi).\n";
        LogDiag("PNL tmpl-load dir=" + config.hp_bar_dir + " ok=1 w=" + std::to_string(gray.cols) +
                " h=" + std::to_string(gray.rows));
    } else {
        std::cout << "[status_panel] '" << hpBarPath
                   << "' bulunamadi - cift dogrulama devre disi kalacak "
                      "(template_picker.exe'de 'h' ile ekleyin).\n";
        LogDiag("PNL tmpl-load dir=" + config.hp_bar_dir + " ok=0");
    }
}

const StatusPanelDetector::ScaledVariant& StatusPanelDetector::GetScaledTemplate(double scale) const {
    for (const auto& kv : m_scaleCache) {
        if (std::abs(kv.first - scale) < 1e-6) return kv.second;
    }
    ScaledVariant v;
    if (std::abs(scale - 1.0) < 1e-3) {
        v.gray = m_templateGray;
        v.edges = m_templateEdges;
    } else {
        int tw = std::max(12, (int)std::lround(m_templateGray.cols * scale));
        int th = std::max(4, (int)std::lround(m_templateGray.rows * scale));
        cv::resize(m_templateGray, v.gray, cv::Size(tw, th), 0, 0,
                   scale < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR);
        cv::Canny(v.gray, v.edges, 50, 150);
    }
    m_scaleCache.emplace_back(scale, v);
    return m_scaleCache.back().second;
}

StatusPanelDetector::Reading StatusPanelDetector::Read(const cv::Mat& frameBgr) const {
    Reading r;
    if (m_templateGray.empty() || frameBgr.empty()) return r;
    r.hasTemplate = true;

    // ============ HIZLI YOL: OGRENILMIS KONUMA BAK ============
    // Bu fonksiyon bot dongusunde HER KAREDE calisir. Genis bant taramasinin
    // maliyeti OLCULDU: 37-57 ms - her karede yapilirsa bot felc oluyor
    // (kullanici bildirimi: "inanilmaz yavas"). Ama HP cubugu HER ZAMAN AYNI
    // yerde belirir, cunku sabit boyutlu ve capali bir arayuz ogesidir. Bu
    // yuzden konum BIR KEZ ogrenilir, sonrasinda sadece o kucuk bolgeye
    // bakilir - hem panel VARKEN hem YOKKEN cok ucuz.
    //
    // Kare boyutu degisirse (kullanici pencereyi yeniden boyutlandirdi ya da
    // cozunurluk degistirdi) ogrenilen konum ANINDA gecersiz sayilir ve
    // yeniden aranir - bu, cozunurluk bagimsizligini BOZMAZ.
    if (frameBgr.size() != m_knownFrameSize) {
        m_knownRect = cv::Rect();
        m_missStreak = 0;
    }

    if (m_knownRect.width > 0) {
        // Ogrenilen kutunun etrafinda kucuk bir pay - panel birkac piksel
        // oynarsa yine yakalanir.
        const int kMargin = 10;
        cv::Rect roi(m_knownRect.x - kMargin, m_knownRect.y - kMargin, m_knownRect.width + 2 * kMargin,
                      m_knownRect.height + 2 * kMargin);
        roi &= cv::Rect(0, 0, frameBgr.cols, frameBgr.rows);
        const auto& variant = GetScaledTemplate(m_knownScale);
        if (roi.width >= variant.gray.cols && roi.height >= variant.gray.rows) {
            cv::Mat roiGray;
            cv::cvtColor(frameBgr(roi), roiGray, cv::COLOR_BGR2GRAY);
            cv::Mat roiEdges;
            cv::Canny(roiGray, roiEdges, 50, 150);
            cv::Mat resGray, resEdges;
            cv::matchTemplate(roiGray, variant.gray, resGray, cv::TM_CCOEFF_NORMED);
            cv::matchTemplate(roiEdges, variant.edges, resEdges, cv::TM_CCOEFF_NORMED);
            double mg, me;
            cv::Point lg, le;
            cv::minMaxLoc(resGray, nullptr, &mg, nullptr, &lg);
            cv::minMaxLoc(resEdges, nullptr, &me, nullptr, &le);
            r.bestScore = std::max(mg, me);
            cv::Point loc = (mg >= me) ? lg : le;
            r.matchRect = cv::Rect(roi.x + loc.x, roi.y + loc.y, variant.gray.cols, variant.gray.rows);
            r.visible = r.bestScore >= m_config.status_panel_name_match_threshold;
            if (r.visible) {
                m_knownRect = r.matchRect;  // kucuk kaymalari takip et
                m_missStreak = 0;
                return r;
            }
            // Panel yok (ya da yeri degisti). Uzun sure bulunamazsa konumu
            // unutup yeniden ogrenmek icin genis aramaya donulur; aksi halde
            // "yok" cevabini UCUZA verip cikiyoruz.
            const int kRelearnAfterMisses = 600;  // ~1 dakika
            if (++m_missStreak < kRelearnAfterMisses) return r;
            m_knownRect = cv::Rect();
            m_missStreak = 0;
        }
    }

    // ============ YAVAS YOL: GENIS BANT ARAMASI ============
    Reading found = SearchBand(frameBgr);
    if (found.visible) {
        m_knownRect = found.matchRect;
        m_knownFrameSize = frameBgr.size();
        m_missStreak = 0;
    }
    return found;
}

StatusPanelDetector::Reading StatusPanelDetector::SearchBand(const cv::Mat& frameBgr) const {
    Reading r;
    r.hasTemplate = true;

    // ================== COZUNURLUKTEN BAGIMSIZ ARAMA ==================
    // KULLANICI BILDIRIMI: "cozunurlugu yukseltince can barini ASLA gormuyor".
    //
    // KOK NEDEN (OLCULDU, tahmin degil): bolge ESKIDEN kare boyutunun
    // ORANLARIYLA (hp_region_*) hesaplaniyordu. Bu, arayuzun pencereyle
    // birlikte buyudugu VARSAYIMINA dayaniyordu - ama bu VARSAYIM YANLIS.
    // Kullanicinin 1602x1049 gercek ekran goruntusunde HP cubugu sablonu
    // aranmis ve TAM OLARAK su bulunmustur:
    //     konum=(817,56)  boyut=129x10  OLCEK=1.0  skor=0.987
    // Referans 802x632'de ise ayni cubuk y=56'da ve yine 129x10 idi. Yani:
    //   * Arayuz SABIT PIKSEL boyutunda - pencere buyuyunce BUYUMUYOR.
    //   * Dikeyde USTE capali (y=56 her iki cozunurlukte de AYNI).
    //   * Yatayda ORTALI (merkezden sapma ~+80..87 px, ayni).
    // Oranli bolge ise 1602x1049'da y=93..110'u gosteriyordu - yani cubugun
    // TAMAMEN ALTINI. Bu yuzden hicbir zaman eslesemiyordu.
    //
    // COZUM: bolgeyi ORANLA tanimlamayi TAMAMEN biraktik. Bunun yerine
    // ust-orta bolgede PIKSEL olcusunde makul bir BANT taranir ve sablon o
    // bant icinde GERCEKTEN ARANIR (kayan pencere). Boylece:
    //   - Hangi cozunurluk/en-boy orani olursa olsun calisir.
    //   - Panelin birkac piksel oynamasi sorun olmaz (eskiden bolge birebir
    //     hizali olmak zorundaydi).
    //   - Kullanicinin bolge kalibrasyonu yapmasina GEREK KALMAZ; sadece
    //     sablon (_hpbar.png) yeterli.
    const int W = frameBgr.cols, H = frameBgr.rows;
    const int bandW = std::min(W, 1000);
    cv::Rect band((W - bandW) / 2, 0, bandW, std::min(H, 240));
    band &= cv::Rect(0, 0, W, H);
    if (band.width < m_templateGray.cols || band.height < m_templateGray.rows) return r;

    cv::Mat bandGray;
    cv::cvtColor(frameBgr(band), bandGray, cv::COLOR_BGR2GRAY);
    cv::Mat bandEdges;
    cv::Canny(bandGray, bandEdges, 50, 150);

    // Arayuz sabit boyutlu oldugu icin ASIL beklenen olcek 1.0'dir; yine de
    // DPI/olcekleme kullanan kurulumlar icin dar bir tolerans birakiyoruz.
    // (Genis olcek taramasi hem gereksiz yavas hem de yanlis eslesme riski.)
    static const double kSearchScales[] = {0.85, 1.0, 1.15, 1.35};
    for (double scale : kSearchScales) {
        const auto& variant = GetScaledTemplate(scale);
        int tw = variant.gray.cols, th = variant.gray.rows;
        if (tw > band.width || th > band.height || tw < 12 || th < 4) continue;

        cv::Mat resGray, resEdges;
        cv::matchTemplate(bandGray, variant.gray, resGray, cv::TM_CCOEFF_NORMED);
        cv::matchTemplate(bandEdges, variant.edges, resEdges, cv::TM_CCOEFF_NORMED);
        double mg, me;
        cv::Point lg, le;
        cv::minMaxLoc(resGray, nullptr, &mg, nullptr, &lg);
        cv::minMaxLoc(resEdges, nullptr, &me, nullptr, &le);
        double score = std::max(mg, me);
        if (score > r.bestScore) {
            m_knownScale = scale;  // hizli yol bu olcegi kullanacak
            r.bestScore = score;
            cv::Point loc = (mg >= me) ? lg : le;
            r.matchRect = cv::Rect(band.x + loc.x, band.y + loc.y, tw, th);
        }
    }

    r.visible = r.bestScore >= m_config.status_panel_name_match_threshold;
    return r;
}
