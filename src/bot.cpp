#include "bot.h"
#include "diag_log.h"
#include "win_window.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <random>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;

namespace {
thread_local std::mt19937 g_rng{std::random_device{}()};

// desktop_app.exe konsolsuz (WIN32_EXECUTABLE) calistigi icin std::cout
// hicbir yere gitmiyor - discord_bot.cpp/yolo_detector.cpp
// ile AYNI desen: [click-debug] gibi kritik tanı satirlari (tespit->ekran
// tiklama koordinat donusumu) GORUNMEDEN kayboluyordu, bu bir dosyaya da
// yazilir ki tiklama koordinat sorunlari (ör. "tespit ettigi yerden uzaga
// tikliyor") GERCEK VERIYLE teshis edilebilsin.
void LogClickDebug(const std::string& msg) {
    // Ayni "gizli" tani dosyasi input.cpp'nin de yazdigi dosyayla AYNI -
    // bkz. input.cpp kDiagFile yorumu (kisaltma anahtari orada).
    std::ofstream f("diag.log", std::ios::app);
    if (!f.is_open()) return;
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
    localtime_s(&tmBuf, &t);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tmBuf);
    f << "[" << buf << "] " << msg << "\n";
}

double RandRange(double lo, double hi) {
    std::uniform_real_distribution<double> dist(lo, hi);
    return dist(g_rng);
}

int RandSign() { return (g_rng() % 2 == 0) ? 1 : -1; }

// Pencere basligi "Misali2 (Feda) | KarakterAdi" formatinda - son "|"
// isaretinden sonraki kismi (bosluklari kirpilmis) KARAKTER ADI olarak
// cikarir. Boyle bir ayirici yoksa (beklenmedik baslik formati) basligin
// tamamini oldugu gibi dondurur.
std::string ExtractCharacterName(const std::string& windowTitle) {
    auto pos = windowTitle.find_last_of('|');
    std::string name = (pos == std::string::npos) ? windowTitle : windowTitle.substr(pos + 1);
    size_t start = name.find_first_not_of(" \t");
    size_t end = name.find_last_not_of(" \t");
    if (start == std::string::npos) return "";
    return name.substr(start, end - start + 1);
}

void SleepSeconds(double sec) {
    if (sec <= 0) return;
    std::this_thread::sleep_for(std::chrono::duration<double>(sec));
}

// TANI: "odak kaybedildi" sadece tekrar tekrar bu mesaji basip HANGI
// pencerenin araya girdigini SOYLEMIYORDU - kullanici oyunun aktif
// oldugundan EMIN olsa bile bunu KANITLAYAMIYORDUK. Artik o an GERCEKTEN
// on planda olan pencerenin HEM handle'ini HEM basligini basiyoruz -
// boylece ya (a) gercekten baska bir pencere (ör. konsol, tarayici) araya
// giriyor, ya da (b) beklenmedik sekilde ayni basligi gosteren FARKLI bir
// handle donuyor (m_hwnd yanlis pencereye baglanmis olabilir) AYIRT EDILEBILIR.
void LogForegroundMismatch(HWND expectedHwnd, const std::string& expectedTitle) {
    HWND actual = GetForegroundWindow();
    std::string actualTitle = actual ? win_window::GetWindowTitle(actual) : "(pencere yok)";
    std::cout << "[bot] odak kaybedildi - burst kesildi, fare serbest | beklenen: HWND=" << expectedHwnd << " (\""
               << expectedTitle << "\")  su an on planda: HWND=" << actual << " (\"" << actualTitle << "\")\n";
}
}  // namespace

MetinBot::MetinBot(const Config& config)
    : m_config(config),
      m_colorDetector(config),
      m_nameLabelDetector(config),
      m_panelDetector(config) {
    // SADECE config.json'da tanimlanmis kategoriler icin YoloDetector
    // olustur - hicbiri tanimli degilse (yolo_models bos) bu dongu hic
    // calismaz, eski (isim etiketi/renk) tespit sistemi HICBIR sekilde
    // etkilenmez.
    for (const auto& entry : config.yolo_models) {
        // Modele OZEL bir esik verilmisse (config.json yolo_models[].
        // conf_threshold) onu kullan, verilmemisse (negatif) global
        // yolo_conf_threshold'a dus - bkz. config.h YoloModelEntry yorumu.
        float confThr = entry.conf_threshold >= 0.0
                            ? (float)entry.conf_threshold
                            : (float)config.yolo_conf_threshold;
        m_yoloDetectors.emplace_back(entry.path, entry.category, config.yolo_input_size, confThr,
                                      (float)config.yolo_nms_threshold, entry.class_index);
    }
}

MetinBot::~MetinBot() = default;

void MetinBot::BindWindow() {
    HWND hwnd = win_window::FindGameWindow(m_config.window_title_hint);
    // Bulunamazsa: bot daha once pencereyi yeniden adlandirmis olabilir AMA
    // oyun istemcisi o zamandan beri yeniden acilip basligini kendi
    // orijinal ismine dondurmus olabilir - bu durumda ikinci (fallback)
    // ipucuyla tekrar dene, boylece manuel config degisikligi gerekmez.
    if (!hwnd && !m_config.window_title_fallback_hint.empty()) {
        hwnd = win_window::FindGameWindow(m_config.window_title_fallback_hint);
    }
    if (!hwnd) {
        auto titles = win_window::ListVisibleWindowTitles();
        std::string joined;
        for (auto& t : titles) {
            joined += t;
            joined += ", ";
        }
        throw std::runtime_error(
            "Oyun penceresi bulunamadi (window_title_hint='" + m_config.window_title_hint +
            "'). Acik pencereler: " + (joined.empty() ? "(yok)" : joined));
    }
    BindWindowCore(hwnd);
}

void MetinBot::BindWindow(HWND hwnd) {
    if (!hwnd) {
        throw std::runtime_error("MetinBot::BindWindow(HWND): gecersiz (null) pencere.");
    }
    BindWindowCore(hwnd);
}

void MetinBot::BindWindowCore(HWND hwnd) {
    win_window::BringToForeground(hwnd);
    m_hwnd = hwnd;
    m_capture = std::make_unique<WindowCapture>(hwnd, m_config.capture_min_interval_ms);
    m_input = std::make_unique<InputController>(m_config);
    m_input->SetBounds(hwnd);  // fare ASLA bu pencerenin disina cikmasin

    // Pencere basligina HICBIR SEKILDE dokunulmuyor (ONCEDEN "ElbitPremium |
    // tarih" seklinde gizli bir isimle degistiriliyordu - bu davranis
    // kaldirildi). Gercek/degismemis basligi ("Misali2 (Feda) | KarakterAdi")
    // hem tanilama loglarinda hem de KARAKTER ADI cikarmak icin kullaniyoruz.
    m_originalTitle = win_window::GetWindowTitle(hwnd);
    m_characterName = ExtractCharacterName(m_originalTitle);

    // TANI: arayuzdeki anahtarlarin bota GERCEKTEN ulasip ulasmadigini
    // ("acmistim ama calismiyor" tipi sorularda) tahminle degil LOGLA
    // dogrulayabilmek icin, baglanma aninda etkin bayraklari yaziyoruz.
    LogDiag(std::string("CFG queue=") + (m_config.queue_other_visible_metins ? "1" : "0") +
             " queueInt=" + std::to_string(m_config.queue_scan_interval_sec) +
             " atpro=" + (m_config.at_pro_enabled ? "1" : "0") +
             " antistuck=" + (m_config.anti_stuck_enabled ? "1" : "0") +
             " blindR=" + std::to_string(m_config.blind_spot_radius_ratio));
    std::cout << "[bot] baglaniliyor -> HWND=" << hwnd << "  baslik=\"" << m_originalTitle << "\"  karakter=\""
               << m_characterName << "\"\n";

    {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        RECT wr = win_window::GetWindowRectOnScreen(hwnd);
        std::cout << "[bot] baglanilan pencere -> PID=" << pid << "  HWND=" << hwnd << "  sinirlar=("
                   << wr.left << "," << wr.top << ")-(" << wr.right << "," << wr.bottom << ")  boyut="
                   << (wr.right - wr.left) << "x" << (wr.bottom - wr.top) << "\n";
    }

    m_sessionStart = std::chrono::steady_clock::now();
    m_sessionStarted = true;
    // "HP cubugu hic gorulmedi" sayaci BASLANGICTAN itibaren islesin - ama
    // sifirdan degil SIMDIDEN, yoksa bot acilir acilmaz (henuz hicbir sey
    // yapmadan) kurtarma dizisini tetiklerdi.
    m_lastHpBarSeenAt = m_sessionStart;
    m_lastSearchPanelCheckAt = m_sessionStart;
}

std::optional<Detection> MetinBot::ClosestTo(
    const std::vector<Detection>& detections, cv::Point2d pos, double maxDist) {
    std::optional<Detection> best;
    double bestDist = maxDist;
    for (const auto& d : detections) {
        cv::Point2d c = d.Center();
        double dist = std::hypot(c.x - pos.x, c.y - pos.y);
        if (dist <= bestDist) {
            best = d;
            bestDist = dist;
        }
    }
    return best;
}

bool MetinBot::InFarmZone(cv::Point2d pos, cv::Point2d anchor) const {
    double dx = (pos.x - anchor.x) / RadiusPxX(m_config.farm_zone_radius_x_ratio);
    double dy = (pos.y - anchor.y) / RadiusPxY(m_config.farm_zone_radius_y_ratio);
    return (dx * dx + dy * dy) <= 1.0;
}

bool MetinBot::IsRejectedSpot(cv::Point2d pos, std::chrono::steady_clock::time_point now) const {
    // Cozunurlukten bagimsiz - bkz. config.h detection_stable_radius_ratio
    // yorumu. Y eksenini referans alan tek bir yaricap kullaniyoruz (mesafe
    // olcusu izotropik olmali, eliptik farm-alani gibi eksene gore ayri
    // olcekli olmamali).
    double radiusPx = RadiusPxY(m_config.reject_spot_radius_ratio);
    for (const auto& r : m_rejectedSpots) {
        double ageSec = std::chrono::duration<double>(now - r.rejectedAt).count();
        if (ageSec > m_config.reject_spot_expire_sec) continue;  // suresi dolmus, artik gecerli degil
        double dist = std::hypot(pos.x - r.pos.x, pos.y - r.pos.y);
        if (dist <= radiusPx) return true;
    }
    return false;
}

bool MetinBot::IsQueuedSpot(cv::Point2d pos, std::chrono::steady_clock::time_point now) const {
    double radiusPx = RadiusPxY(m_config.queue_spot_radius_ratio);
    for (const auto& q : m_queuedSpots) {
        double ageSec = std::chrono::duration<double>(now - q.queuedAt).count();
        if (ageSec > m_config.queue_spot_expire_sec) continue;
        double dist = std::hypot(pos.x - q.pos.x, pos.y - q.pos.y);
        if (dist <= radiusPx) return true;
    }
    return false;
}

void MetinBot::QueueOtherVisibleMetins(const Detection& primary) {
    auto frameOpt = m_capture->GetFrame();
    if (!frameOpt) {
        std::cout << "[bot] QueueOtherVisibleMetins: kare alinamadi, atlaniyor\n";
        return;
    }
    cv::Mat frame = *frameOpt;

    cv::Point2d anchor(frame.cols / 2.0, frame.rows / 2.0);
    auto now = std::chrono::steady_clock::now();
    std::vector<Detection> detections = DetectInFarmZone(frame, anchor, now);
    std::cout << "[bot] QueueOtherVisibleMetins: farm alaninda " << detections.size()
               << " tespit var (birincil dahil)\n";
    // TANI: bu fonksiyonun ciktilari eskiden SADECE std::cout'a gidiyordu ve
    // hicbir yere yazilmadigi icin "neden kuyruklamiyor" sorusu cevaplanamiyordu.
    LogDiag("QUEUE scan dets=" + std::to_string(detections.size()));

    // Suresi gecmis kuyruk kayitlarini temizle (m_rejectedSpots ile ayni desen).
    m_queuedSpots.erase(
        std::remove_if(m_queuedSpots.begin(), m_queuedSpots.end(),
                        [&](const QueuedSpot& q) {
                            return std::chrono::duration<double>(now - q.queuedAt).count() >
                                   m_config.queue_spot_expire_sec;
                        }),
        m_queuedSpots.end());

    // BIRINCIL HEDEFI DISLAMA (IKI ASAMALI - KRITIK): ClickBurst saniyeler
    // surebildigi icin (karakter hedefe yururken kamera/ekran konumu
    // kayabilir) birincil hedefin GUNCEL konumunu GENIS bir yaricapla
    // ariyoruz (1). Ama disleme (2) SIKI/DAR bir yaricapla yapilir - SADECE
    // ayni fiziksel nesnenin (ör. ORB + renk dedektorunden gelen yinelenen)
    // tespitlerini eler. GENIS yaricapla DISLESEYDIK, birbirine yakin
    // KUMELENMIS baska GERCEK metinler de (bu ozelligin asil fayda
    // saglayacagi tam senaryo) yanlislikla atlanirdi.
    // KRITIK DUZELTME: bu fonksiyon ClickBurst'ten (birkac saniye surebilir,
    // karakter hedefe dogru YURUR) SONRA, TAMAMEN YENI/BAGIMSIZ bir kare ile
    // calisiyor - "primary"nin ESKI (kilitlenme anindaki, kilitlenmeden
    // ONCEKI) konumuna (primary.Center()) gore eslestirme yapmak, karakter
    // hareket ettigi icin YENI karede AYNI metnin bile kaymis olmasina
    // (yanlislikla "farkli" sayilmasina) yol aciyordu. Karakter ARTIK
    // hedefin yanindadir - bu yuzden YENI karede ekran MERKEZINE (anchor,
    // Step()'teki "best" secimiyle AYNI mantik) en yakin tespit, guvenilir
    // sekilde birincil hedefin GUNCEL konumudur.
    auto primaryMatch = ClosestTo(detections, anchor, RadiusPxY(m_config.target_match_radius_ratio) * 2.5);

    // KRITIK DUZELTME (KANITA DAYALI - kullanicinin bildirdigi "ayni metne
    // tekrar tikliyor, kesim iptal oluyor, 45sn kilitlenip ESC ile ayarlar
    // acikta kaliyor" sorununun KOK SEBEBI): "ayni obje mi" karsilastirmasi
    // ONCEDEN d.Center() (renk-iyilestirmesiyle HER KAREDE YENIDEN
    // hesaplanan, kucuk isik/golge degisiklikleriyle bile birkac piksel
    // KAYABILEN nokta) kullaniyordu - bu nokta bir kareden digerine 45px'i
    // asacak kadar kayarsa, HALA AYNI fiziksel tas "farkli" sanilip ona
    // TEKRAR shift+sag-tik atiliyordu. Oyun sunucusunda ayni hedefe IKINCI
    // bir shift+sag-tik, o hedefin kesim/kilit islemini IPTAL EDIYOR -
    // panel gorunmeye devam ediyor (KESILMEDI, ama YANLIS HEDEF de degil)
    // ama ilerleme asla tamamlanmiyor, sonunda stuck_panel_timeout_sec
    // dolup ESC basiliyor. Artik karsilastirma, isim ETIKETI KUTUSUNUN
    // (matchTemplate ile eslesen, karadan kareye COK daha stabil) merkezini
    // kullaniyor.
    auto labelCenter = [](const Detection& det) { return cv::Point2d(det.x + det.w / 2.0, det.y + det.h / 2.0); };
    auto labelBox = [](const Detection& det) { return cv::Rect(det.x, det.y, det.w, det.h); };
    const Detection& primaryForBox = primaryMatch ? *primaryMatch : primary;
    cv::Point2d primaryExcludeCenter = labelCenter(primaryForBox);
    cv::Rect primaryLabelBox = labelBox(primaryForBox);
    // KANITA DAYALI EK SIKILASTIRMA: kullanici AYNI sorunun (ayni metne
    // ikinci kez shift+sag-tik atilmasi) DEVAM ETTIGINI bildirdi - tek basina
    // mesafe esigini (45 -> 60px) buyutmek yetmedi. Artik IKI BAGIMSIZ
    // kontrol birlikte kullaniliyor: (a) etiket kutulari HERHANGI bir
    // sekilde CAKISIYORSA (matchTemplate'in kendi bulduğu bolge -
    // gurultudan neredeyse tamamen bagimsiz, en guvenilir isaret) KESIN
    // ayni obje sayilir, (b) etiket merkezleri arasindaki mesafe daha da
    // genis bir payla (90px) kontrol edilir. Ikisinden BIRI bile "ayni"
    // derse dislenir - yanlislikla ayni hedefe ikinci kez tiklamaktansa,
    // nadiren gercekten yakin baska bir metni bir tur atlamak COK daha
    // az zararli (bir sonraki kilitlenmede zaten tekrar denenir).
    const double kSameObjectRadiusPx = 90.0;

    // GUVENLIK: yogun bir metin kumesinde tek seferde COK fazla shift+sag
    // tik atmak (sunucunun kuyruk derinligi sinirini asip ne olacagi
    // BILINMEYEN bir davranisa yol acabilir) yerine, bu cagrida en fazla
    // birkacini kuyruklayip GERI KALANINI dokunmadan birakiyoruz - onlar,
    // hala uygun (henuz kuyuklanmamis) oldugu surece BIR SONRAKI kilitlenme
    // doneminde (bir sonraki metin bitip yeni hedef kilitlenince) tekrar
    // denenir, kaybolmazlar.
    const int kMaxQueuedPerCall = 4;
    int queuedThisCall = 0;

    for (const auto& d : detections) {
        if (queuedThisCall >= kMaxQueuedPerCall) break;

        // GUVENLIK: ClickBurst saniyeler surebildigi icin, buraya gelene
        // kadar odak baska bir pencereye gecmis olabilir - Step() basindaki
        // kontrolle AYNI guvenlik agi (bkz. Step() ve ClickBurst).
        if (GetForegroundWindow() != m_hwnd) {
            if (m_input) m_input->ReleaseClip();
            std::cout << "[bot] QueueOtherVisibleMetins: odak kayboldu, iptal edildi\n";
            return;
        }

        cv::Point2d pos = d.Center();  // tiklama/kayit icin hala kesin (renk-iyilestirilmis) nokta kullanilir
        cv::Point2d thisLabelCenter = labelCenter(d);
        double distToPrimary = std::hypot(thisLabelCenter.x - primaryExcludeCenter.x, thisLabelCenter.y - primaryExcludeCenter.y);
        bool boxOverlapsPrimary = (labelBox(d) & primaryLabelBox).area() > 0;
        if (boxOverlapsPrimary || distToPrimary <= kSameObjectRadiusPx) {
            std::cout << "[bot] QueueOtherVisibleMetins: tespit (" << pos.x << "," << pos.y
                       << ") birincilin ayni objesi sayildi (kutu_cakisiyor=" << (boxOverlapsPrimary ? "EVET" : "hayir")
                       << ", mesafe=" << distToPrimary << "px <= " << kSameObjectRadiusPx << "px), atlandi\n";
            LogDiag("QUEUE skip-primary d=" + std::to_string(distToPrimary));
            continue;  // birincil hedefin (yinelenen) tespidi
        }
        if (IsQueuedSpot(pos, now)) {
            std::cout << "[bot] QueueOtherVisibleMetins: tespit (" << pos.x << "," << pos.y
                       << ") zaten kuyrukta, atlandi\n";
            LogDiag("QUEUE skip-already");
            continue;  // zaten kuyruklanmis, tekrar atarsak kuyruktan cikar
        }

        POINT screenPt = FrameToScreenPoint(pos, frame.cols, frame.rows);
        m_input->ShiftRightClick(screenPt.x, screenPt.y);
        std::cout << "[bot] kadrajdaki ek metin siraya eklendi (shift+sag tik): (" << screenPt.x << ","
                   << screenPt.y << ")\n";
        LogDiag("QUEUE clicked p=(" + std::to_string(screenPt.x) + "," + std::to_string(screenPt.y) + ")");
        m_queuedSpots.push_back({pos, now});
        queuedThisCall++;
    }
    if (queuedThisCall == 0) {
        std::cout << "[bot] QueueOtherVisibleMetins: bu cagrida hicbir ek metin kuyruklanmadi (toplam "
                   << detections.size() << " tespitten " << (detections.empty() ? 0 : detections.size() - 1)
                   << " tanesi birincil disi olabilirdi)\n";
    }

    // ----------------------- KAMERA TARAMASI ------------------------------
    // (kullanici talebi + KANITLANDI: QUEUE scan dets=0 loglarinin cogu, o an
    // kameranin bakmadigi yonde baska metin oldugu icindi - "bazen tiklamiyor"
    // sikayetinin kok sebebi buydu). Mevcut (donmeden) taramaya EK olarak
    // kamerayi KUCUK adimlarla cevirip HER acida da tarar.
    //
    // GUVENLI OLDUGUNUN KANITI: kesim ilerlemesi status_panel_detector ile
    // (ekranin sabit ust-orta seridinde duran bir HUD ogesi) izlenir - DUNYA
    // uzayinda DEGIL, ekran uzayinda sabittir. Yani kamera yonu ne olursa
    // olsun panel HER ZAMAN ayni yerde aranir; kamera cevirmek kesim
    // takibini/karari HICBIR SEKILDE etkilemez.
    //
    // Donen acilarda "kesilmekte olan hedefi disla" icin yukaridaki karmasik
    // etiket-eslesmesine GEREK YOK: kesilen hedef karakterin (kameranin
    // etrafinda dondugu pivot nokta) hemen yanindadir, kucuk donuslerde de
    // merkeze yakin kalir - DetectInFarmZone zaten KOR NOKTA ile onu otomatik
    // disliyor (bkz. bot.cpp DetectInFarmZone).
    if (m_config.queue_scan_rotate_enabled && queuedThisCall < kMaxQueuedPerCall && m_input) {
        int steps = std::max(0, m_config.queue_scan_rotate_steps);
        for (int i = 0; i < steps && queuedThisCall < kMaxQueuedPerCall; ++i) {
            if (GetForegroundWindow() != m_hwnd) {
                if (m_input) m_input->ReleaseClip();
                LogDiag("QUEUE rot-abort no-focus");
                return;
            }
            // KULLANICI TALEBI: "ekrani cok sacma ceviriyor, normal arama
            // modundaki cevirmeyle AYNI olmali". ESKIDEN burada yon
            // (i % 2) ile bir saga bir sola SALINIYORDU ve miktar da arama
            // modundan farkliydi (60-110 px yerine 150-300 px) - bu yuzden
            // kamera ileri-geri titriyor, "sacma" gorunuyordu. Artik hem YON
            // hem MIKTAR arama moduyla BIREBIR AYNI: oturumun tarama yonu
            // (m_lastSearchDir - bkz. SearchAction) korunur, yani kamera
            // duzgun bir sekilde AYNI yonde taramaya devam eder.
            if (m_lastSearchDir == 0) m_lastSearchDir = RandSign();
            int dir = m_lastSearchDir;
            double mag = RandRange(m_config.camera_rotate_px_min, m_config.camera_rotate_px_max);
            m_input->RotateView((int)(dir * mag));
            ClearDetectionMemory();  // kamera dondu - eski konumlar gecersiz
            SleepSeconds(0.18);  // kamera oturup kare taze gelsin

            auto fOpt2 = m_capture->GetFrame();
            if (!fOpt2) continue;
            cv::Mat f2 = *fOpt2;
            cv::Point2d anchor2(f2.cols / 2.0, f2.rows / 2.0);
            auto now2 = std::chrono::steady_clock::now();
            std::vector<Detection> dets2 = DetectInFarmZone(f2, anchor2, now2);
            LogDiag("QUEUE rot-scan step=" + std::to_string(i) + " dets=" + std::to_string(dets2.size()));

            for (const auto& d : dets2) {
                if (queuedThisCall >= kMaxQueuedPerCall) break;
                if (GetForegroundWindow() != m_hwnd) {
                    if (m_input) m_input->ReleaseClip();
                    return;
                }
                cv::Point2d pos = d.Center();
                if (IsQueuedSpot(pos, now2)) continue;
                POINT screenPt2 = FrameToScreenPoint(pos, f2.cols, f2.rows);
                m_input->ShiftRightClick(screenPt2.x, screenPt2.y);
                LogDiag("QUEUE rot-clicked p=(" + std::to_string(screenPt2.x) + "," +
                         std::to_string(screenPt2.y) + ") step=" + std::to_string(i));
                m_queuedSpots.push_back({pos, now2});
                queuedThisCall++;
            }
        }
    }
}

