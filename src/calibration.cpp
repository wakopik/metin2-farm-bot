#include "calibration.h"

#include <windows.h>
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>

#include "capture.h"
#include "config.h"
#include "diag_log.h"
#include "input.h"
#include "status_panel_detector.h"
#include "win_window.h"
#include "yolo_detector.h"

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::mutex g_mutex;
CalibrationStatus g_status;
std::atomic<bool> g_busy{false};

// DIL (kullanici talebi - arayuz TR/EN): 0 = Turkce, 1 = Ingilizce. Kalibrasyon
// baslatilirken /api/calibrate/start'tan gelen "lang" ile ayarlanir (bkz.
// StartCalibration). L(tr, en) o an secili dildeki metni dondurur - boylece
// tum kalibrasyon ciktisi (satirlar + faz basliklari) arayuz diline uyar.
std::atomic<int> g_calLang{0};
inline std::string L(const char* tr, const char* en) { return g_calLang.load() ? std::string(en) : std::string(tr); }

void SetPhase(const std::string& phase, int pct) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_status.phase = phase;
    g_status.progressPct = pct;
}
void AddLine(const std::string& s) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_status.lines.push_back(s);
    LogDiag("CAL " + s);
}
void Fail(const std::string& e) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_status.error = e;
    g_status.running = false;
    g_status.done = true;
    LogDiag("CAL HATA " + e);
}

// ---- POZITIF TARAF UST SINIRLARI (GERCEK GORUNTULERLE OLCULDU) ----------
// Kalibrasyon sirasinda sinyaller EKRANDA YOK, yani yalnizca NEGATIF taban
// olculebilir. Esigi tabanin uzerine koymak tek basina yetmez: esik, sinyal
// GERCEKTEN ekrandayken aldigi skorun da ALTINDA kalmali. O skorlar daha once
// gercek oyun kareleriyle olculdu ve buraya UST SINIR olarak gomuldu:
//   HP cubugu   : can %100 -> 0.987, %75 -> 0.737, %50/%25 -> 0.686
//   Nesne Market: bildirim varken 1.000
//   Bulmaca basligi: gercek bulmacada 0.716
constexpr double kHpMaxThr = 0.60, kHpMinThr = 0.35, kHpMargin = 0.12;
constexpr double kNmMaxThr = 0.97, kNmMinThr = 0.85, kNmMargin = 0.06;

double Pct(std::vector<double> v, double q) {
    if (v.empty()) return -1;
    std::sort(v.begin(), v.end());
    size_t i = (size_t)std::lround(q * (v.size() - 1));
    return v[std::min(i, v.size() - 1)];
}
std::string F2(double v) {
    char b[32];
    snprintf(b, sizeof(b), "%.3f", v);
    return b;
}

bool WriteConfigKeys(const std::vector<std::pair<std::string, double>>& kv, std::string& err) {
    const char* kPath = "config.json";
    json j;
    {
        std::ifstream in(kPath);
        if (in.is_open()) {
            try {
                in >> j;
            } catch (const std::exception& e) {
                err = L("config.json okunamadi: ", "could not read config.json: ") + e.what();
                return false;
            }
        } else {
            j = json::object();
        }
    }
    for (auto& [k, v] : kv) j[k] = v;
    std::ofstream out(kPath);
    if (!out.is_open()) {
        err = L("config.json yazilamadi (dosya kilitli olabilir)", "could not write config.json (file may be locked)");
        return false;
    }
    out << j.dump(2);
    return true;
}

bool ProcessIsElevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION el{};
    DWORD sz = sizeof(el);
    bool ok = GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &sz) && el.TokenIsElevated;
    CloseHandle(tok);
    return ok;
}

