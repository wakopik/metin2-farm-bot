// Tespit Onizleme Araci - TIKLAMA YAPMAZ, Interception gerektirmez.
//
// Ekrani canli gosterir: YOLO'nun tespit ettigi HER metin/canavarin kutusunu
// + kategorisini + guven skorunu, HP CUBUGU arama seridini (bulunursa YESIL
// kutu + skor, bulunamazsa KIRMIZI serit + "en yuksek skor" - GERCEK sayiyla,
// tahmin degil), ve botun BIR SONRAKI ADIMDA muhtemelen ne yapacagini basit
// bir ozet olarak. Goruntu isleme zincirinde (arama bolgesi, HP cubugu
// eslesme skoru vb.) bir sorun olup olmadigini GOZLE dogrulamak icindir -
// yolo_only_mode altinda artik kullanilmayan eski ORB/renk/isim-etiketi
// gorsellestirmeleri KALDIRILDI (karisikligi azaltmak icin).
//
// A-T PRO / KOR NOKTA DOGRULAMA (kullanici talebi): bu arac ayrica botun
// A-T Pro mantigini BIREBIR AYNI formullerle, ama HICBIR SEY YAPMADAN
// (tiklama/tus yok) canli hesaplar ve ekrana yazar - "tutarli mi" sorusunu
// tahminle degil GERCEK SAYIYLA cevaplamak icin:
//   * MERKEZ NOKTA (ekran ortasi = karakter) ve etrafindaki KOR NOKTA cemberi
//     (config blind_spot_radius_ratio). Kor noktaya dusen tespitler KIRMIZI
//     cizilir ve "KOR NOKTA - aday degil" diye isaretlenir (bot da onlari
//     aday saymaz).
//   * KILITLI hedef (secilen metin) ile merkez arasinda cizgi + uzerinde
//     ANLIK MESAFE.
//   * A-T Pro panelinde: kutu yuksekligi h (BIRINCIL sinyal, h ~ 1/d),
//     taban baseH, buyume orani h/baseH ve esigi, ekran mesafesi d ve tabani,
//     "ilerleme yok" sayaci ve tetikleme esigi, "VARDIK" durumu.
// Boylece normal yururken sayacin SIFIRLANIP sifirlanmadigi (yani yanlis
// tetikleme olup olmayacagi) GOZLE izlenebilir.
//
// Kullanim:
//   debug_preview.exe
//
// Izleme TAMAMEN OTOMATIKTIR - hicbir tusa basmaya gerek yok. Kadrajdaki
// (kor nokta disindaki) metinlerden merkeze EN YAKIN olani kendiliginden
// izlemeye alinir; o metin kaybolursa 2 sn sonra otomatik olarak yenisine
// gecilir. Ayrica HER tespitin merkeze uzakligi (d=) kutusunun uzerinde
// surekli yazar.
//
// Kontroller:
//   Q -> cikis
//   R -> izlemeyi sifirla (istege bagli - kendiliginden de yenilenir)

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <opencv2/opencv.hpp>

#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include "capture.h"
#include "config.h"
#include "status_panel_detector.h"
#include "win_window.h"
#include "yolo_detector.h"