// ---------------------- MANUEL TARAMA ---------------------------------
// Metin modellerini ATLAR: kullanicinin kirpma araciyla kaydettigi gorselleri
// (manual_scan/ klasorundeki HEPSI) karede arar, en iyi eslesme esigi gecerse
// eslesmenin MERKEZINE SOL TIK atar.
//
// Hedef bir sure bulunamazsa kamerayi RASTGELE saga/sola cevirir - hep ayni
// aciyla donmek kamerayi ayni yere geri getirip taramayi anlamsizlastiriyordu
// (kullanici bildirimi), bu yuzden yon ve miktar her seferinde rastgeledir.
//
// TM_CCOEFF_NORMED kullaniliyor: sonucu 0..1 arasinda NORMALIZE eder, boylece
// esik (manual_scan_threshold) parlaklik/kontrast degisse bile anlamli kalir.
bool MetinBot::ManualScanStep(const cv::Mat& frame) {
    auto now = std::chrono::steady_clock::now();

    // ---- Sablonlari yukle / tazele -----------------------------------------
    // Klasordeki TUM gorseller sablondur. Imza (dosya adi+boyut+zaman) her
    // ~2 saniyede bir kontrol edilir; degistiyse yeniden yuklenir - boylece
    // kullanici panelden gorsel ekleyip silince bot yeniden baslatilmadan
    // yeni listeyle devam eder. Her karede diskten okumak gereksiz I/O olurdu.
    if (std::chrono::duration<double>(now - m_lastManualReloadCheckAt).count() > 2.0 || m_manualTemplates.empty()) {
        m_lastManualReloadCheckAt = now;
        std::error_code ec;
        std::string sig;
        std::vector<std::string> files;
        for (const auto& e : fs::directory_iterator(m_config.manual_scan_dir, ec)) {
            if (!e.is_regular_file()) continue;
            auto ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp") continue;
            files.push_back(e.path().string());
            sig += e.path().filename().string() + ":" + std::to_string((long long)fs::file_size(e.path(), ec)) + ";";
        }
        std::sort(files.begin(), files.end());
        if (sig != m_manualTemplatesSignature) {
            m_manualTemplatesSignature = sig;
            m_manualTemplates.clear();
            for (const auto& f : files) {
                cv::Mat img = cv::imread(f, cv::IMREAD_COLOR);
                if (img.empty()) continue;
                m_manualTemplates.push_back({fs::path(f).stem().string(), img});
            }
            LogDiag("MAN sablonlar-yuklendi adet=" + std::to_string(m_manualTemplates.size()) +
                     " klasor=" + m_config.manual_scan_dir);
        }
    }

    if (m_manualTemplates.empty()) {
        m_manualStatusText = "Manuel tarama: once gorsel ekleyin";
        return true;  // farm mantigina GIRME - kullanici once gorsel secmeli
    }

    // ---- TUM sablonlari ara, EN IYI eslesmeyi sec ---------------------------
    double maxVal = -1.0;
    cv::Point maxLoc;
    const ManualTemplate* best = nullptr;
    for (const auto& t : m_manualTemplates) {
        // Sablon kareden buyukse matchTemplate cagrilamaz (OpenCV hata verir).
        if (t.img.cols > frame.cols || t.img.rows > frame.rows) continue;
        cv::Mat result;
        cv::matchTemplate(frame, t.img, result, cv::TM_CCOEFF_NORMED);
        double v = 0.0;
        cv::Point loc;
        cv::minMaxLoc(result, nullptr, &v, nullptr, &loc);
        if (v > maxVal) { maxVal = v; maxLoc = loc; best = &t; }
    }
    if (!best) {
        m_manualStatusText = "Manuel tarama: gorseller kareden buyuk";
        return true;
    }
    if (maxVal < m_config.manual_scan_threshold) {
        m_manualStatusText = "Manuel tarama: aranıyor - " + std::to_string(m_manualTemplates.size()) +
                              " gorsel (en iyi skor " + std::to_string(maxVal).substr(0, 4) + ")";
        if (std::chrono::duration<double>(now - m_lastManualLogAt).count() > 3.0) {
            m_lastManualLogAt = now;
            LogDiag("MAN bulunamadi best=" + std::to_string(maxVal) +
                     " thr=" + std::to_string(m_config.manual_scan_threshold));
        }

        // ---- HEDEF YOKSA KAMERAYI CEVIR (kullanici talebi) ----------------
        // Bu kadar suredir hicbir hedef bulunamadiysa SADECE SAGA/SOLA cevirip
        // yeni bir alana bak. Yon ve miktar HER SEFERINDE RASTGELE: hep ayni
        // aciyla donmek ayni yere geri getirir ve tarama anlamsizlasir
        // (kullanici bildirimi) - rastgelelik hem yeni alan gorulmesini saglar
        // hem de hareketi ezberlenebilir olmaktan cikarir.
        if (m_manualNoHitSince.time_since_epoch().count() == 0) m_manualNoHitSince = now;
        if (std::chrono::duration<double>(now - m_manualNoHitSince).count() >= m_config.manual_scan_rotate_after_sec) {
            m_manualNoHitSince = now;
            int dir = RandSign();  // -1 sol / +1 sag - her defasinda rastgele
            int mag = (int)RandRange(m_config.camera_rotate_px_min, m_config.camera_rotate_px_max);
            if (m_input) m_input->RotateView(dir * mag);
            m_manualStatusText = "Manuel tarama: hedef yok - ekran ceviriliyor";
            LogDiag("MAN cevir yon=" + std::string(dir > 0 ? "sag" : "sol") + " px=" + std::to_string(mag));
        }
        return true;
    }
    // Hedef bulundu - "bulunamiyor" sayaci sifirlansin.
    m_manualNoHitSince = now;

    // Ayni hedefe saniyede onlarca kez tiklamayi engelle.
    if (std::chrono::duration<double>(now - m_lastManualClickAt).count() < m_config.manual_scan_click_interval_sec) {
        return true;
    }
    m_lastManualClickAt = now;

    // Eslesmenin MERKEZI + kullanicinin verdigi ofset.
    // Eslesmenin MERKEZINE tiklanir (ofset ayari kullanici talebiyle kaldirildi).
    cv::Point2d clickPt(maxLoc.x + best->img.cols / 2.0, maxLoc.y + best->img.rows / 2.0);
    clickPt.x = std::clamp(clickPt.x, 0.0, (double)frame.cols - 1);
    clickPt.y = std::clamp(clickPt.y, 0.0, (double)frame.rows - 1);

    POINT screenPt = FrameToScreenPoint(clickPt, frame.cols, frame.rows);
    if (m_input) m_input->Click(screenPt.x, screenPt.y, /*rightButton=*/false);  // HER ZAMAN sol tik
    ++m_totalClicks;

    m_manualStatusText = "Manuel tarama: '" + best->name + "' tiklandi (skor " + std::to_string(maxVal).substr(0, 4) + ")";
    LogDiag("MAN tiklandi gorsel=" + best->name + " best=" + std::to_string(maxVal) +
             " kare=(" + std::to_string((int)clickPt.x) + "," + std::to_string((int)clickPt.y) +
             ") ekran=(" + std::to_string(screenPt.x) + "," + std::to_string(screenPt.y) + ") SOL");
    return true;
}

POINT MetinBot::FrameToScreenPoint(cv::Point2d framePt, int frameW, int frameH) const {
    // KRITIK: yakalanan karenin piksel boyutu (frameW x frameH) ile
    // pencerenin GERCEK EKRAN boyutu (GetWindowRect) HER ZAMAN birebir
    // ayni olmayabilir (DPI/gorunmez yeniden-boyutlandirma kenarligi gibi
    // farklar yuzunden) - sadece bir ofset eklemek, pencereden uzak
    // noktalarda gittikce buyuyen bir kaymaya (sonunda masaustune
    // tiklamaya kadar) yol aciyordu. Bunun yerine ORANTILI OLCEKLEME
    // yapiyoruz: kare-uzayindaki her nokta, pencerenin gercek genislik/
    // yukseklik ORANINA gore ekran koordinatina cevriliyor.
    // DUZELTME (kullanicinin logundan OLCULDU - yukaridaki eski yorumun
    // tarif ettigi kayma tam olarak buradan geliyordu): GetWindowRect
    // GORUNMEZ yeniden-boyutlandirma kenarligini da icerdigi icin kare ile
    // BIREBIR ayni alani temsil ETMIYOR (olculen: pencere 816x639, kare
    // 802x632). Kareyi tam pencereye ORANTILAYARAK olceklemek, merkezde 0
    // ama kenarlarda ~7px buyuyen sistematik bir sapma uretiyordu.
    // GetCapturedFrameRectOnScreen, WGC'nin yakaladigi GORUNUR cerceveyi
    // dondurur - boyutlari kareyle AYNI oldugu icin donusum artik saf bir
    // 1:1 ofset; olcekleme kaynakli sapma TAMAMEN ortadan kalkiyor.
    RECT frameRect = win_window::GetCapturedFrameRectOnScreen(m_hwnd);
    int frameRectW = (int)(frameRect.right - frameRect.left);
    int frameRectH = (int)(frameRect.bottom - frameRect.top);

    // Beklenmedik bir uyusmazlik kalirsa (ör. DPI olcekleme) yine de dogru
    // davranalim: oran 1.0 cikacagi icin bu carpim normalde etkisizdir.
    double scaleX = (frameW > 0) ? (double)frameRectW / frameW : 1.0;
    double scaleY = (frameH > 0) ? (double)frameRectH / frameH : 1.0;

    int px = (int)frameRect.left + (int)std::lround(framePt.x * scaleX);
    int py = (int)frameRect.top + (int)std::lround(framePt.y * scaleY);

    // Asagidaki kelepceler/alt-UI korumasi ayni dikdortgen uzerinden islesin.
    RECT windowRect = frameRect;
    int windowH = frameRectH;

    // Guvenlik: hesaplanan nokta pencere sinirlarinin disina asla cikmasin.
    px = std::clamp(px, (int)windowRect.left, (int)windowRect.right - 1);
    py = std::clamp(py, (int)windowRect.top, (int)windowRect.bottom - 1);

    // MUTLAK GARANTI - ALTTAKI OYUNCU/YETENEK CUBUGUNA ASLA TIKLANMAZ
    // (kullanici talebi: "KESINLIKLE alttaki oyuncu alanina basmamali").
    // Kare-uzayindaki kelepceler (searchRegion kirpmasi + tespit sonrasi
    // duzeltme) yanlis/bayat bir kare boyutu, DPI farki ya da gelecekte
    // eklenecek YENI bir tespit yolu yuzunden atlanabilir. Burasi ise
    // metne yapilan TUM tiklamalarin (hem ClickBurst hem kuyruklama)
    // ISTISNASIZ gectigi TEK nokta - kelepceyi EN SONA, GERCEK EKRAN
    // koordinatina koyuyoruz ki hangi yoldan gelirse gelsin alt seride
    // dusmesi MATEMATIKSEL OLARAK imkansiz olsun.
    int excludeH = std::max(m_config.bottom_ui_exclude_px,
                             (int)std::lround(windowH * m_config.bottom_ui_exclude_ratio));
    int maxScreenY = (int)windowRect.bottom - excludeH - 4;
    if (maxScreenY > (int)windowRect.top && py > maxScreenY) py = maxScreenY;
    return POINT{px, py};
}