// ============ ASAMA 1: SISTEM / GIRDI AYARLARI ============================
// Bu oturumda YASANMIS gercek sorun siniflarini kontrol eder.
void CheckSystemSettings() {
    // FARE HIZLANDIRMA ("Isaretci hassasiyetini artir"). ACIKKEN Windows,
    // fare hareketini DOGRUSAL OLMAYAN sekilde olcekler; bot ise kameray
    // "su kadar piksel surukle" diye cevirir - hizlandirma varken ayni
    // surukleme her seferinde FARKLI aci dondurur ve tiklama noktalari kayar.
    int mp[3] = {0, 0, 0};
    SystemParametersInfoA(SPI_GETMOUSE, 0, mp, 0);
    int speed = 0;
    SystemParametersInfoA(SPI_GETMOUSESPEED, 0, &speed, 0);
    AddLine(L("Fare: hizlandirma=", "Mouse: acceleration=") + std::to_string(mp[2]) +
             L(" esik1=", " thr1=") + std::to_string(mp[0]) + L(" esik2=", " thr2=") + std::to_string(mp[1]) +
             L(" hiz=", " speed=") + std::to_string(speed) + "/20");
    if (mp[2] != 0) {
        AddLine(L("UYARI Fare hizlandirma ACIK - kamera cevirme ve tiklama KAYAR.",
                  "WARN Mouse acceleration is ON - camera turns and clicks WILL DRIFT."));
        AddLine(L("  Duzeltme: Ayarlar > Bluetooth ve cihazlar > Fare > Fare ek ayarlari >",
                  "  Fix: Settings > Bluetooth & devices > Mouse > Additional mouse settings >"));
        AddLine(L("  Isaretci Secenekleri > 'Isaretci hassasiyetini artir' TIKINI KALDIRIN.",
                  "  Pointer Options > UNCHECK 'Enhance pointer precision'."));
    } else {
        AddLine(L("Fare hizlandirma KAPALI - dogru ayar.", "Mouse acceleration is OFF - correct setting."));
    }
    if (speed != 10) {
        AddLine(L("UYARI Fare hizi 10 degil (", "WARN Mouse speed is not 10 (") + std::to_string(speed) +
                 L(") - kamera cevirme miktari beklenenden farkli olabilir.",
                   ") - camera turn amount may differ from expected."));
    }

    // YONETICI YETKISI: oyun Yonetici olarak calisiyorsa ve biz DEGILSEK,
    // Windows UIPI TUM girdimizi SESSIZCE engeller (hicbir hata vermeden).
    AddLine(L("Yonetici yetkisi: ", "Administrator rights: ") +
             (ProcessIsElevated() ? L("VAR", "YES") : L("YOK", "NO")));
    if (!ProcessIsElevated()) {
        AddLine(L("UYARI Yonetici DEGIL - oyun Yonetici olarak calisiyorsa tiklamalar",
                  "WARN Not Administrator - if the game runs as Administrator, clicks"));
        AddLine(L("  SESSIZCE engellenir. Elbit.exe'yi 'Yonetici olarak calistir' ile acin.",
                  "  are SILENTLY blocked. Open Elbit.exe with 'Run as administrator'."));
    }
}

// ============ ASAMA 2: IMLEC KONUMLANDIRMA DOGRULUGU =====================
// Bot, tiklamadan once imleci hedef piksele koyar. Fare hizlandirma, DPI
// sanallastirmasi veya surucu araya girerse imlec ISTENEN yere GITMEZ - ve
// bu, "tiklamiyor / yanlis yere tikliyor" siniflarinin en sinsi sebebidir.
// Burada olculebilir bir test yapiyoruz: bilinen noktalara koy, geri oku.
int MeasureCursorAccuracy(HWND hwnd, InputController& input) {
    RECT wr = win_window::GetWindowRectOnScreen(hwnd);
    POINT saved{};
    GetCursorPos(&saved);
    const int pts[5][2] = {{30, 30}, {-30, 30}, {30, -30}, {-30, -30}, {0, 0}};
    int worst = 0;
    for (auto& p : pts) {
        int tx = (p[0] >= 0) ? (int)wr.left + 60 + p[0] : (int)wr.right - 60 + p[0];
        int ty = (p[1] >= 0) ? (int)wr.top + 60 + p[1] : (int)wr.bottom - 60 + p[1];
        input.PlaceCursor(tx, ty);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        POINT got{};
        GetCursorPos(&got);
        int err = std::max(std::abs(got.x - tx), std::abs(got.y - ty));
        worst = std::max(worst, err);
    }
    SetCursorPos(saved.x, saved.y);
    return worst;
}

