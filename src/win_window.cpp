#include "win_window.h"
#include <dwmapi.h>  // DwmGetWindowAttribute - bkz. GetCapturedFrameRectOnScreen
#include <algorithm>
#include <atomic>
#include <cctype>

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// OYUN OLMAYAN PENCERELERI ELE: pencere arama SADECE BASLIGA bakiyordu, bu
// yuzden basliginda ipucu gecen bir Dosya Gezgini klasoru ("Harbi2" adli bir
// klasor), tarayici sekmesi ya da Discord sohbeti de OYUN sanilip bota
// baglaniyordu. Sahte eslesen pencere gercek oyunla ON PLAN icin yarisip
// ikisini de calisamaz hale getiriyordu (tani izinde: "STEP not-foreground
// hwnd=... actual=..." satirlari, win t='Harbi2' c='CabinetWClass').
//
// Oyun istemcisinin sinif adi sunucudan sunucuya DEGISTIGI icin (ör.
// 'Aysanbanla', 'eter - s0:b4:...') beyaz liste yapilamaz - bunun yerine
// KESIN OLARAK oyun OLMAYAN, yaygin kabuk/tarayici/uygulama siniflarini
// disariyoruz. Listede olmayan bilinmeyen bir sinif OYUN KABUL EDILIR
// (yani filtre asla gercek oyunu elemez, sadece bilinen yanlislari atar).
bool IsExcludedWindowClass(HWND hwnd) {
    static const char* kExcluded[] = {
        "CabinetWClass",            // Dosya Gezgini
        "ExploreWClass",            // eski Dosya Gezgini
        "Progman", "WorkerW",       // masaustu
        "Shell_TrayWnd",            // gorev cubugu
        "Chrome_WidgetWin_0",       // Chrome/Edge/Electron (Discord, VS Code...)
        "Chrome_WidgetWin_1",
        "MozillaWindowClass",       // Firefox
        "ApplicationFrameWindow",   // UWP kabugu
        "Windows.UI.Core.CoreWindow",
        "ConsoleWindowClass",       // cmd
        "CASCADIA_HOSTING_WINDOW_CLASS",  // Windows Terminal
        "Notepad",                  // Not Defteri
        "#32770",                   // standart iletisim kutulari
    };
    char cls[128] = {0};
    if (GetClassNameA(hwnd, cls, (int)sizeof(cls)) <= 0) return false;
    for (const char* ex : kExcluded) {
        if (_stricmp(cls, ex) == 0) return true;
    }
    return false;
}

// Baslik eslestirme. Varsayilan: ALT DIZE (eski davranis, degismedi).
// Ipucu "=" ile baslarsa TAM ESLESME aranir: "=baerim" sadece basligi tam
// olarak "Baerim" olan pencereyi bulur, "Baerim2"yi bulmaz. Bu, biri digerinin
// alt dizesi olan iki sunucunun (Baerim / Baerim2) karismamasi icin gerekli.
static bool TitleMatchesHint(const std::string& titleLower, const std::string& hintLower) {
    if (!hintLower.empty() && hintLower[0] == '=') return titleLower == hintLower.substr(1);
    return titleLower.find(hintLower) != std::string::npos;
}

struct FindContext {
    std::string hintLower;
    HWND result = nullptr;
};

BOOL CALLBACK FindEnumProc(HWND hwnd, LPARAM lparam) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (IsExcludedWindowClass(hwnd)) return TRUE;
    auto* ctx = reinterpret_cast<FindContext*>(lparam);
    std::string title = ToLower(win_window::GetWindowTitle(hwnd));
    if (TitleMatchesHint(title, ctx->hintLower)) {
        ctx->result = hwnd;
        return FALSE;  // durdur, bulduk
    }
    return TRUE;
}

struct FindAllContext {
    std::string hintLower;
    const std::vector<HWND>* exclude;
    std::vector<HWND> results;
};

BOOL CALLBACK FindAllEnumProc(HWND hwnd, LPARAM lparam) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (IsExcludedWindowClass(hwnd)) return TRUE;
    auto* ctx = reinterpret_cast<FindAllContext*>(lparam);
    if (ctx->exclude && std::find(ctx->exclude->begin(), ctx->exclude->end(), hwnd) != ctx->exclude->end()) {
        return TRUE;
    }
    std::string title = ToLower(win_window::GetWindowTitle(hwnd));
    if (TitleMatchesHint(title, ctx->hintLower)) {
        ctx->results.push_back(hwnd);
    }
    return TRUE;
}

BOOL CALLBACK ListEnumProc(HWND hwnd, LPARAM lparam) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    auto* out = reinterpret_cast<std::vector<std::string>*>(lparam);
    std::string title = win_window::GetWindowTitle(hwnd);
    if (!title.empty()) out->push_back(title);
    return TRUE;
}

}  // namespace

