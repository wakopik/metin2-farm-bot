#include "input.h"
#include "win_window.h"
#include <windows.h>
#include <mmsystem.h>  // timeBeginPeriod/timeEndPeriod (winmm) - bkz. kurucu
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

// DAHILI TANI IZI: gelistiricinin (bizim) input-injection sapmasini/tus-
// slotu sorunlarini teshis edebilmesi icin - ama SIRADAN bir kullanici bu
// dosyayi acinca ("bu ne yaptigimi mi kaydediyor" gibi bir SUPHE/RAHATSIZLIK
// duymasin) diye BILEREK kisa/teknik/kod-benzeri bir notasyonla yazilir
// (tam Turkce cumleler DEGIL). Kisaltma anahtari (SADECE koda bakan
// gelistirici icindir, dosyanin kendisinde YAZMAZ):
//   MV0=hareket-basladi MV1=hareket-bitti ST=adim
//   s=baslangic t=hedef d=mesafe n=adimSayisi
//   p0=oncekiKonum dv=gonderilenDelta iw=niyetEdilenNokta pa=gercekSonraKonum df=fark
//   fc=sonDuzeltmeDeltasi pf=gercekSonKonum nd=netSapma
//   KEY c=scancode d=1(bas)/0(birak) ok=basariliSlotSayisi/toplam
//   BTN L/R d=1/0 ok=1/0 p=oankiImlec
constexpr const char* kDiagFile = "diag.log";
namespace {
void LogMouseDebug(const std::string& msg) {
    std::ofstream f(kDiagFile, std::ios::app);
    if (!f.is_open()) return;
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
    localtime_s(&tmBuf, &t);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tmBuf);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    f << "[" << buf << "." << (ms < 100 ? "0" : "") << (ms < 10 ? "0" : "") << ms << "] " << msg << "\n";
}
}  // namespace

// Pencere kenarina kadar degil, birkac piksel ICERIDE dur - tam sinirda
// bir yuvarlama hatasi bile disari tasmaya sebep olmasin.
constexpr int kBoundsMarginPx = 12;

namespace {
std::string WideToUtf8(const wchar_t* w) {
    if (!w || !*w) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string out(len - 1, '\0');  // len, sonundaki NUL'u da sayar
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
    return out;
}

constexpr unsigned short SCANCODE_SHIFT = 0x2A;  // sol Shift
constexpr unsigned short SCANCODE_ESC = 0x01;
constexpr unsigned short SCANCODE_Z = 0x2C;
// ANTI-TAKILMA yon tuslari (donanim tarama kodlari - bkz. SendKey'in
// KEYEVENTF_SCANCODE yolu; oyun klavyeyi DirectInput ile okudugu icin
// sanal tus kodu DEGIL, TARAMA KODU gonderiliyor).
constexpr unsigned short SCANCODE_W = 0x11;
constexpr unsigned short SCANCODE_A = 0x1E;
constexpr unsigned short SCANCODE_S = 0x1F;
constexpr unsigned short SCANCODE_D = 0x20;

thread_local std::mt19937 g_rng{std::random_device{}()};

double RandRange(double lo, double hi) {
    std::uniform_real_distribution<double> dist(lo, hi);
    return dist(g_rng);
}

int RandInt(int lo, int hi) {
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(g_rng);
}

void SleepSeconds(double sec) {
    if (sec <= 0) return;
    std::this_thread::sleep_for(std::chrono::duration<double>(sec));
}
}  // namespace

InputController::InputController(const Config& config) : m_config(config) {
    // Interception SURUCUSU TAMAMEN KALDIRILDI (kullanici talebi) - tum
    // fare/klavye kontrolu SetCursorPos+SendMessage+SendInput ile yapilir,
    // hicbir sürücüye/kuruluma ihtiyac yoktur.
    //
    // AKICILIK ICIN KRITIK (kullanici: "ekrani frame frame donduruyormus gibi"):
    // Windows'ta varsayilan zamanlayici cozunurlugu ~15.6ms'dir - yani "4ms
    // uyu" dedigimizde GERCEKTE ~15.6ms uyuyorduk. Kamera donusunu ne kadar
    // kucuk adimlara bolersek bolelim, her adim arasinda 15.6ms beklendigi
    // icin hareket saniyede ancak ~64 kez guncellenip KESIK KESIK gorunuyordu.
    // timeBeginPeriod(1) cozunurlugu 1ms'ye cekiyor; boylece DragViaMessage'in
    // ~6ms'lik adimlari GERCEKTEN 6ms suruyor ve donus akici oluyor. Yikicida
    // timeEndPeriod ile geri birakiliyor (sistem geneli bir ayardir).
    timeBeginPeriod(1);
    m_timerResolutionSet = true;

    POINT cur{};
    GetCursorPos(&cur);
    m_lastX = cur.x;
    m_lastY = cur.y;

    int mouseParams[3] = {0, 0, 0};
    SystemParametersInfoA(SPI_GETMOUSE, 0, mouseParams, 0);
    int mouseSpeed = 0;
    SystemParametersInfoA(SPI_GETMOUSESPEED, 0, &mouseSpeed, 0);
    std::ostringstream oss;
    oss << "init t1=" << mouseParams[0] << " t2=" << mouseParams[1] << " acc=" << mouseParams[2]
        << " spd=" << mouseSpeed << " dpi=" << GetDpiForSystem();
    LogMouseDebug(oss.str());
}

InputController::~InputController() {
    // GUVENLIK AGI: bot hangi sebeple olursa olsun (normal durdurma,
    // hata/exception, pencere kapanmasi) yok edilirken, once ClipCursor
    // hapsini KESIN OLARAK kaldiriyoruz - aksi halde kullanicinin faresi
    // tum ekranda kilitli kalabilirdi. Windows zaten process sonlaninca
    // hapsi otomatik temizler, ama burada da acikca cagirmak ekstra
    // guvence - bedelsiz, hicbir riski yok.
    ReleaseClip();
    // Zamanlayici cozunurlugu SISTEM GENELI bir ayardir - aldiysak GERI
    // BIRAKMAK zorundayiz (aksi halde biz kapandiktan sonra bile sistem
    // 1ms'de kalir, pil/guc tuketimini artirir).
    if (m_timerResolutionSet) {
        timeEndPeriod(1);
        m_timerResolutionSet = false;
    }
}