bool MetinBot::ClickBurst(const Detection& detection, int frameW, int frameH) {
    RECT windowRect = win_window::GetWindowRectOnScreen(m_hwnd);
    cv::Point2d center = detection.Center();
    POINT screenPt = FrameToScreenPoint(center, frameW, frameH);
    int cx = screenPt.x;
    int cy = screenPt.y;

    DWORD pid = 0;
    GetWindowThreadProcessId(m_hwnd, &pid);
    // client rect'i de (window rect'e EK olarak) logluyoruz - Windows
    // Graphics Capture'in GERCEKTE ne yakaladigini (tam pencere mi, sadece
    // istemci alani mi) FrameToScreenPoint'in kullandigi GetWindowRectOnScreen
    // ile KARSILASTIRMAK icin. Ikisi arasinda (baslik cubugu/kenarlik kadar)
    // fark varsa, bu tam olarak "tespit ettigi yerden uzaga tikliyor" hatasinin
    // KAYNAGI olabilir - kare istemci alanini temsil ediyorsa ama olcekleme
    // tam pencereye gore yapiliyorsa, merkezden uzaklastikca kayma buyur.
    // CLK anlami (koda bakan gelistirici icin): w=pencere-tam c=pencere-
    // istemci f=kare-boyutu t=tespit-noktasi p=ekran-tiklama-noktasi.
    RECT clientRect = win_window::GetClientRectOnScreen(m_hwnd);
    std::string dbgMsg = "CLK pid=" + std::to_string(pid) + " cat=" + detection.category +
                          " w=(" + std::to_string(windowRect.left) + "," + std::to_string(windowRect.top) +
                          ")-(" + std::to_string(windowRect.right) + "," + std::to_string(windowRect.bottom) + ")" +
                          " c=(" + std::to_string(clientRect.left) + "," + std::to_string(clientRect.top) +
                          ")-(" + std::to_string(clientRect.right) + "," + std::to_string(clientRect.bottom) + ")" +
                          " f=" + std::to_string(frameW) + "x" + std::to_string(frameH) +
                          " t=(" + std::to_string((int)center.x) + "," + std::to_string((int)center.y) + ")" +
                          " p=(" + std::to_string(cx) + "," + std::to_string(cy) + ")" +
                          " score=" + std::to_string(detection.score);
    LogClickDebug(dbgMsg);

    // NOT: Shift'i burst boyunca TEK SEFER basili tutmayi denedik ama bu
    // Shift'in SANIYELERCE kesintisiz basili kalmasina yol acip Windows'un
    // BASKA bir erisilebilirlik kisayolunu (Filter Keys - Shift'e ~8 saniye
    // basili tutma) tetikleyip cok daha agir bir kaosa (pencereler arasi
    // sicrama, fare kontrolsuz hareket) sebep oldu - o yontem GERI ALINDI.
    // Eski (guvenli, her tiklamada ayri basip birakma) davranisa donuldu.
    // Panel (isim) sablonu yukluyse, her tiklamadan sonra panelin cikip
    // cikmadigina bakiyoruz - CIKAR CIKMAZ burst'u hemen kesiyoruz (karakter
    // zaten hedefe yurumeye/vurmaya basladi, fazladan tiklamak gereksiz).
    // Sablon yoksa (canVerify=false) eski davranis: burst'un TAMAMI atilir
    // ve dogrulama yapilmadan basarili sayilir.
    bool canVerify = m_panelDetector.HasTemplate();
    bool panelConfirmed = false;
    // TANI/KORUMA (kritik bulgu - kullanicinin logundan KANITLANDI): golge
    // gibi zayif/marjinal eslesen kategorilerde panel HICBIR ZAMAN tam
    // esigi GECMIYOR, bu yuzden ClickBurst SUREKLI basarisiz donup bot HIC
    // kilitlenemeden (m_target hic set edilemeden) sonsuza kadar "tespit et
    // -> 3 kez tikla -> basarisiz -> kamera dondur -> yeni nokta" donguSunde
    // kalabiliyordu - DISARIDAN "bir metni keserken digerine atliyor" gibi
    // GORUNUYORDU ama aslinda HICBIR ZAMAN gercekten kilitlenmemisti. Step()
    // icindeki near-miss-uzatma korumalari SADECE zaten kilitliyken (m_target
    // set) devreye giriyordu, bu ILK kilitlenme kapisini KAPSAMIYORDU. Burada
    // da AYNI mantigin bir kopyasi: TUM burst boyunca gorulen EN YUKSEK skoru
    // takip edip, sert esik hic gecilmese bile skor esige COK YAKINSA (ayni
    // near-miss payi) yine de "yeterince dogru hedef" sayip kilitleniyoruz -
    // NIHAI dogrulama zaten cok daha guvenilir olan Step()'teki surekli
    // panel-izleme mantigina (kendi near-miss korumalariyla) devrediliyor.
    double bestScoreSeen = 0.0;

    for (int i = 0; i < m_config.click_burst_count && !panelConfirmed; ++i) {
        // KRITIK GUVENLIK: burst birden fazla saniye surebiliyor (10 tiklamaya
        // kadar + her tiklama sonrasi dogrulama beklemesi) - eger kullanici bu
        // sirada BASKA bir pencereye gecerse (alt-tab), fare KESINLIKLE bot
        // kontrolunde kalmamali. Her tiklamadan ONCE odagi tekrar kontrol
        // ediyoruz; kaybolduysa hapsi aninda kaldirip burst'u tamamen kesiyoruz.
        if (GetForegroundWindow() != m_hwnd) {
            if (m_input) m_input->ReleaseClip();
            LogForegroundMismatch(m_hwnd, m_originalTitle);
            return false;
        }

        int jitterY = 0;
        if (m_config.click_jitter_px > 0) {
            std::uniform_int_distribution<int> jd(-m_config.click_jitter_px, m_config.click_jitter_px);
            jitterY = jd(g_rng);
        }
        int clickY = std::clamp(cy + jitterY, (int)windowRect.top, (int)windowRect.bottom - 1);
        m_input->ShiftRightClick(cx, clickY);
        m_totalClicks++;

        if (canVerify) {
            // Panel bazen oyunun render gecikmesi yuzunden tiklamadan HEMEN
            // sonraki karede henuz gorunmeyebilir - kisa birkac kontrolle bu
            // gecikmeye tolerans taniyoruz (yeniden tiklamadan). Cikmazsa bir
            // sonraki tiklamaya gecilir - panel gorunene kadar (en fazla
            // click_burst_count kez) tiklamaya devam edilir.
            for (int chk = 0; chk < m_config.click_verify_checks; ++chk) {
                if (GetForegroundWindow() != m_hwnd) {
                    if (m_input) m_input->ReleaseClip();
                    std::cout << "[bot] odak kaybedildi - burst kesildi, fare serbest\n";
                    return false;
                }
                auto frameOpt = m_capture->GetFrame();
                if (frameOpt) {
                    auto reading = m_panelDetector.Read(*frameOpt);
                    if (reading.bestScore > bestScoreSeen) bestScoreSeen = reading.bestScore;
                    if (reading.visible) {
                        panelConfirmed = true;
                        std::cout << "[bot] panel cikti (" << (i + 1) << ". tiklamada) - burst erken durduruldu\n";
                        break;
                    }
                }
                SleepSeconds(m_config.click_verify_check_delay_sec);
            }
        }

        if (!panelConfirmed && i < m_config.click_burst_count - 1) {
            SleepSeconds(RandRange(m_config.click_burst_delay_sec_min, m_config.click_burst_delay_sec_max));
        }
    }

    bool softAccepted = false;
    if (canVerify && !panelConfirmed) {
        const double kBurstNearMissMargin = 0.10;
        if (bestScoreSeen >= (m_config.status_panel_name_match_threshold - kBurstNearMissMargin)) {
            softAccepted = true;
            std::cout << "[bot] panel sert esigi gecmedi ama en yuksek skor (" << bestScoreSeen
                       << ") esige (" << m_config.status_panel_name_match_threshold
                       << ") cok yakin - YINE DE kilitleniyoruz (nihai dogrulama Step()'te devam eder)\n";
            LogClickDebug("PNL burst-soft-accept best=" + std::to_string(bestScoreSeen) +
                           " thr=" + std::to_string(m_config.status_panel_name_match_threshold));
        } else {
            std::cout << "[bot] tiklama sonrasi panel hic cikmadi (en yuksek skor=" << bestScoreSeen
                       << ") - yanlis hedef, araniyor\n";
            LogClickDebug("PNL burst-fail cat=" + detection.category +
                           " best=" + std::to_string(bestScoreSeen) +
                           " thr=" + std::to_string(m_config.status_panel_name_match_threshold));
            // GORSEL KANIT (kullanicinin logunda TUM kategorilerde skor
            // anlamsizca dusuk cikiyordu - sayisal skor tek basina NEDEN
            // dusuk oldugunu (yanlis arama bolgesi mi, panel gercekten yok
            // mu, cozunurluk farki mi) gostermiyor). Basarisiz her burst'ten
            // sonra o anki TAM kareyi SABIT bir dosyaya kaydediyoruz (her
            // seferinde UZERINE yazilir, disk simsiz sismesin) - kullanici
            // bu dosyayi atinca panelin GERCEKTEN nerede/nasil goründugunu
            // (ya da hic gorunmedigini) gozle dogrulayabiliriz.
            auto debugFrameOpt = m_capture->GetFrame();
            if (debugFrameOpt) {
                cv::imwrite("panel_debug_last.png", *debugFrameOpt);
                LogClickDebug("PNL debug-frame-saved f=" + std::to_string(debugFrameOpt->cols) + "x" +
                               std::to_string(debugFrameOpt->rows));
            }
        }
    }
    return !canVerify || panelConfirmed || softAccepted;
}

void MetinBot::SearchAction() {
    // SADECE SAGA-SOLA kamera cevirme - yukari/asagi bakma HIC YOK.
    //
    // HEP AYNI YONE DONME (kullanici talebi): "1 kere sola 1 kere saga
    // cevirmek" yerine SUREKLI AYNI yone donulur. Sebep TAMAMEN pratik:
    // ileri-geri salinan bir kamera ZATEN BAKTIGI alani tekrar tekrar tarar
    // ve cevredeki metinlerin bir kismini HIC gormez. Tek yone donmek ise
    // karakterin cevresini duzenli bir TARAMA ile 360 derece kapatir - yani
    // her metin en gec bir tam turda gorus alanina girer. Bu, "metni daha
    // hizli bulma" acisindan olcülebilir sekilde daha iyidir.
    // Yon oturum basina BIR KEZ rastgele secilir (her bot ayni yone donup
    // birebir ayni desen olusturmasin diye), sonra DEGISMEZ.
    // NOT: her donusten SONRA (bkz. Step - m_nextSearchAt) 2-3sn beklenip
    // "metin var mi" taranir; yoksa bu fonksiyon tekrar cagrilip AYNI yone
    // biraz daha donulur - "bulana kadar".
    if (m_lastSearchDir == 0) {
        m_lastSearchDir = RandSign();  // oturumun tarama yonu - bir kez secilir
    }
    int dir = m_lastSearchDir;
    m_searchSameDirCount++;

    // Miktar da rastgele (ezber disi) - her seferinde farkli genislikte tarar.
    int mag = (int)RandRange(m_config.camera_rotate_px_min, m_config.camera_rotate_px_max);
    m_input->RotateView(dir * mag);
    // Kamera dondu -> onceki karelerden hatirlanan tespitlerin konumlari
    // ARTIK GECERSIZ (bkz. bot.h m_detMemory).
    ClearDetectionMemory();
}

// OTO NESNE (SADE): metin kesilir kesilmez 2 saniye boyunca YERINDE kalip
// Z'yi hizli hizli spamlar - karakter HIC HAREKET ETMEZ.
int MetinBot::RunAutoLoot() {
    if (!m_input) return 0;
    int lootPresses = 0;
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    while (std::chrono::steady_clock::now() < until) {
        if (GetForegroundWindow() != m_hwnd) break;  // odak gitti - hemen birak
        m_input->PressLootKey();
        ++lootPresses;
        SleepSeconds(0.04);
    }
    std::cout << "[bot] Oto Nesne: 2sn boyunca " << lootPresses << " kez Z basildi\n";
    LogDiag("LOOT simple n=" + std::to_string(lootPresses));
    return lootPresses;
}

void MetinBot::AntiStuckRecover(bool pressEsc) {
    if (!m_input) return;
    std::cout << "[bot] ANTI-TAKILMA: karakter takilmis gorunuyor - kurtarma dizisi basliyor\n";
    LogDiag("STUCK anti-stuck-start");

    // Odagi KESIN al - sadece kontrol edip cikma (Activate gercekten one
    // getirir). Alinamazsa hicbir sey yapmadan cik (yanlis pencereye
    // input gitmesin).
    Activate();
    if (GetForegroundWindow() != m_hwnd) {
        LogDiag("STUCK anti-stuck-abort no-focus");
        return;
    }

    RECT wr = win_window::GetWindowRectOnScreen(m_hwnd);
    int winW = (int)(wr.right - wr.left);
    int cx = (int)(wr.left + wr.right) / 2;
    int cy = (int)(wr.top + wr.bottom) / 2;

    // 1) ESC - secili metni birak, HP cubugu kapansin (kullanici bilgisi:
    // anti-takilma ANINDA metin KESINLIKLE secili -2.5dk HP cubugu-, ESC onu
    // birakip karakteri o ulasilamayan metne yurumekten kurtarir; ESC
    // basmazsak HP cubugu kapanmaz ve bot yine takili kalir).
    // pressEsc=false ise (ikinci tetikleyici - hic HP cubugu gorulmedi,
    // yani ZATEN kilitli bir hedef yok) bu adim TAMAMEN atlanir - birakilacak
    // bir secim olmadigi icin ESC'nin burada hicbir anlami/etkisi yok.
    if (pressEsc && GetForegroundWindow() == m_hwnd) {
        m_input->PressEscape();
        SleepSeconds(0.3);
    }

    // 2) YON TUSLARI - sikisilan yerden FIZIKSEL olarak kurtul.
    //
    // KULLANICI GERI BILDIRIMI DUZELTMESI: sira ESKIDEN W, S, D, A idi. W'den
    // hemen SONRA S basmak karakteri gittigi yoldan AYNEN geri getiriyor -
    // yani 4-5 saniyelik W tamamen bosa gidiyor ve karakter TAM OLARAK
    // takildigi noktaya donuyordu ("w s yaparak zaten takildigi yere geri
    // donuyor"). Artik sira W, A, D, S: W'den sonra ONCE YANLARA aciliyoruz,
    // yanal hareket gercek bir yer degistirme saglar; S en sona birakildi ki
    // calistiginda artik BASKA bir noktadan geri cekilme olsun. Sureler de
    // 4sn'den 2sn'ye indi (bkz. config.h anti_stuck_move_seconds).
    const char kDirs[] = {'W', 'A', 'D', 'S'};
    for (char dir : kDirs) {
        if (GetForegroundWindow() != m_hwnd) break;
        m_input->HoldMoveKey(dir, m_config.anti_stuck_move_seconds);
        SleepSeconds(0.1);
    }

    // 3) ORTA ALANA RASTGELE SOL TIKLAR (kullanici talebi): kurtarma
    // hareketinden SONRA, ekranin ORTA bolgesine 3-4 rastgele sol tik at.
    // Amac: takilmayi bozacak/oyunu "uyandiracak" bir etkilesim yaratmak ve
    // hemen onunde duran bir hedefi yeniden secilebilir kilmak. KESINLIKLE
    // orta alan: pencerenin ortasindan +-%15'lik bir kutu - boylece ne
    // alttaki arayuz cubuguna ne de kenarlardaki menulere denk gelmez.
    if (GetForegroundWindow() == m_hwnd) {
        int winH = (int)(wr.bottom - wr.top);
        int spreadX = std::max(20, (int)(winW * 0.15));
        int spreadY = std::max(20, (int)(winH * 0.15));
        int clicks = (RandRange(0.0, 1.0) < 0.5) ? 3 : 4;
        for (int i = 0; i < clicks; ++i) {
            if (GetForegroundWindow() != m_hwnd) break;
            int px = cx + (int)std::lround(RandRange(-spreadX, spreadX));
            int py = cy + (int)std::lround(RandRange(-spreadY, spreadY));
            m_input->Click(px, py, /*rightButton=*/false);
            SleepSeconds(RandRange(0.12, 0.25));
        }
        LogDiag("STUCK center-clicks n=" + std::to_string(clicks));
    }

    // 4) AZICIK KAMERA CEVIRME (kullanici talebi). ESKIDEN bu adim dizinin
    // BASINDA ve 4 x %55 genislikte idi - yani neredeyse TAM TUR donduruyor,
    // uzun suruyor ve karakterin bakis acisini tamamen kaybettiriyordu.
    // Artik: kurtarma hareketlerinin SONUNDA, TEK ve KUCUK bir cevirme
    // (pencere genisliginin ~%15'i) - amac gorusu tazeleyip onceden
    // gorunmeyen bir hedefi/acikligi ortaya cikarmak, tur atmak degil.
    if (GetForegroundWindow() == m_hwnd) {
        m_input->PlaceCursor(cx, cy);
        SleepSeconds(0.05);
        m_input->RotateView((int)std::lround(m_config.recover_small_rotate_px));
        ClearDetectionMemory();
        SleepSeconds(0.2);
        LogDiag("STUCK small-rotate");
    }

    // 5) TEMIZLIK: hicbir fare tusu/Shift/yon tusu TAKILI KALMASIN - "sonra
    // mouse tiklamiyor" durumunu kesin onler.
    m_input->ReleaseAllButtonsAndKeys();

    std::cout << "[bot] ANTI-TAKILMA: kurtarma dizisi tamamlandi\n";
    LogDiag("STUCK anti-stuck-done");
}