namespace win_window {

namespace {
std::atomic<bool> g_avoidAttachThreadInput{false};
}

void SetAvoidAttachThreadInput(bool avoid) { g_avoidAttachThreadInput = avoid; }

HWND FindGameWindow(const std::string& titleHint) {
    FindContext ctx;
    ctx.hintLower = ToLower(titleHint);
    EnumWindows(FindEnumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.result;
}

std::vector<HWND> FindAllGameWindows(const std::string& titleHint, const std::vector<HWND>& exclude) {
    FindAllContext ctx;
    ctx.hintLower = ToLower(titleHint);
    ctx.exclude = &exclude;
    EnumWindows(FindAllEnumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.results;
}

std::vector<std::string> ListVisibleWindowTitles() {
    std::vector<std::string> titles;
    EnumWindows(ListEnumProc, reinterpret_cast<LPARAM>(&titles));
    return titles;
}

void BringToForegroundDirect(HWND hwnd) {
    ShowWindow(hwnd, SW_RESTORE);

    // Windows, arka plandaki bir surecin SetForegroundWindow ile odagi
    // "calmasini" varsayilan olarak ENGELLER (kilitlenmis foreground -
    // hedef pencere gercekten one gelmez, bunun yerine gorev cubugunda
    // SARI/TURUNCU yanip soner). COKLU CLIENT gecisinde tam bu problem
    // yasaniyordu - webapp.exe arka planda calisan bir surec oldugu icin.
    //
    // Cozum: AttackThreadInput ile GECICI olarak mevcut foreground
    // pencerenin girdi kuyruguna "iliseriz" - bu, Windows'un izin
    // kontrolunu atlatan, TAMAMEN standart/belgeli bir Win32 teknigidir.
    // NOT: bu SADECE isletim sistemine "hangi pencere onde olsun" bilgisi
    // verir - klavye/fareye HICBIR sekilde dokunmaz, bu yuzden onceki
    // input-enjeksiyon krizleriyle (Interception/Shift) hicbir ilgisi yok.
    // SON CARE (bazi sistemlerde yukaridaki teknikler TEK BASINA
    // yetersiz kaliyor): Windows'un "foreground kilit zaman asimi"
    // kontrolu esas olarak "en son GERCEK input hangi surece geldi" sorusuna
    // bakar. Standart/yaygin cozum: SetForegroundWindow'dan hemen once TEK,
    // ANLIK bir ALT tusu basip-birakma simule etmek - Windows bunu "bu
    // surece kullanicidan input geldi" sayip izni verir.
    //
    // BU, ONCEKI KRITIK HATAYLA (Shift'in defalarca/uzun sure basili
    // tutulmasi -> Yapiskan/Filtre Tuslari erisilebilirlik kisayollari)
    // AYNI KATEGORIDE DEGIL: (1) ALT tusu - Shift DEGIL; Yapiskan/Filtre
    // Tuslari SADECE Shift'i izler. (2) TEK, ANLIK basip-birakma - ASLA
    // tekrarli veya uzun-sureli basili tutma yok. (3) Oyuna degil, Windows
    // kabugunun kendi odak-arabuluculuguna yonelik - oyun sunucusunun
    // algilamasiyla (LLMHF_INJECTED) hicbir ilgisi yok, cunku bu tus oyunun
    // OYNANIS girdisi olarak degil, sadece pencere gecisi sirasinda
    // Windows'un kendi ic kontrolunu tatmin etmek icin kullanilir.
    INPUT altTap[2] = {};
    altTap[0].type = INPUT_KEYBOARD;
    altTap[0].ki.wVk = VK_MENU;
    altTap[1].type = INPUT_KEYBOARD;
    altTap[1].ki.wVk = VK_MENU;
    altTap[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, altTap, sizeof(INPUT));

    // EK COZUM (kritik hata sonrasi eklendi): Windows'un "foreground kilit
    // zaman asimi" (SPI_SETFOREGROUNDLOCKTIMEOUT) ayari, AttachThreadInput/
    // Alt-tap hileleri BILE bazen yetersiz kalacak kadar sikiysa devreye
    // giriyor - iki client de birbirini engelleyip HICBIRI gercekten one
    // gelmiyordu (loglarda "Windows odak degisimini engelliyor" dongusu).
    // Bu SADECE bir Windows KABUK ayari - klavye/fareye dokunmuyor, oyun
    // sunucusunun goremeyecegi turden - gecici olarak 0'a cekilip islem
    // bitince ESKI DEGERINE geri yukleniyor.
    DWORD oldLockTimeout = 0;
    SystemParametersInfoW(SPI_GETFOREGROUNDLOCKTIMEOUT, 0, &oldLockTimeout, 0);
    SystemParametersInfoW(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, 0, SPIF_SENDCHANGE);

    DWORD currentThreadId = GetCurrentThreadId();
    DWORD foregroundThreadId = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    DWORD targetThreadId = GetWindowThreadProcessId(hwnd, nullptr);

    // UYUMLULUK MODU (bkz. win_window.h SetAvoidAttachThreadInput yorumu):
    // acikken AttachThreadInput HIC CAGRILMAZ - sadece Alt-tap + kilit zaman
    // asimi + topmost-toggle + tekrar denenen SetForegroundWindow'a guvenilir.
    bool attachedFg = false, attachedTarget = false;
    if (!g_avoidAttachThreadInput.load()) {
        if (foregroundThreadId && foregroundThreadId != currentThreadId) {
            attachedFg = AttachThreadInput(currentThreadId, foregroundThreadId, TRUE) != 0;
        }
        if (targetThreadId && targetThreadId != currentThreadId && targetThreadId != foregroundThreadId) {
            attachedTarget = AttachThreadInput(currentThreadId, targetThreadId, TRUE) != 0;
        }
    }

    // EK GUVENCE: gecici olarak "en ustte" (topmost) yapip hemen geri
    // almak, z-sirasini zorlayan AYRI/tamamlayici bir teknik - bazi
    // sistemlerde AttachThreadInput TEK BASINA yeterli olmuyor, bu ikisi
    // BIRLIKTE cok daha guvenilir. Yine SADECE pencere yonetimi API'leri -
    // klavye/fare girdisi enjekte etmiyor.
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    // TEKRAR DENEME: tek bir SetForegroundWindow cagrisi bazen (mesaj
    // kuyrugu/timing yuzunden) sessizce basarisiz olabiliyor - basarili
    // olana kadar (ya da makul bir sinira kadar) kisa araliklarla tekrar
    // dene, boylece cagiran taraf (MetinBot::Activate) donusunde pencere
    // GERCEKTEN one gelmis olsun.
    for (int attempt = 0; attempt < 5; ++attempt) {
        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);
        SetFocus(hwnd);
        SetActiveWindow(hwnd);
        if (GetForegroundWindow() == hwnd) break;
        Sleep(30);
    }

    if (attachedFg) AttachThreadInput(currentThreadId, foregroundThreadId, FALSE);
    if (attachedTarget) AttachThreadInput(currentThreadId, targetThreadId, FALSE);

    // Kilit zaman asimini eski (kullanicinin sistem genelindeki) degerine geri yukle.
    SystemParametersInfoW(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, (PVOID)(uintptr_t)oldLockTimeout, SPIF_SENDCHANGE);
}

namespace {
std::function<void(HWND)> g_foregroundRequestHandler;
}

void SetForegroundRequestHandler(std::function<void(HWND)> handler) {
    g_foregroundRequestHandler = std::move(handler);
}

void BringToForeground(HWND hwnd) {
    // KAYITLI BIR HANDLER VARSA (bkz. web_main.cpp - tepsi simgesi is
    // parcacigina yonlendirir) ONU kullan - GERCEK bir mesaj dongusu olan
    // bir is parcacigindan gelen SetForegroundWindow istekleri Windows
    // tarafindan cok daha guvenilir kabul ediliyor. Handler henuz kayitli
    // degilse (ör. cok erken bir cagri) dogrudan denemeye DUS.
    if (g_foregroundRequestHandler) {
        g_foregroundRequestHandler(hwnd);
        return;
    }
    BringToForegroundDirect(hwnd);
}

DWORD EnsureInputFocus(HWND hwnd) {
    DWORD pid = 0;
    if (!hwnd) return pid;
    GetWindowThreadProcessId(hwnd, &pid);

    DWORD currentThreadId = GetCurrentThreadId();
    DWORD targetThreadId = GetWindowThreadProcessId(hwnd, nullptr);
    bool attached = false;
    // UYUMLULUK MODU: bkz. win_window.h SetAvoidAttachThreadInput. Bu
    // fonksiyon HER TEK tiklama/tus basimindan once cagrildigi icin
    // (bkz. input.cpp), acikken AttachThreadInput'u burada da ATLAMAK
    // KRITIK - aksi halde tek noktada kapatilip burada unutulmus olurdu.
    if (!g_avoidAttachThreadInput.load() && targetThreadId && targetThreadId != currentThreadId) {
        attached = AttachThreadInput(currentThreadId, targetThreadId, TRUE) != 0;
    }
    // DAHA AGRESIF (Grok onerisi): sadece SetFocus yetmeyebilir - ayni anda
    // GERCEK foreground/aktif pencere de zorlanir. Ucuzdur (agir alt-tap/
    // topmost-toggle/5-deneme dongusu YOK, bkz. BringToForeground) - her
    // tus/tik oncesi cagirmak icin uygundur.
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
    if (attached) AttachThreadInput(currentThreadId, targetThreadId, FALSE);
    return pid;
}

std::string GetWindowTitle(HWND hwnd) {
    // KRITIK: GetWindowTextA sistem ANSI kod sayfasini (Turkce Windows'ta
    // CP1254) kullanir, UTF-8 DEGIL - karakter adinda Turkce harf (ç, ğ, ı,
    // ö, ş, ü) varsa sonuc gecersiz UTF-8 baytlar icerir. Bu deger daha
    // sonra nlohmann::json ile /api/status yanitina konulunca dump() "invalid
    // UTF-8 byte" exception'i atip HER status sorgusunu (250ms'de bir)
    // basarisiz kiliyordu - bu da arayuzdeki durum/gecmis guncellemelerinin
    // tumden calismamasina sebep oluyordu. GetWindowTextW + UTF-8'e dogru
    // donusum ile kokten cozuluyor.
    wchar_t buf[512] = {0};
    int len = GetWindowTextW(hwnd, buf, sizeof(buf) / sizeof(buf[0]) - 1);
    if (len <= 0) return "";
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, buf, len, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return "";
    std::string out(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, len, out.data(), utf8Len, nullptr, nullptr);
    return out;
}

RECT GetClientRectOnScreen(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);  // (0, 0, w, h) - pencereye gore
    POINT topLeft{0, 0};
    ClientToScreen(hwnd, &topLeft);
    RECT result;
    result.left = topLeft.x;
    result.top = topLeft.y;
    result.right = topLeft.x + client.right;
    result.bottom = topLeft.y + client.bottom;
    return result;
}

RECT GetWindowRectOnScreen(HWND hwnd) {
    RECT r{};
    GetWindowRect(hwnd, &r);
    return r;
}

RECT GetCapturedFrameRectOnScreen(HWND hwnd) {
    // KRITIK (kullanicinin logundan OLCULDU): GetWindowRect, Windows'un
    // GORUNMEZ yeniden-boyutlandirma kenarligini (DWM genisletilmis cerceve)
    // de icerir - kullanicinin oyununda pencere 816x639 gorunurken Windows
    // Graphics Capture'in urettigi kare 802x632 idi. Yani kare, pencere
    // dikdortgeninin TAMAMINA denk GELMIYOR; sadece GORUNUR cerceveye denk
    // geliyor. Kareyi tam pencereye ORANTILAYARAK olceklemek bu yuzden
    // merkezde 0, kenarlarda ~7px buyuyen SISTEMATIK bir kayma uretiyordu
    // ("tespit ettigi yerden uzaga tikliyor").
    //
    // DWMWA_EXTENDED_FRAME_BOUNDS tam olarak o GORUNUR cerceveyi dondurur -
    // yani WGC'nin yakaladigi alani. Boylece donusum ORANTILI OLCEKLEME
    // olmadan, PIKSEL PIKSEL (1:1 ofset) yapilabilir.
    RECT ext{};
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &ext, sizeof(ext))) &&
        (ext.right - ext.left) > 0 && (ext.bottom - ext.top) > 0) {
        return ext;
    }
    // Desteklenmiyorsa (cok eski Windows) eski davranisa don.
    return GetWindowRectOnScreen(hwnd);
}