namespace {
// Kategori basina SABIT/AYIRT EDICI bir renk - ayni kategori HER ZAMAN ayni
// renkte cizilsin diye (rastgele degil, basit bir hash).
cv::Scalar ColorForCategory(const std::string& category) {
    static const cv::Scalar kPalette[] = {
        cv::Scalar(0, 255, 0),    cv::Scalar(255, 128, 0),  cv::Scalar(0, 200, 255),
        cv::Scalar(255, 0, 255),  cv::Scalar(0, 165, 255),  cv::Scalar(200, 255, 0),
    };
    size_t h = std::hash<std::string>{}(category);
    return kPalette[h % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

// Koyu bir zemin uzerine yazi - ham goruntunun uzerinde okunakli olsun diye.
void PutLabel(cv::Mat& img, const std::string& text, cv::Point org, cv::Scalar color,
               double scale = 0.5) {
    int base = 0;
    cv::Size ts = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, scale, 1, &base);
    cv::rectangle(img, cv::Rect(org.x - 3, org.y - ts.height - 4, ts.width + 6, ts.height + base + 6),
                   cv::Scalar(0, 0, 0), cv::FILLED);
    cv::putText(img, text, org, cv::FONT_HERSHEY_SIMPLEX, scale, color, 1, cv::LINE_AA);
}

// A-T PRO izleyici: bot.cpp Step() icindeki mantigin BIREBIR AYNISI, ama
// hicbir eylem yapmaz - sadece sayilari tutar ki ekrana yazabilelim.
struct AtProTracker {
    bool locked = false;
    cv::Point2d pos{};        // kilitli hedefin son bilinen konumu
    double baseDist = -1.0;   // son ONAYLANMIS ilerleme anindaki mesafe
    double baseBoxH = 0.0;    // son ONAYLANMIS ilerleme anindaki kutu yuksekligi
    bool arrived = false;
    std::chrono::steady_clock::time_point lastProgressAt{};

    // Son karede hesaplanan degerler (sadece gosterim icin)
    double curDist = 0.0, curBoxH = 0.0, growth = 0.0, noProgSec = 0.0;
    bool seenThisFrame = false;
    bool progressedThisFrame = false;
    // Hedef kac saniyedir bulunamiyor - bu sure asilirsa kilit birakilip
    // otomatik olarak yeni/en yakin hedefe gecilir.
    std::chrono::steady_clock::time_point lostSince{};
    bool isLost = false;

    void Reset() { *this = AtProTracker{}; }
};
}  // namespace

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::cout << "=== Metin2 Farm Botu - Tespit Onizleme (YOLO + HP Cubugu) ===\n";
    std::cout << "TIKLAMA YAPMAZ. Renkli kutular = YOLO tespitleri (kategori+skor). Ust-orta serit\n";
    std::cout << "= HP cubugu arama alani (YESIL bulundu / KIRMIZI bulunamadi + en yuksek skor).\n";
    std::cout << "Alt satir = botun bu karede muhtemelen ne yapacagi. Cikmak icin Q.\n\n";

    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);

        Config config = Config::Load("config.json");

        HWND hwnd = win_window::FindGameWindow(config.window_title_hint);
        if (!hwnd) {
            std::cerr << "[hata] Oyun penceresi bulunamadi (window_title_hint='"
                       << config.window_title_hint << "').\n";
            std::cout << "\nKapatmak icin Enter'a basin...";
            std::cin.get();
            return 1;
        }
        win_window::BringToForeground(hwnd);

        WindowCapture capture(hwnd, /*minIntervalMs=*/0);

        // TUM yapilandirilmis YOLO modellerini yukle - bu arac TANI amacli
        // oldugu icin (Hedef Sec secimine bakmadan) HEPSINI gosterir, hangi
        // kategorilerin model olarak yuklu oldugunu da gormek icin faydali.
        std::vector<YoloDetector> yoloDetectors;
        for (const auto& entry : config.yolo_models) {
            float confThr = entry.conf_threshold >= 0.0 ? (float)entry.conf_threshold
                                                          : (float)config.yolo_conf_threshold;
            yoloDetectors.emplace_back(entry.path, entry.category, config.yolo_input_size, confThr,
                                        (float)config.yolo_nms_threshold, entry.class_index);
            std::cout << "[yolo] '" << entry.category << "' -> " << entry.path
                       << (yoloDetectors.back().IsReady() ? " (hazir)\n" : " (YUKLENEMEDI)\n");
        }

        StatusPanelDetector panelDetector(config);
        if (!panelDetector.HasTemplate()) {
            std::cout << "[uyari] hp_bar/_hpbar.png bulunamadi - HP cubugu tespiti devre disi olarak "
                          "gosterilecek.\n";
        }

        // NESNE MARKET SIMULASYONU (kullanici talebi - "devamli ESC basiyor,
        // esigi canli goreyim"): bot.cpp CheckAndDismissNesneMarket ile AYNI
        // yukleme + arama-bolgesi + eslesme mantigi, ama HICBIR ESC/tiklama
        // gondermez - sadece skoru canli ekrana yazar, boylece esik GERCEK
        // sayiyla (tahminle degil) ayarlanabilir.
        cv::Mat nesneMarketTemplBgr, nesneMarketTemplMask;
        {
            cv::Mat raw = cv::imread(config.nesne_market_template_path, cv::IMREAD_UNCHANGED);
            if (!raw.empty()) {
                if (raw.channels() == 4) {
                    std::vector<cv::Mat> ch;
                    cv::split(raw, ch);
                    nesneMarketTemplMask = ch[3];
                    cv::merge(std::vector<cv::Mat>{ch[0], ch[1], ch[2]}, nesneMarketTemplBgr);
                } else {
                    nesneMarketTemplBgr = raw;
                    nesneMarketTemplMask = cv::Mat(raw.size(), CV_8UC1, cv::Scalar(255));
                }
                std::cout << "[nesne_market] sablon yuklendi (" << config.nesne_market_template_path << ", "
                           << nesneMarketTemplBgr.cols << "x" << nesneMarketTemplBgr.rows << ")"
                           << " (bolge: ust-orta bant, otomatik) - ESC/tiklama YAPILMAYACAK.\n";
            } else {
                std::cout << "[nesne_market] UYARI - sablon yuklenemedi (" << config.nesne_market_template_path
                           << ") - Nesne Market paneli gosterilmeyecek.\n";
            }
        }

        const std::string winName = "Tespit Onizleme (Q=cikis)";
        cv::namedWindow(winName, cv::WINDOW_NORMAL);

        double fps = 0.0;
        auto prevTime = std::chrono::steady_clock::now();
        AtProTracker tracker;

        while (true) {
            auto frameOpt = capture.GetFrame();
            if (!frameOpt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            cv::Mat frame = *frameOpt;
            cv::Mat display = frame.clone();

            cv::Point2d anchor(frame.cols / 2.0, frame.rows / 2.0);
            // Cozunurlukten bagimsiz - bot.cpp DetectInFarmZone ile AYNI formul.
            double zoneRx = frame.cols * config.farm_zone_radius_x_ratio;
            double zoneRy = frame.rows * config.farm_zone_radius_y_ratio;
            cv::Rect searchRegion(
                (int)std::lround(anchor.x - zoneRx),
                (int)std::lround(anchor.y - zoneRy),
                (int)std::lround(zoneRx * 2),
                (int)std::lround(zoneRy * 2));
            cv::rectangle(display, searchRegion, cv::Scalar(255, 255, 0), 1);

            // --- KOR NOKTA (config blind_spot_radius_ratio) ---
            // Botun DetectInFarmZone icindeki filtreyle AYNI yaricap.
            const double blindR = frame.rows * config.blind_spot_radius_ratio;
            // Merkez ekran ortasinin biraz ALTI - bot.cpp ile BIREBIR ayni.
            const cv::Point2d blindCenter(anchor.x, anchor.y + frame.rows * config.blind_spot_offset_y_ratio);
            cv::circle(display, cv::Point((int)blindCenter.x, (int)blindCenter.y), (int)std::lround(blindR),
                        cv::Scalar(0, 0, 255), 2);
            // MERKEZ NOKTA = karakter
            cv::drawMarker(display, cv::Point((int)anchor.x, (int)anchor.y), cv::Scalar(255, 255, 255),
                            cv::MARKER_CROSS, 18, 2);
            PutLabel(display, "MERKEZ NOKTA (karakter)", cv::Point((int)anchor.x + 12, (int)anchor.y - 8),
                      cv::Scalar(255, 255, 255));
            PutLabel(display, cv::format("KOR NOKTA r=%.0fpx (%.0f%%)", blindR,
                                           config.blind_spot_radius_ratio * 100.0),
                      cv::Point((int)anchor.x + 12, (int)anchor.y + (int)blindR + 16), cv::Scalar(0, 0, 255));

            // --- YOLO tespitleri ---
            int totalDetections = 0;
            int blindRejected = 0;
            int smallRejected = 0;
            // Bu karede gorulen, KOR NOKTA DISINDAKI tespitler - kilitleme ve
            // A-T Pro eslesmesi bunlar uzerinden yapilir (bot da oyle yapar).
            std::vector<Detection> candidates;
            for (const auto& yolo : yoloDetectors) {
                if (!yolo.IsReady()) continue;
                auto dets = yolo.Detect(frame, searchRegion);
                cv::Scalar color = ColorForCategory(yolo.GetCategory());
                for (const auto& d : dets) {
                    cv::Point2d clickPt = d.hasPrecisePoint ? d.precisePoint : d.Center();
                    double dx = clickPt.x - blindCenter.x, dy = clickPt.y - blindCenter.y;
                    bool inBlind = (dx * dx + dy * dy) <= blindR * blindR;

                    // MINIK KUTU ELEMESI - bot.cpp DetectInFarmZone ile AYNI
                    // kural (kullanici gozlemi: hareketli hayvanlar cok kucuk
                    // kutu uretiyor, gercek metinler belirli bir boyutun
                    // altina inmiyor). Burada SADECE gosterilir, esik degeri
                    // GOZLE dogrulanip ayarlanabilsin diye.
                    const double minH = frame.rows * config.min_detection_height_ratio;
                    bool tooSmall = !inBlind && d.h < minH;

                    cv::Rect box(d.x, d.y, d.w, d.h);
                    cv::Scalar drawColor = inBlind      ? cv::Scalar(0, 0, 255)
                                            : tooSmall  ? cv::Scalar(0, 140, 255)  // turuncu
                                                        : color;
                    cv::rectangle(display, box, drawColor, 2);
                    char label[160];
                    double distToCenter = std::sqrt(dx * dx + dy * dy);
                    if (inBlind) {
                        snprintf(label, sizeof(label), "%s %.2f  d=%.0f  KOR NOKTA - aday degil",
                                  yolo.GetCategory().c_str(), d.score, distToCenter);
                        ++blindRejected;
                    } else if (tooSmall) {
                        snprintf(label, sizeof(label), "%s %.2f  h=%d < %.0f  MINIK KUTU - TIKLAMAZ",
                                  yolo.GetCategory().c_str(), d.score, d.h, minH);
                        ++smallRejected;
                    } else {
                        // HER tespitin merkeze uzakligi + kutu yuksekligi hep gorunur
                        // olsun (kullanici talebi: "sadece izlemek istiyorum").
                        snprintf(label, sizeof(label), "%s %.2f  d=%.0f  h=%d", yolo.GetCategory().c_str(),
                                  d.score, distToCenter, d.h);
                        candidates.push_back(d);
                    }
                    // Merkezden HER tespite ince bir kilavuz cizgi.
                    cv::line(display, cv::Point((int)anchor.x, (int)anchor.y),
                              cv::Point((int)clickPt.x, (int)clickPt.y), drawColor, 1);
                    PutLabel(display, label, cv::Point(d.x, std::max(d.y - 6, 15)), drawColor);
                    cv::drawMarker(display, cv::Point((int)clickPt.x, (int)clickPt.y), drawColor,
                                    cv::MARKER_TILTED_CROSS, 12, 2);
                }
                totalDetections += (int)dets.size();
            }

            // --- A-T PRO izleme (bot.cpp Step() ile BIREBIR AYNI formuller) ---
            auto nowTick = std::chrono::steady_clock::now();
            tracker.seenThisFrame = false;
            tracker.progressedThisFrame = false;

            // OTOMATIK KILITLENME (kullanici talebi - elle SPACE'e basmaya gerek
            // yok): kilitli hedef yoksa merkeze EN YAKIN adayi kendiliginden
            // izlemeye al. Hedef kaybolursa asagida kilit birakilir ve bir
            // sonraki karede yine kendiliginden yenisine gecer.
            if (!tracker.locked && !candidates.empty()) {
                const Detection* best = nullptr;
                double bestD = 1e18;
                for (const auto& d : candidates) {
                    double dd = cv::norm(d.Center() - anchor);
                    if (dd < bestD) { bestD = dd; best = &d; }
                }
                if (best) {
                    tracker.Reset();
                    tracker.locked = true;
                    tracker.pos = best->Center();
                    tracker.lastProgressAt = nowTick;
                }
            }

            if (tracker.locked) {
                // Kilitli hedefi bu karede yeniden bul - bot ile ayni yaricap.
                const Detection* match = nullptr;
                double bestD = frame.rows * config.target_match_radius_ratio;
                for (const auto& d : candidates) {
                    double dd = cv::norm(d.Center() - tracker.pos);
                    if (dd <= bestD) { bestD = dd; match = &d; }
                }

                if (match) {
                    tracker.pos = match->Center();
                    tracker.seenThisFrame = true;
                    tracker.curBoxH = (double)match->h;
                    tracker.curDist = cv::norm(tracker.pos - anchor);

                    if (tracker.curBoxH >= frame.rows * config.at_pro_arrived_box_ratio) {
                        tracker.arrived = true;
                    }
                    if (!tracker.arrived) {
                        if (tracker.baseDist < 0.0) {
                            tracker.baseDist = tracker.curDist;
                            tracker.baseBoxH = tracker.curBoxH;
                            tracker.lastProgressAt = nowTick;
                        } else {
                            bool bigger = tracker.curBoxH > tracker.baseBoxH + frame.rows * config.at_pro_min_box_growth_ratio;
                            bool closer = tracker.curDist < tracker.baseDist - frame.rows * config.at_pro_min_progress_ratio;
                            bool rebase = tracker.curBoxH < tracker.baseBoxH * config.at_pro_rebase_shrink;
                            if (bigger || closer || rebase) {
                                tracker.baseBoxH = tracker.curBoxH;
                                tracker.baseDist = tracker.curDist;
                                tracker.lastProgressAt = nowTick;
                                tracker.progressedThisFrame = true;
                            }
                        }
                        tracker.growth = tracker.curBoxH - tracker.baseBoxH;  // artik ORAN degil, MUTLAK piksel fark
                    }
                    tracker.isLost = false;
                } else {
                    // Bot da bunu "ilerleme yok KANITI" saymaz - sayaci durdurur.
                    tracker.lastProgressAt = nowTick;
                    if (!tracker.isLost) {
                        tracker.isLost = true;
                        tracker.lostSince = nowTick;
                    } else if (std::chrono::duration<double>(nowTick - tracker.lostSince).count() > 2.0) {
                        // 2 sn'dir yok - kilidi birak, bir sonraki karede
                        // kendiliginden en yakin yeni hedefe gecer.
                        tracker.Reset();
                    }
                }
                tracker.noProgSec = std::chrono::duration<double>(nowTick - tracker.lastProgressAt).count();

                // Merkezden hedefe cizgi + anlik mesafe
                if (tracker.seenThisFrame) {
                    cv::Scalar lineCol = tracker.arrived        ? cv::Scalar(0, 255, 0)
                                          : tracker.progressedThisFrame ? cv::Scalar(0, 255, 255)
                                                                         : cv::Scalar(0, 165, 255);
                    cv::line(display, cv::Point((int)anchor.x, (int)anchor.y),
                              cv::Point((int)tracker.pos.x, (int)tracker.pos.y), lineCol, 2);
                    cv::circle(display, cv::Point((int)tracker.pos.x, (int)tracker.pos.y), 7, lineCol, 2);
                    PutLabel(display, cv::format("d=%.0fpx", tracker.curDist),
                              cv::Point((int)((anchor.x + tracker.pos.x) / 2),
                                        (int)((anchor.y + tracker.pos.y) / 2)),
                              lineCol, 0.55);
                }
            }

            // --- A-T PRO paneli (sag ust) ---
            {
                int px = display.cols - 430, py = 30;
                auto row = [&](const std::string& s, cv::Scalar c) {
                    PutLabel(display, s, cv::Point(px, py), c, 0.5);
                    py += 20;
                };
                row(config.at_pro_enabled ? "A-T PRO (config: ACIK)" : "A-T PRO (config: kapali - izleme yine calisir)",
                     cv::Scalar(255, 255, 255));
                if (!tracker.locked) {
                    row("Izlenecek metin YOK (kadrajda aday tespit yok)", cv::Scalar(180, 180, 180));
                } else if (tracker.arrived) {
                    row("VARDIK (kutu >= %" + std::to_string((int)(config.at_pro_arrived_box_ratio * 100)) +
                             " kare yuk.) -> A-T Pro SUSAR",
                         cv::Scalar(0, 255, 0));
                    row(cv::format("h=%.0f  esik=%.0f", tracker.curBoxH,
                                    frame.rows * config.at_pro_arrived_box_ratio),
                         cv::Scalar(0, 255, 0));
                } else if (!tracker.seenThisFrame) {
                    row("Hedef bu karede GORUNMUYOR - sayac durduruldu", cv::Scalar(0, 165, 255));
                } else {
                    row(cv::format("h=%.0f  baseH=%.0f  buyume=%.1fpx (esik %.1fpx)", tracker.curBoxH,
                                    tracker.baseBoxH, tracker.growth, frame.rows * config.at_pro_min_box_growth_ratio),
                         tracker.growth >= frame.rows * config.at_pro_min_box_growth_ratio ? cv::Scalar(0, 255, 0)
                                                                           : cv::Scalar(200, 200, 200));
                    row(cv::format("d=%.0f  baseD=%.0f  (yaklasma esigi %.0fpx)", tracker.curDist,
                                    tracker.baseDist, frame.rows * config.at_pro_min_progress_ratio),
                         cv::Scalar(200, 200, 200));
                    row(tracker.progressedThisFrame ? ">>> ILERLEME VAR - sayac SIFIRLANDI"
                                                     : "    ilerleme yok (bu kare)",
                         tracker.progressedThisFrame ? cv::Scalar(0, 255, 0) : cv::Scalar(160, 160, 160));
                    bool wouldFire = tracker.noProgSec > config.at_pro_no_progress_sec;
                    row(cv::format("ilerlemesiz: %.1f sn / %.1f sn %s", tracker.noProgSec,
                                    config.at_pro_no_progress_sec, wouldFire ? " -> TETIKLENIRDI!" : ""),
                         wouldFire ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 220, 255));
                }
            }


            // --- NESNE MARKET SIMULASYONU: bot.cpp CheckAndDismissNesneMarket
            // ile BIREBIR AYNI bolge-kirpma + cok-olcekli eslesme, ama ESC
            // GONDERMEZ - sadece canli skoru ekrana yazar (kullanici talebi:
            // "esigi debug preview'den canli goreyim"). ---
            if (!nesneMarketTemplBgr.empty()) {
                // bot.cpp CheckAndDismissNesneMarket ile AYNI otonom bant:
                // oran YOK - banner ust-orta bolgede ARANARAK bulunur.
                const int nmBandW = std::min(frame.cols, 700);
                cv::Rect nmSearchArea((frame.cols - nmBandW) / 2, 0, nmBandW, std::min(frame.rows, 320));
                nmSearchArea &= cv::Rect(0, 0, frame.cols, frame.rows);
                cv::Mat nmSearchMat = frame(nmSearchArea);
                // Arama bolgesini HER ZAMAN ciz (esik gecilsin gecilmesin) -
                // "hangi bolgede aradigini" gozle dogrulamak icin.
                cv::rectangle(display, nmSearchArea, cv::Scalar(180, 180, 255), 1);

                double bestScore = -1.0;
                cv::Rect bestBox;
                // bot.cpp CheckAndDismissNesneMarket ile AYNI genis olcek taramasi.
                static const double kScales[] = {0.85, 0.9, 0.95, 1.0, 1.05, 1.1, 1.15, 1.25, 1.4};
                for (double sc : kScales) {
                    // bot.cpp CheckAndDismissNesneMarket ile BIREBIR AYNI
                    // yontem (TM_CCOEFF_NORMED, maskesiz) - bkz. oradaki
                    // "KANITLANMIS" yorum: TM_CCORR_NORMED bos arka planla
                    // banner'i ayirt edemiyordu (0.95 vs 1.00).
                    cv::Mat tmpl;
                    cv::resize(nesneMarketTemplBgr, tmpl, cv::Size(), sc, sc, cv::INTER_LINEAR);
                    if (tmpl.cols < 4 || tmpl.rows < 4 || tmpl.cols >= nmSearchMat.cols ||
                        tmpl.rows >= nmSearchMat.rows)
                        continue;
                    cv::Mat result;
                    cv::matchTemplate(nmSearchMat, tmpl, result, cv::TM_CCOEFF_NORMED);
                    double minV, maxV;
                    cv::Point minLoc, maxLoc;
                    cv::minMaxLoc(result, &minV, &maxV, &minLoc, &maxLoc);
                    if (maxV > bestScore) {
                        bestScore = maxV;
                        bestBox = cv::Rect(nmSearchArea.x + maxLoc.x, nmSearchArea.y + maxLoc.y, tmpl.cols,
                                             tmpl.rows);
                    }
                }
                bool found = bestScore >= config.nesne_market_match_threshold;
                cv::Scalar nmCol = found ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 200, 200);
                if (bestScore >= 0.0) {
                    cv::rectangle(display, bestBox, nmCol, 2);
                }
                PutLabel(display,
                          cv::format("NESNE MARKET: skor=%.3f esik=%.3f %s", bestScore,
                                      config.nesne_market_match_threshold, found ? "ESC BASARDI" : "sessiz kalirdi"),
                          cv::Point(10, 125), nmCol, 0.55);
            }

            // --- HP cubugu (cift dogrulama) - panel.matchRect ARANAN DAR
            // bolgenin kendisi (PHANTOM ile ayni: ust %2-%7, orta %30-%70). ---
            auto panel = panelDetector.Read(frame);
            if (!panel.hasTemplate) {
                cv::putText(display, "HP CUBUGU: referans YOK (hp_bar/_hpbar.png alinmadi)", cv::Point(10, 50),
                            cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(128, 128, 128), 2);
            } else if (panel.visible) {
                cv::rectangle(display, panel.matchRect, cv::Scalar(0, 255, 0), 2);
                char label[128];
                snprintf(label, sizeof(label), "HP CUBUGU BULUNDU skor=%.2f (esik=%.2f)", panel.bestScore,
                          config.status_panel_name_match_threshold);
                cv::putText(display, label, cv::Point(10, 50), cv::FONT_HERSHEY_SIMPLEX, 0.55,
                            cv::Scalar(0, 255, 0), 2);
            } else {
                cv::rectangle(display, panel.matchRect, cv::Scalar(0, 0, 255), 2);
                char label[160];
                snprintf(label, sizeof(label), "HP CUBUGU YOK - en yuksek skor=%.2f (esik=%.2f)", panel.bestScore,
                          config.status_panel_name_match_threshold);
                cv::putText(display, label, cv::Point(10, 50), cv::FONT_HERSHEY_SIMPLEX, 0.55,
                            cv::Scalar(0, 0, 255), 2);
            }

            // --- Basit "sonraki adim" ozeti (botun TAM durum makinesi
            // DEGIL - sadece bu karedeki ham sinyallere gore kaba bir tahmin,
            // gorsel dogrulama icin yeterli). ---
            std::string nextAction;
            if (panel.hasTemplate && panel.visible) {
                nextAction = "SONRAKI: hedefte kilitli, devam (Z ile loot, kesilene kadar bekler)";
            } else if (totalDetections > 0) {
                nextAction = "SONRAKI: bir metin tespit edildi, tiklanacak";
            } else {
                nextAction = "SONRAKI: hicbir sey tespit edilmedi, kamera donup aranacak";
            }
            cv::putText(display, nextAction, cv::Point(10, display.rows - 15), cv::FONT_HERSHEY_SIMPLEX, 0.55,
                        cv::Scalar(0, 220, 255), 2);

            auto now = std::chrono::steady_clock::now();
            fps = 0.9 * fps + 0.1 * (1.0 / std::max(1e-6, std::chrono::duration<double>(now - prevTime).count()));
            prevTime = now;

            char info[192];
            snprintf(info, sizeof(info), "FPS:%.1f  YOLO:%d (kor:%d minik:%d)  minH=%.0fpx  [R=sifirla Q=cikis]",
                      fps, totalDetections, blindRejected, smallRejected,
                      frame.rows * config.min_detection_height_ratio);
            cv::putText(display, info, cv::Point(10, 25), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 200, 255),
                        2);

            // CANLI IMLEC KONUMU (kullanici talebi: "Nesne Market icin HP bar
            // gibi koordinat gireyim" - koordinatlari BULMAK icin): gercek
            // ekran imlec konumunu, GetCapturedFrameRectOnScreen ile AYNI 1:1
            // donusumle (bkz. bot.cpp FrameToScreenPoint duzeltmesi) KARE-uzayi
            // koordinatina cevirip yaziyoruz - buradan okunan X/Y, dogrudan
            // config.json / arayuzdeki nesne_market_region_* alanlarina
            // yazilabilir.
            {
                RECT capRect = win_window::GetCapturedFrameRectOnScreen(hwnd);
                POINT cur{};
                GetCursorPos(&cur);
                int fx = cur.x - (int)capRect.left;
                int fy = cur.y - (int)capRect.top;
                char curInfo[96];
                snprintf(curInfo, sizeof(curInfo), "IMLEC (kare-uzayi): X=%d Y=%d", fx, fy);
                PutLabel(display, curInfo, cv::Point(10, 100), cv::Scalar(255, 255, 0), 0.55);
            }

            cv::imshow(winName, display);
            int key = cv::waitKey(15) & 0xFF;
            if (key == 'q' || key == 'Q') break;
            if (key == 'r' || key == 'R') {
                tracker.Reset();  // izlemeyi sifirla - bir sonraki karede yine otomatik kilitlenir
            }
        }

        capture.Stop();
        cv::destroyAllWindows();
        return 0;
    } catch (const winrt::hresult_error& e) {
        std::cerr << "[hata] Windows/WinRT hatasi: " << winrt::to_string(e.message()) << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[hata] " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[hata] Bilinmeyen bir hata olustu.\n";
    }
    std::cout << "\nKapatmak icin Enter'a basin...";
    std::cin.get();
    return 1;
}