namespace {
// KAPSAMLI ORTAM PARMAK IZI: birden fazla kullanicinin (ör. arkadaslarin)
// loglarini YAN YANA koyup "hangi makine ayari farkli" diye kiyaslayabilmek
// icin - client bir pencereye baglandiginda BIR KEZ, akla gelebilecek TUM
// makine/pencere/fare/klavye ayarlarini tek bir raporda toplar. Sadece
// fare hareketiyle SINIRLI DEGIL - Windows surumu, DPI farkindaligi,
// monitor/cozunurluk/yenileme hizi, pencere stili, klavye duzeni, cift
// tiklama hizi da dahil.
void LogEnvironmentFingerprint(HWND hwnd) {
    // NOT (koda bakan gelistirici icin): asagidaki alanlar - osv/dpi/mon/
    // win/inp - hepsi tek harfli/kisa anahtarlarla yaziliyor, BILEREK.
    // Anlamlari: osv=Windows surum bilgisi, dpi=DPI farkindaligi,
    // mon=monitor/cozunurluk, win=pencere geometri/stil, inp=fare+klavye
    // ayarlari.
    std::ostringstream oss;
    oss << "\n--8f2c--\n";

    {
        char buf[128] = {};
        DWORD bufSize = sizeof(buf);
        char productName[128] = {};
        DWORD pnSize = sizeof(productName);
        RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "ProductName",
                     RRF_RT_REG_SZ, nullptr, productName, &pnSize);
        RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "CurrentBuildNumber",
                     RRF_RT_REG_SZ, nullptr, buf, &bufSize);
        char displayVersion[64] = {};
        DWORD dvSize = sizeof(displayVersion);
        RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "DisplayVersion",
                     RRF_RT_REG_SZ, nullptr, displayVersion, &dvSize);
        oss << "osv p=" << productName << " b=" << buf << " r=" << displayVersion << "\n";
    }

    {
        BOOL legacyAware = IsProcessDPIAware();
        DPI_AWARENESS_CONTEXT ctx2 = GetWindowDpiAwarenessContext(hwnd);
        DPI_AWARENESS awareness = GetAwarenessFromDpiAwarenessContext(ctx2);
        oss << "dpi la=" << (legacyAware ? 1 : 0) << " aw=" << (int)awareness << " wdpi=" << GetDpiForWindow(hwnd)
            << " sdpi=" << GetDpiForSystem() << "\n";
    }

    {
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(mon, &mi);
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm);
        oss << "mon n=" << GetSystemMetrics(SM_CMONITORS) << " r=(" << mi.rcMonitor.left << "," << mi.rcMonitor.top
            << ")-(" << mi.rcMonitor.right << "," << mi.rcMonitor.bottom << ") res=" << dm.dmPelsWidth << "x"
            << dm.dmPelsHeight << " hz=" << dm.dmDisplayFrequency << " prim="
            << ((mi.dwFlags & MONITORINFOF_PRIMARY) ? 1 : 0) << " vs=" << GetSystemMetrics(SM_CXVIRTUALSCREEN) << "x"
            << GetSystemMetrics(SM_CYVIRTUALSCREEN) << "\n";
    }

    {
        RECT winRect{}, clientRectScreen{};
        GetWindowRect(hwnd, &winRect);
        RECT clientLocal{};
        GetClientRect(hwnd, &clientLocal);
        POINT topLeft{0, 0};
        ClientToScreen(hwnd, &topLeft);
        clientRectScreen = {topLeft.x, topLeft.y, topLeft.x + clientLocal.right, topLeft.y + clientLocal.bottom};
        wchar_t title[256] = {};
        GetWindowTextW(hwnd, title, 256);
        wchar_t className[256] = {};
        GetClassNameW(hwnd, className, 256);
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        oss << "win t='" << WideToUtf8(title) << "' c='" << WideToUtf8(className) << "'\n";
        oss << "win.r=(" << winRect.left << "," << winRect.top << ")-(" << winRect.right << "," << winRect.bottom
            << ") win.c=(" << clientRectScreen.left << "," << clientRectScreen.top << ")-(" << clientRectScreen.right
            << "," << clientRectScreen.bottom << ") st=0x" << std::hex << style << " ex=0x" << exStyle << std::dec
            << "\n";
    }

    {
        int mouseParams[3] = {0, 0, 0};
        SystemParametersInfoA(SPI_GETMOUSE, 0, mouseParams, 0);
        int mouseSpeed = 0;
        SystemParametersInfoA(SPI_GETMOUSESPEED, 0, &mouseSpeed, 0);
        BOOL mouseTrails = FALSE;
        SystemParametersInfoA(SPI_GETMOUSETRAILS, 0, &mouseTrails, 0);
        UINT dblClickMs = GetDoubleClickTime();
        BOOL swapButtons = GetSystemMetrics(SM_SWAPBUTTON);
        HKL layout = GetKeyboardLayout(0);
        oss << "inp t1=" << mouseParams[0] << " t2=" << mouseParams[1] << " acc=" << mouseParams[2]
            << " spd=" << mouseSpeed << " trl=" << (mouseTrails ? 1 : 0) << " swp=" << (swapButtons ? 1 : 0)
            << " dc=" << dblClickMs << " kl=0x" << std::hex << (DWORD_PTR)layout << std::dec << "\n";
    }

    oss << "\n--/8f2c--";
    LogMouseDebug(oss.str());
}
}  // namespace

void InputController::SetBounds(HWND hwnd) {
    m_boundsHwnd = hwnd;
    m_hasBounds = (hwnd != nullptr);
    if (hwnd) LogEnvironmentFingerprint(hwnd);
}

void InputController::UpdateClipToWindow() {
    if (!m_hasBounds) return;
    RECT winRect = win_window::GetWindowRectOnScreen(m_boundsHwnd);
    RECT clip;
    clip.left = winRect.left + kBoundsMarginPx;
    clip.top = winRect.top + kBoundsMarginPx;
    clip.right = winRect.right - kBoundsMarginPx;
    clip.bottom = winRect.bottom - kBoundsMarginPx;
    // Gecersiz/ters bir dikdortgen ASLA ClipCursor'a verilmez - bu, fareyi
    // sifir genislikte bir alana hapsedip kullaniciyi kilitleyebilirdi.
    if (clip.right > clip.left && clip.bottom > clip.top) {
        ClipCursor(&clip);
    }
}

void InputController::ReleaseClip() {
    ClipCursor(nullptr);
}

int InputController::ClampDelta(int start, int delta, int minB, int maxB) {
    int end = start + delta;
    if (end < minB) end = minB;
    if (end > maxB) end = maxB;
    return end - start;
}

void InputController::SendRelativeMove(int dx, int dy) {
    // Interception KALDIRILDI - SendInput'un GORELI fare hareketi (surucu
    // gerektirmez). SADECE m_hasBounds=false guvenlik-agi yollarindan
    // (MoveTo/DragRelative) cagrilir - gercek botta HIC kullanilmaz.
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(INPUT));
}

void InputController::SendMouseButton(bool down, bool rightButton) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = rightButton ? (down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP)
                                     : (down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
    UINT sent = SendInput(1, &input, sizeof(INPUT));
    POINT pos{};
    GetCursorPos(&pos);
    std::ostringstream oss;
    oss << "BTN " << (rightButton ? "R" : "L") << " d=" << (down ? 1 : 0) << " ok=" << (sent > 0 ? 1 : 0) << " p=("
        << pos.x << "," << pos.y << ")";
    LogMouseDebug(oss.str());
}

