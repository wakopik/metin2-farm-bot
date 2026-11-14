#include "auto_color_detector.h"
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace {

int Percentile(std::vector<int>& v, double pct) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    size_t idx = (size_t)std::clamp((pct / 100.0) * (v.size() - 1), 0.0, (double)(v.size() - 1));
    return v[idx];
}

}  // namespace

AutoColorDetector::AutoColorDetector(const Config& config) : m_config(config) {
    std::error_code ec;

    std::vector<int> hPool, sPool, vPool;
    std::vector<int> widths, heights;

    // BIRDEN FAZLA metin turu secildiyse, TUM klasorlerin piksellerini TEK
    // bir ortak havuzda birlestiriyoruz - tek bir (biraz daha genis toleransli)
    // HSV araligi cikar. Bu, ORB'un aksine ayri ayri profil TUTMUYOR ama
    // AutoColor zaten sadece EK/toleransli bir ikincil yontem (ana tespit
    // ORB) - bu basitlestirme kabul edilebilir bir tradeoff.
    for (const auto& dir : config.templates_dirs) {
        if (!fs::exists(dir, ec)) {
            std::cerr << "[auto_color] '" << dir << "' klasoru yok.\n";
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

            widths.push_back(img.cols);
            heights.push_back(img.rows);

            cv::Mat bgr;
            cv::Mat mask;
            if (img.channels() == 4) {
                std::vector<cv::Mat> ch(4);
                cv::split(img, ch);
                cv::merge(std::vector<cv::Mat>{ch[0], ch[1], ch[2]}, bgr);
                cv::threshold(ch[3], mask, 10, 255, cv::THRESH_BINARY);
            } else {
                bgr = (img.channels() == 3) ? img : cv::Mat();
                if (bgr.empty()) cv::cvtColor(img, bgr, cv::COLOR_GRAY2BGR);
                // Alfasiz sablon: sadece merkez %60'lik bolgeyi kullan (etrafta
                // biraz arka plan olsa da merkeze yaklastikca gercek nesne
                // rengine rastlama ihtimali artar).
                int x0 = (int)(bgr.cols * 0.2), x1 = (int)(bgr.cols * 0.8);
                int y0 = (int)(bgr.rows * 0.2), y1 = (int)(bgr.rows * 0.8);
                mask = cv::Mat::zeros(bgr.size(), CV_8U);
                mask(cv::Rect(x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0))).setTo(255);
            }

            cv::Mat hsv;
            cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
            for (int y = 0; y < hsv.rows; ++y) {
                const uchar* maskRow = mask.ptr<uchar>(y);
                const cv::Vec3b* hsvRow = hsv.ptr<cv::Vec3b>(y);
                for (int x = 0; x < hsv.cols; ++x) {
                    if (!maskRow[x]) continue;
                    hPool.push_back(hsvRow[x][0]);
                    sPool.push_back(hsvRow[x][1]);
                    vPool.push_back(hsvRow[x][2]);
                }
            }
        }
    }

    if (hPool.empty()) {
        std::cerr << "[auto_color] sablonlardan hic piksel cikarilamadi - templates/ bos olabilir.\n";
        return;
    }

    const int padH = 10, padS = 40, padV = 40;
    int lowH = std::max(0, Percentile(hPool, 5) - padH);
    int lowS = std::max(0, Percentile(sPool, 5) - padS);
    int lowV = std::max(0, Percentile(vPool, 5) - padV);
    int highH = std::min(179, Percentile(hPool, 95) + padH);
    int highS = std::min(255, Percentile(sPool, 95) + padS);
    int highV = std::min(255, Percentile(vPool, 95) + padV);
    m_lower = cv::Scalar(lowH, lowS, lowV);
    m_upper = cv::Scalar(highH, highS, highV);

    std::sort(widths.begin(), widths.end());
    std::sort(heights.begin(), heights.end());
    const double margin = 0.35;
    m_minW = std::max(4, (int)(widths.front() * (1 - margin)));
    m_maxW = (int)(widths.back() * (1 + margin));
    m_minH = std::max(4, (int)(heights.front() * (1 - margin)));
    m_maxH = (int)(heights.back() * (1 + margin));

    // GUVENLIK: SIYAH/KOYU GRI gibi renksiz (dusuk doygunluk) nesnelerde Hue
    // (renk tonu) kanali matematiksel olarak ANLAMSIZ/GURULTULU hale gelir
    // (doygunluk ve/veya parlaklik sifira yaklastikca hangi "renk" oldugu
    // rastgelelesir) - bu da cikarilan H araligini neredeyse TUM tayfi
    // (0-179) kaplayacak kadar genisletebilir. Boyle bir profil "her rengi
    // eslestir" demekle esdeger - hicbir ayirt edicilik saglamaz, ustelik
    // ekrandaki alakasiz her seyi (yanlislikla) aday gosterip asil (ORB)
    // tespitiyle yarisip onu gecebilir. Bu durumda AutoColor'u TAMAMEN devre
    // disi birakiyoruz (m_ready=false) - "gurultulu bir filtre" yerine "hic
    // filtre yok, ORB'a guven" tercih edilir. NOT: bu SADECE renk tabanli
    // EK/yedek yontemi kapatir - ORB (asil/birincil tespit) bundan HICBIR
    // sekilde etkilenmez.
    const int kDegenerateHueSpan = 150;
    if ((highH - lowH) > kDegenerateHueSpan) {
        std::cout << "[auto_color] Hue araligi cok genis (H[" << lowH << "-" << highH << "]) - bu metin turu "
                   << "renksel olarak ayirt edici degil (ör. siyah/gri), AutoColor devre disi birakildi - "
                   << "SADECE ORB tespiti kullanilacak.\n";
        return;
    }

    m_ready = true;
    std::cout << "[auto_color] " << widths.size() << " sablondan renk profili cikarildi -> H["
               << lowH << "-" << highH << "] S[" << lowS << "-" << highS << "] V[" << lowV << "-" << highV
               << "], boyut " << m_minW << "-" << m_maxW << " x " << m_minH << "-" << m_maxH << "px\n";
}