void KillProcessOwningWindow(HWND hwnd) {
    if (!hwnd) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return;
    HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!proc) return;
    TerminateProcess(proc, 0);
    CloseHandle(proc);
}

std::string GetMachineHwid() {
    HKEY key;
    // KEY_WOW64_64KEY: bu deger 32/64-bit surumden BAGIMSIZ HER ZAMAN ayni
    // (gercek/tek) kayit defteri konumundan okunsun diye - aksi halde 32-bit
    // bir derleme, WOW6432Node yonlendirmesi yuzunden FARKLI (yanlis) bir
    // yerden okuyabilirdi.
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY,
                       &key) != ERROR_SUCCESS) {
        return "";
    }
    char buf[256] = {0};
    DWORD size = sizeof(buf) - 1;
    DWORD type = 0;
    std::string result;
    if (RegQueryValueExA(key, "MachineGuid", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size) ==
            ERROR_SUCCESS &&
        type == REG_SZ) {
        result = buf;
    }
    RegCloseKey(key);
    return result;
}

std::string GetLocalComputerName() {
    wchar_t buf[256] = {0};
    DWORD size = sizeof(buf) / sizeof(buf[0]);
    if (!GetComputerNameW(buf, &size)) return "";
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return "";
    std::string out(utf8Len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, out.data(), utf8Len, nullptr, nullptr);
    return out;
}

}  // namespace win_window