void InputController::SendMouseButtonRaw(bool down, bool rightButton) {
    // OPSIYONEL IKILI GONDERIM (varsayilan KAPALI - bkz. config.h
    // click_dual_sendinput_enabled): SendMessage'a EK olarak, imlecin O ANKI
    // (zaten SetCursorPos ile konumlanmis) yerinde bir SendInput tus
    // olayi gonderir - amac GetAsyncKeyState/raw-input dinleyen bir mekanizmaya
    // da GERCEK/global buton durumunu yansitmak. DIKKAT: bu sunucuda daha once
    // SendInput fare tiklamasi TEK BASINA reddedilmisti (bkz. Click() yorumu) -
    // burada SendMessage'IN YANINDA/SONRASINDA denendigi icin FARKLI bir
    // kombinasyon, ama test edilmeden varsayilan acilmamali.
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = rightButton ? (down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP)
                                     : (down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
    SendInput(1, &input, sizeof(INPUT));
}

namespace {
// SendKey'in scancode parametresini WM_KEYDOWN/UP'in bekledigi sanal tus
// (VK_*) koduna cevirir - mesaj-tabanli yol Interception'in donanim
// scancode'larini degil Win32'nin VK_* kodlarini kullanir.
unsigned short ScancodeToVk(unsigned short scancode) {
    switch (scancode) {
        case 0x2A: return VK_SHIFT;    // SCANCODE_SHIFT
        case 0x01: return VK_ESCAPE;   // SCANCODE_ESC
        case 0x2C: return 'Z';         // SCANCODE_Z
        case 0x11: return 'W';         // SCANCODE_W
        case 0x1E: return 'A';         // SCANCODE_A
        case 0x1F: return 'S';         // SCANCODE_S
        case 0x20: return 'D';         // SCANCODE_D
        default: return 0;
    }
}
}  // namespace

void InputController::SendKey(unsigned short scancode, bool down) {
    // Interception TAMAMEN KALDIRILDI (kullanici talebi) - klavye SADECE
    // SendMessage(WM_KEYDOWN/UP) + SendInput ile gonderilir, sürücüye HIC
    // ihtiyac/referans yok.
    if (m_hasBounds) {
        DWORD pid = win_window::EnsureInputFocus(m_boundsHwnd);
        if (pid) {
            m_lastFocusedPid = pid;
            m_lastFocusedPidAt = std::chrono::steady_clock::now();
        }

        unsigned short vk = ScancodeToVk(scancode);

        // 1) DispatchMsg - dogrudan pencereye (Varsayilan'da SendMessage/senkron,
        // PostMessage modunda PostMessage/asenkron - bkz. config.h
        // input_use_postmessage).
        LPARAM lp = 1 | (scancode << 16);
        if (!down) lp |= (1 << 30) | (1 << 31);
        DispatchMsg(down ? WM_KEYDOWN : WM_KEYUP, vk, lp);

        // 2) SendInput - AMA sanal tus kodu (wVk) DEGIL, DONANIM TARAMA
        // KODU (wScan + KEYEVENTF_SCANCODE) ile. Oyun klavyeyi DirectInput
        // ile okuyorsa (buyuk ihtimalle) DirectInput'un GetDeviceState'i
        // DOGRUDAN scan-code'a gore indexlenir - VK koduyla gonderilen bir
        // SendInput bu tamponu HIC doldurmuyor olabilir, scan-code'lu
        // gonderim ise dogrudan bu tamponu besler (Windows'un dahili
        // "donanim benzeri" isleme yolu).
        // KRITIK - PostMessage MODUNDA DA GONDERILIR (olculerek dogrulandi):
        // Bu oyun klavyeyi DirectInput/ham klavye durumu uzerinden okuyor,
        // pencere mesajlarindan DEGIL. PostMessage(WM_KEYDOWN, VK_SHIFT)
        // GERCEK klavye durumunu degistirmedigi icin, SendInput atlandiginda
        // oyun Shift'i "basili" GORMUYOR -> "Shift + Sag Tik" duz sag tika
        // dusuyor -> metin SECILMIYOR. Tani izinde net gorulmustu:
        //   Varsayilan mod : KEY dual    vk=0x10 ... si_ok=1  -> secim CALISIYOR
        //   PostMessage    : KEY postmsg vk=0x10 ... si_ok=0  -> secim YOK
        // Bu yuzden "Tiklama Yontemi" SADECE FARE mesajlarinin gonderim
        // seklini degistirir; klavye HER IKI MODDA da SendInput ile de
        // gonderilir - baska turlu calismasi mumkun degil.
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = scancode;
        input.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
        UINT siSent = SendInput(1, &input, sizeof(INPUT));

        std::ostringstream oss;
        oss << "KEY " << (m_config.input_use_postmessage ? "postmsg" : "dual") << " vk=0x" << std::hex << vk
            << " scan=0x" << scancode << std::dec << " d=" << (down ? 1 : 0) << " pid=" << pid
            << " si_ok=" << siSent;
        LogMouseDebug(oss.str());
        return;
    }

    // Pencere sinirlari HENUZ ayarlanmamissa (bagimsiz araclarda) SADECE
    // SendInput (hwnd'ye ihtiyaci yok, surucu de gerekmiyor) - scan-code ile.
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = scancode;
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    UINT sent = SendInput(1, &input, sizeof(INPUT));
    std::ostringstream oss;
    oss << "KEY sendinput scan=0x" << std::hex << scancode << std::dec << " d=" << (down ? 1 : 0) << " ok=" << sent;
    LogMouseDebug(oss.str());
}

void InputController::ClampCursorToBounds() {
    if (!m_hasBounds) return;
    RECT winRect = win_window::GetWindowRectOnScreen(m_boundsHwnd);
    POINT cur{};
    GetCursorPos(&cur);
    int clampedX = std::clamp((int)cur.x, (int)winRect.left + kBoundsMarginPx, (int)winRect.right - kBoundsMarginPx);
    int clampedY = std::clamp((int)cur.y, (int)winRect.top + kBoundsMarginPx, (int)winRect.bottom - kBoundsMarginPx);
    if (clampedX != cur.x || clampedY != cur.y) {
        SetCursorPos(clampedX, clampedY);
        m_lastX = clampedX;
        m_lastY = clampedY;
    }
}

void InputController::MoveTo(int x, int y) {
    // KRITIK: baslangic noktasini KENDI TAHMININDEN (m_lastX/Y) degil,
    // GERCEK imlec konumundan (GetCursorPos) al. Sebep: fare relative
    // (goreli) hareket ile gonderiliyor - eger daha once bir hareket
    // ekran/monitor kenarina CARPIP Windows tarafindan sessizce
    // kirpildiyse (clamp), bizim ic sayacimiz bunu bilmez ve gercek
    // imlecten gittikce SAPAR. Bu sapma birikince tiklamalar pencere/ekran
    // disina dusebiliyordu. Her hareketi gercek konumdan baslatmak bu
    // sapmayi HER SEFERINDE sifirlar.
    POINT real{};
    GetCursorPos(&real);
    double startX = real.x, startY = real.y;
    m_lastX = real.x;
    m_lastY = real.y;

    // EK GUVENLIK: hedef nokta da pencere sinirlarina kirpilir (bot.cpp
    // zaten tiklama hedefini kirpiyor, ama burada da garanti altina
    // aliyoruz - MoveTo baska bir yerden cagrilirsa bile disari cikilmaz).
    if (m_hasBounds) {
        RECT winRect = win_window::GetWindowRectOnScreen(m_boundsHwnd);
        x = std::clamp(x, (int)winRect.left + kBoundsMarginPx, (int)winRect.right - kBoundsMarginPx);
        y = std::clamp(y, (int)winRect.top + kBoundsMarginPx, (int)winRect.bottom - kBoundsMarginPx);
    }

    double dist = std::hypot(x - startX, y - startY);

    double durationMs = RandRange(m_config.mouse_move_duration_ms_min, m_config.mouse_move_duration_ms_max);
    int steps = std::clamp((int)(dist / 15), 6, 30);
    double stepDelay = (durationMs / 1000.0) / steps;

    {
        std::ostringstream oss;
        oss << "MV0 s=(" << (int)startX << "," << (int)startY << ") t=(" << x << "," << y << ") d=" << (int)dist
            << " n=" << steps;
        LogMouseDebug(oss.str());
    }

    // KRITIK DUZELTME (KANITA DAYALI): Windows'un fare hizlandirmasi
    // ("Isaretci hassasiyetini gelistir") ACIK bir makinede, gonderdigimiz
    // goreli delta'lar DOGRUSAL OLMAYAN bir egriden gecip GERCEKTE cok
    // daha fazla/az ekran mesafesine denk geliyordu - ONCEDEN her adimda
    // "bir onceki NIYET ettigimiz nokta"dan (prevX/prevY, VARSAYIM) devam
    // ediyorduk, bu yuzden hizlandirmanin yarattigi sapma adimdan adima
    // BIRIKIP tek bir MoveTo cagrisinin SONUNDA 250 piksele kadar
    // ULASABILIYORDU (bkz. mouse_debug.log kanit verisi). Artik HER
    // adimda GERCEK imlec konumunu (GetCursorPos) olcup, bir sonraki
    // goreli delta'yi VARSAYIMDAN DEGIL bu GERCEK konumdan hesapliyoruz -
    // boylece hizlandirma o adimda ne kadar saparsa sapsin, BIR SONRAKI
    // adim otomatik telafi eder, hata ASLA birikmez. Bu, makinenin fare
    // hizlandirma ayari NE OLURSA OLSUN (kapali/acik, herhangi bir egri)
    // dogru calisir - kullanicinin sistem ayarina dokunmaya GEREK KALMAZ.
    for (int i = 1; i <= steps; ++i) {
        double t = (double)i / steps;
        double eased = t * t * (3 - 2 * t);  // ease-in-out
        double ix = startX + (x - startX) * eased;
        double iy = startY + (y - startY) * eased;

        POINT before{};
        GetCursorPos(&before);
        int sendDx = (int)std::lround(ix - before.x);
        int sendDy = (int)std::lround(iy - before.y);
        SendRelativeMove(sendDx, sendDy);
        SleepSeconds(stepDelay);

        // TANI: HER adimdan sonra GERCEK imlec konumunu olc - "niyet
        // ettigimiz" (ix,iy) ile "Windows'un GERCEKTE goturdugu yer"
        // arasindaki farki (sapma) burada goruruz. Artik bu sapma bir
        // SONRAKI adimda otomatik telafi edildigi icin BIRIKMEZ (yukarida
        // GERCEK konumdan (before) devam ediyoruz, tahminden degil).
        POINT afterStep{};
        GetCursorPos(&afterStep);
        double sapmaX = afterStep.x - ix;
        double sapmaY = afterStep.y - iy;
        std::ostringstream oss;
        oss << "ST " << i << "/" << steps << " p0=(" << before.x << "," << before.y << ") dv=(" << sendDx << ","
            << sendDy << ") iw=(" << (int)ix << "," << (int)iy << ") pa=(" << afterStep.x << "," << afterStep.y
            << ") df=(" << (int)sapmaX << "," << (int)sapmaY << ")";
        LogMouseDebug(oss.str());
    }

    // Hedefe varinca kucuk rastgele sapma ekle (tam ayni piksele her seferinde gitmemek icin)
    int jitter = m_config.pre_click_jitter_px;
    double fx = x + RandInt(-jitter, jitter);
    double fy = y + RandInt(-jitter, jitter);

    // ONEMLI (ama KUCUK) DUZELTME: son adimi hesaplarken TAHMINI konum
    // (prevX/prevY - animasyon boyunca gonderdigimiz deltalardan
    // biriktirdigimiz sayac) yerine GERCEK olculen imlec konumunu
    // (GetCursorPos) kullaniyoruz. Fare hizlandirmasi animasyon sirasinda
    // tahminimizi gercekten kaydirabiliyordu, bu da son tiklama noktasinin
    // hedeften kaymasina sebep oluyordu. Bu TEK SEFERLIK bir olcum+duzeltme -
    // onceki krize sebep olan TEKRARLI/agresif duzeltme donguisune KESINLIKLE
    // DONULMEDI, sadece bu son adim tahmine degil gerceğe dayandiriliyor.
    POINT actual{};
    GetCursorPos(&actual);
    int finalDx = (int)std::lround(fx) - actual.x;
    int finalDy = (int)std::lround(fy) - actual.y;
    SendRelativeMove(finalDx, finalDy);

    m_lastX = (int)std::lround(fx);
    m_lastY = (int)std::lround(fy);

    ClampCursorToBounds();

    POINT finalActual{};
    GetCursorPos(&finalActual);
    std::ostringstream oss;
    oss << "MV1 t=(" << x << "," << y << ") fc=(" << finalDx << "," << finalDy << ") pf=(" << finalActual.x << ","
        << finalActual.y << ") nd=(" << (finalActual.x - x) << "," << (finalActual.y - y) << ")";
    LogMouseDebug(oss.str());
}

void InputController::Click(int x, int y, bool rightButton) {
    // KANITLANMIS YONTEM (kullanicinin kendi testiyle DOGRULANDI - bkz.
    // setcursor_sendmessage_test.exe): bu sunucu Interception'in ürettigi
    // donanim-seviyesi tiklamayi FARE ICIN reddediyor (SendInput/mouse_event/
    // PostMessage/SendMessage TEK BASINA da calismiyordu) - ama GERCEK
    // imleci once SetCursorPos ile hedefe tasiyip HEMEN ARDINDAN SendMessage
    // ile WM_*BUTTONDOWN/UP gonderince oyun tiklamayi kabul ediyor. Oyun
    // muhtemelen "tiklama GERCEKTEN imlecin bulundugu yerde mi oldu" diye
    // dogruluyor - SetCursorPos bunu saglıyor, SendMessage da mesaji
    // dogrudan pencereye (odak/on-plan gerektirmeden) iletiyor.
    if (m_hasBounds) {
        DWORD pid = win_window::EnsureInputFocus(m_boundsHwnd);
        if (pid) {
            m_lastFocusedPid = pid;
            m_lastFocusedPidAt = std::chrono::steady_clock::now();
        }

        // INSANCILLASTIR: ACIKSA imleci hedefe ANINDA isinlamak yerine kisa
        // bir sure icinde YUMUSAKCA kaydir (ease-in-out + hafif jitter).
        // KAPALIYKEN BIREBIR eski davranis (aninda SetCursorPos).
        if (m_config.humanize_enabled) {
            GlideCursorTo(x, y);
        } else {
            SetCursorPos(x, y);
        }
        SleepSeconds(RandRange(0.02, 0.05));

        POINT client{x, y};
        ScreenToClient(m_boundsHwnd, &client);
        LPARAM lp = MAKELPARAM(client.x, client.y);
        WPARAM downParam = rightButton ? MK_RBUTTON : MK_LBUTTON;
        if (m_shiftDown) downParam |= MK_SHIFT;

        DispatchMsg(WM_MOUSEMOVE, 0, lp);
        SleepSeconds(RandRange(0.03, 0.09));  // tiklamadan once kisa "tepki suresi"
        DispatchMsg(rightButton ? WM_RBUTTONDOWN : WM_LBUTTONDOWN, downParam, lp);
        if (m_config.click_dual_sendinput_enabled && !m_config.input_use_postmessage) SendMouseButtonRaw(true, rightButton);
        // TUTMA SURESI (KANITA DAYALI DUZELTME): eskiden 20-50ms idi - gercek
        // insan tiklamalari genelde 60-150ms arasi surer (uniform 20-50ms
        // istatistiksel olarak fark edilebilir kadar kisa/dar). 50-130ms bandina
        // cekildi.
        SleepSeconds(RandRange(0.05, 0.13));
        DispatchMsg(rightButton ? WM_RBUTTONUP : WM_LBUTTONUP, 0, lp);
        if (m_config.click_dual_sendinput_enabled && !m_config.input_use_postmessage) SendMouseButtonRaw(false, rightButton);

        POINT after{};
        GetCursorPos(&after);
        std::ostringstream oss;
        oss << "CLK " << (m_config.input_use_postmessage ? "postmsg" : "sendmsg") << " " << (rightButton ? "R" : "L") << " p=(" << x << "," << y << ") client=(" << client.x
            << "," << client.y << ") pa=(" << after.x << "," << after.y << ")";
        LogMouseDebug(oss.str());
        return;
    }

    // Pencere sinirlari HENUZ ayarlanmamissa (SetBounds cagrilmadan once,
    // bagimsiz araclarda) SendInput tabanli guvenlik agi yolu kalsin.
    MoveTo(x, y);
    SleepSeconds(RandRange(0.03, 0.09));
    SendMouseButton(true, rightButton);
    SleepSeconds(RandRange(0.05, 0.13));
    SendMouseButton(false, rightButton);
}

void InputController::ShiftRightClick(int x, int y) {
    // TIKLAMA SEKLI (bkz. config.h click_use_shift, Fonksiyonlar > Tiklama
    // Degistirme):
    //   true  (VARSAYILAN) -> SHIFT basili + SAG TIK
    //   false               -> Shift YOK, SADECE SOL TIK
    // Ikisi de AYNI fonksiyondan gecer, boylece bot.cpp'deki tum cagri yerleri
    // (kesim, kuyruklama, burst) degismeden calisir.
    if (m_config.click_use_shift) {
        HoldShift(true);
        SleepSeconds(RandRange(0.02, 0.06));
        Click(x, y, /*rightButton=*/true);
        SleepSeconds(RandRange(0.02, 0.06));
        HoldShift(false);
    } else {
        Click(x, y, /*rightButton=*/false);  // SADECE SOL TIK
    }
}

void InputController::PressEscape() {
    SendKey(SCANCODE_ESC, true);
    SleepSeconds(RandRange(0.03, 0.08));
    SendKey(SCANCODE_ESC, false);
}

void InputController::PressLootKey() {
    SendKey(SCANCODE_Z, true);
    SleepSeconds(RandRange(0.03, 0.08));
    SendKey(SCANCODE_Z, false);
}

void InputController::DispatchMsg(UINT msg, WPARAM wp, LPARAM lp) {
    if (m_config.input_use_postmessage) {
        PostMessageW(m_boundsHwnd, msg, wp, lp);
    } else {
        SendMessageW(m_boundsHwnd, msg, wp, lp);
    }
}

void InputController::HoldShift(bool down) {
    m_shiftDown = down;
    SendKey(SCANCODE_SHIFT, down);
}

void InputController::HoldMoveKey(char key, double seconds) {
    unsigned short scancode = 0;
    switch (key) {
        case 'W': case 'w': scancode = SCANCODE_W; break;
        case 'A': case 'a': scancode = SCANCODE_A; break;
        case 'S': case 's': scancode = SCANCODE_S; break;
        case 'D': case 'd': scancode = SCANCODE_D; break;
        default: return;
    }

    SendKey(scancode, true);
    // Tusu basili TUTARKEN, Windows'un kendi otomatik-tekrarini taklit etmek
    // icin araliklarla WM_KEYDOWN'i TAZELIYORUZ - bazi oyun istemcileri
    // hareketi surdurmek icin tekrarlanan tus mesajlarina bakar, sadece
    // "tus su an basili" durumuna degil. SendInput tarafi zaten tusu
    // gercekten basili tutuyor, bu sadece EK guvence.
    auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < until) {
        SleepSeconds(0.1);
        SendKey(scancode, true);
    }
    SendKey(scancode, false);

    std::ostringstream oss;
    oss << "MOVE hold key=" << key << " sec=" << seconds;
    LogMouseDebug(oss.str());
}