// ============ EKRAN OLCEGI (DPI) - TESPIT + OTOMATIK DUZELTME =============
// AMAC (kullanici talebi): laptoplarda Windows'un varsayilan onerdigi ekran
// olcegi cogunlukla %125 oluyor; oyun DPI-farkinda-olmayan eski bir istemci
// oldugundan bu, DWM'in oyunu yeniden olceklemesine ve tiklama koordinatlarinin
// kaymasina yol acabiliyor. Burada SADECE oyun penceresinin bulundugu monitorun
// olcegini, Windows'un kendi Ayarlar > Ekran > Olcek kaydiricisinin de kullandigi
// DisplayConfigGetDeviceInfo/DisplayConfigSetDeviceInfo API'siyle (belgeli
// fonksiyonlar, sadece DPI-olcek turu -3/-4 belgelenmemis ama yillardir kararli)
// %100'e cekiyoruz. Registry'e DOGRUDAN YAZMIYORUZ - bu yuzden yanlis monitoru
// etkileme ya da gecersiz bir deger yazma riski yok: Windows kendi izin verilen
// araligini (min/max) raporluyor, %100 HER ZAMAN bu araligin alt sinirindadir.
// "DUZELTTIM" DEMEDEN ONCE DOGRULARIZ: yazdiktan sonra tekrar okuyup GERCEKTEN
// %100 olup olmadigina bakariz - basarisizsa oldugu gibi raporlanir, uydurulmaz.
struct DpiScaleGetPacket {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    INT32 minScaleRel;
    INT32 curScaleRel;
    INT32 maxScaleRel;
};
struct DpiScaleSetPacket {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    INT32 scaleRel;
};
constexpr int kDciGetDpiScale = -3;  // DISPLAYCONFIG_DEVICE_INFO_GET_DPI_SCALE (belgesiz ama kararli)
constexpr int kDciSetDpiScale = -4;  // DISPLAYCONFIG_DEVICE_INFO_SET_DPI_SCALE
constexpr UINT32 kDpiSteps[] = {100, 125, 150, 175, 200, 225, 250, 300, 350, 400, 450, 500};
constexpr int kDpiStepsN = (int)(sizeof(kDpiSteps) / sizeof(kDpiSteps[0]));

// hwnd'nin uzerinde oldugu monitorun (adapterId, sourceId) ciftini, QueryDisplayConfig
// ile aktif yollari GDI cihaz adiyla (\\.\DISPLAYn) eslestirerek bulur.
bool FindMonitorSource(HWND hwnd, LUID& outAdapterId, UINT32& outSourceId) {
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!hMon || !GetMonitorInfoW(hMon, &mi)) return false;

    UINT32 numPaths = 0, numModes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &numPaths, &numModes) != ERROR_SUCCESS) return false;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(numPaths);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(numModes);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &numPaths, paths.data(), &numModes, modes.data(), nullptr) !=
        ERROR_SUCCESS)
        return false;

    for (auto& p : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME srcName{};
        srcName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        srcName.header.size = sizeof(srcName);
        srcName.header.adapterId = p.sourceInfo.adapterId;
        srcName.header.id = p.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&srcName.header) != ERROR_SUCCESS) continue;
        if (lstrcmpiW(srcName.viewGdiDeviceName, mi.szDevice) == 0) {
            outAdapterId = p.sourceInfo.adapterId;
            outSourceId = p.sourceInfo.id;
            return true;
        }
    }
    return false;
}