// ============================== A-T PRO ====================================
// Bkz. config.h at_pro_* yorumlari.
// ===========================================================================

std::optional<MetinBot::AtProSample> MetinBot::AtProSampleTarget() {
    if (!m_target || !m_capture) return std::nullopt;
    auto frameOpt = m_capture->GetFrame();
    if (!frameOpt) return std::nullopt;
    const cv::Mat& frame = *frameOpt;

    cv::Point2d anchor(frame.cols / 2.0, frame.rows / 2.0);  // MERKEZ NOKTA = karakter
    auto now = std::chrono::steady_clock::now();
    std::vector<Detection> detections = DetectInFarmZone(frame, anchor, now);
    // Kilitli hedefi bu karede yeniden bul - konumu her karede birkac piksel
    // oynadigi icin "en yakin eslesme" mantigi (Step ile AYNI yarical).
    auto match = ClosestTo(detections, m_target->pos, RadiusPxY(m_config.target_match_radius_ratio));
    if (!match) return std::nullopt;

    AtProSample s;
    cv::Point2d c = match->Center();  // HEDEF NOKTA
    s.dist = cv::norm(c - anchor);
    s.boxArea = (double)match->w * (double)match->h;
    s.boxH = (double)match->h;
    s.frameH = (double)frame.rows;
    return s;
}

bool MetinBot::AtProRecover() {
    if (!m_input || !m_target) return false;
    std::cout << "[bot] A-T PRO: hedefe yaklasma yok - kurtarma basliyor\n";
    LogDiag("ATPRO recover-start");

    // Kurtarma boyunca "iyilesme oldu mu" karsilastirmasinin TABANI - giriste
    // BIR KEZ sabitlenir (kurtarma sirasinda guncellenmez), boylece sadece
    // GERCEK bir yaklasma cikisi tetikler.
    double baseDist = m_target->atProBaseDist;
    double baseH = m_target->atProBaseBoxH;

    // Yaklasma basladi mi? true donerse kurtarma ANINDA kesilir.
    // BIRINCIL sinyal Step() ile AYNI: kutu yuksekligi (h ~ 1/d).
    auto improved = [&]() -> bool {
        auto s = AtProSampleTarget();
        if (!s) return false;  // hedef bu karede gorunmuyor - karar verme, devam
        bool bigger = (baseH > 0.0) && (s->boxH > baseH + RadiusPxY(m_config.at_pro_min_box_growth_ratio));
        bool closer = (baseDist > 0.0) && (s->dist < baseDist - RadiusPxY(m_config.at_pro_min_progress_ratio));
        if (bigger || closer) {
            // Tabani guncelle - Step() sayaci buradan temiz devam etsin.
            m_target->atProBaseBoxH = s->boxH;
            m_target->atProBaseDist = s->dist;
            return true;
        }
        return false;
    };

    // ---- FAZ 1: SIRAYLA bir sol bir sag (A/D) basimlari -----------------
    // MoveKeyDown/Up (BLOKLAMAYAN) kullaniyoruz - HoldMoveKey blokladigi icin
    // tus basiliyken yaklasma yoklamasi YAPILAMAZDI ve "iyilesince aninda kes"
    // sarti tutulamazdi.
    //
    // KULLANICI BILDIRIMI ("A-T Pro bazen sorunlu calisiyor") DUZELTMESI:
    // yon ESKIDEN her hamlede %50-%50 RASTGELE seciliyordu. Rastgele secim
    // ayni yonu ust uste birkac kez secebilir (ör. A,A,A,A) - bu durumda
    // karakter hic SALINMAZ, tek yone bastirip ayni engele yaslanmaya devam
    // eder ve kurtarma bosa gider. "Bir sag bir sol" GARANTISI ancak KESIN
    // SIRAYLA saglanir: artik yon her hamlede ZORUNLU olarak ters cevriliyor,
    // yani A,D,A,D... Baslangic tarafi rastgele (ezber/robotik durmasin) ama
    // sonrasi tamamen deterministik - engelin hangi tarafinda sikisilmis
    // olursa olsun her iki yon de garantili ve esit sayida denenir.
    auto phase1End = std::chrono::steady_clock::now() +
                      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                          std::chrono::duration<double>(m_config.at_pro_phase1_sec));
    char key = (RandRange(0.0, 1.0) < 0.5) ? 'A' : 'D';  // sadece BASLANGIC tarafi rastgele
    int atProMoveIndex = 0;
    while (std::chrono::steady_clock::now() < phase1End) {
        if (GetForegroundWindow() != m_hwnd) break;
        if (atProMoveIndex > 0) key = (key == 'A') ? 'D' : 'A';  // KESIN SIRAYLA ters yon
        ++atProMoveIndex;
        LogDiag(std::string("ATPRO phase1 move=") + key + " n=" + std::to_string(atProMoveIndex));
        m_input->MoveKeyDown(key);
        auto holdEnd = std::chrono::steady_clock::now() +
                        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                            std::chrono::duration<double>(m_config.at_pro_key_hold_sec));
        bool aborted = false;
        while (std::chrono::steady_clock::now() < holdEnd) {
            if (GetForegroundWindow() != m_hwnd) { aborted = true; break; }
            SleepSeconds(0.12);
            if (improved()) { aborted = true; break; }
        }
        m_input->MoveKeyUp(key);
        if (aborted) {
            // Odak kaybi ya da YAKLASMA - ikisinde de faz 2'ye GECMIYORUZ.
            if (GetForegroundWindow() == m_hwnd) {
                std::cout << "[bot] A-T PRO: yaklasma basladi - kurtarma iptal (faz 1)\n";
                LogDiag("ATPRO abort-phase1-improved");
            }
            return false;
        }
    }

    // ---- FAZ 2: ESC -> kamera -> merkeze tiklar ------------------------
    if (GetForegroundWindow() != m_hwnd) return false;
    std::cout << "[bot] A-T PRO: faz 1 sonuc vermedi - faz 2 (ESC + kamera + merkez tik)\n";
    LogDiag("ATPRO phase2");

    RECT wr = win_window::GetWindowRectOnScreen(m_hwnd);
    int winW = (int)(wr.right - wr.left);
    int winH = (int)(wr.bottom - wr.top);
    int cx = (int)(wr.left + wr.right) / 2;
    int cy = (int)(wr.top + wr.bottom) / 2;

    m_input->PressEscape();  // hedefi birak (HP cubugu kapansin)
    SleepSeconds(0.3);

    if (GetForegroundWindow() == m_hwnd) {
        m_input->PlaceCursor(cx, cy);
        SleepSeconds(0.05);
        m_input->RotateView((int)std::lround(m_config.recover_rotate_px));
        ClearDetectionMemory();
        SleepSeconds(0.25);
    }

    // MERKEZ NOKTAYA 2-3 sol tik (kullanici talebi) - ekranin tam ortasina,
    // cok kucuk bir sacilmayla (ust uste birebir ayni piksel robotik durur).
    if (GetForegroundWindow() == m_hwnd) {
        int spread = std::max(6, (int)(std::min(winW, winH) * 0.02));
        int clicks = (RandRange(0.0, 1.0) < 0.5) ? 2 : 3;
        for (int i = 0; i < clicks; ++i) {
            if (GetForegroundWindow() != m_hwnd) break;
            int px = cx + (int)std::lround(RandRange(-spread, spread));
            int py = cy + (int)std::lround(RandRange(-spread, spread));
            m_input->Click(px, py, /*rightButton=*/false);
            SleepSeconds(RandRange(0.12, 0.22));
        }
        LogDiag("ATPRO center-clicks n=" + std::to_string(clicks));
    }

    m_input->ReleaseAllButtonsAndKeys();
    std::cout << "[bot] A-T PRO: kurtarma tamamlandi - arama moduna donuluyor\n";
    LogDiag("ATPRO recover-done");
    return true;  // hedef birakilmali
}

void MetinBot::CheckAndDismissNesneMarket(const cv::Mat& frame) {
    if (!m_config.nesne_market_dismiss_enabled || !m_input) return;

    auto now = std::chrono::steady_clock::now();
    // PERIYODIK/DUSUK MALIYETLI: her Step()'te DEGIL, sadece
    // check_interval_sec'te bir - zaten yakalanmis kareyi kullanir, EK bir
    // capture YAPMAZ.
    if (std::chrono::duration<double>(now - m_lastNesneMarketCheckAt).count() <
        m_config.nesne_market_check_interval_sec) {
        return;
    }
    m_lastNesneMarketCheckAt = now;

    // ESC SPAM KORUMASI: az once basildiysa banner'in kapanma animasyonuna
    // zaman tanimadan tekrar basmayalim.
    if (std::chrono::duration<double>(now - m_lastNesneMarketEscAt).count() <
        m_config.nesne_market_esc_cooldown_sec) {
        return;
    }

    // Sablonu TEMBEL yukle.
    if (!m_nesneMarketTemplateLoaded) {
        m_nesneMarketTemplateLoaded = true;
        cv::Mat raw = cv::imread(m_config.nesne_market_template_path, cv::IMREAD_UNCHANGED);
        if (!raw.empty()) {
            if (raw.channels() == 4) {
                std::vector<cv::Mat> ch;
                cv::split(raw, ch);
                m_nesneMarketTemplateMask = ch[3];
                cv::merge(std::vector<cv::Mat>{ch[0], ch[1], ch[2]}, m_nesneMarketTemplateBgr);
            } else {
                m_nesneMarketTemplateBgr = raw;
                m_nesneMarketTemplateMask = cv::Mat(raw.size(), CV_8UC1, cv::Scalar(255));
            }
            std::cout << "[bot] NESNE MARKET: sablon yuklendi (" << m_config.nesne_market_template_path << ", "
                       << m_nesneMarketTemplateBgr.cols << "x" << m_nesneMarketTemplateBgr.rows << ")\n";
        } else {
            std::cout << "[bot] NESNE MARKET: UYARI - sablon yuklenemedi (" << m_config.nesne_market_template_path
                       << ")\n";
        }
    }
    if (m_nesneMarketTemplateBgr.empty()) return;

    // ARAMA BOLGESI - OTONOM (bkz. status_panel_detector.cpp'deki AYNI
    // duzeltme). ESKIDEN bolge ORAN olarak saklanip kare boyutuyla
    // carpiliyordu; bu, arayuzun pencereyle ORANTILI buyudugu VARSAYIMINA
    // dayaniyordu ve bu varsayim OLCUMLE YANLIS bulundu (arayuz SABIT PIKSEL
    // boyutunda). Ornek: bolge 802x632'de y=133..153 iken ayni oranlar
    // 1602x1049'da y=221..254'u gosterir - banner orada DEGILDIR, yani
    // farkli cozunurlukte tespit CALISMAZDI.
    //
    // Artik oran YOK: banner yatayda ORTALI ve ustten sabit piksel mesafede
    // oldugu icin, ust-orta bolgede PIKSEL olcusunde genis bir bant taranir
    // ve sablon o bant icinde GERCEKTEN ARANIR. Hangi cozunurluk olursa
    // olsun kendiliginden dogru yeri bulur - kullanicinin bolge kalibrasyonu
    // yapmasina GEREK YOKTUR.
    // Bant olcusu PIKSEL (arayuz sabit boyutlu). Genis tutmak maliyeti
    // dogrudan artirdigi icin (bkz. asagidaki iki kademeli arama notu)
    // banner'in gercekten cikabilecegi alani rahatca kapsayan, ama gereksiz
    // buyuk olmayan bir kutu: yatayda ortali 700px, ustten 320px.
    const int nmBandW = std::min(frame.cols, 700);
    cv::Rect searchArea((frame.cols - nmBandW) / 2, 0, nmBandW, std::min(frame.rows, 320));
    searchArea &= cv::Rect(0, 0, frame.cols, frame.rows);
    if (searchArea.width < 8 || searchArea.height < 8) return;
    cv::Mat searchMat = frame(searchArea);

    // Kucuk cok-olcekli tarama - banner UI ogesi oldugu icin genelde sabit
    // boyutta olur ama farkli pencere boyutlarina karsi yine de birkac olcek
    // denenir.
    //
    // KRITIK DUZELTME (KANITLANDI - kullanicinin canli debug_preview
    // olcumu): TM_CCORR_NORMED ORTALAMA PARLAKLIGI CIKARMAZ, bu yuzden banner
    // hic EKRANDA YOKKEN bile bos arka plan sablona %95 "benziyor" cikiyordu
    // (ayni koyu/UI tonu yeterli) - banner GORUNURKEN ise %100. Aradaki fark
    // (0.95 - 1.00) o kadar dar ki HICBIR esik guvenilir ayrim yapamiyordu,
    // bu da "devamli ESC basiyor" sikayetinin KOK SEBEBIYDI. TM_CCOEFF_NORMED
    // (ORTALAMA-CIKARMALI normalize korelasyon) bunun yerine YAPISAL farka
    // bakar - bos arka plan ile gercek banner arasinda COK daha net ayrisir.
    // NOT: sablon BGR (alfasiz) kaydedildigi icin maske zaten TAMAMEN opak -
    // TM_CCOEFF_NORMED maske PARAMETRESI ALMADIGI icin (OpenCV kisitlamasi)
    // burada maskeyi hic GECMIYORUZ - islevsel bir kayip YOK (maske zaten
    // hicbir pikseli devre disi birakmiyordu).
    // IKI KADEMELI ARAMA (PERFORMANS - KRITIK). OLCULDU: bir ara burada 9
    // olcek 1200x500'luk bir bantta, 3 KANALLI goruntude taraniyordu ve
    // 1920x1080'de cagri basina 403 ms suruyordu. Kontrol 1.5 saniyede bir
    // calistigi icin bu, botun zamaninin %27'sini TEK BASINA yiyordu
    // ("inanilmaz yavas" sikayetinin en buyuk sebebi).
    //
    // Cozum: banner ekranda neredeyse HER ZAMAN YOKTUR, yani cagrilarin
    // %99'u "yok" cevabi verir. Bu yuzden once TEK olcekte (1.0 - arayuz
    // sabit boyutlu oldugu icin BEKLENEN olcek budur) UCUZ bir bakis
    // yapiyoruz. Skor esige yaklasmadiysa (banner yok) hemen cikiyoruz.
    // Sadece "olabilir" durumunda tum olcek kumesi ile DOGRULUYORUZ -
    // boylece hassasiyet AYNEN korunur, maliyet ise ~9 kat duser.
    double bestScore = -1.0;
    cv::Rect bestBox;
    const double kProbeThreshold = m_config.nesne_market_match_threshold - 0.08;
    static const double kProbeScale[] = {1.0};
    static const double kFullScales[] = {0.9, 0.95, 1.0, 1.05, 1.15};
    const double* scales = kProbeScale;
    size_t scaleCount = 1;
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            // 1. gecis esige YAKLASMADIYSA banner kesinlikle yok - dur.
            if (bestScore < kProbeThreshold) break;
            scales = kFullScales;
            scaleCount = sizeof(kFullScales) / sizeof(kFullScales[0]);
            bestScore = -1.0;  // tam kume ile yeniden olc
        }
        for (size_t si = 0; si < scaleCount; ++si) {
            double sc = scales[si];
            cv::Mat tmpl;
            cv::resize(m_nesneMarketTemplateBgr, tmpl, cv::Size(), sc, sc, cv::INTER_LINEAR);
            if (tmpl.cols < 4 || tmpl.rows < 4 || tmpl.cols >= searchMat.cols ||
                tmpl.rows >= searchMat.rows) {
                continue;
            }
            cv::Mat result;
            cv::matchTemplate(searchMat, tmpl, result, cv::TM_CCOEFF_NORMED);
            double minV, maxV;
            cv::Point minLoc, maxLoc;
            cv::minMaxLoc(result, &minV, &maxV, &minLoc, &maxLoc);
            if (maxV > bestScore) {
                bestScore = maxV;
                // Bolgeye GORELI konumu KARE-uzayina geri cevir (sadece TANI
                // logu icin - tiklama yapilmiyor, kritik degil ama dogru olsun).
                bestBox = cv::Rect(searchArea.x + maxLoc.x, searchArea.y + maxLoc.y, tmpl.cols, tmpl.rows);
            }
        }
    }  // iki kademeli arama dongusu

    if (bestScore < m_config.nesne_market_match_threshold) return;  // gorunmuyor - sessizce cik

    LogDiag("NESNE_MARKET dismiss score=" + std::to_string(bestScore) + " box=(" +
             std::to_string(bestBox.x) + "," + std::to_string(bestBox.y) + ")");
    std::cout << "[bot] NESNE MARKET: bildirim goruldu (skor=" << bestScore << "), ESC basiliyor\n";
    m_input->PressEscape();
    m_lastNesneMarketEscAt = now;
}