// OTO NESNE v2: HoldMoveKey ile AYNI tus gonderme yolu, ama BLOKLAMADAN -
// tus basili kalirken cagiran taraf (bkz. bot.cpp oto-nesne blogu) ayni anda
// Z spamlayabilsin diye. Tusu TAZELEMEK (Windows'un otomatik-tekrarini taklit)
// cagiranin isi: dongu icinde MoveKeyDown'i tekrar cagirmak zararsizdir.
static unsigned short MoveScancodeFor(char key) {
    switch (key) {
        case 'W': case 'w': return SCANCODE_W;
        case 'A': case 'a': return SCANCODE_A;
        case 'S': case 's': return SCANCODE_S;
        case 'D': case 'd': return SCANCODE_D;
        default: return 0;
    }
}

void InputController::MoveKeyDown(char key) {
    unsigned short sc = MoveScancodeFor(key);
    if (sc) SendKey(sc, true);
}

void InputController::MoveKeyUp(char key) {
    unsigned short sc = MoveScancodeFor(key);
    if (sc) SendKey(sc, false);
}

void InputController::PlaceCursor(int x, int y) {
    SetCursorPos(x, y);
}

void InputController::GlideCursorTo(int x, int y) {
    // Imleci GERCEK konumundan (GetCursorPos) hedefe ease-in-out egrisiyle,
    // kisa bir sure icinde YUMUSAKCA kaydirir - tamamen SetCursorPos ile
    // (Interception YOK). Hafif rastgele jitter ile robotik-lik kirilir.
    POINT cur{};
    GetCursorPos(&cur);
    double sx = cur.x, sy = cur.y;
    double dist = std::hypot(x - sx, y - sy);
    if (dist < 3.0) {  // zaten hedefte - kaydirmaya gerek yok
        SetCursorPos(x, y);
        return;
    }
    // Mesafeyle orantili adim sayisi/sure (kisa mesafe hizli, uzun daha uzun).
    int steps = std::clamp((int)(dist / 12.0), 8, 26);
    double totalMs = std::clamp(dist * 0.9, 90.0, 320.0);  // toplam kayma suresi
    double stepDelay = (totalMs / 1000.0) / steps;

    // GERCEKCILIK (overshoot-correct): gercek kullanicilar uzun bir hareketin
    // sonunda hedefi hafifce ASIP hemen geri duzeltir - saf ease-in-out'un
    // hep monotonik yaklasmasindan farkli. Sadece yeterince uzun (>80px)
    // hareketlerde, son-1. adimda hedefin biraz otesine gidip son adimda
    // tam hedefe donuyoruz.
    bool overshoot = dist > 80.0;
    double overshootPx = overshoot ? RandRange(3.0, 7.0) : 0.0;
    double ux = overshoot ? (x - sx) / dist : 0.0;
    double uy = overshoot ? (y - sy) / dist : 0.0;

    for (int i = 1; i <= steps; ++i) {
        double t = (double)i / steps;
        double eased = t * t * (3 - 2 * t);  // ease-in-out
        double ix = sx + (x - sx) * eased + (i < steps ? RandRange(-1.2, 1.2) : 0.0);
        double iy = sy + (y - sy) * eased + (i < steps ? RandRange(-1.2, 1.2) : 0.0);
        if (overshoot && i == steps - 1) {
            ix = x + ux * overshootPx;
            iy = y + uy * overshootPx;
        } else if (overshoot && i == steps) {
            ix = x;
            iy = y;
        }
        SetCursorPos((int)std::lround(ix), (int)std::lround(iy));
        // ARA HAREKET MESAJI: eskiden sadece Click() icinde SON konumda TEK
        // bir WM_MOUSEMOVE gonderiliyordu - imlec gercekten kayiyordu ama
        // pencereye "yol boyunca hareket ediyordum" mesaji gitmiyordu. Artik
        // her adimda da gonderiliyor (mesaj-tabanli izleyen bir mekanizma
        // icin gercek yolu yansitir).
        if (m_hasBounds) {
            POINT client{(int)std::lround(ix), (int)std::lround(iy)};
            ScreenToClient(m_boundsHwnd, &client);
            DispatchMsg(WM_MOUSEMOVE, 0, MAKELPARAM(client.x, client.y));
        }
        SleepSeconds(stepDelay);
    }
    SetCursorPos(x, y);  // son adim tam hedefe
    if (m_hasBounds) {
        POINT client{x, y};
        ScreenToClient(m_boundsHwnd, &client);
        DispatchMsg(WM_MOUSEMOVE, 0, MAKELPARAM(client.x, client.y));
    }
}