struct DpiScaleInfo {
    UINT32 current = 100, minimum = 100, maximum = 100, recommended = 100;
    bool ok = false;
};
DpiScaleInfo ReadDpiScale(LUID adapterId, UINT32 sourceId) {
    DpiScaleInfo out;
    DpiScaleGetPacket req{};
    req.header.type = (DISPLAYCONFIG_DEVICE_INFO_TYPE)kDciGetDpiScale;
    req.header.size = sizeof(req);
    req.header.adapterId = adapterId;
    req.header.id = sourceId;
    if (DisplayConfigGetDeviceInfo(&req.header) != ERROR_SUCCESS) return out;
    if (req.curScaleRel < req.minScaleRel) req.curScaleRel = req.minScaleRel;
    if (req.curScaleRel > req.maxScaleRel) req.curScaleRel = req.maxScaleRel;
    int minAbs = std::abs(req.minScaleRel);
    if (minAbs + req.maxScaleRel + 1 > kDpiStepsN || minAbs + req.curScaleRel < 0) return out;  // savunma
    out.current = kDpiSteps[minAbs + req.curScaleRel];
    out.recommended = kDpiSteps[minAbs];
    out.minimum = kDpiSteps[minAbs + req.minScaleRel];  // belgeye gore her zaman 100
    out.maximum = kDpiSteps[minAbs + req.maxScaleRel];
    out.ok = true;
    return out;
}
bool SetDpiScale(LUID adapterId, UINT32 sourceId, UINT32 targetPercent, const DpiScaleInfo& info) {
    int idxTarget = -1, idxRecommended = -1;
    for (int i = 0; i < kDpiStepsN; ++i) {
        if (kDpiSteps[i] == targetPercent) idxTarget = i;
        if (kDpiSteps[i] == info.recommended) idxRecommended = i;
    }
    if (idxTarget < 0 || idxRecommended < 0) return false;

    DpiScaleSetPacket req{};
    req.header.type = (DISPLAYCONFIG_DEVICE_INFO_TYPE)kDciSetDpiScale;
    req.header.size = sizeof(req);
    req.header.adapterId = adapterId;
    req.header.id = sourceId;
    req.scaleRel = idxTarget - idxRecommended;
    return DisplayConfigSetDeviceInfo(&req.header) == ERROR_SUCCESS;
}

// Oyun penceresinin monitorunu %100 olcege cekmeyi DENER, rapora ne oldugunu yazar.
void CheckAndFixDisplayScale(HWND hwnd) {
    LUID adapterId{};
    UINT32 sourceId = 0;
    if (!FindMonitorSource(hwnd, adapterId, sourceId)) {
        AddLine(L("Ekran olcegi: oyun penceresinin monitoru tespit edilemedi, olcek kontrolu atlandi.",
                  "Display scale: could not identify the game window's monitor, scale check skipped."));
        return;
    }
    DpiScaleInfo info = ReadDpiScale(adapterId, sourceId);
    if (!info.ok) {
        AddLine(L("Ekran olcegi: okunamadi (bu Windows surumunde API farkli davraniyor olabilir).",
                  "Display scale: could not read (the API may behave differently on this Windows version)."));
        return;
    }
    AddLine(L("Ekran olcegi (oyunun oldugu monitor): %", "Display scale (the game's monitor): ") +
             std::to_string(info.current) + L(" (onerilen %", "% (recommended ") +
             std::to_string(info.recommended) + L(", izin verilen aralik %", "%, allowed range ") +
             std::to_string(info.minimum) + "-" + std::to_string(info.maximum) + "%)");
    if (info.current == 100) {
        AddLine(L("  Zaten %100 - ideal, dokunulmadi.", "  Already 100% - ideal, left untouched."));
        return;
    }
    AddLine(L("  UYARI %100 degil - laptoplarda sik gorulen bir durum, otomatik %100'e cekiliyor...",
              "  WARN Not 100% - common on laptops, automatically setting to 100%..."));
    bool setOk = SetDpiScale(adapterId, sourceId, 100, info);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    DpiScaleInfo after = ReadDpiScale(adapterId, sourceId);  // yazdik demekle yetinmiyoruz, DOGRULUYORUZ
    if (setOk && after.ok && after.current == 100) {
        AddLine(L("  Basarili: ekran olcegi %100'e cekildi (dogrulandi).",
                  "  Success: display scale set to 100% (verified)."));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));  // DWM'in yeni olcekle yeniden cizmesi icin
    } else {
        AddLine(L("  UYARI Otomatik duzeltme basarisiz oldu (mevcut: %", "  WARN Automatic fix failed (current: ") +
                 std::to_string(after.ok ? after.current : info.current) +
                 L("%) - elle duzeltin: Ayarlar > Sistem > Ekran > Olcek > %100.",
                   "%) - fix manually: Settings > System > Display > Scale > 100%."));
    }
}

}  // namespace