std::vector<Detection> MetinBot::DetectInFarmZone(const cv::Mat& frame, cv::Point2d anchor,
                                                   std::chrono::steady_clock::time_point now) const {
    // KRITIK DUZELTME (cozunurlukten bagimsizlik): asagida InFarmZone/
    // IsRejectedSpot cagrilirken kullanacagi onbellegi, ELIMIZDEKI TAZE
    // kareyle guncelliyoruz - boylece o fonksiyonlar (kare parametresi
    // ALMADIKLARI icin) her zaman DOGRU/guncel kare boyutuna gore
    // hesaplama yapar, asla bayat bir onceki cozunurlukten kalma deger
    // kullanmaz.
    UpdateFrameSize(frame);

    // KRITIK PERFORMANS: sadece farm alaninin dikdortgen sinirlarinda ara -
    // tum karede degil. Bu, tespit maliyetini cok buyuk oranda azaltir.
    // Yaricaplar kare boyutunun ORANI olarak tutulur (bkz. config.h
    // detection_stable_radius_ratio yorumu) - hangi cozunurlukte calisirsa
    // calissin AYNI GORSEL alani kapsar.
    double zoneRx = RadiusPxX(m_config.farm_zone_radius_x_ratio);
    double zoneRy = RadiusPxY(m_config.farm_zone_radius_y_ratio);
    cv::Rect searchRegion(
        (int)std::lround(anchor.x - zoneRx),
        (int)std::lround(anchor.y - zoneRy),
        (int)std::lround(zoneRx * 2),
        (int)std::lround(zoneRy * 2));

    // KRITIK GUVENLIK: ekranin EN ALTINDAKI sabit oyun arayuzu (yetenek/
    // envanter kisayol cubugu) HICBIR ZAMAN arama alanina dahil edilmez -
    // kamera ne yone donerse donsun bu cubuk hep ayni ekran konumunda
    // durur, bu yuzden bir sablon yanlislikla ona benzerse (ör. renk/doku
    // rastlantisi) bot SURPRE surekli aynı yanlis noktaya (arayuz
    // dugmelerine) tiklayip hem gercek bir metin bulamaz HEM DE istenmeyen
    // oyun eylemlerini (yetenek/esya kullanma) tetikleyebilirdi.
    // OLCEK DUZELTMESI: sabit piksel yerine "sabit px VEYA kare yuksekliginin
    // orani - hangisi BUYUKSE" (bkz. config.h bottom_ui_exclude_ratio) - buyuk
    // pencerelerde arayuz cubugu da buyudugu icin sabit 130px yetmiyordu.
    int uiExcludeHeight = std::max(m_config.bottom_ui_exclude_px,
                                    (int)std::lround(frame.rows * m_config.bottom_ui_exclude_ratio));
    int uiExcludeBottom = frame.rows - uiExcludeHeight;
    if (searchRegion.y + searchRegion.height > uiExcludeBottom) {
        searchRegion.height = std::max(0, uiExcludeBottom - searchRegion.y);
    }

    // GORSEL TABANLI ARAMA (ORB ozellik eslestirme + RANSAC dogrulama, ve
    // genis/tum-ekran renk taramasi) ARTIK KULLANILMIYOR - isim etiketi
    // (billboard yazi) yontemi acidan bagimsiz ve kanitlanmis sekilde
    // guvenilir oldugu icin (bkz. kullanicinin canli testi), sistemi
    // bosuna yormamak adina TAMAMEN KALDIRILDI. Tespit artik SADECE isim
    // etiketi + yazinin hemen altindaki dar/hedefli renk merkezi
    // iyilestirmesinden olusuyor. NOT: bir metin turu icin henuz '_name.png'
    // YAKALANMADIYSA bu tur artik HICBIR SEY BULAMAZ (sessiz degil - asagida
    // ARADA BIR loglanir) - yeni bir tur eklenirse template_picker.exe 'n'
    // modu ile once isim etiketi alinmali.
    // yolo_only_mode (TANI/TEST) acikken bu cagri ATLANIR - tespit havuzu
    // SADECE YOLO'dan gelsin diye, eski sistemin etkisi olmadan izole test.
    std::vector<Detection> allDetections =
        m_config.yolo_only_mode ? std::vector<Detection>{} : m_nameLabelDetector.Detect(frame, searchRegion);

    // YOLO TABANLI TESPIT (varsa): isim-etiketi tespitiyle AYNI havuza,
    // EK bir aday kaynagi olarak katilir - hicbir mevcut yontemi
    // DEGISTIRMEZ/KALDIRMAZ, sadece uzerine ekler. config.h yolo_models
    // bossa (henuz egitilmis bir model yoksa) bu dongu hic calismaz.
    // SADECE kullanicinin "Hedef Sec" listesinde ISARETLEDIGI kategoriler
    // (m_config.templates_dirs) calisir - config.json'da tanimli TUM
    // modeller degil. Eskiden TUMU her zaman aktifti (kullanici "golge"yi
    // hic secmese bile bot golge metnine de tiklardi) - bu KAFA KARISTIRICI
    // ve BEKLENMEDIK bir davranisti, kullaniciyla konusulup DUZELTILDI.
    // ============ TUM METIN MODELLERI BIRLESIK (kullanici talebi) ============
    // "Secim falan yok, baslat deyince TUM metin onnx'leri birlessin -
    // ortalamayi yukseltelim." Eskiden yalnizca "Hedef Sec" listesinde
    // isaretli kategoriler calisiyordu; artik HEPSI kullanilir, yani bot
    // ekrandaki HER metin turunu hedef alabilir.
    //
    // AMA: her modelin maliyeti OLCULDU - model basina ~120-135 ms, 7 model
    // HER KAREDE calistirilirsa 861 ms/kare eder. Bu, botu yeni duzelttigimiz
    // yavaslıktan da beter hale getirirdi. Cozum: modeller DONUSUMLU
    // (round-robin) calistirilir - her karede SADECE BIRI. Metinler sabit
    // duran nesneler oldugu icin bu bir kayip degildir: kamera sabitken
    // birkac kare icinde (7 model x ~125ms ~= 0.9 sn) TUM turler taranmis
    // olur, maliyet ise tek model kadar kalir.
    //
    // ISTISNA - KILITLI HEDEF: bir hedefe kilitliyken onun kategorisi HER
    // KAREDE calistirilir; aksi halde A-T Pro'nun izlemesi (bkz.
    // AtProSampleTarget) hedefi kaybederdi.
    {
        std::vector<const YoloDetector*> ready;
        for (const auto& y : m_yoloDetectors) {
            if (y.IsReady()) ready.push_back(&y);
        }
        if (!ready.empty()) {
            const YoloDetector* locked = nullptr;
            if (m_target && !m_target->category.empty()) {
                for (auto* y : ready) {
                    if (y->GetCategory() == m_target->category) {
                        locked = y;
                        break;
                    }
                }
            }
            // TUM MODELLER HER KAREDE (config: yolo_all_models_every_frame).
            // Kullanici eskiden "Hedef Sec"te HEPSINI isaretliyordu ve o
            // zaman TUM modeller her karede calisiyordu - tespit tazeligi en
            // yuksek haldi. Bu secenek o davranisi aynen geri getirir; bedeli
            // OLCULDU: 7 model = 861 ms/kare.
            if (m_config.yolo_all_models_every_frame) {
                // PARALEL CALISTIRMA: her YoloDetector'in KENDI ayri Ort::Session'i
                // var (birbirinden BAGIMSIZ) - sirayla calistirmanin (8 model x
                // ~125ms = ~1sn/kare) hicbir GEREGI yok, ayni anda thread'lerde
                // calistirilinca toplam sure TEK bir modelin suresine iner
                // (~125ms/kare) HICBIR kategori atlanmadan/hafizaya guvenmeden.
                std::vector<std::future<std::vector<Detection>>> futures;
                futures.reserve(ready.size());
                for (auto* y : ready) {
                    futures.push_back(std::async(std::launch::async, [y, &frame, &searchRegion]() {
                        return y->Detect(frame, searchRegion);
                    }));
                }
                for (auto& f : futures) {
                    auto d = f.get();
                    allDetections.insert(allDetections.end(), d.begin(), d.end());
                }
                // Tum kategoriler bu karede tarandi - kamera serbest.
                m_scannedSinceRotate.clear();
                for (auto* y : ready) m_scannedSinceRotate.push_back(y->GetCategory());
                // NOT: burada RETURN YOK - kor nokta/alt arayuz filtreleri
                // asagida CALISMAYA devam etmeli.
            } else {
            const YoloDetector* pick =
                locked ? locked : ready[m_yoloRoundRobin++ % ready.size()];
            auto yoloDets = pick->Detect(frame, searchRegion);

            // Bu kategori, son kamera hareketinden bu yana TARANDI sayilir -
            // bot HEPSI taranmadan "burada metin yok" deyip kamerayi
            // cevirmemeli (bkz. bot.h AllCategoriesScanned).
            if (std::find(m_scannedSinceRotate.begin(), m_scannedSinceRotate.end(), pick->GetCategory()) ==
                m_scannedSinceRotate.end()) {
                m_scannedSinceRotate.push_back(pick->GetCategory());
            }

            // TESPIT HAFIZASI (bkz. bot.h m_detMemory): bu karede taranan
            // kategorinin sonucu hafizaya yazilir, sonra son kamera
            // hareketinden bu yana taranmis TUM kategorilerin sonuclari
            // BIRLESTIRILIP dondurulur. Boylece "bu karede tespit yok" karari
            // sadece TEK bir modele degil, o ana kadar goruilen HEPSINE
            // dayanir - bot tum modelleri ayni anda calistirmis gibi davranir.
            auto nowMem = std::chrono::steady_clock::now();
            bool replaced = false;
            for (auto& e : m_detMemory) {
                if (e.category == pick->GetCategory()) {
                    e.dets = yoloDets;
                    e.at = nowMem;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) m_detMemory.push_back({pick->GetCategory(), yoloDets, nowMem});

            // Bayat girisleri at - karakter/kamera fark edilmeden kaymis
            // olabilir, eski konumlara tiklamak istemeyiz.
            const double kMemoryTtlSec = 2.5;
            m_detMemory.erase(std::remove_if(m_detMemory.begin(), m_detMemory.end(),
                                              [&](const CategoryDets& e) {
                                                  return std::chrono::duration<double>(nowMem - e.at).count() >
                                                         kMemoryTtlSec;
                                              }),
                               m_detMemory.end());

            for (const auto& e : m_detMemory) {
                allDetections.insert(allDetections.end(), e.dets.begin(), e.dets.end());
            }
            }
        }
    }

    // UST-BILGI PANELI (hedef kilitliyken ekranin ust-orta seridinde beliren
    // "Lv.X (Y.Seviye) <isim>" yazisi) isim-etiketi sablonuyla YANLISLIKLA
    // eslesebiliyor - CUNKU AYNI metni (ör. "Dovus Metini") ayni fontla
    // gosteriyor. Bu GERCEK bir tas DEGIL (tiklanacak bir nesne degil), bu
    // yuzden panel SU AN GORUNUYORSA, onun TAM konumuyla (matchRect, kucuk
    // bir payla) CAKISAN her isim-etiketi tespitini burada ELIYORUZ - boylece
    // bot yanlislikla kendi UI panelinin uzerine tiklamaz. NOT: sadece panel
    // GERCEKTEN gorunurken (dinamik) devreye girer - sabit bir ust-ekran
    // seridini HER ZAMAN dislemek, o bolgede GERCEKTEN duran bir metni de
    // korten kaybettirebilirdi.
    if (m_panelDetector.HasTemplate()) {
        // 1) DINAMIK/KESIN disleme: panel SU AN eslesme esigini gectiyse,
        // TAM konumuyla (matchRect) cakisan etiketleri eliyoruz.
        auto panelReading = m_panelDetector.Read(frame);
        if (panelReading.visible) {
            cv::Rect excludeRect = panelReading.matchRect;
            const int kPanelMargin = 12;
            excludeRect.x -= kPanelMargin;
            excludeRect.y -= kPanelMargin;
            excludeRect.width += kPanelMargin * 2;
            excludeRect.height += kPanelMargin * 2;
            allDetections.erase(
                std::remove_if(allDetections.begin(), allDetections.end(),
                                [&](const Detection& d) {
                                    cv::Rect box(d.x, d.y, d.w, d.h);
                                    return (box & excludeRect).area() > 0;
                                }),
                allDetections.end());
        }

        // 2) GEOMETRIK/KOSULSUZ disleme (KANITA DAYALI EK GUVENLIK - kullanici
        // "bazen panele de tikliyor" dedi, yani (1)'deki esik-bagimli eslesme
        // ARADA BIR kacinilmaz sekilde marjinal/gec kaliyor olabilir - ör.
        // panel HENUZ TAM oturmamisken bir kare, ya da eslesme skoru esigin
        // az altindayken). Panel HER ZAMAN ekranin ayni SABIT ust-orta
        // seridinde cikar (bkz. status_panel_detector.cpp Read() - ust %25,
        // orta %70) - bu yuzden panelin GUNCEL "gorunur" durumundan TAMAMEN
        // BAGIMSIZ olarak, o sabit seritte kalan HER etiketi de eliyoruz.
        // Gercek bir metnin ismi bu rezerve UI seridinde tekrar tekrar
        // cikmasi pratikte imkansiza yakin oldugu icin bu tradeoff guvenli.
        // KRITIK DUZELTME (kullanici "metin tespitleri zayif" dedi): bu
        // GEOMETRIK serit eleme SADECE isim-etiketi (YAZI) tespitleri icin
        // anlamli - panel de YAZI oldugu icin karisiyordu. YOLO ise metnin
        // GOVDESINI tespit eder (yazi degil); uzaktaki bir metin ekranda
        // DAHA YUKARIDA gorundugu icin bu seride dusebilir - onceden boyle
        // GERCEK metinler SESSIZCE ATILIYORDU. Panel yazidir, YOLO onu metin
        // sanmaz; bu yuzden YOLO tespitlerini bu seritten ELEMIYORUZ (sadece
        // isim-etiketi tespitlerini). Panelin GERCEK konumuna tiklamaya karsi
        // koruma zaten yukarida DINAMIK (matchRect) elemede var.
        int stripW = (int)(frame.cols * 0.7);
        int stripH = (int)(frame.rows * 0.25);
        int stripX = (frame.cols - stripW) / 2;
        cv::Rect panelStrip(stripX, 0, stripW, stripH);
        allDetections.erase(
            std::remove_if(allDetections.begin(), allDetections.end(),
                            [&](const Detection& d) {
                                if (d.name == "yolo") return false;  // YOLO metinleri KORUNUR
                                cv::Rect box(d.x, d.y, d.w, d.h);
                                return (box & panelStrip).area() > 0;
                            }),
            allDetections.end());
    }

    if (!m_nameLabelDetector.HasTemplates()) {
        static std::chrono::steady_clock::time_point lastNoTemplateLog{};
        auto nowLog = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(nowLog - lastNoTemplateLog).count() > 5.0) {
            lastNoTemplateLog = nowLog;
            std::cout << "[bot] UYARI: hicbir metin turu icin isim etiketi (_name.png) yuklenmemis - "
                       << "gorsel tabanli (ORB/renk) arama kaldirildigi icin HICBIR SEY tespit edilmeyecek. "
                       << "template_picker.exe ile 'n' modunu kullanarak isim etiketini yakalayin.\n";
        }
    }

    // YEREL RENK IYILESTIRMESI: tas SEKLI duzensiz oldugu icin yazinin
    // hemen altina SABIT bir oran kadar tiklamak (name_label_click_offset_factor)
    // her tasta guvenilir degil - bkz. kullanicinin isaretledigi ekran
    // goruntusu (4 Dovus + 1 Savas Metni, hepsi farkli sekilli). Bunun
    // yerine, yazinin HEMEN ALTINDAKI DAR/hedefli bir pencerede, o metin
    // turunun (sablonlardan otomatik cikarilmis) HSV renk aralik
    // profiliyle eslesen EN BUYUK lekeyi arayip MERKEZINE tikliyoruz - bu,
    // tasin GERCEK/duzensiz seklini SABIT orandan cok daha iyi yansitir.
    // Renk bulunamazsa (ör. cok karanlik/gorunmez), eski sabit oran NOKTASI
    // FALLBACK olarak zaten precisePoint icinde duruyor - dokunmuyoruz.
    for (auto& d : allDetections) {
        // YOLO tespitleri BU ADIMI ATLAR - kutu zaten TASIN/CANAVARIN
        // KENDISI (isim etiketi degil), Detection::Center() varsayilan
        // olarak zaten kutunun geometrik merkezini dondurur, bu dogru
        // tiklama noktasidir. Asagidaki pencere, "d etiketin KENDISI"
        // varsayimiyla calisiyor - YOLO tespitlerine uygulanirsa kendi
        // kutusunu maskeden cikarip anlamsiz/gereksiz bir arama yapardi.
        if (d.name == "yolo") continue;
        // Pencere, tasin YAZI GENISLIGINDEN daha genis/uzun olabilecegini
        // hesaba katacak kadar buyuk olmali (yazi genelde tasin tamaminin
        // genisligini/yuksekligini KAPSAMAZ) - ama hala "dar/hedefli" (tum
        // farm alani degil, sadece bu tasin hemen civari).
        // KRITIK DUZELTME: pencere ONCEDEN sadece yazinin ALTINDAN
        // basliyordu - ama tas HER ZAMAN yazinin altinda DEGIL (kamera
        // acisina/arazi egimine gore bazen yazinin USTUNDE/yaninda kalir,
        // kullanicinin canli testinde GORULDU). O durumlarda pencere tasi
        // HIC gormedigi icin renk merkezi bulunamiyor, sabit-oran
        // fallback'ine dusup ISKALIYORDU. Artik pencere yazinin ETRAFINI
        // (ustunu de) kapsiyor; yazinin KENDISI ise maskeden aciken
        // cikariliyor (bkz. FindColorCenterIn excludeRect).
        // BOYUT (TANI DOKUMUNE DAYALI, IKI YONLU DENGE): once d.w*2.2 /
        // d.h*8 idi ve pencere yazinin ALTINDAN basliyordu - tas ustte/
        // yanda kalinca disarida kaliyordu (dump_3/dump_5). Sonra
        // 3.4/10.0'a cikarildi, ama bu sefer pencere IKI FARKLI ZEMIN
        // DOKUSUNU birden kapsayip devasa sahte lekelere yol acti
        // (dump_0/dump_1). Denge: pencere yazinin MERKEZINE gore
        // ortalanmis ve tasi rahatca icine alacak kadar buyuk, ama zemin
        // gecislerini toplayacak kadar buyuk DEGIL.
        int windowW = (int)std::lround(d.w * 2.4);
        int windowH = (int)std::lround(d.h * 6.0);
        cv::Rect labelRect(d.x, d.y, d.w, d.h);
        cv::Rect localWindow(
            (int)std::lround(d.x + d.w / 2.0 - windowW / 2.0),
            (int)std::lround(d.y + d.h / 2.0 - windowH / 2.0),  // yazinin MERKEZINE gore ortala
            windowW, windowH);
        auto colorCenter = m_colorDetector.FindColorCenterIn(frame, localWindow, labelRect);
        if (!colorCenter) {
            // ILK PENCERE BOS DONDUYSE (ör. tas beklenenden daha uzakta/
            // yazidan daha uzun mesafede kalmis), pes etmeden bir kez daha,
            // %50 DAHA GENIS bir pencereyle dene - "tek atista bul" hedefi
            // icin, dogrudan zayif sabit-oran fallback'ine dusmeden once
            // ucuz bir ikinci sans. kMaxAreaRatio (bkz. auto_color_detector.cpp)
            // zaten pencere boyutuna GORE ORANSAL oldugu icin, daha genis
            // pencerede de zemin/dag gibi sahte buyuk lekelere karsi ayni
            // korumayi korur.
            int wideW = (int)std::lround(windowW * 1.5);
            int wideH = (int)std::lround(windowH * 1.5);
            cv::Rect wideWindow(
                (int)std::lround(d.x + d.w / 2.0 - wideW / 2.0),
                (int)std::lround(d.y + d.h / 2.0 - wideH / 2.0),
                wideW, wideH);
            colorCenter = m_colorDetector.FindColorCenterIn(frame, wideWindow, labelRect);
        }
        if (colorCenter) {
            d.hasPrecisePoint = true;
            d.precisePoint = *colorCenter;
        }

    }

    // KRITIK GUVENLIK (KANITA DAYALI): kullanici, tiklamanin bazen pencerenin
    // EN ALTINDAKI sabit arayuz seridine (yetenek/envanter cubugu) dustugunu
    // bildirdi. searchRegion zaten yukarida KESILIYOR, ama nihai tiklama
    // noktasi baska yollarla (yerel renk merkezi, sabit-oran fallback) o
    // sinirin altina sarkabiliyor.
    //
    // KRITIK DUZELTME: bu kelepce ONCEDEN yukaridaki renk-iyilestirme
    // dongusunun ICINDEYDI - o dongu ise EN BASINDA "YOLO ise atla" diyor.
    // yolo_only_mode ACIK oldugu icin (varsayilan) PRATIKTE TUM tespitler
    // YOLO'dur, yani kelepce GERCEKTE HIC CALISMIYORDU. Artik AYRI bir
    // donguda, YOLO dahil ISTISNASIZ HER tespite uygulaniyor.
    {
        double maxClickY = frame.rows - std::max(m_config.bottom_ui_exclude_px,
                                                  (int)std::lround(frame.rows * m_config.bottom_ui_exclude_ratio)) -
                            4.0;
        for (auto& d : allDetections) {
            cv::Point2d finalCenter = d.Center();
            if (finalCenter.y > maxClickY) {
                d.hasPrecisePoint = true;
                d.precisePoint = cv::Point2d(finalCenter.x, maxClickY);
            }
        }
    }

    // KOR NOKTA (bkz. config.h blind_spot_radius_ratio): merkez noktanin
    // (= karakterin) hemen etrafindaki kucuk cember. GERCEK tiklama noktasina
    // gore olculur (Center() degil ClickPoint benzeri) - precisePoint varsa o
    // kullanilir, cunku yasaklanan sey TIKLANACAK yerdir.
    const double blindR = frame.rows * m_config.blind_spot_radius_ratio;
    const double blindR2 = blindR * blindR;
    // Cemberin merkezi ekran ortasinin biraz ALTI (bkz. config.h
    // blind_spot_offset_y_ratio) - karakterin govdesi asagi dogru uzanir.
    const cv::Point2d blindCenter(anchor.x, anchor.y + frame.rows * m_config.blind_spot_offset_y_ratio);
    auto inBlindSpot = [&](const Detection& d) {
        cv::Point2d p = d.hasPrecisePoint ? d.precisePoint : d.Center();
        double dx = p.x - blindCenter.x;
        double dy = p.y - blindCenter.y;
        return (dx * dx + dy * dy) <= blindR2;
    };

    std::vector<Detection> detections;
    detections.reserve(allDetections.size());
    for (auto& d : allDetections) {
        if (!InFarmZone(d.Center(), anchor)) continue;
        // Daha once burst'un TAMAMI bosa gitmis (panel hic cikmamis) bir
        // bolgeyse, aktif hedef aramasi icin ADAY BILE SAYMA - aksi halde
        // bot ayni yanlis tasa/daga sonsuz dongude tiklayip TAKILI kalir.
        if (IsRejectedSpot(d.Center(), now)) continue;
        // KOR NOKTA: karakterin kendi uzerine tiklama.
        if (inBlindSpot(d)) continue;
        // MINIK KUTU ELEMESI (kullanici gozlemi - debug_preview ciktisiyla
        // SAYISAL OLARAK DOGRULANDI): YOLO bazen hareketli hayvanlari
        // (kedi/kopek) metin saniyor. Ayirt edici ozellik BOYUT: ayni karede
        // olculen kutu yukseklikleri GERCEK metinlerde h=36,38,43,57,62,71;
        // HATALI tespitlerde h=23,23,23. Gercek metinler belirli bir
        // buyuklugun altina inmiyor, hatalilar belirgin sekilde MINIK.
        // Esik, kare yuksekliginin ORANI olarak tutulur - metinler 3B DUNYA
        // icerigidir ve cozunurlukle ORANTILI olceklenir (arayuz ogelerinin
        // aksine, bkz. status_panel_detector.cpp). O karede frame.rows=828
        // idi: h=23 -> 0.028, h=32 -> 0.039; varsayilan 0.032 TAM ARALARINDA.
        if (d.h < frame.rows * m_config.min_detection_height_ratio) {
            LogDiag("DET reddedildi minik h=" + std::to_string(d.h) + " esik=" +
                     std::to_string((int)(frame.rows * m_config.min_detection_height_ratio)) + " kat=" + d.category);
            continue;
        }
        detections.push_back(d);
    }
    return detections;
}