namespace {
// OTO BASMA tus-adi -> sanal tus (VK) tablosu. Tek harf/rakam ve "F1".."F12"
// zaten formulle cozuldugu icin burada SADECE isimli (cok harfli) tuslar var.
// Modifier'lar bilerek SOL varyanta (VK_LCONTROL/LMENU/LSHIFT) esleniyor:
// oyunlarin cogu "herhangi bir Ctrl" bekler ve SOL varyantlar EXTENDED
// olmadigi icin scancode gonderimi tek INPUT ile tamamlanir.
struct NamedKey {
    const char* name;
    unsigned short vk;
};
const NamedKey kNamedKeys[] = {
    {"CTRL", VK_LCONTROL},   {"CONTROL", VK_LCONTROL}, {"ALT", VK_LMENU},
    {"SHIFT", VK_LSHIFT},    {"SPACE", VK_SPACE},      {"ENTER", VK_RETURN},
    {"RETURN", VK_RETURN},   {"ESC", VK_ESCAPE},       {"ESCAPE", VK_ESCAPE},
    {"TAB", VK_TAB},         {"BACKSPACE", VK_BACK},   {"DELETE", VK_DELETE},
    {"INSERT", VK_INSERT},   {"HOME", VK_HOME},        {"END", VK_END},
    {"PAGEUP", VK_PRIOR},    {"PAGEDOWN", VK_NEXT},    {"UP", VK_UP},
    {"DOWN", VK_DOWN},       {"LEFT", VK_LEFT},        {"RIGHT", VK_RIGHT},
};

// Tek bir tus parcasini (zaten BUYUK harfe cevrilmis, bosluksuz) VK'ya cevirir.
// Cozulemezse 0 doner.
unsigned short AutoKeyTokenToVk(const std::string& tok) {
    if (tok.empty()) return 0;
    if (tok.size() == 1 && std::isalnum((unsigned char)tok[0])) return (unsigned short)tok[0];
    if (tok[0] == 'F' && tok.size() >= 2 && std::isdigit((unsigned char)tok[1])) {
        int n = std::atoi(tok.c_str() + 1);
        if (n >= 1 && n <= 12) return (unsigned short)(VK_F1 + (n - 1));
    }
    for (const auto& nk : kNamedKeys) {
        if (tok == nk.name) return nk.vk;
    }
    return 0;
}

// Ok tuslari / Del / Home gibi tuslar "genisletilmis" (extended) tus olarak
// gonderilmezse oyunlar bunlari numpad esdegeriyle karistirabilir.
bool AutoKeyIsExtendedVk(unsigned short vk) {
    switch (vk) {
        case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
        case VK_PRIOR: case VK_NEXT: case VK_HOME: case VK_END:
        case VK_INSERT: case VK_DELETE:
            return true;
        default:
            return false;
    }
}
}  // namespace

