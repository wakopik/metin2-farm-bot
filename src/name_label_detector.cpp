#include "name_label_detector.h"
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace {

// OTOMATIK CANNY: sabit bir esik cifti (ör. 50/150) HER goruntude dogru
// olmaz - karanlik bir sahnede cok az kenar, parlak/kontrastli bir sahnede
// gurultu kadar kenar verir. Standart "auto-canny" tarifi: goruntunun
// MEDYAN parlakligina GORE esikleri hesapla - boylece hem KUCUK/sabit bir
// sablonda (metin adi etiketi) hem de BUYUK/degisken bir canli kare
// (arama bolgesi) uzerinde, o goruntunun KENDI kontrastina uyarlanmis,
// KARSILASTIRILABILIR yogunlukta kenar haritalari elde edilir.
cv::Mat AutoCanny(const cv::Mat& gray, double sigma = 0.33) {
    std::vector<uchar> pixels(gray.begin<uchar>(), gray.end<uchar>());
    std::nth_element(pixels.begin(), pixels.begin() + pixels.size() / 2, pixels.end());
    double median = pixels[pixels.size() / 2];
    int lower = (int)std::max(0.0, (1.0 - sigma) * median);
    int upper = (int)std::min(255.0, (1.0 + sigma) * median);
    if (upper <= lower) upper = lower + 1;
    cv::Mat edges;
    cv::Canny(gray, edges, lower, upper);
    return edges;
}

// AYNI fiziksel etiketin farkli olceklerde/komsu piksellerde/farkli
// yontemlerde bulunan yinelenen kayitlarini tek bir (en yuksek skorlu)
// tespide indirger - OrbDetector::NonMaxSuppression ile AYNI mantik,
// burada yerel (bu dosyaya ozel) kucuk bir kopyasi.
std::vector<Detection> SuppressDuplicates(std::vector<Detection> dets, double overlapThresh) {
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

}  // namespace

NameLabelDetector::NameLabelDetector(const Config& config) : m_config(config) {
    for (const auto& dir : config.templates_dirs) {
        std::string path = (fs::path(dir) / "_name.png").string();
        // IMREAD_UNCHANGED: template_picker.exe artik arka plani KALDIRIP
        // SEFFAF (RGBA, 4 kanal) kaydediyor - eski (duz gri, 1 kanal, arka
        // plan GOMULU) sablonlarla GERIYE DONUK uyumluluk icin format burada
        // TESPIT EDILIR, IMREAD_GRAYSCALE gibi SABIT bir formata ZORLANMAZ.
        cv::Mat raw = cv::imread(path, cv::IMREAD_UNCHANGED);
        if (raw.empty()) continue;

        cv::Mat img, mask;
        if (raw.channels() == 4) {
            std::vector<cv::Mat> ch(4);
            cv::split(raw, ch);
            cv::Mat bgr;
            cv::merge(std::vector<cv::Mat>{ch[0], ch[1], ch[2]}, bgr);
            cv::cvtColor(bgr, img, cv::COLOR_BGR2GRAY);
            cv::threshold(ch[3], mask, 10, 255, cv::THRESH_BINARY);
        } else if (raw.channels() == 3) {
            cv::cvtColor(raw, img, cv::COLOR_BGR2GRAY);
        } else {
            img = raw;  // eski format: zaten tek kanal (duz gri)
        }

        // NOT: "dovus/" gibi sonunda ayirici olan bir yol filename()'i BOS
        // dondurebilir - orb_detector.cpp'deki AYNI duzeltme burada da uygulanir.
        std::string category = fs::path(dir).filename().string();
        if (category.empty()) category = fs::path(dir).parent_path().filename().string();

        m_templates.push_back({category, img, AutoCanny(img), mask});
        std::cout << "[name_label] '" << path << "' isim etiketi referansi yuklendi (" << img.cols << "x"
                   << img.rows << "px" << (mask.empty() ? ", ARKA PLAN GOMULU (eski format)" : ", arka plan kaldirilmis")
                   << ") - acidan bagimsiz EK tespit AKTIF.\n";
        // TANI: bu sablon COK KUCUK (ör. 9-14px yukseklik) ise, hem gri-ton
        // hem kenar korelasyonu icin ayirt edici sinyal ciddi sekilde
        // sinirlidir (birkac piksellik bir capture/anti-aliasing farki bile
        // korelasyonu bozabilir) - kullaniciyi ACIKCA uyariyoruz, boylece
        // "neden hala kaciriyor" sorusunun ilk suphelisi belli olsun.
        if (img.rows < 20) {
            std::cout << "[name_label] UYARI: '" << path << "' cok kucuk (" << img.rows << "px yukseklik) - "
                       << "tespit hassasiyeti dusuk olabilir. template_picker.exe ile DAHA BUYUK/BOL bir "
                       << "kirpim (yaziyi rahat bir bosluk payiyla cevreleyen) yeniden almaniz onerilir.\n";
        }

        // KISMI ESLESME (KULLANICININ TALEBI, GENISLETILDI): yazinin TAMAMI
        // (ör. "Dovus Metini") kamera acisina/kismi kapanmaya/kadraj
        // kenarina takilmaya gore bazen TAM eslesme esigini gecemiyor - bu
        // durumda TUM etiket sessizce KACIRILIYORDU (kullanicinin canli
        // testinde bir kez daha GORULDU). Yuklenen TAM goruntuden hem
        // SOLDAN (prefix - sag taraf kesilirse/bozulursa) hem SAGDAN
        // (suffix - sol taraf kesilirse/bozulursa) kismi kirpimlar turetip
        // AYRI sablonlar olarak ekliyoruz - hangi yon kesilirse kesilsin
        // kalan gorunur kisim hala yakalanabilir. Detect() zaten TUM
        // sablonlari dener, ek kod degisikligi gerekmiyor. Cok dar bir
        // kirpim (ör. tek harf) yanlis-pozitif riskini artirir, bu yuzden
        // makul (yarisindan fazla) oranlarla sinirlandirildi.
        const double kPartialFrac = 0.65;
        int pw = (int)std::round(img.cols * kPartialFrac);
        if (pw >= 12) {
            cv::Mat prefixMask = mask.empty() ? cv::Mat() : mask(cv::Rect(0, 0, pw, img.rows)).clone();
            cv::Mat prefix = img(cv::Rect(0, 0, pw, img.rows)).clone();
            m_templates.push_back({category, prefix, AutoCanny(prefix), prefixMask});
            cv::Mat suffixMask =
                mask.empty() ? cv::Mat() : mask(cv::Rect(img.cols - pw, 0, pw, img.rows)).clone();
            cv::Mat suffix = img(cv::Rect(img.cols - pw, 0, pw, img.rows)).clone();
            m_templates.push_back({category, suffix, AutoCanny(suffix), suffixMask});
        }
    }
}

std::vector<Detection> NameLabelDetector::Detect(const cv::Mat& frameBgr, const cv::Rect& searchRegionIn) const {
    std::vector<Detection> dets;
    if (m_templates.empty() || frameBgr.empty()) return dets;

    cv::Rect frameBounds(0, 0, frameBgr.cols, frameBgr.rows);
    cv::Rect region = searchRegionIn & frameBounds;
    if (region.width <= 8 || region.height <= 8) return dets;

    cv::Mat gray;
    cv::cvtColor(frameBgr(region), gray, cv::COLOR_BGR2GRAY);
    // KENAR HARITASI: _name.png ARKA PLANI baktan icerdigi icin (bkz.
    // Template::edges yorumu, name_label_detector.h) ham gri-ton eslesmesi
    // o anki zemin rengine COK duyarli - yazi gozle acikca gorunse bile,
    // arka plan capture aninkinden farkliysa skor cokup HIC yakalanmayabilir.
    // Kenar haritasi buna karsi IKINCI, renkten BAGIMSIZ bir sinyal saglar -
    // yazinin koyu dis hatti hangi zemin uzerinde olursa olsun GUCLU bir
    // kenar birakir. Bolge basina BIR KEZ hesaplanir (tum sablonlar/olcekler
    // AYNI kenar haritasini kullanir).
    cv::Mat edgeRegion = AutoCanny(gray);

    // KRITIK: kadrajda AYNI turden BIRDEN FAZLA metin (dolayisiyla birden
    // fazla isim etiketi) AYNI ANDA gorunebilir - bir tek "en iyi eslesme"
    // ile YETINMIYORUZ, esigin USTUNDEKI TUM tepe noktalarini topluyoruz.
    // PERFORMANS: onceden 8 idi - kullanicinin "tespit yavas" geri
    // bildirimi uzerine 5'e dusuruldu. Pratikte kadrajda ayni anda 5'ten
    // fazla GERCEK metin nadiren olur, iterasyon zaten esigin altina
    // dusunce erken duruyor (bkz. asagidaki break) - bu sadece asiri uc
    // (yogun kume) senaryolarda tarama suresini sinirlar.
    const int kMaxMatchesPerScale = 5;

    // ITERATIF TEPE ARAMA: bir (kaynak, sablon) cifti icin esigin ustundeki
    // TUM tepe noktalarini toplar - hem gri-ton hem kenar gecisi TARAFINDAN
    // ORTAK kullanilir (kod tekrarini onlemek icin ayrildi).
    auto FindPeaks = [&](const cv::Mat& source, const cv::Mat& templ, int tw, int th, double threshold,
                          const std::string& category, std::vector<Detection>& out, int method = cv::TM_CCOEFF_NORMED,
                          const cv::Mat& mask = cv::Mat()) {
        cv::Mat result;
        if (mask.empty()) {
            cv::matchTemplate(source, templ, result, method);
        } else {
            cv::matchTemplate(source, templ, result, method, mask);
        }
        for (int iter = 0; iter < kMaxMatchesPerScale; ++iter) {
            double maxVal = 0;
            cv::Point maxLoc;
            cv::minMaxLoc(result, nullptr, &maxVal, nullptr, &maxLoc);
            if (maxVal < threshold) break;

            Detection d;
            d.x = region.x + maxLoc.x;
            d.y = region.y + maxLoc.y;
            d.w = tw;
            d.h = th;
            d.score = maxVal;
            d.name = "name_label";
            d.category = category;
            // KRITIK: gercek TIKLAMA noktasi yazinin KENDISI DEGIL - yazi
            // tasin USTUNDE yuzuyor, gercek nesne biraz ASAGISINDA. Sabit
            // piksel DEGIL, o anki (olceklenmis) sablon YUKSEKLIGININ bir
            // kati kadar asagi kaydiriyoruz - boylece yakinda/uzakta (yazi
            // buyuk/kucuk gorunse de) ORANTILI olarak dogru noktaya duser.
            d.hasPrecisePoint = true;
            d.precisePoint = cv::Point2d(d.x + d.w / 2.0, d.y + d.h * m_config.name_label_click_offset_factor);
            out.push_back(d);

            int sx = std::max(0, maxLoc.x - tw / 2);
            int sy = std::max(0, maxLoc.y - th / 2);
            int ex = std::min(result.cols, maxLoc.x + tw / 2);
            int ey = std::min(result.rows, maxLoc.y + th / 2);
            if (ex > sx && ey > sy) {
                result(cv::Rect(sx, sy, ex - sx, ey - sy)).setTo(-1.0f);
            }
        }
    };

    for (const auto& tmpl : m_templates) {
        std::vector<Detection> candidates;

        // GENIS olcek araligi: yazi etiketi, karakterin metne olan
        // mesafesine gore ekranda COK farkli boyutlarda gorunebilir
        // (yakin=buyuk, uzak=kucuk) - ORB'un aksine burada sablonun
        // KENDISI hep ayni/duz gorundugu icin (billboard yazi), SADECE
        // olcegi taramak yeterli, aciyi taramaya gerek yok. PERFORMANS:
        // onceden 10 farkli olcek taraniyordu (kullanicinin "tespit
        // yavas" geri bildirimi uzerine 7'ye dusuruldu) - 1.0 civarinda
        // (en sik karsilasilan mesafe) hala sik adimlarla, uc noktalarda
        // (cok yakin/cok uzak) daha seyrek adimlarla tariyoruz.
        for (double scale : {0.5, 0.7, 0.85, 1.0, 1.25, 1.6, 2.0}) {
            int tw = (int)std::round(tmpl.gray.cols * scale);
            int th = (int)std::round(tmpl.gray.rows * scale);
            if (tw < 6 || th < 6 || tw >= gray.cols || th >= gray.rows) continue;

            cv::Mat scaledTempl;
            cv::resize(tmpl.gray, scaledTempl, cv::Size(tw, th), 0, 0, cv::INTER_LINEAR);
            FindPeaks(gray, scaledTempl, tw, th, m_config.name_label_match_threshold, tmpl.category, candidates);

            cv::Mat scaledEdges;
            cv::resize(tmpl.edges, scaledEdges, cv::Size(tw, th), 0, 0, cv::INTER_LINEAR);
            FindPeaks(edgeRegion, scaledEdges, tw, th, m_config.name_label_edge_match_threshold, tmpl.category,
                      candidates);

            // MASKELI ESLESME (arka plandan TAMAMEN etkilenmez): sadece
            // template_picker.exe'nin arka plani kaldirip SEFFAF kaydettigi
            // (bkz. Template::mask) sablonlarda calisir - eski/arka-plan-
            // gomulu sablonlarda mask bos oldugu icin bu gecis atlanir
            // (yukaridaki 2 gecis zaten devrede kalir, regresyon YOK).
            if (!tmpl.mask.empty()) {
                cv::Mat scaledMask;
                cv::resize(tmpl.mask, scaledMask, cv::Size(tw, th), 0, 0, cv::INTER_NEAREST);
                cv::threshold(scaledMask, scaledMask, 127, 255, cv::THRESH_BINARY);
                FindPeaks(gray, scaledTempl, tw, th, m_config.name_label_masked_match_threshold, tmpl.category,
                          candidates, cv::TM_CCORR_NORMED, scaledMask);
            }
        }

        // Farkli olceklerden VE farkli yontemlerden (gri-ton + kenar) gelen,
        // AYNI fiziksel etikete ait yinelenen adaylari (kutulari cakisan)
        // birlestir - her GERCEK etiket icin TEK bir (en iyi skorlu) tespit
        // kalsin.
        candidates = SuppressDuplicates(std::move(candidates), 0.3);
        dets.insert(dets.end(), candidates.begin(), candidates.end());
    }
    return dets;
}