bool MetinBot::Step() {
    // KRITIK: oyun penceresi SU AN AKTIF (odakli/on planda) degilse HICBIR
    // SEY yapma - fareye/klavyeye HIC dokunma. Yakalama (capture) pencere
    // odakli olmasa bile calisir (WGC'nin ozelligi budur), ama TIKLAMA/
    // KAMERA HAREKETI oyle degil - kullanici baska bir pencerede
    // calisirken bot onun farisine/klavyesine asla karismamali. Pencere
    // tekrar odaklaninca bot otomatik kaldigi yerden devam eder.
    if (GetForegroundWindow() != m_hwnd) {
        // Odak baskasindaysa hapsi de aninda kaldir - fare kullanicinin
        // gercekte calistigi pencereye/ekrana serbestce gidebilmeli.
        if (m_input) m_input->ReleaseClip();
        // TANI: bu bot "aktif" sayildigi halde penceresi gercekten on
        // planda degilse (ör. Windows foreground gecisini reddettiyse),
        // bunu ARADA BIR (bogmadan) loglayip sorunu teshis edilebilir kilar.
        auto nowLog = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(nowLog - m_lastNotForegroundLog).count() > 3.0) {
            m_lastNotForegroundLog = nowLog;
            std::cout << "[bot] TANI: pencere (HWND=" << m_hwnd << ") su an foreground DEGIL (gercek foreground="
                       << GetForegroundWindow() << ") - bu client bekliyor\n";
            LogDiag("STEP not-foreground hwnd=" + std::to_string((long long)(intptr_t)m_hwnd) +
                     " actual=" + std::to_string((long long)(intptr_t)GetForegroundWindow()));
        }
        return false;
    }

    // KRITIK: fareyi Windows'un kendi ClipCursor API'siyle DOGRUDAN OYUN
    // PENCERESINE hapsediyoruz - matematiksel tahmine degil isletim
    // sisteminin garantisine dayanir, bu yuzden mouse "tuhaf bir yoldan"
    // bile pencere disina cikamaz. Her Step()'te tazelenir ki pencere
    // tasinirsa/boyut degistirirse hapis alani da guncel kalsin.
    if (m_input) m_input->UpdateClipToWindow();

    // Yere dusen esyalari KACIRMAMAK icin, bu client foreground/aktifken
    // SUREKLI (loot_key_interval_sec araliklarla) Z tusuna basiyoruz - hedef
    // kilitli olsun olmasin. Fonksiyonun EN BASINDA (herhangi bir erken
    // "return false" bu adimi ATLAMASIN diye) ve odak/hapis kurulumundan
    // HEMEN SONRA yapiliyor.
    {
        auto nowLoot = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(nowLoot - m_lastLootKeyAt).count() >= m_config.loot_key_interval_sec) {
            m_lastLootKeyAt = nowLoot;
            if (m_input) m_input->PressLootKey();
        }
    }

    auto frameOpt = m_capture->GetFrame();
    if (!frameOpt) {
        // TANI: "hicbir sey olmuyor" sikayetinin EN OLASI kok nedeni budur -
        // Windows Graphics Capture bazi sistemlerde (RDP/uzak masaustu, bazi
        // eski GPU surucu kombinasyonlari) istisna FIRLATMADAN baslar ama
        // HICBIR ZAMAN kare uretmez, bu yuzden bot sessizce hicbir sey
        // yapmadan sonsuza dek doner. m_capture'in kendi ic sayaclarindan
        // (bkz. capture.h) NEDEN kare gelmedigini (hic gelmedi mi, gercek bir
        // GPU/surucu hatasi mi) buraya tasiyoruz.
        auto nowCap = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(nowCap - m_lastCaptureFailLog).count() > 3.0) {
            m_lastCaptureFailLog = nowCap;
            double sinceLast = m_capture->SecondsSinceLastFrame();
            LogDiag("STEP no-frame hwnd=" + std::to_string((long long)(intptr_t)m_hwnd) +
                     " sinceLastFrame=" + std::to_string(sinceLast) +
                     " failCount=" + std::to_string(m_capture->FailCount()) +
                     " lastFailReason=" + m_capture->LastFailReason());
            std::cout << "[bot] TANI: capture kare vermiyor (son basarili kareden bu yana "
                       << sinceLast << " sn, hataSayaci=" << m_capture->FailCount()
                       << ", sonHataNedeni=" << m_capture->LastFailReason() << ")\n";
        }
        return false;
    }
    cv::Mat frame = *frameOpt;
    UpdateFrameSize(frame);

    // NESNE MARKET KAPAT (kullanici talebi): bildirim seridi gorulurse ESC
    // basar. Kendi ICINDE periyodik seyreltilir (config.h
    // nesne_market_check_interval_sec) - burada HER Step()'te cagirmak
    // GUVENLIDIR, ekstra maliyet yaratmaz.
    CheckAndDismissNesneMarket(frame);

    // ---- MANUEL TARAMA (kullanici talebi) --------------------------------
    // Acikken metin modelleri (YOLO) HIC calistirilmaz - sadece kullanicinin
    // sectigi gorsel aranir ve ofsetli noktaya tiklanir.
    if (m_config.manual_scan_enabled) {
        return ManualScanStep(frame);
    }

    cv::Point2d anchor(frame.cols / 2.0, frame.rows / 2.0);  // karakter = ekranin merkezi
    auto now = std::chrono::steady_clock::now();
    std::vector<Detection> detections = DetectInFarmZone(frame, anchor, now);

    if (m_target) {
        auto match = ClosestTo(detections, m_target->pos, RadiusPxY(m_config.target_match_radius_ratio));
        if (match) {
            m_target->pos = match->Center();
            m_target->lastSeen = now;
            m_lastDetection = match;
        }

        // CIFT DOGRULAMA: ust bilgi panelindeki ISIM yazisina bakarak dogru
        // hedefe kilitli miyiz ve metin GERCEKTEN kesildi mi anla - sabit
        // 40 saniyelik beklemeye artik gerek yok. Isim gorunuyorsa dogru
        // hedefteyiz (hala kesiliyor), kayboldugunda kesilmis demektir.
        auto panel = m_panelDetector.Read(frame);
        double waitSec = std::chrono::duration<double>(now - m_target->lockedAt).count();

        // KANITLA KAPATILDI (kullanicinin logundan OLCULDU - kesin sayisal
        // veri): coklu-olcek + histogram-esitleme guclendirmesinden SONRA,
        // GERCEK panel eslesmeleri ~0.93 skor veriyor, arka plan/doku
        // GURULTUSU ise en fazla ~0.48-0.50'de kaliyor - aralarinda DEVASA
        // bir bosluk var, mevcut sert esik (0.55) bu bosluga TAM oturuyor
        // ve tek basina KUSURSUZ ayirt ediyor. Daha once burada denenen
        // "kilitliyken daha genis tolerans" yamalari (once 0.20, sonra 0.08
        // pay) TAM DA bu gurultu tavanina (~0.48-0.50) denk dusup gurultuyu
        // "hala goruyorum" sanip botun SONSUZA KADAR (asla aramaya gecmeden)
        // kilitli kalmasina sebep oluyordu - kullanicinin ekran goruntusuyle
        // KANITLADIGI "Hedefte Kilitli" yazip hicbir hedef olmadan donup
        // durma hatasi TAM OLARAK buydu. Artik hicbir ek tolerans YOK - panel
        // dogrudan kendi (StatusPanelDetector::Read icindeki) sert esigine
        // gore "goruluyor/gorulmuyor" sayiliyor. Marjinal/sinirda durumlar
        // icin ZATEN asagida ayrica near-miss-extend (TEK SEFERLIK, SINIRLI
        // sureli) korumasi var - o SINIRLI oldugu icin sonsuz kilitlenmeye
        // yol acamaz, buradaki KALICI tolerans gibi degil.
        bool stillVisible = panel.visible;

        // IKINCI ANTI-TAKILMA TETIKLEYICISI icin: HP cubugunu HER gordugumuzde
        // "en son gorulme" zamanini tazeliyoruz (bkz. config.h
        // anti_stuck_no_hp_seconds ve asagidaki arama modu kontrolu).
        if (panel.visible) m_lastHpBarSeenAt = now;

        // ------------------- SUREKLI KUYRUKLAMA (kesim sirasinda) ---------
        // (kullanici talebi) Metin kesilirken bile kadraji taramaya devam et:
        // gorunen DIGER metinlere TEK bir shift+sag-tik atarak oyunun kendi
        // hedef kuyruguna ekle. Boylece bu metin bitince karakter sirdaki
        // metne KENDILIGINDEN yonelir, bot bos beklemez.
        //
        // "Ayni metne ikinci kez tiklamama" (ki bu sunucuda kuyruktan CIKARIR)
        // iki katmanla garanti altinda: (1) hali hazirda kesilen metin ekran
        // merkezindedir ve KOR NOKTA onu tespit havuzundan cikarir,
        // (2) m_queuedSpots bir kez tiklanan noktayi hatirlar (IsQueuedSpot).
        if (m_config.queue_other_visible_metins) {
            double sinceScan =
                std::chrono::duration<double>(now - m_lastQueueScanAt).count();
            if (sinceScan >= m_config.queue_scan_interval_sec) {
                m_lastQueueScanAt = now;
                // Birincil olarak o anki eslesmeyi (yoksa son tespiti) veriyoruz -
                // QueueOtherVisibleMetins bunu "zaten kesilen metin" olarak eler.
                if (match) {
                    QueueOtherVisibleMetins(*match);
                } else if (m_lastDetection) {
                    QueueOtherVisibleMetins(*m_lastDetection);
                }
            }
        }

        // ---------------------------- A-T PRO ----------------------------
        // Bkz. config.h at_pro_* - MESAFE tabanli, saniyeler icinde tepki veren
        // takilma tespiti. NOT: bu sunucuda HP cubugu metne TIKLANIR TIKLANMAZ
        // (karakter daha YOLDAYKEN) aciliyor, bu yuzden "vardik mi" sorusunun
        // cevabi panel DEGIL, tespit KUTUSUNUN BOYUTUDUR (karakter yaklastikca
        // metin ekranda buyur).
        if (m_config.at_pro_enabled && !m_target->atProArrived) {
            double dist = cv::norm(m_target->pos - anchor);
            // O anki kutuyu (varsa) bu karenin tespitinden al - Step zaten
            // detections'i hesapladi, ekstra kare/YOLO maliyeti YOK.
            double boxH = match ? (double)match->h : 0.0;

            // "VARDIK": kutu kare yuksekliginin belli bir oranini gectiyse
            // karakter metne ulasmis ve VURUYOR demektir - mesafenin artik
            // azalmamasi NORMALDIR. A-T Pro bu hedef icin tamamen susar.
            if (boxH > 0.0 && boxH >= frame.rows * m_config.at_pro_arrived_box_ratio) {
                m_target->atProArrived = true;
                LogDiag("ATPRO arrived boxH=" + std::to_string(boxH));
            } else if (boxH <= 0.0) {
                // Hedef bu karede GORUNMUYOR (tespit kacti). Bu "ilerleme yok"
                // KANITI DEGIL - bilgi yoklugu. Sayaci ilerletmemek icin taban
                // zamanini tazeliyoruz, aksi halde birkac kare tespit kaybi
                // yanlislikla takilma sayilirdi.
                m_target->atProLastProgressAt = now;
            } else {
                if (m_target->atProBaseDist < 0.0) {
                    // Hedef yeni kilitlendi - ilk olcum taban degeri olur.
                    m_target->atProBaseDist = dist;
                    m_target->atProBaseBoxH = boxH;
                    m_target->atProLastProgressAt = now;
                } else {
                    // BIRINCIL: kutu yuksekligi buyudu mu (h ~ 1/d - yaklasma).
                    bool bigger = boxH > m_target->atProBaseBoxH + RadiusPxY(m_config.at_pro_min_box_growth_ratio);
                    // IKINCIL: ekran mesafesi anlamli olcude azaldi mi.
                    bool closer = dist < m_target->atProBaseDist - RadiusPxY(m_config.at_pro_min_progress_ratio);
                    // GORUNUM DEGISTI: kutu tabanin cok altina dustu (kamera
                    // dondu / uzaklastik) - tabani ASAGI cekmezsek bir daha
                    // asla ilerleme kaydedemeyiz.
                    bool rebase = boxH < m_target->atProBaseBoxH * m_config.at_pro_rebase_shrink;

                    if (bigger || closer || rebase) {
                        // TABAN SADECE BURADA guncellenir - her karede degil.
                        // (Eski kod her minicik buyumede tabani yukseltiyordu:
                        // "circir" hatasi, kademeli buyume asla esigi gecemezdi.)
                        m_target->atProBaseBoxH = boxH;
                        m_target->atProBaseDist = dist;
                        m_target->atProLastProgressAt = now;
                    }
                }

                double noProgSec =
                    std::chrono::duration<double>(now - m_target->atProLastProgressAt).count();

                // TANI: saniyede bir o anki olcum - esikleri gercek veriyle
                // ayarlayabilmek icin (tahminle degil).
                if (std::chrono::duration<double>(now - m_lastAtProLogAt).count() >= 1.0) {
                    m_lastAtProLogAt = now;
                    LogDiag("ATPRO s h=" + std::to_string(boxH) +
                             " baseH=" + std::to_string(m_target->atProBaseBoxH) +
                             " d=" + std::to_string(dist) +
                             " baseD=" + std::to_string(m_target->atProBaseDist) +
                             " noprog=" + std::to_string(noProgSec));
                }

                if (noProgSec > m_config.at_pro_no_progress_sec) {
                    LogDiag("ATPRO trigger noprog=" + std::to_string(noProgSec) +
                             " dist=" + std::to_string(dist) + " boxH=" + std::to_string(boxH));
                    bool released = AtProRecover();
                    if (released) {
                        // Faz 2'ye kadar gitti - bu bolgeye hemen geri
                        // kilitlenmeyelim, hedefi birakip aramaya donuyoruz.
                        m_rejectedSpots.push_back({m_target->pos, now});
                        m_target.reset();
                        m_targetEndedFlag = true;
                    } else {
                        // Yaklasma basladi (ya da odak gitti) - hedef KORUNUR,
                        // sayaci sifirlayip normal akisa donuyoruz.
                        m_target->atProLastProgressAt = std::chrono::steady_clock::now();
                    }
                    // Kamera/karakter az once hareket etmis olabilir -
                    // elimizdeki tespitler bayat, taze kareyi bekliyoruz.
                    return false;
                }
            }
        }

        // A-Z TANI (kullanici talebi: "en ufak ince ayrintiya kadar
        // loglamali" - iki farkli bilgisayar arasindaki davranis farkini
        // karsilastirabilmek icin): kilitliyken HER Step()'te panelin o
        // anki tam durumu - sadece karar anlarinda degil, SUREKLI. Boylece
        // "neden bu bilgisayarda skor hep dusuk kaliyor" gibi sorular
        // ANLIK VERIYLE (tahminle degil) cevaplanabilir.
        {
            double lostForSecNow =
                m_target->everSawValidPanel
                    ? std::chrono::duration<double>(now - m_target->lastValidPanelSeen).count()
                    : 0.0;
            LogClickDebug("PNL step vis=" + std::string(panel.visible ? "1" : "0") +
                           " sv=" + std::string(stillVisible ? "1" : "0") +
                           " best=" + std::to_string(panel.bestScore) +
                           " thr=" + std::to_string(m_config.status_panel_name_match_threshold) +
                           " ever=" + std::string(m_target->everSawValidPanel ? "1" : "0") +
                           " wait=" + std::to_string(waitSec) + " lost=" + std::to_string(lostForSecNow) +
                           " det=" + std::to_string(detections.size()));
        }

        bool cutConfirmed = false;
        bool wrongTarget = false;

        if (!panel.hasTemplate) {
            // _panel.png henuz alinmamis - cift dogrulama devre disi, eski
            // (guvenlik agi zaman asimina dayali) davranisa dusuyoruz.
        } else if (m_target->everSawValidPanel ? stillVisible : panel.visible) {
            if (!m_target->everSawValidPanel) {
                std::cout << "[bot] metin bulundu, isim dogrulandi - kesiliyor\n";
            }
            if (!m_target->panelFirstVisibleAt) {
                m_target->panelFirstVisibleAt = now;
            }
            m_target->everSawValidPanel = true;
            m_target->lastValidPanelSeen = now;
        } else if (m_target->everSawValidPanel) {
            // Isim daha once goruluyordu, simdi gorunmuyor - ama TEK bir
            // karelik gecici kayip (vurus efekti, capture titremesi vb.)
            // olabilir. Gercekten "kesildi" sayabilmek icin kesintisiz bir
            // sure (status_panel_lost_grace_sec) boyunca hic gorunmemis olmali.
            // (Z/loot tusu artik burada degil - Step() basinda SUREKLI
            // basiliyor, bkz. loot_key_interval_sec.)
            double lostForSec = std::chrono::duration<double>(now - m_target->lastValidPanelSeen).count();

            // OTO NESNE - ERKEN BASLATMA (kullanici talebi: "metin kesilir
            // kesilmez baslamali, cok hafif bekliyor"). Toplama ESKIDEN
            // "kesildi" KARARINI bekliyordu; o karar ise guvenlik geregi
            // panelin status_panel_lost_grace_sec (1.2sn) boyunca KESINTISIZ
            // kayip olmasini sart kosar - gozle gorulur bir gecikmeydi ve bu
            // sirada yere dusen esyalar toplanmadan bekliyordu.
            // Artik ikisi AYRILDI: toplama, panel kaybolduktan sadece
            // auto_loot_start_grace_sec (0.35sn - tek karelik titremeleri
            // eleyecek kadar, ~2-3 kare) sonra baslar; "kesildi" karari eski
            // muhafazakar esigiyle AYNEN kalir ve zaten toplama SURERKEN
            // arka planda olgunlasir. Yani ekstra gecikme YOK, kazanc net.
            bool lootAny = m_config.auto_loot_enabled;
            if (lootAny && m_input && !m_target->lootStarted &&
                lostForSec > m_config.auto_loot_start_grace_sec) {
                m_target->lootStarted = true;
                LogDiag("LOOT early-start lost=" + std::to_string(lostForSec));
                RunAutoLoot();
                return false;
            }

            if (lostForSec > m_config.status_panel_lost_grace_sec) {
                // KORUMA (KRITIK - kullanici talebi: "metin kesilmeden ASLA
                // birakilmamali"): panel su an gorunmuyor ama en yuksek
                // korelasyon skoru esige COK YAKINSA, gercekten kaybolmus
                // OLMAYABILIR - vurus efekti/animasyon/isik degisimi
                // panelin GECICI olarak marjinal okunmasina sebep olabilir.
                // Boyle bir durumda TEK SEFERLIK ek sure taniyoruz, hemen
                // "kesildi" demiyoruz - appear-phase'deki ayni mantigin
                // (nearMissExtended) kayip/cut karari icin AYRI kopyasi.
                const double kLostNearMissMargin = 0.10;
                bool lostNearMiss =
                    panel.bestScore >= (m_config.status_panel_name_match_threshold - kLostNearMissMargin);
                if (lostNearMiss && !m_target->lostNearMissExtended) {
                    m_target->lostNearMissExtended = true;
                    // Sayaci SIFIRLIYORUZ - tek bir Step() dongusu kadar
                    // degil, TAM BIR status_panel_lost_grace_sec kadar ek
                    // sure taninmasi icin (aksi halde bir sonraki Step()'te
                    // ayni "esik asildi" durumu tekrar tetiklenip pratikte
                    // hicbir sey kazandirmazdi).
                    m_target->lastValidPanelSeen = now;
                    std::cout << "[bot] TANI: panel kayboldu ama skor esige yakin (best=" << panel.bestScore
                               << ", esik=" << m_config.status_panel_name_match_threshold
                               << ") - KESILDI sayilmadan once TEK SEFERLIK ek sure taniniyor\n";
                    LogClickDebug("PNL cut-near-miss-extend best=" + std::to_string(panel.bestScore) +
                                   " thr=" + std::to_string(m_config.status_panel_name_match_threshold) +
                                   " lost=" + std::to_string(lostForSec));
                } else {
                    cutConfirmed = true;
                    // TANI: bir sonraki "stabil degil" sikayetinde bu satir,
                    // kararin GERCEKTEN ne kadar surelik bir kayiptan sonra
                    // verildigini gosterir - kanitla ilerlemek icin.
                    std::cout << "[bot] TANI: panel " << lostForSec << " sn kayiptan sonra KESILDI sayildi (esik="
                               << m_config.status_panel_lost_grace_sec << "sn)\n";
                    LogClickDebug("PNL cut best=" + std::to_string(panel.bestScore) +
                                   " thr=" + std::to_string(m_config.status_panel_name_match_threshold) +
                                   " lost=" + std::to_string(lostForSec));
                }
            }
        } else if (waitSec > m_config.status_panel_appear_grace_sec) {
            // KORUMA (kullanici talebi: metin kesilmeden ASLA birakilmamali):
            // "yanlis hedef" derken KOR KORUNE bir zaman asimina guvenmek
            // yerine, en yuksek korelasyon skoruna (bestScore) bakiyoruz -
            // esigin HEMEN ALTINDAYSA (nearMissMargin icinde) sablon aslinda
            // DOGRU ama zayif/marjinal eslesiyor olabilir (ör. golge metninin
            // soluk render edilmesi) - bu durumda TEK SEFERLIK ek sure
            // taniyip hemen vazgecmiyoruz. bestScore COK dusukse (gercekten
            // hic benzemiyor) ek sure vermenin anlami yok, direkt vazgeciliyor.
            const double kNearMissMargin = 0.08;
            bool nearMiss = panel.bestScore >= (m_config.status_panel_name_match_threshold - kNearMissMargin);
            if (nearMiss && !m_target->nearMissExtended) {
                m_target->nearMissExtended = true;
                std::cout << "[bot] TANI: panel esige yakin (best=" << panel.bestScore << ", esik="
                           << m_config.status_panel_name_match_threshold
                           << ") - yanlis hedef sayilmadan once TEK SEFERLIK ek sure taniniyor\n";
                LogClickDebug("PNL near-miss-extend best=" + std::to_string(panel.bestScore) +
                               " thr=" + std::to_string(m_config.status_panel_name_match_threshold) +
                               " wait=" + std::to_string(waitSec));
            } else {
                // Isim hic cikmadi (kisa bir sure verildikten sonra, EK sure de
                // tukendiyse) - yanlis yere tiklanmis.
                wrongTarget = true;
                std::cout << "[bot] TANI: " << waitSec << " sn boyunca panel hic cikmadi, YANLIS HEDEF sayildi (esik="
                           << m_config.status_panel_appear_grace_sec << "sn)\n";
                // TANI: panel HIC bir kez bile esigi gecemedi mi (bestScore
                // dusuk = sablon gercekten uyusmuyor) yoksa esigin HEMEN
                // ALTINDA mi kaliyordu (bestScore esige yakin = sablon dogru
                // ama esik/kalite marjinal) ayirt etmek icin.
                LogClickDebug("PNL wrong best=" + std::to_string(panel.bestScore) +
                               " thr=" + std::to_string(m_config.status_panel_name_match_threshold) +
                               " wait=" + std::to_string(waitSec));
            }
        }

        // GUVENLIK AGI: panel sistemi bir sekilde calismazsa/algilanamazsa
        // bile bot sonsuza kadar kilitli kalmasin. ONEMLI: panel dogrulamasi
        // AKTIFSE (hasTemplate) ve isim en az bir kez dogrulandiysa, GORSEL
        // yeniden-tespidin (ORB) kaybolmasi (ör. metnin etrafi hayvan/efekt
        // kalabalasip kutuyu bulamamasi) ARTIK HEDEFI SIFIRLAMAMALI - panel
        // zaten cok daha guvenilir bir kanit. Bu durumda sadece panelin
        // kendisi (yukarida) veya cok daha uzun bir mutlak ust sinir karar verir.
        bool visualLockRelevant = !(panel.hasTemplate && m_target->everSawValidPanel);
        double lostSec = std::chrono::duration<double>(now - m_target->lastSeen).count();
        // ANTI-TAKILMA ACIKKEN mutlak ust siniri (varsayilan 90sn) YUKARI
        // CEKIYORUZ: aksi halde 90. saniyede burasi tetiklenip hedefi birakir
        // ve 150. saniyedeki kurtarma dizisine HIC SIRA GELMEZDI.
        double effectiveMaxWait = m_config.anti_stuck_enabled
                                       ? std::max(m_config.max_target_wait_sec, m_config.anti_stuck_seconds + 30.0)
                                       : m_config.max_target_wait_sec;
        bool safetyTimeout = (visualLockRelevant && lostSec > m_config.target_lost_grace_sec) ||
                              (waitSec > effectiveMaxWait);

        // Ayni HP cubugunun KESINTISIZ ekranda kalma suresi - hem eski
        // (60sn) takilma korumasi hem de yeni ANTI-TAKILMA bunu kullanir.
        double panelOpenSec = m_target->panelFirstVisibleAt.has_value()
                                   ? std::chrono::duration<double>(now - *m_target->panelFirstVisibleAt).count()
                                   : 0.0;
        bool panelStillUp = panel.hasTemplate && panel.visible && m_target->panelFirstVisibleAt.has_value();

        // ANTI-TAKILMA (kullanici talebi): AYNI HP cubugu 2.5 dakikadir
        // ekrandaysa kurtarma dizisini calistir - bkz. AntiStuckRecover().
        bool antiStuckTrigger =
            m_config.anti_stuck_enabled && panelStillUp && panelOpenSec > m_config.anti_stuck_seconds;

        // 3. MEKANIZMA - TAKILMA KORUMASI: panel KESINTISIZ olarak
        // stuck_panel_timeout_sec'ten UZUN suredir aciksa (hala gorunuyor,
        // ne kesildi ne yanlis hedef sayildi), karakter bir sekilde takilmis
        // demektir (ör. metnin arkasina/icine sikismis, yol bulamiyor).
        // ANTI-TAKILMA ACIKKEN bu eski/kisa (60sn) mekanizma DEVRE DISI -
        // yerini daha uzun sureli ve GERCEKTEN kurtarma yapan yeni dizi alir.
        bool stuckTimeout = !m_config.anti_stuck_enabled && panelStillUp &&
                             panelOpenSec > m_config.stuck_panel_timeout_sec;

        if (antiStuckTrigger) {
            // ANTI-TAKILMA: kesim onayindan ONCE bakiyoruz - HP cubugu 2.5
            // dakikadir duruyorsa zaten "kesildi" sinyali GELMEMIS demektir.
            std::cout << "[bot] ANTI-TAKILMA: HP cubugu " << panelOpenSec
                       << " sn'dir kesintisiz ekranda - takilma varsayiliyor\n";
            LogDiag("STUCK trigger open=" + std::to_string(panelOpenSec) +
                     " thr=" + std::to_string(m_config.anti_stuck_seconds));
            // pressEsc=false (kullanici bildirimi ile DUZELTILDI - bkz.
            // AntiStuckRecover yorumu): ESC'nin "hedefi birak" varsayimi
            // TUM sunucularda/durumlarda gecerli degil - bazi durumlarda
            // hedefi birakmak yerine dogrudan AYARLAR MENUSUNU aciyor, ve
            // kurtarma dizisi bunu ASLA tekrar kapatmiyor. Menu acikken
            // WASD'nin hicbir etkisi olmadigi icin karakter TAMAMEN kilitli
            // kaliyor ("tasa takilinca WASD yapip cikamadigi zamanlar" -
            // bkz. kullanici log analizi). Hedef, asagidaki WASD/uzaklasma
            // adimlarindan zaten DOGAL olarak birakilir (Metin2 tarzi
            // istemcilerde belirli bir mesafeden sonra kilit otomatik
            // dusuyor) - ESC'ye hic gerek yok, riski gore degmiyor.
            AntiStuckRecover(/*pressEsc=*/false);
            // Bu bolgeye hemen geri kilitlenmeyelim - kurtarilamayan/erisilemez
            // bir hedefe tekrar donmemek icin kisa sureligine reddedildi say.
            m_rejectedSpots.push_back({m_target->pos, now});
            m_target.reset();
            m_targetEndedFlag = true;
            // Kamera+karakter AZ ONCE hareket etti - elimizdeki tespitler
            // artik BAYAT. Taze bir kare icin bir sonraki Step()'i bekliyoruz.
            return false;
        } else if (cutConfirmed) {
            std::cout << "[bot] metin kesildi (isim yazisi kayboldu) - yeni metin araniyor\n";
            // GUVENLIK AGI: toplama COGU ZAMAN buraya gelmeden, panel
            // kaybolur kaybolmaz (auto_loot_start_grace_sec) ZATEN calismistir
            // - lootStarted bayragi tekrarlamayi onler. Buradaki cagri sadece
            // "toplama hic baslamamis" (ör. cok hizli/atlanmis) durum icindir.
            bool lootAnyMode = m_config.auto_loot_enabled;
            if (lootAnyMode && m_input && m_target && !m_target->lootStarted) {
                m_target->lootStarted = true;
                RunAutoLoot();
            }
            m_target.reset();
            m_targetsCompleted++;
            m_targetEndedFlag = true;  // COKLU CLIENT: bir dongu bitti, sirayi diger client'a birak
        } else if (wrongTarget) {
            std::cout << "[bot] yanlis hedef (isim yazisi hic cikmadi) - yeniden araniyor\n";
            // KRITIK (hizlandirmayla birlikte ZORUNLU): bu nokta artik
            // REDDEDILDI olarak isaretleniyor. Onceden isaretlenmiyordu ama
            // zarari sinirliydi, cunku bot yeni hedefe gecmek icin bir
            // sonraki Step()'i beklerdi. Artik AYNI adimda hemen yeni hedef
            // sectigimiz icin (bkz. asagidaki hizlandirma), isaretlenmezse
            // ayni yanlis noktaya HIZLI bir sonsuz donguyle tekrar tekrar
            // tiklanabilirdi.
            m_rejectedSpots.push_back({m_target->pos, now});
            m_target.reset();
            m_targetEndedFlag = true;
        } else if (stuckTimeout) {
            std::cout << "[bot] panel " << m_config.stuck_panel_timeout_sec
                       << " saniyedir kesintisiz acik - karakter takilmis olabilir, hedef degistiriliyor\n";
            LogClickDebug("PNL stuck open=" + std::to_string(m_config.stuck_panel_timeout_sec));
            // KRITIK GUVENLIK DUZELTMESI: ONCEDEN burada PressEscape()
            // cagriliyordu ("takilma"yi ESC ile iptal etmeye calisir gibi) -
            // ama Metin2'de ESC OYUN ICI SISTEM/AYARLAR MENUSUNU acar, bir
            // eylemi IPTAL ETMEZ. Kullanicinin bildirdigi "45 saniye donuyor,
            // sonra bir anda ayarlar penceresi aciliyor ve BIR DAHA
            // KAPANMIYOR" sorununun sebebi TAM OLARAK buydu - bot ekrani
            // kapatan bir menuyu ACIP hicbir zaman KAPATMIYOR, botu fiilen
            // kullanilmaz hale getiriyordu. Bu "takilma" senaryosunun asil
            // KOK SEBEBI ayrica duzeltildi (bkz. QueueOtherVisibleMetins'teki
            // etiket-merkezi degisikligi - ayni hedefe yanlislikla ikinci
            // shift+sag-tik atilmasi enGELLENDI), bu yuzden bu dal artik cok
            // daha NADIR tetiklenmeli - ama tetiklense bile ESK'YE ARTIK
            // DOKUNMUYORUZ, sadece hedefi birakip normal aramaya donuyoruz.
            // Bu bolgeye biraz daha kilitlenmeyelim - takilan/erisilemez
            // bir hedefe hemen tekrar dönmemek icin kisa sureligine reddedildi say.
            m_rejectedSpots.push_back({m_target->pos, now});
            m_target.reset();
            SearchAction();
            m_targetEndedFlag = true;
            // Kamera AZ ONCE dondu - elimizdeki tespitler artik BAYAT
            // (baska bir goruntuye ait). Bu adimda yeni hedef secmeye
            // kalkismak yanlis noktaya tiklamak olurdu; taze bir kare icin
            // bir sonraki Step()'i bekliyoruz.
            return false;
        } else if (safetyTimeout) {
            std::cout << "[bot] guvenlik agi zaman asimi - tamamlandi sayildi\n";
            LogClickDebug("PNL safety-timeout wait=" + std::to_string(waitSec) +
                           " lostSec=" + std::to_string(lostSec) + " visLockRel=" +
                           std::string(visualLockRelevant ? "1" : "0"));
            m_target.reset();
            m_targetsCompleted++;
            m_targetEndedFlag = true;
        }

        // HALA kilitliysek bu adimda yapacak bir sey yok.
        if (m_target) return false;

        // HIZLANDIRMA: hedef AZ ONCE cozuldu (kesildi/yanlis/zaman asimi).
        // ONCEDEN burada kosulsuz "return false" vardi - yani yeni hedefe
        // gecmek icin BIR SONRAKI Step() cagrisi beklenirdi (uzerine dongu
        // bekleme suresi de binerek gozle gorulur bir "olu zaman" olusurdu).
        // Artik AYNI adimda, ZATEN elimizde olan (bu karede hesaplanmis)
        // tespitlerle devam edip yeni hedefe ANINDA geciyoruz. Kararlilik
        // sayaci da sifirlaniyor ki eski hedefin verisi yeni secimi
        // etkilemesin.
        m_pendingPoint.reset();
        m_pendingStableCount = 0;
    }

    // IKINCI ANTI-TAKILMA TETIKLEYICISI (kullanicinin canli gozlemi - bkz.
    // config.h anti_stuck_no_hp_seconds): buraya SADECE hedef KILITLI DEGILKEN
    // (yani "arama modunda") geliyoruz. Kullanicinin tarif ettigi sinsi
    // takilma tam da burada olusuyordu: karakter fiziksel olarak takilmis, HP
    // cubugu HIC acilmiyor, bot ise arama modunda oldugunu sanip ulasilamayan
    // metinlere tiklayip duruyor - HP cubugu hic acilmadigi icin BIRINCI
    // tetikleyici (ayni HP cubugu X dakikadir ekranda) ASLA calisamiyor ve bot
    // sonsuza kadar takili kaliyordu.
    //
    // Panel okumasi PAHALI (coklu-olcek sablon eslestirme) oldugu icin arama
    // modunda her karede DEGIL, yarim saniyede bir okunur - sayac dakikalar
    // mertebesinde oldugu icin bu hassasiyet fazlasiyla yeterli.
    if (m_config.anti_stuck_enabled && m_panelDetector.HasTemplate()) {
        if (std::chrono::duration<double>(now - m_lastSearchPanelCheckAt).count() >= 0.5) {
            m_lastSearchPanelCheckAt = now;
            if (m_panelDetector.Read(frame).visible) m_lastHpBarSeenAt = now;
        }
        double noHpSec = std::chrono::duration<double>(now - m_lastHpBarSeenAt).count();
        if (noHpSec > m_config.anti_stuck_no_hp_seconds) {
            std::cout << "[bot] ANTI-TAKILMA: " << noHpSec
                       << " sn'dir HIC HP cubugu gorulmedi - gizli takilma varsayiliyor\n";
            LogDiag("STUCK trigger-no-hp sec=" + std::to_string(noHpSec) +
                     " thr=" + std::to_string(m_config.anti_stuck_no_hp_seconds));
            // pressEsc=false: buraya SADECE hedef KILITLI DEGILKEN (arama
            // modunda) gelinir - birakilacak bir hedef yok, ESC'ye GEREK YOK
            // (kullanici talebi: bu tetikleyicide ESC basilmamali).
            AntiStuckRecover(/*pressEsc=*/false);
            // Sayaci SIFIRLA - aksi halde kurtarma dizisi hemen ardindan
            // tekrar tekrar tetiklenirdi (HP cubugu daha yeni acilamadi).
            m_lastHpBarSeenAt = std::chrono::steady_clock::now();
            m_lastDetection.reset();
            m_pendingPoint.reset();
            m_pendingStableCount = 0;
            // Kamera+karakter AZ ONCE hareket etti - elimizdeki tespitler
            // BAYAT. Taze bir kare icin bir sonraki Step()'i bekliyoruz.
            return false;
        }
    }

    if (detections.empty()) {
        // TANI: capture calisiyor (buraya kadar geldik) ama YOLO/tespit
        // hicbir sey bulmuyor - bu, "yanlis sunucu/pencere baglandi", "model
        // dosyalari eksik/bozuk" veya "gercekten ekranda metin yok" ayrimini
        // yapmak icin gerekli. Bogmamak icin birkac saniyede bir.
        {
            auto nowIdle = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(nowIdle - m_lastIdleSearchLog).count() > 5.0) {
                m_lastIdleSearchLog = nowIdle;
                LogDiag("STEP idle-search hwnd=" + std::to_string((long long)(intptr_t)m_hwnd) +
                         " frame=" + std::to_string(frame.cols) + "x" + std::to_string(frame.rows) +
                         " yoloModels=" + std::to_string(m_yoloDetectors.size()) + " det=0");
            }
        }
        m_lastDetection.reset();
        m_pendingPoint.reset();
        m_pendingStableCount = 0;
        // KRITIK (kullanici bildirimi: "metin tespit seviyesi dustu"):
        // modeller donusumlu calistigi icin, kamera dondukten hemen sonra
        // HENUZ tum kategoriler taranmamis olur. Eskiden burada sadece sureye
        // bakiliyordu; sure dolunca bot, siradaki modellerin BAKMA firsati
        // bulamadigi bir kareden "burada metin yok" sonucunu cikarip kamerayi
        // ceviriyordu - yani tam karsidaki bir metin, sirasi gelmemis bir
        // modele ait oldugu icin GORULMEDEN geride kaliyordu. Artik kamera,
        // YUKLU TUM kategoriler en az bir kez taranmadan DONMEZ.
        // GUVENLIK: bir model takilir/hic tespit uretmezse sonsuza kadar
        // beklememek icin ust sinir (3 sn) var.
        bool scannedAll = AllCategoriesScanned();
        bool waitedTooLong = std::chrono::duration<double>(now - m_lastRotateAt).count() > 3.0;
        if (now >= m_nextSearchAt && (scannedAll || waitedTooLong)) {
            LogDiag(std::string("SEARCH rotate scannedAll=") + (scannedAll ? "1" : "0") +
                     " cats=" + std::to_string(m_scannedSinceRotate.size()));
            SearchAction();
            double waitSec = RandRange(m_config.search_action_wait_sec_min, m_config.search_action_wait_sec_max);
            m_nextSearchAt = std::chrono::steady_clock::now() +
                              std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                  std::chrono::duration<double>(waitSec));
        }
        return false;
    }

    // Farm alani icindeki tespitler arasindan karaktere en yakin olani sec.
    auto best = ClosestTo(detections, anchor, 1e18);
    m_lastDetection = best;

    // KARARLILIK KAPISI: bu nokta onceki karedeki bekleyen noktayla
    // (kucuk bir yaricap icinde) uyusuyor mu diye bak - uyusuyorsa
    // sayaci artir, uyusmuyorsa (kamera henuz oturmadi, gurultulu okuma)
    // sayaci sifirla ve bu karede TIKLAMA, bir sonraki karede tekrar dogrula.
    cv::Point2d bestCenter = best->Center();
    if (m_pendingPoint &&
        std::hypot(bestCenter.x - m_pendingPoint->x, bestCenter.y - m_pendingPoint->y) <=
            RadiusPxY(m_config.detection_stable_radius_ratio)) {
        m_pendingStableCount++;
    } else {
        m_pendingPoint = bestCenter;
        m_pendingStableCount = 1;
    }

    if (m_pendingStableCount < m_config.detection_stable_frames) {
        return false;  // henuz kararli degil - bu karede tiklama, dogrulanmasini bekle
    }
    m_pendingPoint.reset();
    m_pendingStableCount = 0;

    bool confirmed = ClickBurst(*best, frame.cols, frame.rows);
    if (!confirmed) {
        // Panel (isim) 10 tiklama boyunca HIC cikmadi - yanlis hedef
        // (dag/tas/duvar dokusu vb.). Bu bolgeyi bir sure REDDEDILDI olarak
        // isaretliyoruz ki bir sonraki karede ayni yanlis noktaya tekrar
        // kilitlenmesin (sonsuz dongu/"takilma" onlenir), sonra kamerayi
        // cevirip arayis moduna giriyoruz ki gorunum degissin ve baska bir
        // metin bulma sansi olsun.
        // Ekleme sirasinda suresi gecmis eski kayitlari temizle - liste
        // uzun bir farm oturumu boyunca sinirsizca buyumesin.
        m_rejectedSpots.erase(
            std::remove_if(m_rejectedSpots.begin(), m_rejectedSpots.end(),
                            [&](const RejectedSpot& r) {
                                return std::chrono::duration<double>(now - r.rejectedAt).count() >
                                       m_config.reject_spot_expire_sec;
                            }),
            m_rejectedSpots.end());
        m_rejectedSpots.push_back({best->Center(), now});
        std::cout << "[bot] bolge reddedildi (panel cikmadi) - kamera donduruluyor, aramaya devam\n";
        SearchAction();
        return false;
    }

    Target t;
    t.pos = best->Center();
    t.category = best->category;  // izleme icin - bkz. DetectInFarmZone round-robin
    t.lastSeen = now;
    t.lockedAt = now;
    t.everSawValidPanel = true;  // ClickBurst zaten panelin cikip goruldugunu dogruladi
    t.lastValidPanelSeen = now;
    m_target = t;
    ClearDetectionMemory();  // karakter hedefe yuruyecek - eski konumlar gecersiz
    m_targetsFound++;

    // HIZLANDIRMA (KRITIK GUVENLIK ANAHTARIYLA KORUNUYOR - varsayilan
    // KAPALI): kadrajda GORUNEN BASKA metinler varsa oyunun kendi kuyruguna
    // eklensinler - bkz. config.h queue_other_visible_metins yorumu (KANITLANMIS
    // "hali hazirda kesilen metne yanlislikla ikinci tik atip kesimi iptal
    // etme" ve "loot toplamadan digerine gecme" hatalari nedeniyle KAPALI).
    if (m_config.queue_other_visible_metins) {
        QueueOtherVisibleMetins(*best);
    }

    return true;
}