void InputController::PressKeyTap(const std::string& key) {
    if (!m_hasBounds || key.empty()) return;

    // "ctrl + x" -> ["CTRL","X"]: '+' ile bol, her parcayi buyuk harfe cevir ve
    // bosluklarini at. Son parca ANA tus, oncekiler MODIFIER'dir. Tek parca
    // verilmisse (ör. "CTRL") o tusun kendisine tap yapilir - eskiden bu
    // durumda vk=0 kalip fonksiyon SESSIZCE cikiyordu, "Ctrl/Alt calismiyor"
    // sikayetinin kok sebebi TAM OLARAK buydu.
    std::vector<std::string> tokens;
    {
        std::string cur;
        for (char ch : key) {
            if (ch == '+') {
                if (!cur.empty()) tokens.push_back(cur);
                cur.clear();
            } else if (!std::isspace((unsigned char)ch)) {
                cur.push_back((char)std::toupper((unsigned char)ch));
            }
        }
        if (!cur.empty()) tokens.push_back(cur);
    }
    if (tokens.empty()) return;

    unsigned short mainVk = AutoKeyTokenToVk(tokens.back());
    if (mainVk == 0) return;  // ana tus cozulemedi - hicbir sey gonderme
    std::vector<unsigned short> modVks;
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        unsigned short mv = AutoKeyTokenToVk(tokens[i]);
        if (mv == 0) return;  // kombinasyonun bir parcasi anlasilmadi - YARIM gonderme
        modVks.push_back(mv);
    }

    win_window::EnsureInputFocus(m_boundsHwnd);

    // AYNI ikili gonderim (SendMessage + SendInput scancode) - bkz. SendKey.
    // SendMessage pencereye dogrudan mesaj atar, SendInput ise GERCEK klavye
    // durumunu degistirir - modifier'in "basili kalmasi" ikincisi sayesinde
    // olur (oyun GetKeyState/DirectInput ile okuyorsa bu sart).
    auto sendVk = [&](unsigned short vk, bool down) {
        UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        bool ext = AutoKeyIsExtendedVk(vk);
        LPARAM lp = 1 | ((LPARAM)scan << 16);
        if (ext) lp |= (LPARAM)1 << 24;
        if (!down) lp |= (1 << 30) | (1 << 31);
        // ALT basiliyken oyunlar WM_SYSKEYDOWN/UP bekler (Windows'un kendi
        // davranisi) - aksi halde mesaj yok sayilabilir.
        bool sys = false;
        for (unsigned short mv : modVks) {
            if (mv == VK_LMENU || mv == VK_RMENU || mv == VK_MENU) sys = true;
        }
        if (vk == VK_LMENU || vk == VK_RMENU || vk == VK_MENU) sys = true;
        UINT msg = down ? (sys ? WM_SYSKEYDOWN : WM_KEYDOWN) : (sys ? WM_SYSKEYUP : WM_KEYUP);
        DispatchMsg(msg, vk, lp);

        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wScan = (WORD)scan;
        in.ki.dwFlags = KEYEVENTF_SCANCODE | (ext ? KEYEVENTF_EXTENDEDKEY : 0) | (down ? 0 : KEYEVENTF_KEYUP);
        SendInput(1, &in, sizeof(INPUT));
    };

    // Modifier'lari SIRAYLA bas -> ana tusa tap -> modifier'lari TERS SIRADA
    // birak. Ters sira onemli: ic ice basilan tuslar disaridan iceri dogru
    // cozulmezse oyun modifier'i "takili" gorebilir.
    for (unsigned short mv : modVks) {
        sendVk(mv, true);
        SleepSeconds(RandRange(0.01, 0.03));
    }
    sendVk(mainVk, true);
    SleepSeconds(RandRange(0.03, 0.07));
    sendVk(mainVk, false);
    for (auto it = modVks.rbegin(); it != modVks.rend(); ++it) {
        SleepSeconds(RandRange(0.01, 0.03));
        sendVk(*it, false);
    }

    std::ostringstream oss;
    oss << "AUTOKEY tap mods=" << modVks.size() << " vk=0x" << std::hex << mainVk;
    LogMouseDebug(oss.str());
}