namespace {

void RunCalibration(std::string titleHint) {
    try {
        SetPhase(L("Ayarlar okunuyor...", "Reading settings..."), 2);
        Config config = Config::Load("config.json");
        if (!titleHint.empty()) config.window_title_hint = titleHint;

        AddLine(L("=== 1/6  SISTEM VE GIRDI AYARLARI ===", "=== 1/6  SYSTEM AND INPUT SETTINGS ==="));
        SetPhase(L("Sistem ayarlari kontrol ediliyor...", "Checking system settings..."), 5);
        CheckSystemSettings();

        SetPhase(L("Oyun penceresi araniyor...", "Looking for game window..."), 9);
        HWND hwnd = win_window::FindGameWindow(config.window_title_hint);
        if (!hwnd) {
            Fail(L("Oyun penceresi bulunamadi ('", "Game window not found ('") + config.window_title_hint +
                  L("'). Oyun acik mi, sunucu secimi dogru mu?", "'). Is the game open, is the server selection correct?"));
            return;
        }
        win_window::BringToForeground(hwnd);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        AddLine(L("Oyun penceresi: '", "Game window: '") + win_window::GetWindowTitle(hwnd) + "'");
        CheckAndFixDisplayScale(hwnd);

        WindowCapture capture(hwnd, /*minIntervalMs=*/0);
        InputController input(config);
        input.SetBounds(hwnd);

        // ---- ASAMA 2: GEOMETRI + IMLEC ----
        AddLine("");
        AddLine(L("=== 2/6  TIKLAMA KOORDINATI ===", "=== 2/6  CLICK COORDINATE ==="));
        SetPhase(L("Tiklama koordinati dogrulaniyor...", "Verifying click coordinate..."), 14);
        cv::Mat probe;
        {
            auto f = capture.GetFrame();
            for (int i = 0; i < 60 && !f; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                f = capture.GetFrame();
            }
            if (!f) {
                Fail(L("Ekran goruntusu alinamadi - pencere simge durumunda olabilir.",
                       "Could not capture the screen - the window may be minimized."));
                return;
            }
            probe = *f;
            RECT fr = win_window::GetCapturedFrameRectOnScreen(hwnd);
            int fw = (int)(fr.right - fr.left), fh = (int)(fr.bottom - fr.top);
            AddLine(L("Kare boyutu: ", "Frame size: ") + std::to_string(probe.cols) + "x" + std::to_string(probe.rows));
            int dw = std::abs(fw - probe.cols), dh = std::abs(fh - probe.rows);
            if (dw <= 2 && dh <= 2) {
                AddLine(L("Kare/pencere ortusmesi: UYUMLU", "Frame/window match: OK"));
            } else {
                AddLine(L("UYARI Kare ", "WARN Frame ") + std::to_string(probe.cols) + "x" + std::to_string(probe.rows) +
                         L(" ama pencere cercevesi ", " but window frame ") + std::to_string(fw) + "x" + std::to_string(fh) +
                         L(" - fark ", " - diff ") + std::to_string(dw) + "x" + std::to_string(dh) +
                         L(" px, tiklama kayabilir.", " px, clicks may drift."));
            }
        }
        int curErr = MeasureCursorAccuracy(hwnd, input);
        AddLine(L("Imlec konumlandirma hatasi: ", "Cursor placement error: ") + std::to_string(curErr) + " px");
        if (curErr == 0) {
            AddLine(L("  Imlec istenen piksele TAM oturuyor - ideal.", "  Cursor lands EXACTLY on the target pixel - ideal."));
        } else if (curErr <= 2) {
            AddLine(L("  Kucuk sapma - kabul edilebilir.", "  Small deviation - acceptable."));
        } else {
            AddLine(L("UYARI Imlec istenen yere OTURMUYOR (", "WARN Cursor does NOT land where intended (") +
                     std::to_string(curErr) +
                     L(" px sapma) - fare hizlandirma / DPI olcekleme / surucu sebebi olabilir.",
                       " px off) - could be mouse acceleration / DPI scaling / driver."));
        }

        // ---- ASAMA 3: GIRDI GERCEKTEN OYUNA GIDIYOR MU ----
        AddLine("");
        AddLine(L("=== 3/6  GIRDI OYUNA ULASIYOR MU ===", "=== 3/6  DOES INPUT REACH THE GAME ==="));
        SetPhase(L("Kamera cevirme testi...", "Camera turn test..."), 20);
        {
            // Kamerayi cevirip GORUNTUNUN GERCEKTEN DEGISTIGINI olcuyoruz.
            // Degismiyorsa girdi oyuna ULASMIYOR demektir (UIPI/odak sorunu) -
            // bu, "bot hicbir sey yapmiyor" sikayetinin en sik sebebi.
            cv::Mat before = probe.clone();
            RECT wr = win_window::GetWindowRectOnScreen(hwnd);
            input.PlaceCursor((int)(wr.left + wr.right) / 2, (int)(wr.top + wr.bottom) / 2);
            input.RotateView((int)config.camera_rotate_px_max);
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            auto after = capture.GetFrame();
            if (after && before.size() == after->size()) {
                cv::Mat d;
                cv::absdiff(before, *after, d);
                cv::cvtColor(d, d, cv::COLOR_BGR2GRAY);
                double changed = (double)cv::countNonZero(d > 25) / (d.rows * d.cols) * 100.0;
                AddLine(L("Kamera cevrildikten sonra degisen piksel: ", "Pixels changed after camera turn: ") +
                         F2(changed) + "%");
                if (changed < 1.0) {
                    AddLine(L("UYARI Goruntu DEGISMEDI - girdi oyuna ULASMIYOR olabilir.",
                              "WARN Image did NOT change - input may NOT be reaching the game."));
                    AddLine(L("  Sebep: Yonetici yetkisi eksik (UIPI) ya da pencere odakta degil.",
                              "  Cause: missing Administrator rights (UIPI) or window not focused."));
                } else {
                    AddLine(L("Girdi oyuna ULASIYOR - kamera cevirme calisiyor.",
                              "Input REACHES the game - camera turning works."));
                }
            }
            input.ReleaseAllButtonsAndKeys();
        }

        // ---- ASAMA 4: SABLON / MODEL ENVANTERI ----
        AddLine("");
        AddLine(L("=== 4/6  SABLONLAR VE MODELLER ===", "=== 4/6  TEMPLATES AND MODELS ==="));
        SetPhase(L("Sablonlar ve modeller kontrol ediliyor...", "Checking templates and models..."), 26);
        StatusPanelDetector panel(config);
        cv::Mat nmTemplate = cv::imread(config.nesne_market_template_path, cv::IMREAD_COLOR);

        AddLine(L("HP cubugu sablonu: ", "HP bar template: ") + (panel.HasTemplate() ? L("VAR", "YES") : L("YOK", "NO")));
        if (!panel.HasTemplate())
            AddLine(L("UYARI HP cubugu sablonu yok - hedef kilitleme dogrulanamaz.",
                      "WARN No HP bar template - target lock cannot be verified."));
        AddLine(L("Nesne Market sablonu: ", "Item Market template: ") + (nmTemplate.empty() ? L("YOK", "NO") : L("VAR", "YES")));

        std::vector<YoloDetector> yolos;
        int readyModels = 0;
        double totalMs = 0;
        for (const auto& e : config.yolo_models) {
            float thr = e.conf_threshold >= 0.0 ? (float)e.conf_threshold : (float)config.yolo_conf_threshold;
            yolos.emplace_back(e.path, e.category, config.yolo_input_size, thr,
                                (float)config.yolo_nms_threshold, e.class_index);
            if (!yolos.back().IsReady()) {
                AddLine(L("UYARI Model YUKLENEMEDI: ", "WARN Model FAILED TO LOAD: ") + e.category + " (" + e.path + ")");
                continue;
            }
            ++readyModels;
            cv::Rect all(0, 0, probe.cols, probe.rows);
            yolos.back().Detect(probe, all);  // isinma
            auto t0 = std::chrono::steady_clock::now();
            yolos.back().Detect(probe, all);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            totalMs += ms;
            AddLine(L("  model ", "  model ") + e.category + ": " + F2(ms) + " ms");
        }
        AddLine(L("Yuklu model: ", "Loaded models: ") + std::to_string(readyModels) +
                 L(", kare basina tek model ~", ", per-frame single model ~") +
                 F2(readyModels ? totalMs / readyModels : 0) + " ms");
        if (readyModels == 0)
            AddLine(L("UYARI Hicbir metin modeli yuklenemedi - bot metin bulamaz!",
                      "WARN No metin models loaded - the bot cannot find metins!"));

        // ---- ASAMA 5: UZUN GOZLEM (esik tabanlari) ----
        AddLine("");
        AddLine(L("=== 5/6  ESIK OLCUMU (ekran izleniyor, tiklama yok) ===",
                  "=== 5/6  THRESHOLD MEASUREMENT (watching screen, no clicks) ==="));
        const double kObserveSec = 90.0;
        auto t0 = std::chrono::steady_clock::now();
        auto lastRotate = t0;
        std::vector<double> hpScores, nmScores;
        std::map<std::string, int> detCount;
        std::vector<int> detHeights;
        int frames = 0;
        size_t rr = 0;

        while (true) {
            double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (el >= kObserveSec) break;
            SetPhase(L("Ekran izleniyor (", "Watching screen (") + std::to_string((int)(kObserveSec - el)) +
                      L(" sn kaldi)...", "s left)..."), 30 + (int)(58.0 * el / kObserveSec));
            auto fo = capture.GetFrame();
            if (!fo) {
                std::this_thread::sleep_for(std::chrono::milliseconds(60));
                continue;
            }
            cv::Mat frame = *fo;
            ++frames;

            if (panel.HasTemplate()) hpScores.push_back(panel.Read(frame).bestScore);
            if (!nmTemplate.empty()) {
                const int bw = std::min(frame.cols, 700);
                cv::Rect band((frame.cols - bw) / 2, 0, bw, std::min(frame.rows, 320));
                band &= cv::Rect(0, 0, frame.cols, frame.rows);
                if (band.width > nmTemplate.cols && band.height > nmTemplate.rows) {
                    cv::Mat res;
                    cv::matchTemplate(frame(band), nmTemplate, res, cv::TM_CCOEFF_NORMED);
                    double mx;
                    cv::minMaxLoc(res, nullptr, &mx, nullptr, nullptr);
                    nmScores.push_back(mx);
                }
            }
            if (!yolos.empty()) {
                cv::Rect region(0, 0, frame.cols, frame.rows);
                const YoloDetector& y = yolos[rr++ % yolos.size()];
                if (y.IsReady()) {
                    for (const auto& d : y.Detect(frame, region)) {
                        detCount[y.GetCategory()]++;
                        detHeights.push_back(d.h);
                    }
                }
            }
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - lastRotate).count() > 3.0) {
                lastRotate = std::chrono::steady_clock::now();
                if (GetForegroundWindow() == hwnd) {
                    RECT wr = win_window::GetWindowRectOnScreen(hwnd);
                    input.PlaceCursor((int)(wr.left + wr.right) / 2, (int)(wr.top + wr.bottom) / 2);
                    input.RotateView((int)config.camera_rotate_px_max);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        input.ReleaseAllButtonsAndKeys();
        AddLine(L("Incelenen kare: ", "Frames examined: ") + std::to_string(frames) +
                 L("  (yakalama ~", "  (capture ~") + F2(frames / kObserveSec) + L(" kare/sn)", " fps)"));
        if (frames < 30)
            AddLine(L("UYARI Cok az kare alindi - ekran yakalama yavas/sorunlu olabilir.",
                      "WARN Too few frames captured - screen capture may be slow/faulty."));

        // ---- ASAMA 6: SONUC + YAZMA ----
        AddLine("");
        AddLine(L("=== 6/6  ESIKLER HESAPLANIYOR ===", "=== 6/6  CALCULATING THRESHOLDS ==="));
        SetPhase(L("Sonuclar hesaplaniyor...", "Calculating results..."), 92);
        std::vector<std::pair<std::string, double>> toWrite;
        auto recommend = [&](const std::string& name, const char* key, std::vector<double>& v, double margin,
                              double lo, double hi, double current) {
            if (v.size() < 20) {
                AddLine(name + L(": yeterli olcum yok - DEGISTIRILMEDI", ": not enough measurements - UNCHANGED"));
                return;
            }
            double base = Pct(v, 0.99);
            double rec = std::min(hi, std::max(lo, base + margin));
            AddLine(name + L(": taban=", ": base=") + F2(base) + L(" -> esik=", " -> threshold=") + F2(rec) +
                     L("  (eski ", "  (old ") + F2(current) + ")");
            if (base + margin > hi)
                AddLine(L("  UYARI Taban yuksek - esik ust sinira dayandi, yanlis pozitif riski var.",
                          "  WARN Base is high - threshold hit the upper limit, false-positive risk."));
            toWrite.push_back({key, rec});
        };
        recommend(L("HP cubugu", "HP bar"), "status_panel_name_match_threshold", hpScores, kHpMargin, kHpMinThr,
                   kHpMaxThr, config.status_panel_name_match_threshold);
        if (!nmTemplate.empty())
            recommend(L("Nesne Market", "Item Market"), "nesne_market_match_threshold", nmScores, kNmMargin,
                       kNmMinThr, kNmMaxThr, config.nesne_market_match_threshold);

        int totalDet = 0;
        for (auto& [c, n] : detCount) totalDet += n;
        AddLine(L("Metin tespiti: ", "Metin detections: ") + std::to_string(totalDet) + L(" adet / ", " total / ") +
                 std::to_string(detCount.size()) + L(" kategori", " categories"));
        if (!detHeights.empty()) {
            std::sort(detHeights.begin(), detHeights.end());
            AddLine(L("  kutu yuksekligi min=", "  box height min=") + std::to_string(detHeights.front()) +
                     L(" orta=", " mid=") + std::to_string(detHeights[detHeights.size() / 2]) + " max=" +
                     std::to_string(detHeights.back()));
            AddLine(L("  (boyut filtresi KALIBRE EDILMEZ - metin/hayvan ayrimi etiketli veri ister;",
                      "  (size filter is NOT calibrated - metin/animal distinction needs labeled data;"));
            AddLine(L("   tek ornekten tahmin etmek daha once TUM tespitleri elemisti)",
                      "   guessing from a single sample once eliminated ALL detections)"));
        }
        if (totalDet == 0)
            AddLine(L("UYARI Hic metin tespit edilmedi - karakter metin bolgesinde mi, modeller dogru mu?",
                      "WARN No metins detected - is the character in a metin area, are the models correct?"));

        std::string err;
        SetPhase(L("config.json yaziliyor...", "Writing config.json..."), 97);
        if (toWrite.empty()) {
            AddLine(L("Yazilacak yeni esik yok.", "No new thresholds to write."));
        } else if (WriteConfigKeys(toWrite, err)) {
            AddLine(L("config.json guncellendi (", "config.json updated (") + std::to_string(toWrite.size()) +
                     L(" esik).", " thresholds)."));
            AddLine(L("Degisikliklerin gecerli olmasi icin botu YENIDEN BASLATIN.",
                      "RESTART the bot for the changes to take effect."));
        } else {
            AddLine(L("HATA ", "ERROR ") + err);
        }

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_status.running = false;
            g_status.done = true;
            g_status.progressPct = 100;
            g_status.phase = L("Kalibrasyon tamamlandi", "Calibration complete");
        }
    } catch (const std::exception& e) {
        Fail(L("Beklenmeyen hata: ", "Unexpected error: ") + e.what());
    } catch (...) {
        Fail(L("Beklenmeyen hata.", "Unexpected error."));
    }
    g_busy = false;
}

}  // namespace

void StartCalibration(const std::string& windowTitleHint, const std::string& lang) {
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true)) return;
    g_calLang.store(lang == "en" ? 1 : 0);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_status = CalibrationStatus{};
        g_status.running = true;
        g_status.phase = g_calLang.load() ? "Starting..." : "Baslatiliyor...";
    }
    std::thread([windowTitleHint]() { RunCalibration(windowTitleHint); }).detach();
}

CalibrationStatus GetCalibrationStatus() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_status;
}