bool MetinBot::PollUnfocusedCut() {
    // Sadece KILITLI bir hedef varken anlamli - yoksa zaten "arama" modunda.
    if (!m_target || !m_capture) return false;
    auto frameOpt = m_capture->GetFrame();
    if (!frameOpt) return false;  // capture henuz kare vermedi - bir sonraki sefere
    cv::Mat frame = *frameOpt;
    auto now = std::chrono::steady_clock::now();

    auto panel = m_panelDetector.Read(frame);
    double waitSec = std::chrono::duration<double>(now - m_target->lockedAt).count();

    // Panel sablonu yoksa (cift dogrulama devre disi): odak-suz guvenilir bir
    // kesim tespiti yapamayiz - SADECE mutlak guvenlik ust siniriyla serbest
    // birak (aksi halde sonsuza kadar kilitli kalirdi).
    if (!panel.hasTemplate) {
        if (waitSec > m_config.max_target_wait_sec) {
            m_target.reset();
            m_targetsCompleted++;
            return true;
        }
        return false;
    }

    if (panel.visible) {
        // HP cubugu HALA gorunuyor -> metin kesiliyor, bekle.
        m_target->lastValidPanelSeen = now;
        if (!m_target->panelFirstVisibleAt) m_target->panelFirstVisibleAt = now;
        m_target->everSawValidPanel = true;
        return false;
    }

    // HP cubugu gorunmuyor.
    if (m_target->everSawValidPanel) {
        double lostForSec = std::chrono::duration<double>(now - m_target->lastValidPanelSeen).count();
        if (lostForSec > m_config.status_panel_lost_grace_sec) {
            // Step() ile AYNI near-miss-extend korumasi: skor esige cok yakinsa
            // (gecici/marjinal kayip olabilir) TEK SEFERLIK ek sure tani.
            const double kLostNearMissMargin = 0.10;
            bool lostNearMiss =
                panel.bestScore >= (m_config.status_panel_name_match_threshold - kLostNearMissMargin);
            if (lostNearMiss && !m_target->lostNearMissExtended) {
                m_target->lostNearMissExtended = true;
                m_target->lastValidPanelSeen = now;
                return false;
            }
            // KESILDI - hedefi serbest birak.
            m_target.reset();
            m_targetsCompleted++;
            LogDiag("POLL cut-done (odak-suz) lost=" + std::to_string(lostForSec));
            return true;
        }
    } else if (waitSec > m_config.max_target_wait_sec) {
        // Hic panel gormeden cok uzun kilitli (odak-suz clientte olmamali) -
        // guvenlik agi.
        m_target.reset();
        m_targetsCompleted++;
        return true;
    }
    return false;
}