// "Basılı Tut" (kullanici talebi): PressKeyTap ile AYNI tokenize/cozumleme -
// tek fark, ana tus tap yerine SADECE bas (down=true) ya da SADECE birak
// (down=false) yapar, araya bekleme/otomatik-birakma KOYMAZ. Cagiran taraf
// (bkz. desktop_app_main.cpp run loop) down=true'yu PERIYODIK olarak tekrar
// cagirarak tusu "taze" tutar (oyunun otomatik-tekrari fark etmesi icin -
// HoldMoveKey/MoveKeyDown ile AYNI desen).
void InputController::SetAutoKeyHeld(const std::string& key, bool down) {
    if (!m_hasBounds || key.empty()) return;

    std::vector<std::string> tokens;
    {
        std::string cur;
        for (char ch : key) {
            if (ch == '+') {
                if (!cur.empty()) tokens.push_back(cur);
                cur.clear();
            } else if (!std::isspace((unsigned char)ch)) {
                cur.push_back((char)std::toupper((unsigned char)ch));
            }
        }
        if (!cur.empty()) tokens.push_back(cur);
    }
    if (tokens.empty()) return;

    unsigned short mainVk = AutoKeyTokenToVk(tokens.back());
    if (mainVk == 0) return;
    std::vector<unsigned short> modVks;
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        unsigned short mv = AutoKeyTokenToVk(tokens[i]);
        if (mv == 0) return;
        modVks.push_back(mv);
    }

    win_window::EnsureInputFocus(m_boundsHwnd);

    auto sendVk = [&](unsigned short vk, bool d) {
        UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        bool ext = AutoKeyIsExtendedVk(vk);
        LPARAM lp = 1 | ((LPARAM)scan << 16);
        if (ext) lp |= (LPARAM)1 << 24;
        if (!d) lp |= (1 << 30) | (1 << 31);
        bool sys = false;
        for (unsigned short mv : modVks) {
            if (mv == VK_LMENU || mv == VK_RMENU || mv == VK_MENU) sys = true;
        }
        if (vk == VK_LMENU || vk == VK_RMENU || vk == VK_MENU) sys = true;
        UINT msg = d ? (sys ? WM_SYSKEYDOWN : WM_KEYDOWN) : (sys ? WM_SYSKEYUP : WM_KEYUP);
        DispatchMsg(msg, vk, lp);

        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wScan = (WORD)scan;
        in.ki.dwFlags = KEYEVENTF_SCANCODE | (ext ? KEYEVENTF_EXTENDEDKEY : 0) | (d ? 0 : KEYEVENTF_KEYUP);
        SendInput(1, &in, sizeof(INPUT));
    };

    if (down) {
        for (unsigned short mv : modVks) {
            sendVk(mv, true);
            SleepSeconds(RandRange(0.01, 0.03));
        }
        sendVk(mainVk, true);
    } else {
        sendVk(mainVk, false);
        for (auto it = modVks.rbegin(); it != modVks.rend(); ++it) {
            SleepSeconds(RandRange(0.01, 0.03));
            sendVk(*it, false);
        }
    }
}

void InputController::ReleaseAllButtonsAndKeys() {
    if (!m_hasBounds) return;
    // Fare tuslari (sol + sag) - takili kalmis olabilecek her ihtimale karsi
    // birak. SendButtonViaMessage o anki imlec konumundan WM_*BUTTONUP yollar.
    SendButtonViaMessage(false, /*rightButton=*/true);
    SendButtonViaMessage(false, /*rightButton=*/false);
    // Shift ve tum yon tuslarini birak.
    m_shiftDown = false;
    SendKey(SCANCODE_SHIFT, false);
    SendKey(SCANCODE_W, false);
    SendKey(SCANCODE_A, false);
    SendKey(SCANCODE_S, false);
    SendKey(SCANCODE_D, false);
    LogMouseDebug("CLEANUP release-all");
}