std::optional<cv::Point2d> AutoColorDetector::FindColorCenterIn(const cv::Mat& frameBgr, cv::Rect windowIn,
                                                                  cv::Rect excludeRect, cv::Mat* outMask,
                                                                  cv::Mat* outCrop) const {
    // NOT: m_ready burada KASITLI olarak KONTROL EDILMIYOR - m_lower/m_upper
    // (renksiz/siyah nesnelerde genis olsa bile) HER ZAMAN hesaplanmis
    // durumda (bkz. yapici, m_ready=false donusunden ONCE atanmislar).
    // Global taramada genis bir aralik tehlikeliydi (ekranin her yerinde
    // yanlis pozitif), ama BURADA pencere zaten cok kucuk/hedefli oldugu
    // icin o risk yok.
    if (frameBgr.empty()) return std::nullopt;
    cv::Rect frameBounds(0, 0, frameBgr.cols, frameBgr.rows);
    cv::Rect window = windowIn & frameBounds;
    if (window.width <= 4 || window.height <= 4) return std::nullopt;

    // NOT: onceden burada m_lower/m_upper (sablonlardan ONCEDEN/statik
    // ogrenilmis global HSV araligi) kullaniliyordu - bu, isik/saat/
    // bolgeye gore degisen cimen tonlarinin bazen bu SABIT araliga girip
    // tasin YERINE buyuk bir cimen lekesinin secilmesine yol acti
    // (kullanicinin UST USTE canli testlerinde GORULDU). YONTEM
    // DEGISTIRILDI: statik bir renge guvenmek yerine, BU KAREDE cimenin
    // GERCEK rengini ORNEKLEYIP, o renkten YETERINCE FARKLI olan
    // pikselleri "on plan/tas" sayiyoruz - bu, o anki aydinlatmaya otomatik
    // uyum sagladigi icin statik bir HSV bandindan cok daha saglam.
    // NOT (3. DUZELTME - TANI DOKUMUNE DAYALI): once tum kenarlardan, sonra
    // sadece ALT seritten ORTALAMA ile ornekleniyordu. Tani dokumu
    // (dump_4_crop/mask.png) ALT SERIDE denk gelen KIRMIZI HASAR YAZISININ
    // ortalamayi bozup maskeyi tamamen yanlis yere (hasar yazisina)
    // kaydirdigini KANITLADI. Artik TEK bir seride degil, PENCERENIN
    // TAMAMININ KANAL-BAZLI MEDYANINA bakiyoruz: medyan, aykiri degerlere
    // (hasar yazisi, tasin kendisi, kucuk parlamalar) ortalamanin aksine
    // DAYANIKLIDIR ve pencerede cogunlugu zemin olusturdugu icin
    // guvenilir sekilde ZEMIN rengini verir.
    cv::Mat crop = frameBgr(window);
    cv::Scalar bgMean;
    {
        std::vector<cv::Mat> bgrCh;
        cv::split(crop, bgrCh);
        const int total = crop.rows * crop.cols;
        for (int i = 0; i < (int)bgrCh.size() && i < 4; ++i) {
            int hist[256] = {0};
            for (int y = 0; y < bgrCh[i].rows; ++y) {
                const uchar* row = bgrCh[i].ptr<uchar>(y);
                for (int x = 0; x < bgrCh[i].cols; ++x) hist[row[x]]++;
            }
            int cum = 0, med = 0;
            for (int v = 0; v < 256; ++v) {
                cum += hist[v];
                if (cum >= total / 2) { med = v; break; }
            }
            bgMean[i] = med;
        }
    }

    cv::Mat bgMat(crop.size(), crop.type(), bgMean);
    cv::Mat diff;
    cv::absdiff(crop, bgMat, diff);
    std::vector<cv::Mat> ch;
    cv::split(diff, ch);
    cv::Mat distSq = cv::Mat::zeros(crop.size(), CV_32F);
    for (auto& c : ch) {
        cv::Mat c32;
        c.convertTo(c32, CV_32F);
        distSq += c32.mul(c32);
    }
    cv::Mat dist;
    cv::sqrt(distSq, dist);
    const double kDistThresh = 38.0;  // BGR Oklid mesafesi - cimenden yeterince farkli sayilma esigi
    cv::Mat mask;
    cv::threshold(dist, mask, kDistThresh, 255, cv::THRESH_BINARY);
    mask.convertTo(mask, CV_8U);

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

    // ISIM YAZISINI MASKEDEN CIKAR: pencere artik yazinin ETRAFINI (ustunu
    // de) tariyor, yani yazinin KENDISI de iceride - yazi pikselleri
    // (parlak/kontrastli) cimenden cok farkli oldugu icin, cikarilmazsa
    // "en buyuk leke" olarak YAZI secilip tiklama yazinin uzerine
    // duserdi. Pencere-yerel koordinata cevirip sifirliyoruz.
    if (excludeRect.width > 0 && excludeRect.height > 0) {
        cv::Rect localExclude(excludeRect.x - window.x, excludeRect.y - window.y, excludeRect.width,
                               excludeRect.height);
        localExclude &= cv::Rect(0, 0, mask.cols, mask.rows);
        if (localExclude.width > 0 && localExclude.height > 0) mask(localExclude).setTo(0);
    }

    if (outMask) *outMask = mask.clone();
    if (outCrop) *outCrop = crop.clone();

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // SECIM KRITERI (TANI DOKUMUNE DAYALI): onceden "merkez SUTUNUNU
    // kapsayan EN BUYUK leke" seciliyordu. Tani dokumu bunun iki sekilde
    // bozuldugunu gosterdi: (1) tas yanal olarak kaymis oldugunda merkez
    // sutunu kacirip TAMAMEN eleniyordu (dump_3/dump_5 - tas solda),
    // (2) hasar yazisi gibi alakasiz ama BUYUK bir leke kazanabiliyordu
    // (dump_4). Artik oyunun degismez bir gercegini kullaniyoruz: ISIM
    // ETIKETI, ait oldugu tasin TAM UZERINDE yuzer - yani DOGRU leke,
    // etikete EN YAKIN olanidir. Hasar yazilari/uzak cimen lekeleri
    // etiketten uzaklastigi icin dogal olarak elenir; tasin yanal
    // kaymasi ise artik sorun degil.
    cv::Point2d labelCenterLocal(mask.cols / 2.0, mask.rows / 2.0);
    if (excludeRect.width > 0 && excludeRect.height > 0) {
        labelCenterLocal = cv::Point2d(excludeRect.x - window.x + excludeRect.width / 2.0,
                                        excludeRect.y - window.y + excludeRect.height / 2.0);
    }

    // UST SINIR (TANI DOKUMUNE DAYALI - dump_0/dump_1): pencere iki farkli
    // ZEMIN DOKUSUNU (ör. koyu cim <-> acik toprak) birden kapsadiginda,
    // medyan bunlardan BIRINI arka plan secer ve DIGER ZEMININ TAMAMI dev
    // tek bir "nesne" lekesi olur; merkezi de tesadufen etikete yakin
    // dusup KAZANABILIYORDU (kullanicinin ekran goruntusundeki o absurt
    // sapmanin sebebi buydu). Gercek bir metin tasi pencerenin ancak
    // KUCUK bir kismini kaplar - pencerenin buyuk bolumunu dolduran her
    // leke tanim geregi ZEMINDIR, eleniyor.
    const double kMaxAreaRatio = 0.30;
    const double maxArea = kMaxAreaRatio * (double)mask.rows * mask.cols;

    // RENK KIMLIGI DOGRULAMASI (YENI): onceden "etikete EN YAKIN" leke KOR
    // KORUNE seciliyordu - zemin-disi HERHANGI bir leke (baska bir oyuncu,
    // hasar yazisi, parlama efekti, YAKINDAKI baska bir metin/canavar)
    // etikete tas KENDISINDEN daha yakinsa, YANLIS leke secilip tiklama
    // tamamen alakasiz bir noktaya duserdi - kullanicinin bildirdigi "merkez
    // cok yanlis tespit ediliyor" sikayetinin en olasi kok sebebi budur.
    // Duzeltme: sablonlardan cikarilan HSV renk profiliyle (m_lower/m_upper,
    // yapicida hesaplanir) HER adayin ORTALAMA rengini karsilastiriyoruz -
    // profile uyanlar arasindan en yakini seciyoruz, SADECE hicbiri
    // uymuyorsa (ör. renksiz/siyah hedef - bkz. yapicidaki "degenerate hue"
    // notu) eski (saf geometrik) davranisa donuyoruz.
    cv::Mat hsvCrop;
    cv::cvtColor(crop, hsvCrop, cv::COLOR_BGR2HSV);

    struct Candidate {
        cv::Point2d center;
        double distToLabel;
        bool colorMatch;
    };
    std::vector<Candidate> candidates;
    for (const auto& c : contours) {
        double area = cv::contourArea(c);
        // Kucuk gurultu lekelerini (tek tuk parlama pikselleri) ele - ama
        // esik, uzaktaki KUCUK gorunen taslari da kaybetmeyecek kadar dusuk.
        if (area < 60.0) continue;
        if (area > maxArea) continue;  // zemin dokusu gecisi - tas degil
        cv::Moments mo = cv::moments(c);
        if (mo.m00 <= 0) continue;
        cv::Point2d cen(mo.m10 / mo.m00, mo.m01 / mo.m00);

        cv::Mat contourMask = cv::Mat::zeros(mask.size(), CV_8U);
        cv::drawContours(contourMask, std::vector<std::vector<cv::Point>>{c}, 0, cv::Scalar(255), cv::FILLED);
        cv::Scalar meanHsv = cv::mean(hsvCrop, contourMask);
        bool colorMatch = meanHsv[0] >= m_lower[0] && meanHsv[0] <= m_upper[0] && meanHsv[1] >= m_lower[1] &&
                           meanHsv[1] <= m_upper[1] && meanHsv[2] >= m_lower[2] && meanHsv[2] <= m_upper[2];

        double dist = std::hypot(cen.x - labelCenterLocal.x, cen.y - labelCenterLocal.y);
        candidates.push_back({cen, dist, colorMatch});
    }

    if (candidates.empty()) return std::nullopt;

    // Once RENGI UYAN adaylar arasinda en yakini ara; hicbiri uymuyorsa
    // TUM adaylar arasinda en yakina don (eski davranis - regresyon yok).
    const Candidate* best = nullptr;
    for (const auto& cand : candidates) {
        if (!cand.colorMatch) continue;
        if (!best || cand.distToLabel < best->distToLabel) best = &cand;
    }
    if (!best) {
        for (const auto& cand : candidates) {
            if (!best || cand.distToLabel < best->distToLabel) best = &cand;
        }
    }
    if (!best) return std::nullopt;
    return cv::Point2d(window.x + best->center.x, window.y + best->center.y);
}