void MetinBot::Stop() {
    if (m_input) m_input->ReleaseClip();  // fare hapsi KESIN kaldirilir
    if (m_capture) m_capture->Stop();
}

void MetinBot::ReleaseMouseClip() {
    if (m_input) m_input->ReleaseClip();
}

void MetinBot::Activate() {
    if (!m_hwnd) return;
    // Windows'un odak-calma engelini asmak icin (bkz. win_window::
    // BringToForeground yorumu) bir deneme yeterli olmayabilir - kisa bir
    // aralikla birkac kez daha dene, sonra dogrula/logla ki gecis sessizce
    // basarisiz olup bot "hicbir sey yapmiyormus gibi" takilmasin.
    for (int attempt = 0; attempt < 3; ++attempt) {
        win_window::BringToForeground(m_hwnd);
        if (GetForegroundWindow() == m_hwnd) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    if (GetForegroundWindow() != m_hwnd) {
        std::cout << "[bot] uyari: client one getirilemedi (HWND=" << m_hwnd
                   << ") - Windows odak degisimini engelliyor olabilir\n";
    }
}

bool MetinBot::ConsumeTargetEndedFlag() {
    bool v = m_targetEndedFlag;
    m_targetEndedFlag = false;
    return v;
}

void MetinBot::SetRunning(bool running) {
    auto now = std::chrono::steady_clock::now();
    if (running && !m_running) {
        m_lastResumeAt = now;
    } else if (!running && m_running) {
        m_activeSeconds += std::chrono::duration<double>(now - m_lastResumeAt).count();
    }
    m_running = running;
}

BotStats MetinBot::GetStats() const {
    BotStats s;
    double liveSeconds =
        m_running ? std::chrono::duration<double>(std::chrono::steady_clock::now() - m_lastResumeAt).count() : 0;
    s.session_seconds = m_sessionStarted ? (m_activeSeconds + liveSeconds) : 0;
    s.total_clicks = m_totalClicks;
    s.targets_found = m_targetsFound;
    s.targets_completed = m_targetsCompleted;
    s.is_locked = m_target.has_value();
    if (m_lastDetection) {
        s.has_last_detection = true;
        s.last_detection_name = m_lastDetection->name;
        s.last_detection_score = m_lastDetection->score;
    }
    return s;
}