void InputController::DragRelative(int totalDx, int totalDy, double durationMs) {
    int steps = RandInt(8, 18);
    std::vector<double> cum(steps + 1, 0.0);
    for (int i = 1; i <= steps; ++i) {
        double t = (double)i / steps;
        cum[i] = t * t * (3 - 2 * t);
    }

    int doneX = 0, doneY = 0;
    for (int i = 1; i <= steps; ++i) {
        double frac = cum[i] - cum[i - 1];
        int ix = (int)std::lround(totalDx * frac + RandRange(-1.5, 1.5));
        int iy = (int)std::lround(totalDy * frac + RandRange(-1.5, 1.5));
        doneX += ix;
        doneY += iy;
        SendRelativeMove(ix, iy);
        SleepSeconds(std::max((durationMs / 1000.0 / steps) + RandRange(-0.012, 0.02), 0.004));
    }

    int leftoverX = totalDx - doneX, leftoverY = totalDy - doneY;
    if (leftoverX || leftoverY) SendRelativeMove(leftoverX, leftoverY);
}

void InputController::SendButtonViaMessage(bool down, bool rightButton) {
    POINT cur{};
    GetCursorPos(&cur);
    POINT client = cur;
    ScreenToClient(m_boundsHwnd, &client);
    LPARAM lp = MAKELPARAM(client.x, client.y);
    WPARAM wp = rightButton ? MK_RBUTTON : MK_LBUTTON;
    if (m_shiftDown) wp |= MK_SHIFT;
    UINT msg;
    if (rightButton) {
        msg = down ? WM_RBUTTONDOWN : WM_RBUTTONUP;
    } else {
        msg = down ? WM_LBUTTONDOWN : WM_LBUTTONUP;
    }
    DispatchMsg(msg, down ? wp : 0, lp);
    std::ostringstream oss;
    oss << "BTN " << (m_config.input_use_postmessage ? "postmsg" : "sendmsg") << " " << (rightButton ? "R" : "L") << " d=" << (down ? 1 : 0) << " p=(" << cur.x << "," << cur.y
        << ")";
    LogMouseDebug(oss.str());
}

void InputController::DragViaMessage(int totalDx, int totalDy, double durationMs) {
    // KRITIK: sag tik "basili" olarak SendMessage'a HABER VERMEK icin her
    // WM_MOUSEMOVE'un wParam'inda MK_RBUTTON bayragini taniyoruz - oyun
    // butonun GERCEKTEN basili oldugunu boyle anliyor (fiziksel tus durumu
    // degil, mesajin kendi wParam'i).
    // AKICILIK (kullanici talebi: "ekrani frame frame donduruyormus gibi"):
    // eskiden adim sayisi SABIT 8-18 idi - 200px'lik bir donus 8 adimda
    // yapilinca her adimda ~25px SICRIYOR ve oyun bunu kesik kesik gosteriyordu.
    // Artik adim sayisi SUREYE gore hesaplaniyor: ~6ms'de bir adim (yaklasik
    // 165 adim/sn) - yani hareket oyunun kendi kare hizindan daha sik
    // guncelleniyor, gozle "surekli/akici" gorunuyor. Uyku cozunurlugunun
    // GERCEKTEN 1ms olmasi da sart - bkz. kurucudaki timeBeginPeriod(1).
    const double kStepMs = 6.0;
    int steps = (int)std::lround(durationMs / kStepMs);
    steps = std::clamp(steps, 12, 220);
    std::vector<double> cum(steps + 1, 0.0);
    for (int i = 1; i <= steps; ++i) {
        double t = (double)i / steps;
        cum[i] = t * t * (3 - 2 * t);
    }

    // Titreme (jitter) SADECE hareketin OLDUGU eksende uygulanir. Eskiden
    // yatay donuste bile Y'ye +-1.5px rastgele ekleniyordu - kamera donerken
    // gorunen o kucuk "yukari-asagi zipzip" TAM OLARAK buydu (kullanici
    // "yukari asagi yapan bir fonksiyon" diye bunu tarif etti).
    double jitterX = (totalDx != 0) ? 0.8 : 0.0;
    double jitterY = (totalDy != 0) ? 0.8 : 0.0;

    int doneX = 0, doneY = 0;
    for (int i = 1; i <= steps; ++i) {
        double frac = cum[i] - cum[i - 1];
        int ix = (int)std::lround(totalDx * frac + RandRange(-jitterX, jitterX));
        int iy = (int)std::lround(totalDy * frac + RandRange(-jitterY, jitterY));
        doneX += ix;
        doneY += iy;

        POINT cur{};
        GetCursorPos(&cur);
        SetCursorPos(cur.x + ix, cur.y + iy);

        POINT client{cur.x + ix, cur.y + iy};
        ScreenToClient(m_boundsHwnd, &client);
        DispatchMsg(WM_MOUSEMOVE, MK_RBUTTON, MAKELPARAM(client.x, client.y));

        SleepSeconds(std::max(durationMs / 1000.0 / steps, 0.001));
    }

    int leftoverX = totalDx - doneX, leftoverY = totalDy - doneY;
    if (leftoverX || leftoverY) {
        POINT cur{};
        GetCursorPos(&cur);
        SetCursorPos(cur.x + leftoverX, cur.y + leftoverY);
        POINT client{cur.x + leftoverX, cur.y + leftoverY};
        ScreenToClient(m_boundsHwnd, &client);
        DispatchMsg(WM_MOUSEMOVE, MK_RBUTTON, MAKELPARAM(client.x, client.y));
    }
}

void InputController::RotateView(int dx) {
    // KRITIK: kamera cevirme, fareyi pencerenin DISINA cikaramaz. Gercek
    // imlec konumundan, hareketin bitecegi noktayi hesaplayip pencere
    // sinirlarina gore dx'i kirpiyoruz - boylece surukleme ASLA disariya
    // tasmaz (matematiksel olarak garanti, tahmine dayanmiyor).
    if (m_hasBounds) {
        POINT cur{};
        GetCursorPos(&cur);
        RECT winRect = win_window::GetWindowRectOnScreen(m_boundsHwnd);
        dx = ClampDelta(cur.x, dx, (int)winRect.left + kBoundsMarginPx, (int)winRect.right - kBoundsMarginPx);
    }

    double durationMs = RandRange(m_config.camera_rotate_duration_ms_min, m_config.camera_rotate_duration_ms_max);
    // HIZ (kullanici talebi: "arada cok bekleniyor"): sag tikin basilmasi ile
    // suruklemenin baslamasi arasindaki bekleme 50-150ms idi ve AYNISI donus
    // sonunda tekrar ediliyordu - her donuse ~300ms'e varan OLU zaman
    // ekliyordu. Oyunun sag tiki kaydetmesi icin bu kadar uzun sure gerekmiyor.
    if (m_hasBounds) {
        SendButtonViaMessage(true, true);
        SleepSeconds(RandRange(0.02, 0.045));
        DragViaMessage(dx, 0, durationMs);
        ClampCursorToBounds();
        SleepSeconds(RandRange(0.02, 0.045));
        SendButtonViaMessage(false, true);
        return;
    }
    SendMouseButton(true, true);
    SleepSeconds(RandRange(0.02, 0.045));
    DragRelative(dx, 0, durationMs);
    ClampCursorToBounds();
    SleepSeconds(RandRange(0.02, 0.045));
    SendMouseButton(false, true);
}