std::vector<Detection> AutoColorDetector::Detect(const cv::Mat& frameBgr, const cv::Rect& searchRegionIn) const {
    std::vector<Detection> dets;
    if (!m_ready || frameBgr.empty()) return dets;

    cv::Rect frameBounds(0, 0, frameBgr.cols, frameBgr.rows);
    cv::Rect region = searchRegionIn & frameBounds;
    if (region.width <= 4 || region.height <= 4) return dets;

    cv::Mat crop = frameBgr(region);
    cv::Mat hsv;
    cv::cvtColor(crop, hsv, cv::COLOR_BGR2HSV);
    cv::Mat mask;
    cv::inRange(hsv, m_lower, m_upper, mask);

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    for (const auto& c : contours) {
        double area = cv::contourArea(c);
        if (area < m_config.color_min_area || area > m_config.color_max_area) continue;

        cv::Rect box = cv::boundingRect(c);
        if (box.width < m_minW || box.width > m_maxW || box.height < m_minH || box.height > m_maxH) continue;

        double fillRatio = area / std::max(1.0, (double)(box.width * box.height));
        if (fillRatio < m_config.color_min_score) continue;

        Detection d;
        d.x = region.x + box.x;
        d.y = region.y + box.y;
        d.w = box.width;
        d.h = box.height;
        d.score = fillRatio;
        d.name = "auto_color";
        dets.push_back(d);
    }

    return dets;
}
