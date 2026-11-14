// ELBIT OTOMASYON - NATIVE MASAUSTU UYGULAMASI
// Web arayuzunun (tarayici sekmesi) YERINE GECER - kendi penceresi/simgesi
// olan bagimsiz bir .exe. Gorsel katman WebView2 (Microsoft'un Chromium
// tabanli, Windows'a gomulu native kontrolu - VS Code/Discord/Slack'in
// kullandigi AYNI teknik) ile HTML/CSS/JS olarak cizilir - boylece animasyon/
// glow/blur gibi "premium SaaS" gorunumu, elle piksel yerlestiren bir GUI
// kutuphanesinden (ImGui) COK daha az efor ile, COK daha kaliteli elde
// edilir. Veri/otomasyon mantigi (NativeSession) TAMAMEN C++ tarafinda -
// WebView2 sadece bir "goruntu penceresi", JS sadece yerel HTTP sunucusuna
// (asagida, ayni exe icinde) fetch() ile konusur - internet GEREKTIRMEZ.
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <wrl.h>
#include <wil/com.h>
#include <WebView2.h>
#include <intrin.h>
#include <tlhelp32.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

// Discord OAuth kod-degisimi (authorization code -> access token) ve
// kullanici bilgisi (/users/@me) icin discord.com'a HTTPS cikisi gerekir -
// httplib::SSLClient burdan kullanilir. CPPHTTPLIB_OPENSSL_SUPPORT, vcpkg'nin
// cpp-httplib[openssl] ozelligi uzerinden httplib::httplib hedefinin kendi
// derleme tanimi olarak zaten geliyor (CMakeLists.txt).
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <random>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/imgcodecs.hpp>

#include "bot.h"
#include "calibration.h"
#include "capture.h"
#include "config.h"
#include "diag_log.h"
#include "win_tls_trust.h"
#include "discord_bot.h"
#include "telegram_bot.h"
#include "win_window.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace Microsoft::WRL;

// ============================================================================
// KAPSAYICI COKME KORUMASI (ACIL - kritik kullanici bildirimi: "acilir
// acilmaz kendi kendine kapaniyor").
// ----------------------------------------------------------------------------
// TUM bagimsiz arka plan thread'lerimiz (HTTP sunucusu, zamanlayici, lisans
// bekcisi, Discord/oyun-icinde-durum dongusu) buradan gecirilir. Bu dosya
// /EHa ile derleniyor (bkz. CMakeLists.txt) - yani catch(...) artik SADECE
// C++ istisnalarini degil, DONANIM KAYNAKLI cokmeleri de (ör. GoodbyeDPI gibi
// bir DPI-atlatma aracinin TLS/paket akisini bozmasi sonucu olusan access
// violation) YAKALAR. Boylece HERHANGI bir arka plan is parcaciginda olusacak
// beklenmeyen bir hata, ARTIK TUM UYGULAMAYI DEGIL, SADECE O TEK ozelligi
// (ör. sadece lisans kontrolu) sessizce durdurur - kullanici uygulamayi
// KULLANMAYA DEVAM EDEBILIR. Ayrica TAM detay (hata koduyla) crash_guard.log'a
// yazilir - "neden kapandi" artik TAHMIN degil, KANITLA cevaplanabilir.
static void LogCrashGuard(const std::string& where, const std::string& detail) {
    std::ofstream f("crash_guard.log", std::ios::app);
    if (!f.is_open()) return;
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
    localtime_s(&tmBuf, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmBuf);
    f << "[" << buf << "] [" << where << "] " << detail << "\n";
}

// SEH (YAPISAL istisna - ör. access violation) sinirini izole eden minimal
// yardimci - ICINDE parametreler disinda HICBIR C++ nesnesi (yikici/destructor
// gerektiren) YOK. MSVC, __try/__except'i "nesne geri-sarma (unwinding)
// gerektiren" bir fonksiyonda kullanmaya izin VERMEZ (C2712 derleme hatasi) -
// bu yuzden SEH sinirini boyle KENDI BASINA, POD-DISI hicbir sey icermeyen
// bir fonksiyona ayirmak GEREKLI ve YETERLI (asenkron /EHa gibi genis capli
// bir derleyici bayragina GEREK KALMADAN, core_lib'in performans-kritik
// kodunu ETKILEMEDEN calisir).
static bool CallWithSehGuard(void (*fn)(void*), void* ctx, DWORD* outCode) {
    __try {
        fn(ctx);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (outCode) *outCode = GetExceptionCode();
        return false;
    }
}

template <typename Fn>
static void RunGuarded(const char* name, Fn fn) {
    for (;;) {
        DWORD sehCode = 0;
        bool crashed = false;
        // C++ istisnalarini (std::exception) NORMAL catch ile, YAPISAL
        // (SEH/donanim) istisnalari ise yukaridaki izole SEH siniriyla
        // yakaliyoruz - boylece HEM "kod hatasi" (ör. beklenmeyen JSON tipi)
        // HEM "cokme" (ör. GoodbyeDPI'nin bozdugu bir TLS akisinin tetikledigi
        // access violation) sinifindaki hatalarin HICBIRI tum uygulamayi
        // goturmez.
        try {
            bool sehOk = CallWithSehGuard(
                [](void* p) { (*reinterpret_cast<Fn*>(p))(); }, &fn, &sehCode);
            if (!sehOk) {
                crashed = true;
            } else {
                // fn() DUZGUN (istisnasiz) dondu - bu dongu fonksiyonlari
                // normalde "while(true)" icerdigi icin buraya SADECE
                // beklenmeyen bir erken-donus durumunda dusulur.
                LogCrashGuard(name, "fonksiyon beklenmedik sekilde (istisnasiz) sona erdi");
                return;
            }
        } catch (const std::exception& e) {
            char buf[160];
            snprintf(buf, sizeof(buf), "yakalanmamis C++ istisnasi: %s - is parcacigi 2sn sonra YENIDEN BASLATILIYOR", e.what());
            LogCrashGuard(name, buf);
        } catch (...) {
            LogCrashGuard(name, "bilinmeyen istisna - is parcacigi 2sn sonra YENIDEN BASLATILIYOR");
        }
        if (crashed) {
            char buf[96];
            snprintf(buf, sizeof(buf), "YAPISAL (SEH) istisna yakalandi, kod=0x%08lX - is parcacigi 2sn sonra YENIDEN BASLATILIYOR",
                      sehCode);
            LogCrashGuard(name, buf);
        }
        // Siki bir cokme dongusune (ani CPU tuketimine) girmemek icin kisa bir
        // bekleme, sonra ozelligi YENIDEN baslat - kullanici o ozelligi
        // (ör. lisans kontrolu) TAMAMEN kaybetmesin, gecici bir hata bir
        // SONRAKI denemede duzelebilir (ör. GoodbyeDPI o an baglantiyi
        // bozmus olabilir, birazdan duzelir).
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

static const int kLocalPort = 8010;

// BU BUILD'IN surumu - /api/version-check bunu update_check_url'den donen
// "version" alaniyla KARSILASTIRIR. Yeni bir surum yayinlarken (config.json
// -> update_check_url'deki JSON'u GitHub'dan degistirip yukseltince) bu
// sabiti de YENI surumle EŞLESECEK sekilde guncelleyip YENIDEN DERLEMEK
// gerekir - aksi halde YENI build de "guncelleme mevcut" der.
static const char* kAppVersion = "2.0";

// ============================================================================
// METIN KLASORU TARAMA - web_main.cpp'deki ScanMetinFolders ile ayni mantik,
// web panelini etkilememesi icin BAGIMSIZ/kopya kod.
// ============================================================================
struct MetinOption {
    std::string name;
};
static const std::vector<std::string> kSkipFolders = {"build",   "src",    "third_party", "web_ui", "desktop_ui",
                                                        "uploads", "models", "frontend",    "dist",
                                                        // "metin": template_picker.exe'nin TAM EKRAN OTOMATIK YAKALAMA
                                                        // modunun (Roboflow egitim verisi toplama) ham goruntu
                                                        // klasoru - gercek bir hedef TURU DEGIL, secilebilir
                                                        // listede GORUNMEMELI.
                                                        "metin",
                                                        // "siyah": kullanilmayan/terk edilmis eski bir deneme
                                                        // kategorisi - listeden kaldirildi (klasor SILINMEDI,
                                                        // sadece secilebilir listeden gizlendi).
                                                        "siyah",
                                                        // "hp_bar" (yeni adiyla hp_bar): HP cubugu referans
                                                        // goruntusunun (_hpbar.png) tutuldugu klasor - gercek bir
                                                        // hedef TURU DEGIL, secilebilir listede GORUNMEMELI.
                                                        "hp_bar",
                                                        // "puzzle_kayit": PuzzleSolver'in cozdugu her bulmacayi
                                                        // teshis amaciyla kaydettigi klasor (bkz. puzzle_solver.cpp
                                                        // ComputeOddTile) - CALISMA ANINDA olusur ve ici .png dolar,
                                                        // bu yuzden asagidaki "icinde png var mi" testini gecip
                                                        // yanlislikla bir hedef TURU gibi listeleniyordu.
                                                        "puzzle_kayit",
                                                        // "eldiven" (yeni adiyla eldiven): periyodik envanter-esyasi
                                                        // ozelliginin sablon ikonunu tuttugu klasor (bkz. config.h
                                                        // eldiven_item_template_path) - hedef TURU degil.
                                                        "eldiven",
                                                        // "nesne_market" (yeni adiyla nesne_market): Nesne Market
                                                        // bildirim seridi sablonunu tuttugu klasor - hedef TURU degil.
                                                        "nesne_market"};
static std::vector<MetinOption> ScanMetinFoldersNative() {
    std::vector<MetinOption> options;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(".", ec)) {
        if (!entry.is_directory()) continue;
        std::string name = entry.path().filename().string();
        if (!name.empty() && name[0] == '.') continue;
        if (std::find(kSkipFolders.begin(), kSkipFolders.end(), name) != kSkipFolders.end()) continue;
        bool hasPng = false;
        for (const auto& f : fs::directory_iterator(entry.path(), ec)) {
            if (!f.is_regular_file()) continue;
            auto ext = f.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".png" && f.path().filename().string() != "_panel.png") {
                hasPng = true;
                break;
            }
        }
        if (hasPng) options.push_back({name});
    }
    std::sort(options.begin(), options.end(), [](auto& a, auto& b) { return a.name < b.name; });
    return options;
}

// ============================================================================
// NATIVE OTURUM YONETICISI: web_main.cpp'deki BotWorker ile AYNI coklu-client
// round-robin + puzzle-onceligi mantigi, HTTP/JSON'dan BAGIMSIZ (dogrudan
// MetinBot ile calisir - asagidaki yerel sunucu bunu sadece JSON'a sarar).
// ============================================================================
// OTO BASMA slotu: bir tus (harf/rakam/"F1") ve kendi araligi (saniye).
struct AutoKeySlot {
    std::string key;
    double intervalSec = 60.0;
    // "Basılı Tut" (kullanici talebi): true ise intervalSec YOK SAYILIR,
    // tus surekli basili tutulur (sure/aralik girilmez) - bkz. run loop.
    bool hold = false;
};

struct NativeSession {
    std::atomic<bool> running{false};
    std::atomic<bool> shouldExit{false};
    std::atomic<bool> connected{false};
    std::string error;
    std::string statusText = "Bir metin türü seçip Başlat'a basın.";
    std::vector<std::string> selectedMetins;
    int maxClients = 2;
    // Anasayfadaki "Sunucu" secimi (bkz. desktop_ui/app.js SERVER_WINDOW_TITLES) -
    // dolu ise config.json'daki window_title_hint yerine BUNU kullanir. Bos
    // birakilirsa (ör. zamanlayici/oturum-devam ile baslatilan cagirilar)
    // config.json'daki varsayilan aranir.
    std::string windowTitleOverride;
    // OTO NESNE (kullanici talebi) - Anasayfa/Ayarlar'dan iletilir,
    // /api/start govdesiyle gelir - bkz. config.h auto_loot_enabled yorumu.
    bool autoLootEnabled = false;
    bool antiStuckEnabled = false;
    double antiStuckSeconds = 150.0;  // varsayilan 2.5 dakika - Fonksiyonlar'dan degistirilebilir
    // INSANCILLASTIR + OTO BASMA (kullanici talebi): Start() imzasini
    // sismesin diye bu ikisi /api/start handler'inda DOGRUDAN set ediliyor
    // (Start bunlara DOKUNMAZ) - zamanlayici/discord ile yeniden baslatmada
    // son degerler korunur. RunLoop bu uyeleri okur.
    bool humanizeEnabled = false;
    bool autoKeyEnabled = false;
    // OTO BASMA FARMI (kullanici talebi): "Oto Basma Farmı" butonuyla
    // baslatilir - pencereye gecer, SADECE Oto Basma calisir, hedef arama/
    // tiklama/anti-takilma vb. TAMAMEN devre disi kalir. AYNI desen:
    // /api/start handler'inda dogrudan set edilir, RunLoop okur.
    bool autoKeyOnlyMode = false;
    // A-T PRO (kullanici talebi) - humanizeEnabled ile AYNI desen: Start()
    // imzasini sismesin diye /api/start handler'inda DOGRUDAN set edilir.
    bool atProEnabled = false;
    // SURDU KUYRUKLAMA (kullanici talebi) - kesim sirasinda da diger metinleri
    // oyunun kuyruguna ekle. Gecmiste hatali oldugu icin ayri bir switch.
    bool queueOthersEnabled = false;
    // TIKLAMA SEKLI (kullanici talebi - Fonksiyonlar > Tiklama Degistirme):
    // true = Shift + Sag Tik (VARSAYILAN), false = SADECE Sag Tik. humanizeEnabled
    // ile AYNI desen: Start() imzasini sismesin diye /api/start handler'inda
    // DOGRUDAN set edilir, Start() icinde config'e aktarilir.
    bool clickUseShift = true;
    // TIKLAMA YONTEMI (kullanici talebi - Fonksiyonlar > Tiklama Yontemi):
    // false = Varsayilan (SendMessage + klavyede cift SendInput), true =
    // SADECE PostMessage. clickUseShift ile AYNI desen: /api/start
    // handler'inda DOGRUDAN set edilir, Start() icinde config'e aktarilir.
    bool inputUsePostmessage = false;
    // MANUEL TARAMA (kullanici talebi) - Start() imzasini sismesin diye
    // /api/start handler'inda DOGRUDAN set edilir, Start() icinde config'e
    // aktarilir (clickUseShift ile AYNI desen).
    bool manualScanEnabled = false;
    double manualScanThreshold = 0.75;
    // NESNE MARKET KAPAT (kullanici talebi) - AYNI desen: /api/start
    // handler'inda dogrudan set edilir, MetinBot::Step() kendi ici
    // periyodik seyreltmesiyle okur.
    bool nesneMarketDismissEnabled = false;
    std::vector<AutoKeySlot> autoKeySlots;

    std::mutex mutex;
    std::vector<std::unique_ptr<MetinBot>> bots;
    size_t activeIndex = 0;
    Config config;

    void Start(const std::vector<std::string>& metins, int maxC, const std::string& windowTitle = "",
               bool autoLootEn = false, bool antiStuckEn = false, double antiStuckSec = 150.0) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!bots.empty()) return;
        selectedMetins = metins;
        // KULLANICI TALEBI: artik SIFIR sinir - arayuz sayi girmiyor, secilen
        // sunucu basligiyla eslesen HER pencere kullanilir. Ust sinir (9999)
        // sadece bozuk/negatif bir deger gelirse cokmeyi onleyen bir savunma
        // agi - pratikte FindAllGameWindows zaten gercekte acik olan
        // pencere sayisini dondurur, bu deger asla gercek bir kisitlama
        // olarak calismaz.
        maxClients = std::clamp(maxC, 1, 9999);
        windowTitleOverride = windowTitle;
        autoLootEnabled = autoLootEn;
        antiStuckEnabled = antiStuckEn;
        antiStuckSeconds = antiStuckSec;
        error.clear();
        connected = false;
        statusText = "Oyun penceresine bağlanılıyor...";
        shouldExit = false;
        running = true;
        // RunGuarded: bot dongusunde (goruntu isleme/YOLO/tiklama) beklenmeyen
        // bir cokme olursa artik TUM UYGULAMAYI degil, SADECE bu oturumu
        // durdurur - kullanici "Baslat"a tekrar basabilir, crash_guard.log'a
        // detay yazilir.
        std::thread([this]() { RunGuarded("NativeSession::RunLoop", [this]() { RunLoop(); }); }).detach();
    }

    void Pause() { running = false; }
    void Resume() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!bots.empty()) running = true;
    }
    void Stop() {
        shouldExit = true;
        running = false;
    }

    void RunLoop() {
        try {
            config = Config::Load("config.json");
            config.templates_dirs = selectedMetins;
            if (!windowTitleOverride.empty()) config.window_title_hint = windowTitleOverride;
            config.auto_loot_enabled = autoLootEnabled;
            config.click_use_shift = clickUseShift;
            config.input_use_postmessage = inputUsePostmessage;
            config.manual_scan_enabled = manualScanEnabled;
            config.manual_scan_threshold = manualScanThreshold;
            config.anti_stuck_enabled = antiStuckEnabled;
            config.anti_stuck_seconds = antiStuckSeconds;
            config.humanize_enabled = humanizeEnabled;
            config.at_pro_enabled = atProEnabled;
            config.queue_other_visible_metins = queueOthersEnabled;
            // NESNE MARKET KAPATMA, ANTI-TAKILMA ILE BIRLIKTE OTOMATIK ACILIR
            // (kullanici talebi). SEBEP: anti-takilma kurtarmasi hedefi
            // birakmak icin ESC basar; ama ekranda "Nesne Market" bildirimi
            // varken ESC once O pencereye gider ve bazen oyunun AYARLAR
            // menusunu acip botu tamamen kilitler. Bildirim seridini gorup
            // kapatan mekanizma zaten var - artik ayri bir fonksiyon olarak
            // acilmasi GEREKMIYOR: anti-takilma aciksa bu da devrede olur.
            config.nesne_market_dismiss_enabled = nesneMarketDismissEnabled || antiStuckEnabled;

            {
                std::string t = config.window_title_hint;
                std::transform(t.begin(), t.end(), t.begin(), ::tolower);
                // ORIGINS UYUMLULUK MODU: bkz. config.h avoid_attach_thread_input
                // yorumu - "botu baslatinca kapaniyor" sorununun kok neden
                // adayi. Sunucu secimi Origins ise otomatik acilir.
                if (t.find("origins") != std::string::npos) config.avoid_attach_thread_input = true;
            }
            // Bayragi FIILEN uygula - win_window.cpp'deki AttachThreadInput
            // cagrilari (BringToForegroundDirect + EnsureInputFocus) buna gore
            // atlanir/atlanmaz. Pencere bulunmadan/bagl anmadan ONCE set
            // edilmeli ki ILK BringToForeground cagrisi bile dogru davransin.
            win_window::SetAvoidAttachThreadInput(config.avoid_attach_thread_input);

            std::vector<HWND> found = win_window::FindAllGameWindows(config.window_title_hint);
            if (!config.window_title_fallback_hint.empty()) {
                auto more = win_window::FindAllGameWindows(config.window_title_fallback_hint, found);
                found.insert(found.end(), more.begin(), more.end());
            }
            if (found.empty()) {
                throw std::runtime_error("Oyun penceresi bulunamadı ('" + config.window_title_hint + "').");
            }
            if ((int)found.size() > maxClients) found.resize(maxClients);

            std::vector<std::unique_ptr<MetinBot>> newBots;
            for (HWND hwnd : found) {
                auto b = std::make_unique<MetinBot>(config);
                b->BindWindow(hwnd);
                newBots.push_back(std::move(b));
            }
            std::lock_guard<std::mutex> lock(mutex);
            bots = std::move(newBots);
            activeIndex = 0;
            bots[activeIndex]->Activate();
            connected = true;
        } catch (const std::exception& e) {
            error = e.what();
            std::lock_guard<std::mutex> lock(mutex);
            bots.clear();
            return;
        }

        // Bir client'a odagin GERCEKTEN gecmesini GARANTI eden yardimci.
        // KRITIK (kullanici bildirdi - "gece kalkinca mouse donuyor ama
        // tiklamiyordu, ekrana bir sag tik atinca duzeldi"): Activate() bazen
        // Windows'un odak-calma engeli yuzunden SESSIZCE basarisiz oluyordu;
        // Step() ise on planda DEGILKEN hicbir sey yapmadan cikiyor. Client
        // hedefe KILITLIYKEN odak kaybolursa ne "tiklama" ne "idle" olur, bu
        // yuzden ESKI mantik ASLA yeniden Activate etmiyor -> sonsuza kadar
        // donuyordu. Cozum: HER dongude aktif client'in gercekten on planda
        // oldugunu DOGRULA, degilse yeniden one getir (kendiliginden duzelir).
        // KISITLAMA: pencere GERCEKTEN one gelemiyorsa (ör. simge durumunda)
        // Activate() her dongude Alt-tap enjekte edip durmasin diye en fazla
        // ~350ms'de bir deniyoruz. Normal durumda (Activate ilk denemede
        // basarili) foreground==hwnd olur ve zaten hic tetiklenmez.
        auto lastEnsureAt = std::chrono::steady_clock::time_point{};
        auto ensureForeground = [&](MetinBot* b) {
            if (!b || GetForegroundWindow() == b->Hwnd()) return;
            auto nowE = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(nowE - lastEnsureAt).count() < 0.35) return;
            lastEnsureAt = nowE;
            b->Activate();
        };

        // OTO BASMA: her slot icin bir sonraki basim zamani. Araligli
        // slotlarda ilk basim bir araligin SONUNDA olur (baslar baslamaz
        // spam olmasin). "Basili Tut" slotlari ise HEMEN (0sn) baslar - sure
        // kavrami yok, bot baslar baslamaz tus basili kalmali.
        std::vector<std::chrono::steady_clock::time_point> autoKeyNext;
        // Hangi "Basili Tut" slotlarinin su an FIILEN basili oldugunu izler -
        // duraklatma/durdurmada TUSU BIRAKMAK icin sart (aksi halde tus
        // sonsuza kadar basili KALIR, bkz. asagidaki release-on-pause/exit).
        std::vector<bool> autoKeyHeldNow(autoKeySlots.size(), false);
        {
            auto now0 = std::chrono::steady_clock::now();
            for (const auto& s : autoKeySlots) {
                double firstDelay = s.hold ? 0.0 : std::max(1.0, s.intervalSec);
                autoKeyNext.push_back(now0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                  std::chrono::duration<double>(firstDelay)));
            }
        }

        auto activeSince = std::chrono::steady_clock::now();
        // Aktif client ARAMA modundayken odagi elinde tutacagi en uzun sure.
        // Bir arama dongusu (kamera cevir + search_action_wait_sec ~0.8-1.4sn
        // tarama) ~1-1.5sn surdugu icin bu dilim her sirada ~2 arama dongusu
        // demektir - gecis maliyeti (Activate ~0-180ms) amortize olur.
        constexpr double kSearchSliceSec = 2.5;
        // Clientlerin HEPSI kilitliyken (kesim odak gerektirmez) periyodik
        // donus araligi - anti-takilma/loot mantigi her clientte sirayla
        // calissin diye. Eskiden 8sn idi.
        constexpr double kAllLockedRotateSec = 3.0;
        // Basili tutulan TUM oto-basma tuslarini birakir - duraklatma VE
        // durdurmada cagirilir, aksi halde tus sonsuza kadar basili kalir.
        auto releaseAllAutoKeyHolds = [&]() {
            if (bots.empty()) return;
            MetinBot* act = bots[activeIndex].get();
            for (size_t si = 0; si < autoKeySlots.size(); ++si) {
                if (autoKeyHeldNow[si]) {
                    act->SetAutoKeyHeld(autoKeySlots[si].key, false);
                    autoKeyHeldNow[si] = false;
                }
            }
        };
        // OTO BASMA: suresi dolan slot(lar)in tusunu AKTIF (odakta) client'e
        // bastir. Farm mantigina DOKUNMAZ - sadece araya ek bir tus tap'i.
        // Normal moddda odak asagida ensureForeground ile ayrica garanti
        // edilir; "Oto Basma Farmı" modunda (bkz. autoKeyOnlyMode) BU
        // fonksiyon TEK basina cagirilir, o yuzden kendi odak garantisini de
        // tasir (asagidaki normal-mod cagrisinda zaten odakli oldugundan
        // bu ekstra kontrol zararsizdir).
        auto runAutoKeyTick = [&]() {
            if (!autoKeyEnabled || bots.empty()) return;
            auto nowK = std::chrono::steady_clock::now();
            for (size_t si = 0; si < autoKeySlots.size(); ++si) {
                const auto& slot = autoKeySlots[si];
                if (slot.key.empty()) continue;
                MetinBot* act = bots[activeIndex].get();
                if (slot.hold) {
                    // BASILI TUT: sure/aralik YOK - tus surekli basili
                    // kalir. Oyunun otomatik-tekrari fark etmesi icin
                    // WASD hold ile AYNI desen: periyodik olarak (~0.3sn)
                    // TEKRAR bas - SendKey zaten "basili" durumu, bu
                    // sadece taze tutmak icin ek guvence.
                    if (nowK >= autoKeyNext[si]) {
                        if (GetForegroundWindow() != act->Hwnd()) act->Activate();
                        act->SetAutoKeyHeld(slot.key, true);
                        autoKeyHeldNow[si] = true;
                        autoKeyNext[si] = nowK + std::chrono::milliseconds(300);
                    }
                    continue;
                }
                if (slot.intervalSec < 1.0) continue;
                if (nowK >= autoKeyNext[si]) {
                    if (GetForegroundWindow() != act->Hwnd()) act->Activate();
                    act->PressAutoKey(slot.key);
                    autoKeyNext[si] = nowK + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                 std::chrono::duration<double>(slot.intervalSec));
                }
            }
        };
        while (!shouldExit) {
            for (auto& b : bots) b->SetRunning(running);

            if (!running) {
                releaseAllAutoKeyHolds();
                statusText = "Duraklatıldı";
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            // OTO BASMA FARMI (kullanici talebi): hedef arama/tiklama,
            // anti-takilma, coklu-client odak mantigi TAMAMEN devre disi -
            // SADECE pencereye gecip Oto Basma'yi calistirir.
            if (autoKeyOnlyMode) {
                if (!bots.empty()) {
                    MetinBot* act = bots[activeIndex].get();
                    if (GetForegroundWindow() != act->Hwnd()) act->Activate();
                }
                runAutoKeyTick();
                statusText = "Oto Basma Farmı çalışıyor...";
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                continue;
            }

            runAutoKeyTick();

            // FIX 2 (ADIM 1) - ODAK-SUZ KESIM YOKLAMASI: kilitli ama ODAKTA
            // OLMAYAN clientlerin kesim durumunu, odak gerektirmeden (WGC
            // capture arka planda calisir) yokla. Bir metnin kesimi oyunda
            // OTOMATIK devam ettigi icin, yavas bir client keserken hizli
            // client bos beklemesin: hizlinin kesimi bitince bu yoklama onu
            // "arama" moduna alir, planlayici da (asagida) ona odagi verir.
            // Boylece iki karakter AYNI ANDA ilerler - eski sistemde biri
            // 10sn digeri 30sn iken 40sn+ kaybediliyordu, artik ~30sn.
            if (bots.size() > 1) {
                for (size_t i = 0; i < bots.size(); ++i) {
                    if (i == activeIndex) continue;
                    bots[i]->PollUnfocusedCut();
                }
            }

            // FIX 2 (ADIM 2) - ODAGI KIME VERELIM?
            //  * Aktif client ARAMA modundaysa (odaga IHTIYACI var - kamera
            //    cevirme + tiklama icin) odagi ONDA TUT (arama yarida kesilmesin).
            //  * Aktif client KILITLIYSE (kesim odak gerektirmez), odagi ARAMA
            //    modundaki (odaga ihtiyaci olan) bir client'a devret.
            //  * Herkes kilitliyse: mevcut aktifi tut; ama cok uzun (8sn) ayni
            //    clientteysen digerine gec (takilma/anti-takilma mantigi her
            //    clientte periyodik calissin). Boylece odak SADECE gercekten
            //    gerektiginde degisir -> gereksiz "one getirme" (ve icindeki
            //    Alt-tap enjeksiyonu) thrash'i OLMAZ.
            if (bots.size() > 1) {
                MetinBot* cur = bots[activeIndex].get();
                double activeForSec =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - activeSince).count();
                int desired = (int)activeIndex;

                // Odaga GERCEKTEN ihtiyaci olan client = ARAMA modunda olan
                // (kamera cevirme + tiklama icin on plan sart). KILITLI bir
                // client'in kesimi oyunda odaksiz devam eder.
                int searching = -1;
                for (size_t off = 1; off <= bots.size(); ++off) {
                    size_t idx = (activeIndex + off) % bots.size();
                    if (!bots[idx]->HasTarget()) { searching = (int)idx; break; }
                }

                if (cur->HasTarget()) {
                    if (searching >= 0) {
                        desired = searching;
                    } else if (activeForSec > kAllLockedRotateSec) {
                        desired = (int)((activeIndex + 1) % bots.size());  // hepsi kilitli - periyodik doner
                    }
                } else if (searching >= 0 && activeForSec > kSearchSliceSec) {
                    // CIFT CLIENT HIZI (kullanici sikayeti: "cift client tek
                    // cliente gore cok yavas"): eskiden bu dal HIC YOKTU -
                    // desired ataması tamamen `if (cur->HasTarget())` icindeydi.
                    // Yani aktif client ARARKEN odak ASLA devredilmiyordu ve
                    // ikinci client, birincisi bir hedef kilitleyene kadar
                    // (bazen 10+ saniye) HICBIR SEY yapmadan bekliyordu -
                    // arama fazlari tamamen SERI calisiyordu. Kesim fazlari
                    // zaten PollUnfocusedCut sayesinde paralel ilerliyordu,
                    // darbogaz TAM OLARAK burasiydi.
                    //
                    // Artik arama da zaman-dilimli: kisa bir dilimin sonunda
                    // odak, arama modundaki DIGER client'a gecer. Boylece iki
                    // karakter de duzenli olarak arar - kesim zaten paralel
                    // oldugundan ikisi de kesintisiz ilerler.
                    desired = searching;
                }
                if (desired != (int)activeIndex) {
                    bots[activeIndex]->ReleaseMouseClip();
                    activeIndex = (size_t)desired;
                    activeSince = std::chrono::steady_clock::now();
                }
            }

            MetinBot* active = bots[activeIndex].get();

            // FIX 1: Step'ten ONCE odagi garanti et - odak sessizce kaybolduysa
            // (gece boyu donma hatasi) bu satir onu geri alir, kendiliginden
            // duzelir. Zaten on plandaysa hicbir sey yapmaz (sadece bir
            // karsilastirma) - gereksiz Alt-tap/one-getirme OLMAZ.
            ensureForeground(active);

            bool clicked = active->Step();
            if (!active->ManualStatusText().empty()) {
                // MANUEL TARAMA acikken durum yazisi budur (metin modelleri
                // calismadigi icin "tespit/kesilen" sayaclari anlamsiz olur).
                statusText = active->ManualStatusText();
            } else if (clicked) {
                auto stats = active->GetStats();
                statusText = "Tespit edildi -> tıklandı (tespit:" + std::to_string(stats.targets_found) +
                              ", kesilen:" + std::to_string(stats.targets_completed) + ")";
            } else if (active->HasTarget()) {
                statusText = "Hedefte kilitli, bekleniyor...";
            } else {
                statusText = "Hedef yok, aranıyor...";
            }

            active->ConsumeTargetEndedFlag();

            // Kisa bekleme - CPU'yu bogmamak icin (kilitliyken/ararken
            // saniyede ~15-20 dongu yeterli).
            if (!clicked) {
                std::this_thread::sleep_for(std::chrono::milliseconds(active->HasTarget() ? 50 : 70));
            }
        }

        releaseAllAutoKeyHolds();
        for (auto& b : bots) b->Stop();
        std::lock_guard<std::mutex> lock(mutex);
        bots.clear();
        connected = false;
        statusText = "Durduruldu. Bir metin türü seçip Başlat'a basın.";
    }

    json ToStatusJson() {
        if (!error.empty()) return json{{"error", error}};
        std::lock_guard<std::mutex> lock(mutex);
        json j;
        j["connected"] = connected.load();
        j["running"] = running.load();
        j["active"] = !bots.empty();
        j["status_text"] = statusText;
        j["client_count"] = (int)bots.size();
        j["active_client"] = bots.empty() ? 0 : (int)activeIndex;
        double sessionSeconds = 0;
        int totalClicks = 0, targetsFound = 0, targetsCompleted = 0;
        bool locked = false;
        json characterNames = json::array();
        for (auto& b : bots) {
            auto st = b->GetStats();
            sessionSeconds = std::max(sessionSeconds, st.session_seconds);
            totalClicks += st.total_clicks;
            targetsFound += st.targets_found;
            targetsCompleted += st.targets_completed;
            if (b->HasTarget()) locked = true;
            characterNames.push_back(b->CharacterName());
        }
        j["stats"] = {{"session_seconds", sessionSeconds},
                       {"total_clicks", totalClicks},
                       {"targets_found", targetsFound},
                       {"targets_completed", targetsCompleted}};
        j["is_locked"] = locked;
        j["character_names"] = characterNames;

        // KULLANICI TALEBI: her tik/tus oncesi bulunan PID'i, bulundugu
        // andan itibaren 2-3 saniyeligine durum ekraninda gosterebilmek
        // icin - aktif client'in InputController'inin en son buldugu PID +
        // ne kadar sure once bulundugu.
        if (!bots.empty()) {
            DWORD pid = bots[activeIndex]->LastFocusedPid();
            double ageSec = bots[activeIndex]->LastFocusedPidAgeSec();
            if (pid != 0 && ageSec >= 0 && ageSec < 3.0) {
                j["focused_pid"] = (unsigned long)pid;
                j["focused_pid_age_sec"] = ageSec;
            }
        }
        return j;
    }
};

static NativeSession g_session;

// ============================================================================
// KISAYOLLAR (GLOBAL HOTKEY): kullanici Ayarlar > Kisayollar'dan Baslat/Durdur
// icin bir tus atar. OYUN EKRANI ODAKTAYKEN BILE calismasi gerektigi icin
// (WebView2 arayuzu o an tus almaz) dusuk-seviye klavye kancasi (WH_KEYBOARD_LL)
// kullaniyoruz. KRITIK: kendi ENJEKTE ettigimiz tuslari (WASD/oto-basma vb.)
// YOK SAYIYORUZ (LLKHF_INJECTED) - sadece GERCEK donanim basimina tepki verir;
// ayrica tusu TUKETMIYORUZ (CallNextHookEx) - oyun tusu yine alir, mevcut
// hicbir sey bozulmaz. Kanca sadece "bir bayrak set eder"; asil baslat/durdur
// islemini arayuz (poll ile /api/status'tan okuyup) yapar - boylece secili
// metinler/ayarlar arayuzde kalir, cakisma olmaz.
static std::atomic<int> g_startHotkeyVk{0};
static std::atomic<int> g_stopHotkeyVk{0};
// GUVENILIRLIK KATMANI (kullanici bildirdi: "kısayollar hala sorunlu",
// diger tuslar/fonksiyonlar KESINLIKLE etkilenmeden): tek bir "pending" int
// yerine SAYAC kullanilir - ayni ~250ms'lik poll penceresinde baslat VE
// durdur ayni anda basilirsa (eskiden ikincisi birincisini SESSIZCE
// eziyordu) artik ikisi de kaybolmadan sayilir. Debounce zaman damgalari,
// tus BASILI TUTULDUGUNDA Windows'un gonderdigi otomatik-tekrar WM_KEYDOWN
// selini TEK bir basisa indirger (aksi halde tek bir basili tutma birden
// fazla baslat/durdur tetikleyebilirdi).
static std::atomic<int> g_startHotkeyHits{0};
static std::atomic<int> g_stopHotkeyHits{0};
static std::atomic<DWORD> g_lastStartHotkeyTick{0};
static std::atomic<DWORD> g_lastStopHotkeyTick{0};
constexpr DWORD kHotkeyDebounceMs = 350;
static HHOOK g_kbHook = nullptr;
// KRITIK DUZELTME ("kısayollar çalışmıyor"): kanca eskiden SADECE acilista,
// config.json'daki global_hotkeys_enabled=true ISE kuruluyordu - ama arayuzde
// bu bayragi ACAN bir anahtar/checkbox HIC YOKTU, yani kullanici Ayarlar >
// Kisayollar'dan bir tus atasa bile kanca hicbir zaman kurulmuyordu (bayrak
// HER ZAMAN varsayilan false kaliyordu). Simdi: kullanici GERCEKTEN bir
// kisayol atadiginda (bkz. /api/hotkeys) bu bayrak islenir, ana pencerenin
// zaten var olan 1sn'lik zamanlayicisi (bkz. WM_TIMER/kPartyTimerId) bunu
// gorup kancayi O AN kurar - boylece kullanici hala ACIKCA bir kisayol
// atamadan kanca ASLA kurulmaz (Origins guvenligi bozulmaz), ama atadiginda
// UYGULAMAYI YENIDEN BASLATMADAN calisir.
static std::atomic<bool> g_wantKbHook{false};

// PARTI MODU (kullanici talebi - eglence/gosteris amacli): aciksa uygulama
// penceresinin BASLIGI (gorev cubugu/alt-tab'da gorunen ad) her saniye
// degisir - farkli "premium" isimler + rastgele bir PID numarasi. Isim ve
// PID gorsel/kozmetik amaclidir; SURECIN GERCEK PID'i (isletim sistemi
// tarafindan atanmis) DEGISMEZ, degistirilemez de - burada baslikta gosterilen
// sadece s" surekli farkli bir programmis gibi gorunsun" isteyen bir flex.
// Aciksa arayuz temasi da otomatik RGB (gokkusagi) olur - o kisim JS tarafinda.
static std::atomic<bool> g_partyMode{false};
static HWND g_mainHwnd = nullptr;
constexpr UINT_PTR kPartyTimerId = 0xE1B17;  // pencere zamanlayici kimligi

// GUARD MODU baslik ureticisi (kullanici talebi: "Elbit/Premium falan
// yazmasin, tamamen rastgele sayi/harf olsun"): sabit "premium" isim listesi
// KALDIRILDI - artik her cagrida rastgele buyuk/kucuk harf + rakamdan olusan
// anlamsiz bir dizi + rastgele bir "PID" uretiliyor. Hicbir sabit kelime
// (Elbit, Premium, VIP...) icermez, her seferinde farkli gorunur.
static std::wstring MakePartyTitle() {
    static const wchar_t kChars[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    constexpr int kCharsCount = 62;  // sizeof(kChars)/sizeof(wchar_t) - 1 (sondaki NUL haric)
    static std::mt19937 rng((unsigned)std::chrono::steady_clock::now().time_since_epoch().count());
    int len = 6 + (int)(rng() % 5);  // 6-10 karakter arasi rastgele uzunluk
    std::wstring s;
    s.reserve(len);
    for (int i = 0; i < len; ++i) s.push_back(kChars[rng() % kCharsCount]);
    unsigned pid = 10000 + (rng() % 90000);  // gorsel/sahte PID (4-5 hane)
    return s + L"  ┃  PID: " + std::to_wstring(pid);  // ┃ ayirici
}

// GUARD MODU dongusu (kullanici talebi: "aşırı ama aşırı hızlı değişsin, 1ms
// de falan değişsin"): eski tasarim WM_TIMER (saniyede 1 tik) uzerinden
// calisiyordu - Windows'un SetTimer'i pratikte ~10-15ms altina INEMEZ
// (USER_TIMER_MINIMUM), bu yuzden "1ms" istegi WM_TIMER ile karsilanamaz.
// Cozum: ayri, surekli calisan bir is parcacigi - InputController zaten
// timeBeginPeriod(1) cagirdigindan (bkz. input.cpp) Sleep(1) burada
// GERCEKTEN ~1ms'ye yakin cozunurlukte calisir. Kapaliyken bosuna CPU
// harcamamak icin daha uzun uyur.
static void PartyModeLoop() {
    bool wasParty = false;
    for (;;) {
        bool on = g_partyMode.load();
        if (on) {
            if (g_mainHwnd) SetWindowTextW(g_mainHwnd, MakePartyTitle().c_str());
            wasParty = true;
            Sleep(1);
        } else {
            if (wasParty && g_mainHwnd) SetWindowTextW(g_mainHwnd, L"Elbit | Premium");
            wasParty = false;
            Sleep(50);
        }
    }
}

static LRESULT CALLBACK LowLevelKbProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        auto* p = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (p && !(p->flags & LLKHF_INJECTED)) {  // SADECE gercek donanim basimi
            int vk = (int)p->vkCode;
            int sk = g_startHotkeyVk.load();
            int tk = g_stopHotkeyVk.load();
            DWORD now = GetTickCount();
            if (sk != 0 && vk == sk) {
                DWORD last = g_lastStartHotkeyTick.exchange(now);
                if (now - last > kHotkeyDebounceMs) g_startHotkeyHits.fetch_add(1);
            } else if (tk != 0 && vk == tk) {
                DWORD last = g_lastStopHotkeyTick.exchange(now);
                if (now - last > kHotkeyDebounceMs) {
                    g_stopHotkeyHits.fetch_add(1);
                    // FORGE-STOP (kullanici talebi): eskiden durdurma SADECE
                    // JS'in ~250-800ms'lik polling dongusune (bkz. /api/status
                    // hotkey_action) bagliydi - WebView2/arayuz herhangi bir
                    // sebeple (agir DOM guncellemesi, arka plan tab kisitlamasi,
                    // vb.) yavaslarsa/donarsa, fiziksel tusa basilsa bile
                    // durdurma GECIKIYORDU. Artik tus algilanir algilanmaz,
                    // arayuzu/JS'i HIC BEKLEMEDEN dogrudan burada (hook
                    // callback'i icinde) durduruyoruz - sadece iki atomic bool
                    // set eder (Stop()), hook'un 300ms zaman asimini
                    // tehlikeye atmayacak kadar hizli. Sayac (yukarida) hala
                    // artiyor - JS bunu /api/status'tan okuyup kendi
                    // dugme/gecmis kayit durumunu (bkz. hotkeyStop() ->
                    // saveHistoryEntry) senkronlamaya devam eder.
                    g_session.Stop();
                }
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// ============================================================================
// ZAMANLAYICI: "Zamanli Baslat" / "Zamanli Durdur" - kullanici bir sure
// (saat+dakika) girer, o sure GECTIKTEN sonra bot otomatik baslar/durur.
// Manuel baslatilmis bir oturumda bile "Zamanli Durdur" AYARLIYSA yine de
// devreye girer (bkz. SchedulerLoop) - kullanicinin acikca istedigi budur.
// NativeSession'daki "detached thread + sleep_for donguru" idiomunun AYNISI
// (bkz. FarmPresenceLoop) - bu kod tabaninda zaten yerlesik desen.
// ============================================================================
struct SchedulerState {
    std::mutex mutex;
    bool startEnabled = false;
    std::chrono::system_clock::time_point startAt;
    bool stopEnabled = false;
    std::chrono::system_clock::time_point stopAt;
    bool exitGameAfterStop = false;

    void SetStart(int hours, int minutes) {
        std::lock_guard<std::mutex> lock(mutex);
        startAt = std::chrono::system_clock::now() + std::chrono::hours(hours) + std::chrono::minutes(minutes);
        startEnabled = true;
    }
    void CancelStart() {
        std::lock_guard<std::mutex> lock(mutex);
        startEnabled = false;
    }
    void SetStop(int hours, int minutes, bool exitAfter) {
        std::lock_guard<std::mutex> lock(mutex);
        stopAt = std::chrono::system_clock::now() + std::chrono::hours(hours) + std::chrono::minutes(minutes);
        stopEnabled = true;
        exitGameAfterStop = exitAfter;
    }
    // "Durdurduktan sonra oyundan cik" kutucugunu, MEVCUT bir zamanlanmis
    // durdurmanin hedef saatini SIFIRLAMADAN (SetStop yeniden cagirmak
    // "simdiden itibaren +sure" hesabini BASTAN baslatirdi) tek basina
    // degistirmek icin - bkz. desktop_ui app.js sched-exit-after handler.
    void SetExitAfterStopOnly(bool exitAfter) {
        std::lock_guard<std::mutex> lock(mutex);
        exitGameAfterStop = exitAfter;
    }
    void CancelStop() {
        std::lock_guard<std::mutex> lock(mutex);
        stopEnabled = false;
    }
    std::chrono::system_clock::time_point GetStartAt() {
        std::lock_guard<std::mutex> lock(mutex);
        return startAt;
    }
    std::chrono::system_clock::time_point GetStopAt() {
        std::lock_guard<std::mutex> lock(mutex);
        return stopAt;
    }
    json ToJson() {
        std::lock_guard<std::mutex> lock(mutex);
        json j;
        j["start_enabled"] = startEnabled;
        j["stop_enabled"] = stopEnabled;
        j["exit_after_stop"] = exitGameAfterStop;
        if (startEnabled) {
            j["start_at_ms"] =
                (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(startAt.time_since_epoch()).count();
        }
        if (stopEnabled) {
            j["stop_at_ms"] =
                (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(stopAt.time_since_epoch()).count();
        }
        return j;
    }
};
static SchedulerState g_scheduler;

static std::string FormatClockTime(std::chrono::system_clock::time_point tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm local{};
    localtime_s(&local, &t);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", local.tm_hour, local.tm_min);
    return buf;
}
static std::string FormatRemaining(std::chrono::system_clock::time_point tp) {
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(tp - std::chrono::system_clock::now()).count();
    if (diff < 0) diff = 0;
    int h = (int)(diff / 3600);
    int m = (int)((diff % 3600) / 60);
    return std::to_string(h) + " saat " + std::to_string(m) + " dakika";
}

// ACIK KAYNAK SURUM: lisans sistemi kaldirildi. Botu baslatan yollar
// (buton, zamanlayici, Discord/Telegram komutu) eskiden burada sunucu-tarafi
// lisans dogrulamasindan geciyordu; artik her zaman izin verilir.
static bool VerifyLicenseNow(std::string* outMessage = nullptr, bool* outTransportError = nullptr) {
    if (outMessage) outMessage->clear();
    if (outTransportError) *outTransportError = false;
    return true;
}

// Sure basi (1sn) ayarli zamanlari kontrol edip zamani gelenleri tetikler.
// Baslatma icin ONCEDEN (uygulamadan ya da /baslat ile) secilmis bir metin
// turu OLMALI - tipki /baslat Discord komutunun zaten varsayilan davranisi
// gibi (bkz. HandleDiscordCommand "/baslat"), sessizce/varsayimla BASKA bir
// tur secilmez.
static void SchedulerLoop() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        bool doStart = false, doStop = false, exitAfter = false;
        {
            std::lock_guard<std::mutex> lock(g_scheduler.mutex);
            auto now = std::chrono::system_clock::now();
            if (g_scheduler.startEnabled && now >= g_scheduler.startAt) {
                doStart = true;
                g_scheduler.startEnabled = false;
            }
            if (g_scheduler.stopEnabled && now >= g_scheduler.stopAt) {
                doStop = true;
                exitAfter = g_scheduler.exitGameAfterStop;
                g_scheduler.stopEnabled = false;
            }
        }
        if (doStart) {
            bool alreadyRunning;
            std::vector<std::string> metins;
            int maxC;
            std::string windowTitle;
            bool autoLootEn, antiStuckEn; double antiStuckSec = 150.0;
            {
                std::lock_guard<std::mutex> lock(g_session.mutex);
                alreadyRunning = !g_session.bots.empty();
                metins = g_session.selectedMetins;
                maxC = g_session.maxClients;
                windowTitle = g_session.windowTitleOverride;
                autoLootEn = g_session.autoLootEnabled;
                antiStuckEn = g_session.antiStuckEnabled;
                antiStuckSec = g_session.antiStuckSeconds;
            }
            if (!alreadyRunning && !metins.empty() && VerifyLicenseNow())
                g_session.Start(metins, maxC, windowTitle, autoLootEn, antiStuckEn, antiStuckSec);
        }
        if (doStop) {
            std::vector<HWND> hwnds;
            {
                std::lock_guard<std::mutex> lock(g_session.mutex);
                for (auto& b : g_session.bots) hwnds.push_back(b->Hwnd());
            }
            g_session.Stop();
            if (exitAfter) {
                // NativeSession::RunLoop kendi dongusunden cikip pencere/
                // yakalama kaynaklarini birakmasi icin kisa bir pay birak.
                std::this_thread::sleep_for(std::chrono::milliseconds(600));
                for (HWND h : hwnds) win_window::KillProcessOwningWindow(h);
            }
        }
    }
}

// ============================================================================
// SUNUCU LISTESI: arayuzdeki sunucu secimi icin yerel liste. Pencere
// basliklari "=" ile TAM ESLESME ister (bkz. win_window.cpp TitleMatchesHint).
// exe'nin yanina servers.json koyarak (bicim:
// {"servers":[{slug,name,window_title,status,logo,note}]}) yeniden
// derlemeden degistirilebilir.
static json LocalServerList() {
    {
        std::ifstream f("servers.json");
        if (f.is_open()) {
            json j = json::parse(f, nullptr, false);
            if (!j.is_discarded() && j.contains("servers") && j["servers"].is_array() && !j["servers"].empty()) {
                json out = j;
                out["success"] = true;
                if (!out.contains("default")) out["default"] = out["servers"][0].value("slug", "");
                return out;
            }
        }
    }
    auto entry = [](const char* slug, const char* name, const char* title) {
        return json{{"slug", slug}, {"name", name}, {"window_title", title},
                    {"status", "active"}, {"logo", nullptr}, {"note", nullptr}};
    };
    json servers = json::array();
    servers.push_back(entry("ornek", "Ornek Sunucu", "Metin2"));
    return json{{"success", true}, {"default", "ornek"}, {"servers", servers}};
}

// ============================================================================
// DISCORD ENTEGRASYONU (OAuth baglama): AWAIT eden asil kod-degisim
// (code -> access token) adimi Discord'un HTTPS API'sine cikis yapmayi
// GEREKTIRIR - bu, ayrica eklenecek bir HTTPS istemci yetenegi (bkz.
// TODO asagida). Su an icin: config.json'da discord_client_id ayarliysa
// sistemin varsayilan tarayicisinda Discord'un yetkilendirme sayfasini
// aciyoruz - kullanici izin verince Discord bizi yerel geri-cagirma
// adresimize (redirect_uri) yonlendirecek, ORADA (henuz eklenmedi) kod
// degisimi yapilip baglanti tamamlanacak.
// Baglanti durumu basit bir yerel dosyada (discord_link.json) saklanir.
static const char* kDiscordLinkFile = "discord_link.json";

// ExchangeDiscordCode basarili baglanmada bir "hosgeldiniz" DM'i atabilsin
// diye ileri bildirim - asil DiscordBot orneği asagida (bkz. "DISCORD BOTU"
// basligi) olusturuluyor, bu sadece ayni degiskene erisim icin.
static std::unique_ptr<DiscordBot> g_discordBot;
static json LoadDiscordLink() {
    std::ifstream f(kDiscordLinkFile);
    if (!f.is_open()) return json{{"connected", false}};
    json j;
    try {
        f >> j;
    } catch (...) {
        return json{{"connected", false}};
    }
    return j;
}
static void SaveDiscordLink(const json& j) {
    std::ofstream f(kDiscordLinkFile);
    f << j.dump(2);
}

// ---- TELEGRAM baglama durumu (Discord OAuth'un Telegram karsiligi) ----
// Discord'da OAuth web akisi var; Telegram'da yok. Bunun yerine: uygulama
// 6 haneli gecici bir KOD uretir (UI'da gosterilir), kullanici Telegram'da
// bota "/baglan KOD" yazar; kod tutarsa O sohbetin chat_id'si buraya kaydedilir
// ve bundan sonra SADECE o sohbet botu kontrol edebilir.
static const char* kTelegramLinkFile = "telegram_link.json";
static std::unique_ptr<TelegramBot> g_telegramBot;
static json LoadTelegramLink() {
    std::ifstream f(kTelegramLinkFile);
    if (!f.is_open()) return json{{"connected", false}};
    json j;
    try {
        f >> j;
    } catch (...) {
        return json{{"connected", false}};
    }
    return j;
}
static void SaveTelegramLink(const json& j) {
    std::ofstream f(kTelegramLinkFile);
    f << j.dump(2);
}

// Gecici baglama kodu (bellekte). UI /api/telegram/code cagirinca uretilir,
// /baglan KOD ile karsilastirilir. 15 dk gecerli. Tek yetkili kurulum oldugu
// icin tek global yeterli.
static std::mutex g_telegramCodeMutex;
static std::string g_telegramLinkCode;
static int64_t g_telegramCodeExpiry = 0;
static std::string GenerateTelegramLinkCode() {
    std::lock_guard<std::mutex> lock(g_telegramCodeMutex);
    unsigned int seed = (unsigned int)std::chrono::steady_clock::now().time_since_epoch().count();
    int code = 100000 + (int)(((uint64_t)seed * 2654435761u) % 900000u);
    g_telegramLinkCode = std::to_string(code);
    g_telegramCodeExpiry = (int64_t)std::time(nullptr) + 15 * 60;
    return g_telegramLinkCode;
}

// Discord'un OAuth2 "authorization code" adimini tamamlar: gelen kodu
// access token'a cevirir, sonra o token ile kullanicinin Discord kullanici
// adini ceker. Basarili olursa discord_link.json'a yazar.
static bool ExchangeDiscordCode(const std::string& code, std::string& outError) {
    Config cfg = Config::Load("config.json");
    std::string clientSecret = cfg.discord_client_secret;
    if (cfg.discord_client_id.empty() || clientSecret.empty()) {
        outError = "not_configured";
        return false;
    }

    httplib::SSLClient tokenClient("discord.com");
    tokenClient.set_connection_timeout(10);
    tokenClient.set_read_timeout(10);
    ConfigureSystemTrustStore(tokenClient);

    std::string redirectUri = "http://127.0.0.1:" + std::to_string(kLocalPort) + "/discord/callback";
    httplib::Params params{
        {"client_id", cfg.discord_client_id},
        {"client_secret", clientSecret},
        {"grant_type", "authorization_code"},
        {"code", code},
        {"redirect_uri", redirectUri},
    };
    auto tokenRes = tokenClient.Post("/api/oauth2/token", params);
    if (!tokenRes) {
        outError = "baglanti kurulamadi: " + httplib::to_string(tokenRes.error());
        return false;
    }
    if (tokenRes->status != 200) {
        outError = "Discord HTTP " + std::to_string(tokenRes->status) + ": " + tokenRes->body;
        return false;
    }
    json tokenJson = json::parse(tokenRes->body, nullptr, false);
    if (tokenJson.is_discarded() || !tokenJson.contains("access_token")) {
        outError = "token_parse_failed";
        return false;
    }
    std::string accessToken = tokenJson.at("access_token").get<std::string>();

    httplib::SSLClient meClient("discord.com");
    meClient.set_connection_timeout(10);
    meClient.set_read_timeout(10);
    ConfigureSystemTrustStore(meClient);
    httplib::Headers headers{{"Authorization", "Bearer " + accessToken}};
    auto meRes = meClient.Get("/api/users/@me", headers);
    if (!meRes || meRes->status != 200) {
        outError = "user_fetch_failed";
        return false;
    }
    json meJson = json::parse(meRes->body, nullptr, false);
    if (meJson.is_discarded() || !meJson.contains("username")) {
        outError = "user_parse_failed";
        return false;
    }

    std::string username = meJson.at("username").get<std::string>();
    std::string userId = meJson.value("id", "");
    SaveDiscordLink(json{
        {"connected", true},
        {"username", username},
        {"user_id", userId},
        {"linked_at", std::time(nullptr)},
    });

    // HOSGELDIN DM'I: baglanti basarili olur olmaz, botumuz kullaniciya
    // ozel mesajdan bir karsilama atar. Discord botu (g_discordBot) henuz
    // hic baslatilmamis olabilir (ör. bu OAuth akisi uygulama acilir
    // acilmaz, bot Start() cagrilmadan once tetiklenirse) - bu durumda
    // sessizce atlanir, DM eksikligi ASLA baglanti akisini basarisiz
    // GOSTERMEMELI (kullanicinin hesabi zaten basariyla baglandi).
    if (g_discordBot && !userId.empty()) {
        try {
            g_discordBot->SendDirectMessage(
                userId,
                "Merhaba, hoşgeldin! 👋 Elbit hesabına başarıyla bağlandı.\n"
                "Artık buradan bana DM atarak botunu uzaktan yönetebilirsin.\n"
                "Kullanabileceğin komutlar: /baslat /durdur /canli /analiz /goruntu");
        } catch (const std::exception& e) {
            std::ofstream errLog("discord_bot.log", std::ios::app);
            if (errLog.is_open()) errLog << "hosgeldin DM'i gonderilirken hata (yakalandi, YOKSAYILDI): " << e.what() << "\n";
        } catch (...) {
        }
    }

    return true;
}

// OAuth geri-cagirmasindan sonra tarayicida gosterilen sonuc sayfasi -
// uygulamanin kendi karanlik/premium temasiyla ayni his (koyu kart, mavi
// accent, ince halka animasyonlu ikon) verecek sekilde, hicbir dis
// dosyaya/CDN'e bagli olmadan (tek dosyalik inline HTML) olusturulur.
static std::string BuildDiscordResultPage(bool ok, const std::string& detail) {
    std::string accent = ok ? "#5b8def" : "#e5636f";
    std::string title = ok ? "Discord baglantisi basarili" : "Baglanti basarisiz";
    std::string message = ok
        ? "Hesabin uygulamaya baglandi. Bu sekmeyi kapatip uygulamaya donebilirsin."
        : "Baglanti tamamlanamadi. Bu sekmeyi kapatip uygulamadan tekrar deneyebilirsin.";
    std::string icon = ok
        ? "<path d=\"M6 12.5l4 4 8-9\" stroke=\"" + accent + "\" stroke-width=\"2.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" fill=\"none\"/>"
        : "<path d=\"M8 8l8 8M16 8l-8 8\" stroke=\"" + accent + "\" stroke-width=\"2.2\" stroke-linecap=\"round\" fill=\"none\"/>";
    std::string detailHtml = detail.empty()
        ? ""
        : "<div class=\"detail\">" + detail + "</div>";

    std::ostringstream html;
    html << "<!doctype html><html><head><meta charset='utf-8'>"
         << "<title>" << title << "</title>"
         << "<style>"
         << "*{box-sizing:border-box;margin:0;padding:0}"
         << "body{min-height:100vh;display:flex;align-items:center;justify-content:center;"
         << "background:radial-gradient(circle at 50% 20%, #1b2436 0%, #0b0e14 60%);"
         << "font-family:'Segoe UI',system-ui,sans-serif;color:#f3f5f9}"
         << ".card{background:#151a24;border:1px solid rgba(255,255,255,0.08);border-radius:20px;"
         << "padding:44px 48px;max-width:420px;text-align:center;"
         << "box-shadow:0 20px 60px -20px rgba(0,0,0,0.6)}"
         << ".ring{width:76px;height:76px;margin:0 auto 22px;border-radius:50%;"
         << "background:" << accent << "1a;border:2px solid " << accent << "55;"
         << "display:flex;align-items:center;justify-content:center;"
         << "animation:pop 420ms cubic-bezier(.2,1.4,.4,1)}"
         << "h1{font-size:19px;font-weight:700;margin-bottom:10px;letter-spacing:-0.01em}"
         << "p{font-size:14px;line-height:1.5;color:#9aa3b5}"
         << ".detail{margin-top:14px;padding:10px 14px;border-radius:10px;background:rgba(255,255,255,0.05);"
         << "font-size:12px;color:#c3392f;word-break:break-word}"
         << "@keyframes pop{from{transform:scale(0.6);opacity:0}to{transform:scale(1);opacity:1}}"
         << "</style></head><body>"
         << "<div class='card'>"
         << "<div class='ring'><svg width='34' height='34' viewBox='0 0 24 24'>" << icon << "</svg></div>"
         << "<h1>" << title << "</h1>"
         << "<p>" << message << "</p>"
         << detailHtml
         << "</div></body></html>";
    return html.str();
}

// ============================================================================
// DISCORD BOTU: kullanici hesabini bagladiktan (yukaridaki OAuth akisi)
// SONRA, ayni kisi Discord'dan botla DM'lesip /analiz /canli /goruntu
// /durdur /baslat komutlariyla botu UZAKTAN (PC basinda olmasa bile)
// kontrol edebilsin diye. SADECE discord_link.json'daki (hesabini
// baglamis) kullanicidan gelen DM'lere cevap verir.
// ============================================================================

// Botun tum embed cevaplarinda kullandigi ortak renkler/altbilgi - uygulamanin
// kendi tema renkleriyle (bkz. desktop_ui/style.css --accent) tutarli.
static constexpr int kEmbedBlue = 0x5b8def;
static constexpr int kEmbedGreen = 0x3fc1ac;
static constexpr int kEmbedRed = 0xe5636f;
static constexpr int kEmbedAmber = 0xe0b04a;

static json MakeEmbed(const std::string& title, int color, const std::string& description = "") {
    json e{{"title", title}, {"color", color}, {"footer", {{"text", "Elbit | Premium"}}}};
    if (!description.empty()) e["description"] = description;
    return e;
}

// "/zkur 3 19" / "/dkur 2 0" gibi komutlardan SAAT ve DAKIKA argumanlarini
// cikarir - komut adi zaten bosluktan ayrildigi icin geri kalani basitce
// akis olarak okunur. Ikisi de sayi degilse (ya da hic verilmemisse) false
// doner - cagiran taraf kullanim mesaji gostermelidir.
static bool ParseHourMinuteArgs(const std::string& content, int& hours, int& minutes) {
    std::istringstream iss(content);
    std::string cmdTok;
    iss >> cmdTok;
    if (!(iss >> hours)) return false;
    if (!(iss >> minutes)) minutes = 0;
    if (hours < 0 || minutes < 0) return false;
    return true;
}

// "/dkur" sonrasi "oyundan cikilsin mi?" sorusuna cevap BEKLENEN kullanici -
// TEK yetkili hesap oldugu icin (bkz. HandleDiscordCommand basindaki
// authorId kontrolu) tek bir global degisken yeterli/guvenli.
static std::mutex g_pendingDkurMutex;
static std::string g_pendingDkurAuthorId;
static int g_pendingDkurHours = 0;
static int g_pendingDkurMinutes = 0;

// ORTAK KOMUT ISLEYICI (Discord + Telegram): komut mantigi TEK yerde. `bot`
// hem DiscordBot hem TelegramBot olabilir - ikisi de ayni metot yuzeyine sahip
// (SendEmbed embed-json'u; Telegram'da HTML mesaja cevrilir). Yetki kontrolu
// ve platforma-ozel /baglan gibi seyler cagiran sarmalayicida yapilir; buraya
// gelen mesaj zaten YETKILI kabul edilir.
template <class Bot>
static void HandleBotCommand(Bot& bot, const std::string& authorId, const std::string& channelId,
                             const std::string& content) {
    // "/dkur SAAT DAKIKA" sonrasi "oyundan cikilsin mi?" sorusuna bekleyen
    // bir cevap VARSA, bu mesaji (komut olsun olmasin) ONCE ona gore
    // degerlendiriyoruz - Evet/Hayir disinda bir sey yazarsa bekleyen soru
    // sessizce iptal olur ve mesaj normal komut olarak islenmeye devam eder.
    {
        bool answered = false, isEvet = false;
        int pendingH = 0, pendingM = 0;
        {
            std::lock_guard<std::mutex> lock(g_pendingDkurMutex);
            if (g_pendingDkurAuthorId == authorId) {
                std::string lower = content;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                                [](unsigned char c) { return (char)std::tolower(c); });
                bool evet = lower == "evet" || lower == "e" || lower == "yes" || lower == "y";
                bool hayir = lower == "hayır" || lower == "hayir" || lower == "h" || lower == "no" || lower == "n";
                if (evet || hayir) {
                    answered = true;
                    isEvet = evet;
                    pendingH = g_pendingDkurHours;
                    pendingM = g_pendingDkurMinutes;
                }
                g_pendingDkurAuthorId.clear();
            }
        }
        if (answered) {
            g_scheduler.SetStop(pendingH, pendingM, isEvet);
            auto at = g_scheduler.GetStopAt();
            bot.SendEmbed(
                channelId, MakeEmbed("⏱️ Zamanlı Durdur Ayarlandı", kEmbedGreen,
                                      "Bot **" + FormatClockTime(at) + "** saatinde (" + FormatRemaining(at) +
                                          " sonra) otomatik duracak.\nDurdurunca oyundan çıkılacak: **" +
                                          std::string(isEvet ? "Evet" : "Hayır") + "**"));
            return;
        }
    }

    if (content.empty() || content[0] != '/') return;

    std::string cmd = content;
    size_t sp = cmd.find(' ');
    if (sp != std::string::npos) cmd = cmd.substr(0, sp);
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) { return (char)std::tolower(c); });

    if (cmd == "/analiz") {
        json st = g_session.ToStatusJson();
        std::string names;
        for (auto& n : st.value("character_names", json::array())) {
            if (!names.empty()) names += ", ";
            names += n.get<std::string>();
        }
        if (names.empty()) names = "-";
        json stats = st.value("stats", json::object());
        double seconds = stats.value("session_seconds", 0.0);
        int completed = stats.value("targets_completed", 0);
        bool active = st.value("active", false);
        bool running = st.value("running", false);
        std::string durum = !active ? "Bağlı değil" : (running ? "🟢 Çalışıyor" : "⏸️ Duraklatıldı");

        json embed = MakeEmbed("📊 Analiz", kEmbedBlue);
        embed["fields"] = json::array({
            {{"name", "Durum"}, {"value", durum}, {"inline", true}},
            {{"name", "Karakter(ler)"}, {"value", "`" + names + "`"}, {"inline", true}},
            {{"name", "Farm Süresi"}, {"value", std::to_string((int)(seconds / 60)) + " dakika"}, {"inline", true}},
            {{"name", "Kesilen Metin"}, {"value", std::to_string(completed)}, {"inline", true}},
        });
        bot.SendEmbed(channelId, embed);
    } else if (cmd == "/canli" || cmd == "/canlı") {
        json st = g_session.ToStatusJson();
        std::string text = st.value("status_text", std::string("Bilinmiyor"));
        bot.SendEmbed(channelId, MakeEmbed("🟢 Anlık Durum", kEmbedGreen, "**" + text + "**"));
    } else if (cmd == "/goruntu" || cmd == "/görüntü") {
        std::vector<std::pair<std::string, cv::Mat>> frames;
        {
            std::lock_guard<std::mutex> lock(g_session.mutex);
            for (auto& b : g_session.bots) {
                auto frame = b->CaptureFrame();
                if (frame) frames.emplace_back(b->CharacterName(), *frame);
            }
        }
        if (frames.empty()) {
            bot.SendEmbed(channelId, MakeEmbed("📷 Görüntü", kEmbedRed, "Şu an bağlı bir client yok."));
        } else {
            // KRITIK DUZELTME: cift client'ta ONCEDEN her client icin AYRI/
            // ARDISIK mesaj gonderiliyordu (bkz. kullanicinin "2 clienti de
            // aynı anda tek karede atsın" istegi) - artik HEPSI TEK bir
            // mesajda (tek istekte) birlikte gonderiliyor.
            std::vector<typename Bot::PngAttachment> attachments;
            attachments.reserve(frames.size());
            int idx = 0;
            for (auto& [name, frame] : frames) {
                std::vector<unsigned char> png;
                cv::imencode(".png", frame, png);
                std::string label = name.empty() ? ("client" + std::to_string(idx + 1)) : name;
                attachments.push_back({label + "_" + std::to_string(idx) + ".png", std::move(png), label});
                ++idx;
            }
            bot.SendMultiplePng(channelId, attachments);
        }
    } else if (cmd == "/durdur") {
        g_session.Stop();
        bot.SendEmbed(channelId, MakeEmbed("⏹️ Bot Durduruluyor", kEmbedRed, "Farm oturumu kapatılıyor."));
    } else if (cmd == "/baslat") {
        bool alreadyRunning;
        std::vector<std::string> metins;
        int maxC;
        std::string windowTitle;
        bool autoLootEn, antiStuckEn; double antiStuckSec = 150.0;
        {
            std::lock_guard<std::mutex> lock(g_session.mutex);
            alreadyRunning = !g_session.bots.empty();
            metins = g_session.selectedMetins;
            maxC = g_session.maxClients;
            windowTitle = g_session.windowTitleOverride;
            autoLootEn = g_session.autoLootEnabled;
            antiStuckEn = g_session.antiStuckEnabled;
            antiStuckSec = g_session.antiStuckSeconds;
        }
        // METIN OTOMATIK: kullanici artik metin SECMIYOR (UI'da tum turler her
        // zaman aktif - bkz. index.html yorumu). Bot /baslat'ta selectedMetins
        // bos ise (kullanici uygulamadan hic baslatmadiysa), UI'nin yaptigi gibi
        // TUM mevcut metin turlerini otomatik doldur - artik "metin secilmemis"
        // hatasi cikmaz.
        if (metins.empty()) {
            for (auto& o : ScanMetinFoldersNative()) metins.push_back(o.name);
        }
        std::string licenseMsg;
        if (alreadyRunning) {
            bot.SendEmbed(channelId, MakeEmbed("⚠️ Zaten Çalışıyor", kEmbedAmber, "Bot zaten çalışıyor."));
        } else if (metins.empty()) {
            bot.SendEmbed(channelId, MakeEmbed("⚠️ Metin Bulunamadı", kEmbedAmber,
                                                            "Hiç metin şablonu bulunamadı (kurulum eksik olabilir)."));
        } else if (!VerifyLicenseNow(&licenseMsg)) {
            bot.SendEmbed(channelId, MakeEmbed("🔒 Lisans Geçersiz", kEmbedRed, licenseMsg));
        } else {
            g_session.Start(metins, maxC, windowTitle, autoLootEn, antiStuckEn, antiStuckSec);
            bot.SendEmbed(channelId, MakeEmbed("▶️ Bot Başlatılıyor", kEmbedGreen, "Farm oturumu başlatılıyor."));
        }
    } else if (cmd == "/zkur") {
        int h = 0, m = 0;
        if (!ParseHourMinuteArgs(content, h, m) || (h == 0 && m == 0)) {
            bot.SendEmbed(channelId,
                                     MakeEmbed("⚠️ Kullanım", kEmbedAmber, "`/zkur SAAT DAKİKA` — örnek: `/zkur 3 19`"));
        } else {
            g_scheduler.SetStart(h, m);
            auto at = g_scheduler.GetStartAt();
            bot.SendEmbed(channelId,
                                     MakeEmbed("⏱️ Zamanlı Başlat Ayarlandı", kEmbedGreen,
                                               "Bot **" + FormatClockTime(at) + "** saatinde (" + FormatRemaining(at) +
                                                   " sonra) otomatik başlayacak."));
        }
    } else if (cmd == "/ziptal") {
        g_scheduler.CancelStart();
        bot.SendEmbed(channelId, MakeEmbed("🗑️ Zamanlı Başlat İptal Edildi", kEmbedAmber, ""));
    } else if (cmd == "/dkur") {
        int h = 0, m = 0;
        if (!ParseHourMinuteArgs(content, h, m) || (h == 0 && m == 0)) {
            bot.SendEmbed(channelId,
                                     MakeEmbed("⚠️ Kullanım", kEmbedAmber, "`/dkur SAAT DAKİKA` — örnek: `/dkur 2 0`"));
        } else {
            {
                std::lock_guard<std::mutex> lock(g_pendingDkurMutex);
                g_pendingDkurAuthorId = authorId;
                g_pendingDkurHours = h;
                g_pendingDkurMinutes = m;
            }
            bot.SendEmbed(
                channelId, MakeEmbed("❓ Bir Soru", kEmbedBlue,
                                      "Durdurma işleminden sonra oyundan çıkılsın mı?\n**Evet** ya da **Hayır** yaz."));
        }
    } else if (cmd == "/diptal") {
        g_scheduler.CancelStop();
        bot.SendEmbed(channelId, MakeEmbed("🗑️ Zamanlı Durdur İptal Edildi", kEmbedAmber, ""));
    } else if (cmd == "/zdetay") {
        json st = g_scheduler.ToJson();
        std::string desc;
        if (st.value("start_enabled", false)) {
            auto at = g_scheduler.GetStartAt();
            desc += "**Zamanlı Başlat:** " + FormatClockTime(at) + " (" + FormatRemaining(at) + " sonra)\n\n";
        } else {
            desc += "**Zamanlı Başlat:** Kurulu değil\n\n";
        }
        if (st.value("stop_enabled", false)) {
            auto at = g_scheduler.GetStopAt();
            desc += "**Zamanlı Durdur:** " + FormatClockTime(at) + " (" + FormatRemaining(at) + " sonra)\n";
            desc += "**Durdurunca oyundan çık:** " +
                    std::string(st.value("exit_after_stop", false) ? "Evet" : "Hayır");
        } else {
            desc += "**Zamanlı Durdur:** Kurulu değil";
        }
        bot.SendEmbed(channelId, MakeEmbed("🗓️ Zamanlayıcı Detayları", kEmbedBlue, desc));
    } else if (cmd == "/yardim" || cmd == "/yardım") {
        json embed =
            MakeEmbed("📖 Komutlar", kEmbedBlue, "Elbit Premium botunu uzaktan yönetin.");
        embed["fields"] = json::array({
            {{"name", "/analiz"}, {"value", "Genel durum özeti (karakter, süre, kesilen metin)."}, {"inline", false}},
            {{"name", "/canlı"}, {"value", "Şu anki anlık durum yazısı."}, {"inline", false}},
            {{"name", "/goruntu"}, {"value", "Bağlı client(lar)ın ekran görüntüsünü (hepsi tek mesajda) gönderir."}, {"inline", false}},
            {{"name", "/baslat"}, {"value", "Botu hemen başlatır (önce uygulamadan bir metin türü seçilmiş olmalı)."}, {"inline", false}},
            {{"name", "/durdur"}, {"value", "Botu hemen durdurur."}, {"inline", false}},
            {{"name", "/zkur SAAT DAKİKA"}, {"value", "Belirtilen süre sonra botu otomatik başlatır."}, {"inline", false}},
            {{"name", "/ziptal"}, {"value", "Zamanlı başlatmayı iptal eder."}, {"inline", false}},
            {{"name", "/dkur SAAT DAKİKA"}, {"value", "Belirtilen süre sonra botu otomatik durdurur (ardından oyundan çıkılsın mı diye sorar)."}, {"inline", false}},
            {{"name", "/diptal"}, {"value", "Zamanlı durdurmayı iptal eder."}, {"inline", false}},
            {{"name", "/zdetay"}, {"value", "Ayarlı zamanlı başlat/durdur bilgilerini gösterir."}, {"inline", false}},
            {{"name", "/yardım"}, {"value", "Bu komut listesini gösterir."}, {"inline", false}},
        });
        bot.SendEmbed(channelId, embed);
    } else {
        bot.SendEmbed(
            channelId,
            MakeEmbed("❓ Bilinmeyen Komut", kEmbedAmber,
                       "Kullanılabilir komutlar için `/yardım` yaz."));
    }
}

// --- DISCORD sarmalayici: yetki kontrolu + ortak komut isleyiciye devret ---
static void HandleDiscordCommand(const std::string& authorId, const std::string& channelId,
                                  const std::string& content) {
    if (!g_discordBot) return;
    json link = LoadDiscordLink();
    if (!link.value("connected", false) || link.value("user_id", "") != authorId) {
        return;  // sadece hesabini baglayan kisi botu kontrol edebilir
    }
    HandleBotCommand(*g_discordBot, authorId, channelId, content);
}

// --- TELEGRAM sarmalayici: /baglan KOD (yetkilendirme) + ortak komut isleyici.
// Telegram'da OAuth yok; kullanici uygulamadaki kodu "/baglan KOD" ile yazarak
// o sohbeti yetkilendirir. Yetkili sohbet disindaki mesajlar yok sayilir
// (sadece /baglan'a yol gosterilir). ---
static void HandleTelegramCommand(const std::string& authorId, const std::string& channelId,
                                   const std::string& content) {
    if (!g_telegramBot) return;

    // Ilk token'i ayikla (buyuk/kucuk harf duyarsiz).
    std::string first = content;
    size_t sp = first.find(' ');
    std::string arg;
    if (sp != std::string::npos) {
        arg = first.substr(sp + 1);
        first = first.substr(0, sp);
    }
    std::transform(first.begin(), first.end(), first.begin(), [](unsigned char c) { return (char)std::tolower(c); });

    if (first == "/baglan" || first == "/bağlan" || first == "/start") {
        // /start (Telegram'in varsayilan ilk komutu) argumansizsa talimat ver.
        std::string code;
        {
            size_t s = arg.find_first_not_of(" \t\r\n");
            size_t e = arg.find_last_not_of(" \t\r\n");
            if (s != std::string::npos) code = arg.substr(s, e - s + 1);
        }
        if (code.empty()) {
            g_telegramBot->SendChannelMessage(
                channelId,
                "Bu botu bağlamak için uygulamadaki (Ayarlar) Telegram kartında gösterilen kodu yaz:\n"
                "<b>/baglan 123456</b>");
            return;
        }
        bool ok = false;
        {
            std::lock_guard<std::mutex> lock(g_telegramCodeMutex);
            ok = !g_telegramLinkCode.empty() && code == g_telegramLinkCode &&
                 (int64_t)std::time(nullptr) <= g_telegramCodeExpiry;
            if (ok) {
                g_telegramLinkCode.clear();  // kod tek kullanimlik
                g_telegramCodeExpiry = 0;
            }
        }
        if (ok) {
            SaveTelegramLink(json{{"connected", true}, {"chat_id", channelId}, {"user_id", authorId}});
            g_telegramBot->SendEmbed(
                channelId, MakeEmbed("✅ Bağlantı Başarılı", kEmbedGreen,
                                     "Artık Elbit'i buradan yönetebilirsin. Aşağıdaki **butonları** kullan "
                                     "ya da dilersen komut yaz (**/yardım**)."));
        } else {
            g_telegramBot->SendChannelMessage(
                channelId, "❌ Kod geçersiz veya süresi dolmuş. Uygulamadan yeni bir kod al ve tekrar dene.");
        }
        return;
    }

    json link = LoadTelegramLink();
    if (!link.value("connected", false) || link.value("chat_id", "") != channelId) {
        g_telegramBot->SendChannelMessage(
            channelId,
            "Bu botu kullanmak için önce uygulamadaki kodla bağlan: <b>/baglan KOD</b>");
        return;
    }
    HandleBotCommand(*g_telegramBot, authorId, channelId, content);
}

static void StartDiscordBotIfConfigured() {
    std::string botToken = Config::Load("config.json").discord_bot_token;
    if (botToken.empty()) return;
    g_discordBot = std::make_unique<DiscordBot>(botToken);
    g_discordBot->SetMessageHandler(HandleDiscordCommand);
    g_discordBot->Start();
}

// Telegram token config.json'da (telegram_bot_token) bossa yapilandirilmamis sayilir.
static bool TelegramConfigured() {
    return !Config::Load("config.json").telegram_bot_token.empty();
}

static void StartTelegramBotIfConfigured() {
    if (!TelegramConfigured()) return;
    g_telegramBot = std::make_unique<TelegramBot>(Config::Load("config.json").telegram_bot_token);
    g_telegramBot->SetMessageHandler(HandleTelegramCommand);
    g_telegramBot->Start();
}

// ============================================================================
// RICH PRESENCE - "Toplam N Kisi Farmliyor!": her calisan .exe (arkadaslar
// dahil, HERKES ayni bot token'ini kullaniyor) paylasilan bir kanalin
// BASLIGINA (topic) kendi durumunu kucuk bir JSON olarak yazar - kanalda
// GORUNUR HICBIR MESAJ OLUSMAZ, sadece kanal basligi (normalde zaten pek
// kimsenin bakmadigi bir alan) bir "paylasilan not defteri" gibi kullanilir.
// Her guncellemede once mevcut baslik okunur, KENDI kaydi guncellenir,
// 90 saniyeden eski (artik kapanmis) kayitlar otomatik silinir, toplam
// hesaplanip botun durum yazisi (~= rich presence) buna gore ayarlanir.
// MERKEZI BIR SUNUCU KURULMAZ - koordinasyon TAMAMEN Discord'un kendi
// altyapisi (kanal basligi) uzerinden yapilir.
// ============================================================================
static int UpdateFarmStatusAndGetTotal(const std::string& channelId, const std::string& botToken,
                                        const std::string& myId, int myCount) {
    httplib::SSLClient cli("discord.com");
    cli.set_connection_timeout(10);
    cli.set_read_timeout(10);
    ConfigureSystemTrustStore(cli);
    httplib::Headers headers{{"Authorization", "Bot " + botToken}};

    auto getRes = cli.Get(("/api/v10/channels/" + channelId).c_str(), headers);
    if (!getRes || getRes->status != 200) return -1;
    json chanJson = json::parse(getRes->body, nullptr, false);
    if (chanJson.is_discarded()) return -1;

    json state = json::parse(chanJson.value("topic", ""), nullptr, false);
    if (state.is_discarded() || !state.is_object()) state = json::object();

    int64_t now = (int64_t)std::time(nullptr);
    for (auto it = state.begin(); it != state.end();) {
        int64_t ts = it.value().value("ts", (int64_t)0);
        if (now - ts > 90) {
            it = state.erase(it);
        } else {
            ++it;
        }
    }
    if (!myId.empty()) state[myId] = {{"count", myCount}, {"ts", now}};

    int total = 0;
    for (auto it = state.begin(); it != state.end(); ++it) total += it.value().value("count", 0);

    json patchBody{{"topic", state.dump()}};
    cli.Patch(("/api/v10/channels/" + channelId).c_str(), headers, patchBody.dump(), "application/json");
    return total;
}

static void FarmPresenceLoop() {
    while (true) {
        Config cfg = Config::Load("config.json");
        if (!cfg.discord_status_channel_id.empty() && g_discordBot) {
            std::string myId = LoadDiscordLink().value("user_id", "");
            // "Kisi" sayaci: farm calisiyor mu diye BAKMAZ - uygulama acik ve
            // Discord hesabi bagliysa (myId dolu) o kisi zaten "+1" sayilir.
            int myCount = myId.empty() ? 0 : 1;
            int total = UpdateFarmStatusAndGetTotal(cfg.discord_status_channel_id, cfg.discord_bot_token, myId, myCount);
            if (total >= 0) {
                std::string presence = total == 0 ? "Kimse Farmlamıyor | .gg/elbit"
                                                    : "Toplam " + std::to_string(total) + " kişi farmlıyor | .gg/elbit";
                g_discordBot->SetPresence(presence);
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
}

static void StartFarmPresenceLoopIfConfigured() {
    Config cfg = Config::Load("config.json");
    if (cfg.discord_status_channel_id.empty() || cfg.discord_bot_token.empty()) return;
    std::thread([]() { RunGuarded("FarmPresenceLoop", FarmPresenceLoop); }).detach();
}

// ============================================================================
// YEREL HTTP SUNUCUSU: WebView2'nin gosterdigi sayfa (desktop_ui/) buradan
// servis edilir, JS tarafi /api/* uc noktalariyla NativeSession'i kontrol
// eder. SADECE 127.0.0.1'de dinler - internete/dis aga hicbir sekilde acik
// degildir.
// ============================================================================
// ============================================================================
// KIRPMA ARACI (Manuel Tarama): cv::selectROI'nin sade/demode gorunumu yerine
// kendi cizdigimiz overlay. Secim disinda kalan alan KARARTILIR, secim
// cercevesi + kose isaretleri vurgulanir, ustte Turkce talimat serides durur
// ve canli olarak secim boyutu yazilir.
// ENTER/SPACE = onayla, ESC = iptal. Bos/cok kucuk secim gecersiz sayilir.
// ============================================================================
namespace {
struct CropState {
    cv::Point start{-1, -1};
    cv::Rect sel{0, 0, 0, 0};
    bool dragging = false;
    bool done = false;

    // --- AKILLI SECIM ---
    // Kullanici SURUKLERSE serbest kutu cizer; TIKLARSA (surukleme yok)
    // tikladigi pikselin rengine benzeyen bitisik bolge otomatik secilir
    // (Photoshop'taki sihirli degnek gibi). Metin/cicek ISIM ETIKETLERI
    // duz renkli yazi oldugu icin bu, elle kutu cizmekten cok daha isabetli.
    cv::Point smartAt{-1, -1};   // en son akilli tiklama noktasi (-1: yok)
    int tolerance = 26;          // Lab renk mesafesi esigi (tekerlekle degisir)
    bool smartFailed = false;    // son akilli deneme bir sey bulamadi mi
    int wheel = 0;               // islenmemis tekerlek adimi (+/-)
};

void CropMouse(int event, int x, int y, int flags, void* userdata) {
    auto* st = static_cast<CropState*>(userdata);
    if (event == cv::EVENT_LBUTTONDOWN) {
        st->start = cv::Point(x, y);
        st->sel = cv::Rect(x, y, 0, 0);
        st->dragging = true;
    } else if (event == cv::EVENT_MOUSEMOVE && st->dragging) {
        st->sel = cv::Rect(std::min(st->start.x, x), std::min(st->start.y, y),
                            std::abs(x - st->start.x), std::abs(y - st->start.y));
    } else if (event == cv::EVENT_LBUTTONUP && st->dragging) {
        st->dragging = false;
        int dx = std::abs(x - st->start.x), dy = std::abs(y - st->start.y);
        if (dx < 4 && dy < 4) {
            // SURUKLEME YOK = akilli secim istegi.
            st->smartAt = cv::Point(x, y);
            st->smartFailed = false;
            st->sel = cv::Rect();
        } else {
            st->smartAt = cv::Point(-1, -1);
            st->smartFailed = false;
            st->sel = cv::Rect(std::min(st->start.x, x), std::min(st->start.y, y), dx, dy);
        }
    } else if (event == cv::EVENT_MOUSEWHEEL) {
        st->wheel += (cv::getMouseWheelDelta(flags) > 0) ? 1 : -1;
    }
}

// AKILLI SECIM: tiklanan pikselin rengine Lab uzayinda "tolerance" kadar
// yakin pikselleri isaretler, harfleri kelimeye baglamak icin yatay yonde
// kapatma (close) uygular, sonra TIKLANAN NOKTAYI iceren bitisik bileseni
// alir. Lab kullaniyoruz cunku RGB mesafesi insan gozunun "ayni renk"
// algisiyla ortusmuyor - isim etiketleri golge/parlaklik degisimi altinda
// RGB'de dagiliyor, Lab'da toplu kaliyor.
cv::Rect SmartSelectAt(const cv::Mat& labF, cv::Point p, int tolerance) {
    if (p.x < 0 || p.y < 0 || p.x >= labF.cols || p.y >= labF.rows) return cv::Rect();

    cv::Vec3f c = labF.at<cv::Vec3f>(p);
    std::vector<cv::Mat> ch;
    cv::split(labF, ch);
    cv::Mat d0 = ch[0] - c[0], d1 = ch[1] - c[1], d2 = ch[2] - c[2];
    cv::Mat dist;
    cv::sqrt(d0.mul(d0) + d1.mul(d1) + d2.mul(d2), dist);

    cv::Mat bin = (dist < (float)tolerance);
    // Yatayda genis, dikeyde dar cekirdek: harfleri tek kelime blobuna baglar
    // ama alt/ust satirlari birbirine YAPISTIRMAZ.
    cv::morphologyEx(bin, bin, cv::MORPH_CLOSE,
                     cv::getStructuringElement(cv::MORPH_RECT, cv::Size(13, 3)));

    cv::Mat labels, stats, centroids;
    int n = cv::connectedComponentsWithStats(bin, labels, stats, centroids, 8, CV_32S);
    if (n <= 1) return cv::Rect();
    int id = labels.at<int>(p);
    if (id <= 0) return cv::Rect();

    cv::Rect r(stats.at<int>(id, cv::CC_STAT_LEFT), stats.at<int>(id, cv::CC_STAT_TOP),
               stats.at<int>(id, cv::CC_STAT_WIDTH), stats.at<int>(id, cv::CC_STAT_HEIGHT));

    // Arka plana tasan secimi ATLA - "gokyuzune tikladim, tum ekran secildi"
    // durumu kullaniciya sessizce yanlis sablon kaydettirmesin.
    double area = (double)r.width * r.height;
    if (area > 0.35 * labF.cols * labF.rows) return cv::Rect();
    if (r.width < 5 || r.height < 5) return cv::Rect();

    // Kenarlarda 2px pay - kirpik kenar eslestirme isabetini dusuruyor.
    cv::Rect padded(r.x - 2, r.y - 2, r.width + 4, r.height + 4);
    return padded & cv::Rect(0, 0, labF.cols, labF.rows);
}

cv::Rect RunCropTool(const cv::Mat& frame, bool english) {
    const std::string win = english ? "Elbit - Select Target" : "Elbit - Hedef Sec";
    cv::namedWindow(win, cv::WINDOW_NORMAL);
    // Pencereyi ekrana sigacak sekilde ac (buyuk kareler tasmasin).
    int screenW = GetSystemMetrics(SM_CXSCREEN), screenH = GetSystemMetrics(SM_CYSCREEN);
    double fit = std::min(1.0, std::min((screenW * 0.9) / frame.cols, (screenH * 0.85) / frame.rows));
    cv::resizeWindow(win, (int)(frame.cols * fit), (int)(frame.rows * fit));
    cv::setWindowProperty(win, cv::WND_PROP_TOPMOST, 1);

    // Lab donusumunu BIR KEZ yap - her tiklamada/tekerlekte yeniden
    // hesaplamak 1920x1080'de gereksiz gecikme uretirdi.
    cv::Mat lab, labF;
    cv::cvtColor(frame, lab, cv::COLOR_BGR2Lab);
    lab.convertTo(labF, CV_32F);

    CropState st;
    cv::setMouseCallback(win, CropMouse, &st);

    const cv::Scalar kAccent(180, 220, 90);   // BGR - canli turkuaz/yesil
    const cv::Scalar kWarn(80, 170, 245);     // BGR - turuncu
    const cv::Scalar kInk(245, 245, 245);

    const std::string hintMain = english
        ? "Click the target's NAME (smart select)  -  or drag a box"
        : "Hedefin ISMINE tikla (akilli secim)  -  ya da kutu surukle";
    const std::string hintKeys = english
        ? "wheel: tolerance    ENTER: confirm    ESC: cancel"
        : "tekerlek: tolerans    ENTER: onayla    ESC: iptal";
    const std::string txtNoRegion = english ? "no region found - adjust tolerance"
                                            : "bolge bulunamadi - toleransi degistir";

    while (true) {
        // Tekerlek girdisi: toleransi degistir ve akilli secimi yeniden calistir.
        if (st.wheel != 0) {
            st.tolerance = std::max(4, std::min(90, st.tolerance + st.wheel * 3));
            st.wheel = 0;
            if (st.smartAt.x >= 0) {
                st.sel = SmartSelectAt(labF, st.smartAt, st.tolerance);
                st.smartFailed = (st.sel.width <= 0);
            }
        }
        // Yeni akilli tiklama bekliyorsa uygula.
        if (st.smartAt.x >= 0 && st.sel.width <= 0 && !st.smartFailed) {
            st.sel = SmartSelectAt(labF, st.smartAt, st.tolerance);
            st.smartFailed = (st.sel.width <= 0);
        }

        cv::Mat view;
        // 1) Disari KARART - secim alani net kalir, goz oraya gider.
        frame.convertTo(view, -1, 0.45, 0);
        if (st.sel.width > 0 && st.sel.height > 0) {
            cv::Rect r = st.sel & cv::Rect(0, 0, frame.cols, frame.rows);
            if (r.width > 0 && r.height > 0) frame(r).copyTo(view(r));
        }

        // 2) Ust bilgi seridi + talimat
        int barH = 58;
        cv::Mat bar = view(cv::Rect(0, 0, view.cols, std::min(barH, view.rows)));
        bar *= 0.35;
        cv::putText(view, hintMain, cv::Point(16, 25), cv::FONT_HERSHEY_SIMPLEX, 0.58, kInk, 1, cv::LINE_AA);
        cv::putText(view, hintKeys, cv::Point(16, 46), cv::FONT_HERSHEY_SIMPLEX, 0.46,
                    cv::Scalar(175, 175, 175), 1, cv::LINE_AA);

        // Tolerans gostergesi (sadece akilli secim kullanildiysa anlamli)
        if (st.smartAt.x >= 0) {
            std::string tl = (english ? "tolerance " : "tolerans ") + std::to_string(st.tolerance);
            int b = 0;
            cv::Size ts = cv::getTextSize(tl, cv::FONT_HERSHEY_SIMPLEX, 0.46, 1, &b);
            cv::putText(view, tl, cv::Point(view.cols - ts.width - 16, 46),
                        cv::FONT_HERSHEY_SIMPLEX, 0.46, kAccent, 1, cv::LINE_AA);
        }

        // 3) Secim cercevesi + kose isaretleri + canli boyut
        if (st.sel.width > 1 && st.sel.height > 1) {
            cv::rectangle(view, st.sel, kAccent, 2, cv::LINE_AA);
            int c = std::min(18, std::min(st.sel.width, st.sel.height) / 3);
            auto corner = [&](cv::Point p, int dx, int dy) {
                cv::line(view, p, p + cv::Point(dx * c, 0), kAccent, 4, cv::LINE_AA);
                cv::line(view, p, p + cv::Point(0, dy * c), kAccent, 4, cv::LINE_AA);
            };
            corner(st.sel.tl(), 1, 1);
            corner(cv::Point(st.sel.br().x, st.sel.tl().y), -1, 1);
            corner(cv::Point(st.sel.tl().x, st.sel.br().y), 1, -1);
            corner(st.sel.br(), -1, -1);

            std::string dim = std::to_string(st.sel.width) + " x " + std::to_string(st.sel.height) + " px";
            int base = 0;
            cv::Size ts = cv::getTextSize(dim, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &base);
            cv::Point lp(st.sel.x, std::max(ts.height + 8, st.sel.y - 8));
            cv::rectangle(view, cv::Rect(lp.x - 5, lp.y - ts.height - 5, ts.width + 10, ts.height + 10),
                          kAccent, cv::FILLED);
            cv::putText(view, dim, lp, cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(20, 20, 20), 1, cv::LINE_AA);
        } else if (st.smartFailed) {
            cv::putText(view, txtNoRegion, cv::Point(16, barH + 26), cv::FONT_HERSHEY_SIMPLEX, 0.52,
                        kWarn, 1, cv::LINE_AA);
        }

        cv::imshow(win, view);
        int key = cv::waitKey(16) & 0xFF;
        if (key == 27) { st.sel = cv::Rect(); break; }            // ESC
        if (key == 13 || key == 10 || key == 32) break;            // ENTER / SPACE
        // Pencere capraz ile kapatildiysa iptal say.
        if (cv::getWindowProperty(win, cv::WND_PROP_VISIBLE) < 1) { st.sel = cv::Rect(); break; }
    }
    cv::destroyWindow(win);
    cv::waitKey(1);  // kapanmanin islenmesi icin

    cv::Rect r = st.sel & cv::Rect(0, 0, frame.cols, frame.rows);
    if (r.width < 6 || r.height < 6) return cv::Rect();  // cok kucuk = gecersiz
    return r;
}
}  // namespace

static void RunLocalServer() {
    httplib::Server svr;
    // KRITIK: WebView2 (Chromium tabanli) statik dosyalari (index.html/
    // style.css/app.js) VARSAYILAN OLARAK ONBELLEKLEYEBILIR - biz gelistirme
    // sirasinda bu dosyalari SIK SIK guncelliyoruz, onbellege alinirsa
    // kullanici ESKI/bozuk bir surumu gorup "hicbir seye tiklanamiyor" gibi
    // hayalet hatalarla karsilasabilir (WebView2'nin KENDI profil klasoru -
    // wv2data - HTTP onbellegini de tutar). Tum yanitlara "asla onbellekleme"
    // basligi ekleyerek bu sinif hatayi TAMAMEN ortadan kaldiriyoruz.
    svr.set_default_headers({{"Cache-Control", "no-store, no-cache, must-revalidate"}, {"Pragma", "no-cache"}});
    svr.set_mount_point("/", "desktop_ui");
    // KRITIK: /api/login gibi handler'lar lisans sunucusuna IKI ARDISIK HTTPS
    // cagrisi (init + license) yapar, HER BIRI 10 saniyeye kadar surebilir
    // (lisans istemcisinin timeout'lari) - ILK acilista (sogumus
    // DNS/TLS onbellegi) bu toplamda cpp-httplib'in VARSAYILAN sunucu
    // okuma/yazma zaman asimini (birkac saniye) asabilir, bu da baglanti
    // handler BITMEDEN kapanip istemcide "Sunucuya bağlanılamadı" gibi
    // YANILTICI bir hataya yol acar - OYSA sunucu tarafinda islem GERCEKTE
    // BASARIYLA tamamlanmis olabilir (StoreLicenseState zaten cagrilmis
    // olur, bir sonraki acilista hatirlanan anahtarla OTOMATIK giris
    // basarili olur - kullanicinin bildirdigi TAM DESEN bu). Sunucu
    // zaman asimini cömertce artiriyoruz ki YAVAS ama BASARILI bir lisans
    // yaniti asla yarida kesilmesin.
    // KRITIK GUCLENDIRME: Lisans init+check her biri KENDI icinde
    // 2 deneme x 15sn (lisans istemcisi) yapabiliyor - kotu senaryoda
    // (soguk TLS/DNS onbellegi, ozellikle ILK acilista) TOPLAMDA 60 saniyeye
    // kadar surebilir. Onceki 30sn deger BU KOTU SENARYOYU KAPSAMIYORDU -
    // kullanicinin bildirdigi "ilk giriste sunucuya baglanilamadi, ac-kapa
    // yapinca duzeliyor" deseninin (sunucu ASLINDA basariyla tamamliyor,
    // sadece istemci BEKLEMEDEN vazgeciyor) sebebi TAM OLARAK buydu.
    svr.set_read_timeout(75, 0);
    svr.set_write_timeout(75, 0);
    // KOK NEDEN DUZELTMESI ("Sunucuya baglanilamadi" - diag.log'taki
    // "LGN ... tt=1.28 ok=1" satirlari KANITLADI ki sunucu istegi 1.2 saniyede
    // BASARIYLA tamamliyor, ama yanit istemciye ULASMIYOR):
    //
    // keep_alive_timeout ONCEDEN 75 saniyeydi. Bu deger, "yavas lisans
    // yanitina zaman tanimak" icin artirilmisti - AMA BU TAMAMEN YANLIS BIR
    // ANLAMAYDI: keep_alive_timeout, bir handler'in NE KADAR SUREBILECEGINI
    // HIC ETKILEMEZ (handler istedigi kadar surebilir). Bu deger SADECE
    // "istek bittikten sonra bagalanti, YENI bir istek gelir umuduyla NE KADAR
    // ACIK/BOSTA tutulacak" demektir - ve cpp-httplib'de BOSTA BEKLEYEN her
    // baglanti, havuzdan BIR IS PARCACIGINI (thread) TUTAR.
    //
    // WebView2 (Chromium) sayfayi acarken hem paralel baglantilar acar hem de
    // "preconnect" (onceden baglan, henuz istek gonderme) soketleri acar.
    // 75 saniyelik deger yuzunden bu BOS soketlerin HER BIRI birer is
    // parcacigini 75 SANIYE BOYUNCA rehin aliyordu; havuz dolunca /api/login
    // istegi hicbir zaman islenemiyor, tarayici baglantiyi dusuruyor ve
    // istemcide "Sunucuya baglanilamadi" gorunuyordu - OYSA (loglarda
    // gorulen) basarili istekler havuzda hala yer varken islenmis olanlardi.
    // 5 saniye (kutuphanenin kendi varsayilani) fazlasiyla yeterli.
    svr.set_keep_alive_timeout(5);

    // IKINCI SAVUNMA HATTI: is parcacigi havuzunu ACIKCA buyutuyoruz
    // (varsayilan: en az 8, en fazla 32). Boylece yukaridaki gibi bir
    // "bosta bekleyen baglanti" durumu bir sekilde tekrar olussa BILE
    // /api/login gibi kritik istekler ASLA sirada bekleyip zaman asimina
    // ugramaz. Yerel (127.0.0.1) bir sunucu icin 64 is parcacigi maliyeti
    // ihmal edilebilir.
    svr.new_task_queue = [] { return new httplib::ThreadPool(64, 128); };

    // GUVENLIK AGI: bir /api/* handler'i BEKLENMEDIK bir istisna firlatirsa
    // (ör. dis bir servisin - Discord - yanitindaki bir alanin
    // beklenmedik tipte olmasi), httplib'in VARSAYILAN 500 sayfasi DUZ
    // METIN/HTML doner - app.js'teki HER fetch().then(r => r.json())
    // cagrisi bunu ayristiramaz, catch bloguna duser ve YANILTICI "sunucuya
    // baglanilamadi" gibi bir mesaj gosterir (GERCEKTE sunucu calisiyordur,
    // sadece bir handler'da yakalanmamis bir istisna vardir). Bu global
    // handler, HANGI /api/* uc noktasinda olursa olsun, istemcinin HER ZAMAN
    // ayristirilabilir bir JSON hata yaniti almasini garanti eder.
    svr.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
        std::string what = "bilinmeyen hata";
        try {
            if (ep) std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            what = e.what();
        } catch (...) {
        }
        res.status = 500;
        res.set_content(json{{"ok", false}, {"message", "Sunucu tarafında beklenmeyen bir hata oluştu."}}.dump(),
                         "application/json");
        std::ofstream f("desktop_app_errors.log", std::ios::app);
        if (f.is_open()) f << what << "\n";
    });

    // GUNCELLEME KONTROLU: acik kaynak surumde uzak guncelleme sunucusu yok -
    // arayuz her zaman "guncel" yaniti alir.
    svr.Get("/api/version-check", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(json{{"ok", true}, {"updateRequired", false}, {"checked", true},
                             {"currentVersion", kAppVersion}}.dump(),
                        "application/json");
    });

    svr.Post("/api/update/open-discord", [](const httplib::Request&, httplib::Response& res) {
        Config cfg = Config::Load("config.json");
        if (!cfg.discord_invite_url.empty()) {
            ShellExecuteA(nullptr, "open", cfg.discord_invite_url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        res.set_content(json{{"ok", true}}.dump(), "application/json");
    });

    svr.Get("/api/discord/status", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(LoadDiscordLink().dump(), "application/json");
    });

    svr.Post("/api/discord/connect", [](const httplib::Request&, httplib::Response& res) {
        Config cfg = Config::Load("config.json");
        if (cfg.discord_client_id.empty()) {
            res.set_content(json{{"ok", false}, {"error", "not_configured"}}.dump(), "application/json");
            return;
        }
        std::string url = "https://discord.com/oauth2/authorize?client_id=" + cfg.discord_client_id +
                           "&response_type=code&redirect_uri=http%3A%2F%2F127.0.0.1%3A" + std::to_string(kLocalPort) +
                           "%2Fdiscord%2Fcallback&scope=identify";
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        res.set_content(json{{"ok", true}}.dump(), "application/json");
    });

    svr.Post("/api/discord/disconnect", [](const httplib::Request&, httplib::Response& res) {
        fs::remove(kDiscordLinkFile);
        res.set_content(json{{"ok", true}}.dump(), "application/json");
    });

    // ---- TELEGRAM endpoint'leri (Discord'un karsiligi) ----
    // Durum: bagli mi, yapilandirilmis mi (token var mi), botun @kullanici_adi.
    svr.Get("/api/telegram/status", [](const httplib::Request&, httplib::Response& res) {
        json j = LoadTelegramLink();
        j["configured"] = TelegramConfigured();
        j["bot_username"] = g_telegramBot ? g_telegramBot->GetBotUsername() : "";
        res.set_content(j.dump(), "application/json");
    });
    // Yeni 6 haneli baglama kodu uretir (UI'da gosterilir; /baglan KOD ile eslesir).
    svr.Post("/api/telegram/code", [](const httplib::Request&, httplib::Response& res) {
        if (!TelegramConfigured()) {
            res.set_content(json{{"ok", false}, {"error", "not_configured"}}.dump(), "application/json");
            return;
        }
        std::string code = GenerateTelegramLinkCode();
        res.set_content(json{{"ok", true}, {"code", code},
                             {"bot_username", g_telegramBot ? g_telegramBot->GetBotUsername() : ""}}
                            .dump(),
                        "application/json");
    });
    svr.Post("/api/telegram/disconnect", [](const httplib::Request&, httplib::Response& res) {
        fs::remove(kTelegramLinkFile);
        res.set_content(json{{"ok", true}}.dump(), "application/json");
    });

    svr.Get("/discord/callback", [](const httplib::Request& req, httplib::Response& res) {
        bool ok = false;
        std::string detail;
        if (!req.has_param("code")) {
            detail = "Discord bir yetkilendirme kodu dondurmedi.";
        } else {
            std::string error;
            ok = ExchangeDiscordCode(req.get_param_value("code"), error);
            detail = ok ? "" : error;
        }
        res.set_content(BuildDiscordResultPage(ok, detail), "text/html");
    });

    // GIRIS: acik kaynak surumde lisans dogrulamasi yok - arayuzun giris
    // akisi bozulmasin diye her istek basarili ve sinirsiz doner.
    svr.Post("/api/login", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(json{{"ok", true}, {"message", ""}, {"unlimited", true}}.dump(), "application/json");
    });

    svr.Get("/api/metins", [](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        for (auto& o : ScanMetinFoldersNative()) arr.push_back({{"name", o.name}});
        res.set_content(arr.dump(), "application/json");
    });

    // SUNUCU LISTESI PROXY: lisans sunucusundaki servers.php'yi (public) cekip
    // frontend'e verir. Frontend dogrudan disari (farkli origin, http) istek
    // atmasin diye C++ tarafi proxy'ler. Ulasilamazsa {success:false} doner -
    // frontend o zaman gomulu varsayilan listeye duser.
    // ---------------------- MANUEL TARAMA ----------------------
    // COKLU GORSEL: manual_scan/ klasorundeki TUM png'ler sablondur.
    // Dosyalar diskte durdugu icin uygulama kapanip acilinca secimler KORUNUR.

    // Guvenlik: id sadece dosya ADI olabilir - yol gecisi (../) engellenir.
    auto manualSafeName = [](const std::string& id) -> std::string {
        std::string out;
        for (char c : id) {
            if (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') out += c;
        }
        if (out.find("..") != std::string::npos) return "";
        return out;
    };

    // Kayitli gorsellerin listesi (id + boyut) - UI galerisi icin.
    svr.Get("/api/manual/list", [](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        std::error_code ec;
        std::vector<std::string> files;
        for (const auto& e : fs::directory_iterator("manual_scan", ec)) {
            if (!e.is_regular_file()) continue;
            auto ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".png") files.push_back(e.path().filename().string());
        }
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            cv::Mat t = cv::imread("manual_scan/" + f, cv::IMREAD_COLOR);
            if (t.empty()) continue;
            arr.push_back({{"id", f}, {"w", t.cols}, {"h", t.rows}});
        }
        res.set_content(json{{"ok", true}, {"items", arr}}.dump(), "application/json");
    });

    // Tek bir gorseli servis et (UI onizleme).
    svr.Get("/api/manual/image", [manualSafeName](const httplib::Request& req, httplib::Response& res) {
        std::string id = manualSafeName(req.get_param_value("id"));
        if (id.empty()) { res.status = 400; return; }
        std::ifstream f("manual_scan/" + id, std::ios::binary);
        if (!f) { res.status = 404; return; }
        std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        res.set_content(data, "image/png");
    });

    // Gorsel sil.
    svr.Post("/api/manual/delete", [manualSafeName](const httplib::Request& req, httplib::Response& res) {
        json body = json::parse(req.body, nullptr, false);
        std::string id = body.is_discarded() ? "" : manualSafeName(body.value("id", ""));
        if (id.empty()) {
            res.set_content(json{{"ok", false}, {"error", "bad_id"}}.dump(), "application/json");
            return;
        }
        std::error_code ec;
        fs::remove("manual_scan/" + id, ec);
        res.set_content(json{{"ok", true}}.dump(), "application/json");
    });

    // Kirpma araci: oyun penceresinden bir kare yakalar, kullaniciya kutu
    // cizdirir ve secilen bolgeyi YENI bir dosya olarak EKLER (var olanlari
    // silmez - boylece birden fazla hedef eklenebilir).
    svr.Post("/api/manual/capture", [](const httplib::Request& req, httplib::Response& res) {
        Config cfg = Config::Load("config.json");
        // PENCERE BASLIGI SIRASI: istegin govdesi -> calisan oturum -> config.
        // (Kullanici sunucuyu ARAYUZDEN secer; config.json'daki baslik sadece
        // bir varsayilandir ve farkli olabilir.)
        std::string hint;
        bool english = false;
        {
            json body = json::parse(req.body, nullptr, false);
            if (!body.is_discarded() && body.contains("window_title_hint"))
                hint = body.at("window_title_hint").get<std::string>();
            if (!body.is_discarded() && body.contains("lang"))
                english = (body.at("lang").get<std::string>() == "en");
        }
        if (hint.empty()) {
            std::lock_guard<std::mutex> lock(g_session.mutex);
            hint = g_session.windowTitleOverride;
        }
        if (hint.empty()) hint = cfg.window_title_hint;

        HWND hwnd = win_window::FindGameWindow(hint);
        if (!hwnd && !cfg.window_title_fallback_hint.empty())
            hwnd = win_window::FindGameWindow(cfg.window_title_fallback_hint);
        if (!hwnd) {
            res.set_content(json{{"ok", false}, {"error", "no_window"}, {"hint", hint}}.dump(), "application/json");
            return;
        }
        win_window::BringToForeground(hwnd);
        std::string err;
        cv::Mat frame;
        try {
            WindowCapture capture(hwnd, /*minIntervalMs=*/0);
            for (int i = 0; i < 40; ++i) {
                auto f = capture.GetFrame();
                if (f && !f->empty()) { frame = *f; break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        } catch (const std::exception& e) {
            err = e.what();
        }
        if (frame.empty()) {
            res.set_content(json{{"ok", false}, {"error", "no_frame"}, {"detail", err}}.dump(), "application/json");
            return;
        }

        cv::Rect roi = RunCropTool(frame, english);
        if (roi.width <= 0 || roi.height <= 0) {
            res.set_content(json{{"ok", false}, {"error", "cancelled"}}.dump(), "application/json");
            return;
        }
        std::error_code ec;
        fs::create_directories("manual_scan", ec);
        // Benzersiz ad - var olan gorselleri EZMEZ.
        std::string name = "hedef_" + std::to_string((long long)std::time(nullptr)) + ".png";
        if (!cv::imwrite("manual_scan/" + name, frame(roi))) {
            res.set_content(json{{"ok", false}, {"error", "save_failed"}}.dump(), "application/json");
            return;
        }
        res.set_content(json{{"ok", true}, {"id", name}, {"w", roi.width}, {"h", roi.height}}.dump(),
                        "application/json");
    });

    // SUNUCU LISTESI: yerel liste (bkz. LocalServerList).
    svr.Get("/api/servers", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(LocalServerList().dump(), "application/json");
    });

    // ---------------------- KALIBRASYON ----------------------
    // Oyun ekranini ~40 sn TIKLAMADAN izler, esikleri O CIHAZA gore olcup
    // config.json'a yazar (neyin olculebilip neyin OLCULEMEDIGI acikca
    // calibration.h basinda yazili).
    svr.Post("/api/calibrate/start", [](const httplib::Request& req, httplib::Response& res) {
        json body = json::parse(req.body, nullptr, false);
        std::string hint;
        if (!body.is_discarded() && body.contains("window_title_hint")) {
            hint = body.at("window_title_hint").get<std::string>();
        }
        // Arayuz dili (TR/EN) - kalibrasyon ciktisi bu dile gore uretilir.
        std::string lang = "tr";
        if (!body.is_discarded() && body.contains("lang")) lang = body.at("lang").get<std::string>();
        // Bot calisirken kalibrasyon YAPILMAZ - ikisi de ayni pencereyi ele
        // alir, birbirlerinin olcumunu/girdisini bozarlar.
        {
            std::lock_guard<std::mutex> lock(g_session.mutex);
            if (!g_session.bots.empty()) {
                std::string msg = (lang == "en")
                    ? "Stop the bot first - calibration can't run while the bot is running."
                    : "Önce botu durdurun - kalibrasyon bot çalışırken yapılamaz.";
                res.set_content(json{{"ok", false}, {"message", msg}}.dump(), "application/json");
                return;
            }
        }
        StartCalibration(hint, lang);
        res.set_content(json{{"ok", true}}.dump(), "application/json");
    });

    svr.Get("/api/calibrate/status", [](const httplib::Request&, httplib::Response& res) {
        CalibrationStatus st = GetCalibrationStatus();
        json j{{"running", st.running},
                {"done", st.done},
                {"progress", st.progressPct},
                {"phase", st.phase},
                {"error", st.error}};
        j["lines"] = st.lines;
        res.set_content(j.dump(), "application/json");
    });

    svr.Get("/api/status", [](const httplib::Request&, httplib::Response& res) {
        json st = g_session.ToStatusJson();
        // KISAYOL: bekleyen bir global hotkey eylemi varsa arayuze bildir ve
        // sayaclari SIFIRLA (exchange) - arayuz bunu okuyup baslat/durdur
        // yapar. Ayni ~250ms pencerede HEM baslat HEM durdur basildiysa
        // (nadir ama SESSIZCE kaybolmemesi gereken durum) DURDUR ONCELIKLI -
        // botun yanlislikla calisir kalmasindansa yanlislikla durmasi daha
        // guvenli taraf.
        int startHits = g_startHotkeyHits.exchange(0);
        int stopHits = g_stopHotkeyHits.exchange(0);
        if (stopHits > 0) st["hotkey_action"] = "stop";
        else if (startHits > 0) st["hotkey_action"] = "start";

        st["license_unlimited"] = true;  // acik kaynak surum: lisans yok
        res.set_content(st.dump(), "application/json");
    });

    // KISAYOL ATAMA: arayuz, kullanicinin atadigi Baslat/Durdur tus VK
    // kodlarini buraya gonderir (acilista ve her degisiklikte). 0 = atanmamis.
    svr.Post("/api/hotkeys", [](const httplib::Request& req, httplib::Response& res) {
        json body = json::parse(req.body, nullptr, false);
        if (!body.is_discarded()) {
            g_startHotkeyVk = body.value("start_vk", 0);
            g_stopHotkeyVk = body.value("stop_vk", 0);
            if (g_startHotkeyVk.load() != 0 || g_stopHotkeyVk.load() != 0) g_wantKbHook = true;
        }
        res.set_content(json{{"ok", true}}.dump(), "application/json");
    });

    // PARTI MODU (kullanici talebi): aciksa pencere basligi her saniye
    // degisir (bkz. WndProc WM_TIMER + MakePartyTitle). Arayuz bu bayragi
    // acilista ve her degisimde gonderir; tema tarafi (RGB'ye gecis) tamamen
    // JS'te. Kapatinca baslik varsayilana geri doner.
    svr.Post("/api/party", [](const httplib::Request& req, httplib::Response& res) {
        json body = json::parse(req.body, nullptr, false);
        bool enabled = !body.is_discarded() && body.value("enabled", false);
        g_partyMode = enabled;
        // Aninda bir baslik guncellemesi ver - kullanici 1sn beklemesin.
        if (g_mainHwnd) {
            SetWindowTextW(g_mainHwnd, enabled ? MakePartyTitle().c_str() : L"Elbit | Premium");
        }
        res.set_content(json{{"ok", true}}.dump(), "application/json");
    });

    svr.Post("/api/start", [](const httplib::Request& req, httplib::Response& res) {
        json body = json::parse(req.body, nullptr, false);
        std::vector<std::string> metins;
        if (!body.is_discarded() && body.contains("metins")) metins = body.at("metins").get<std::vector<std::string>>();
        int maxClients =
            (!body.is_discarded() && body.contains("max_clients")) ? body.at("max_clients").get<int>() : 9999;
        std::string windowTitle;
        if (!body.is_discarded() && body.contains("window_title_hint"))
            windowTitle = body.at("window_title_hint").get<std::string>();
        bool autoLootEnabled =
            !body.is_discarded() && body.contains("auto_loot_enabled") && body.at("auto_loot_enabled").get<bool>();
        // TIKLAMA SEKLI: govdede YOKSA varsayilan true (Shift + Sag Tik) kalir -
        // eski istemciler/zamanlayici cagrilari davranis degistirmez.
        bool clickUseShift = true;
        if (!body.is_discarded() && body.contains("click_use_shift"))
            clickUseShift = body.at("click_use_shift").get<bool>();
        bool inputUsePostmessage = false;
        if (!body.is_discarded() && body.contains("input_use_postmessage"))
            inputUsePostmessage = body.at("input_use_postmessage").get<bool>();
        bool manualScanEnabled = false;
        double manualScanThreshold = 0.75;
        if (!body.is_discarded()) {
            if (body.contains("manual_scan_enabled")) manualScanEnabled = body.at("manual_scan_enabled").get<bool>();
            if (body.contains("manual_scan_threshold")) manualScanThreshold = body.at("manual_scan_threshold").get<double>();
        }
        bool antiStuckEnabled =
            !body.is_discarded() && body.contains("anti_stuck_enabled") && body.at("anti_stuck_enabled").get<bool>();
        // Anti-Takilma suresi DAKIKA olarak gelir (Fonksiyonlar'daki giris
        // alani) - saniyeye cevrilir. Makul sinirlar icine kirpilir (0.5 - 30
        // dakika) ki kazara sifir/asiri deger sistemi bozmasin.
        double antiStuckSeconds = 150.0;
        if (!body.is_discarded() && body.contains("anti_stuck_minutes")) {
            double mins = body.at("anti_stuck_minutes").get<double>();
            mins = std::clamp(mins, 0.5, 30.0);
            antiStuckSeconds = mins * 60.0;
        }

        // INSANCILLASTIR + OTO BASMA: Start() imzasina eklemeden, session
        // uyelerine DOGRUDAN yaziyoruz (RunLoop bunlari okur; zamanlayici/
        // discord ile yeniden baslatmada son degerler korunur - Start bunlara
        // dokunmuyor).
        g_session.humanizeEnabled =
            !body.is_discarded() && body.contains("humanize_enabled") && body.at("humanize_enabled").get<bool>();
        g_session.atProEnabled =
            !body.is_discarded() && body.contains("at_pro_enabled") && body.at("at_pro_enabled").get<bool>();
        g_session.queueOthersEnabled =
            !body.is_discarded() && body.contains("queue_others_enabled") &&
            body.at("queue_others_enabled").get<bool>();
        g_session.nesneMarketDismissEnabled =
            !body.is_discarded() && body.contains("nesne_market_dismiss_enabled") &&
            body.at("nesne_market_dismiss_enabled").get<bool>();
        g_session.autoKeyEnabled =
            !body.is_discarded() && body.contains("auto_key_enabled") && body.at("auto_key_enabled").get<bool>();
        // TIKLAMA SEKLI: Start() ICINDE config'e aktarilacak (bkz. clickUseShift
        // uyesi) - govdede yoksa varsayilan Shift + Sag Tik korunur.
        g_session.clickUseShift = clickUseShift;
        g_session.inputUsePostmessage = inputUsePostmessage;
        g_session.manualScanEnabled = manualScanEnabled;
        g_session.manualScanThreshold = manualScanThreshold;
        g_session.autoKeyOnlyMode =
            !body.is_discarded() && body.contains("auto_key_only") && body.at("auto_key_only").get<bool>();
        g_session.autoKeySlots.clear();
        if (!body.is_discarded() && body.contains("auto_key_slots") && body.at("auto_key_slots").is_array()) {
            for (const auto& s : body.at("auto_key_slots")) {
                AutoKeySlot slot;
                slot.key = s.value("key", "");
                slot.intervalSec = s.value("interval_sec", 0.0);
                slot.hold = s.value("hold", false);
                if (slot.key.empty()) continue;
                if (slot.hold) {
                    // Basili Tut: aralik ONEMSIZ, sadece tus dolu olsun yeter.
                    g_session.autoKeySlots.push_back(slot);
                } else if (slot.intervalSec >= 1.0) {
                    // Normal (araligli) slot - 1sn-2 saat arasina kirpilir.
                    slot.intervalSec = std::clamp(slot.intervalSec, 1.0, 7200.0);
                    g_session.autoKeySlots.push_back(slot);
                }
            }
        }

        std::string licenseMsg;
        // JS'teki giris kontrolunu ATLAYIP dogrudan bu uc noktaya istek
        // atilsa BILE (ör. devtools/curl) bot BURADA taze bir lisans
        // dogrulamasi olmadan BASLAMAZ - bkz. VerifyLicenseNow.
        if (!VerifyLicenseNow(&licenseMsg)) {
            res.set_content(json{{"ok", false}, {"error", "license"}, {"message", licenseMsg}}.dump(), "application/json");
            return;
        }
        // OTO BASMA FARMI: metin listesi BOS gelir (hedef arama/tiklama
        // istenmiyor) - normalde bos liste Start()'i hic tetiklemez, bu mod
        // icin ISTISNA taniniyor.
        // MANUEL TARAMA: metin modelleri hic kullanilmadigi icin metin listesi
        // BOS olsa da baslatilmali (auto_key_only ile AYNI istisna).
        if (!metins.empty() || g_session.autoKeyOnlyMode || g_session.manualScanEnabled)
            g_session.Start(metins, maxClients, windowTitle, autoLootEnabled, antiStuckEnabled, antiStuckSeconds);

        // OYUN PENCERESI VAR MI? Baslatmayi ENGELLEMIYORUZ (oyun birazdan
        // acilabilir, zamanlayici da bu ucu kullaniyor) - sadece arayuze
        // bildiriyoruz ki kullanici "basladi ama hicbir sey olmuyor" diye
        // beklemesin. Aranan basligi da donduruyoruz: "bulunamadi"
        // sikayetlerinin cogu yanlis sunucu seciminden cikiyor.
        Config cfgWin = Config::Load("config.json");
        std::string winHint = windowTitle.empty() ? cfgWin.window_title_hint : windowTitle;
        HWND gameWnd = win_window::FindGameWindow(winHint);
        if (!gameWnd && !cfgWin.window_title_fallback_hint.empty())
            gameWnd = win_window::FindGameWindow(cfgWin.window_title_fallback_hint);

        res.set_content(json{{"ok", true},
                             {"window_found", gameWnd != nullptr},
                             {"hint", winHint}}
                            .dump(),
                        "application/json");
    });

    svr.Post("/api/pause", [](const httplib::Request&, httplib::Response& res) {
        g_session.Pause();
        res.set_content(json{{"ok", true}}.dump(), "application/json");
    });
    svr.Post("/api/resume", [](const httplib::Request&, httplib::Response& res) {
        std::string licenseMsg;
        if (!VerifyLicenseNow(&licenseMsg)) {
            res.set_content(json{{"ok", false}, {"error", "license"}, {"message", licenseMsg}}.dump(), "application/json");
            return;
        }
        g_session.Resume();
        res.set_content(json{{"ok", true}}.dump(), "application/json");
    });
    svr.Post("/api/stop", [](const httplib::Request&, httplib::Response& res) {
        g_session.Stop();
        res.set_content(json{{"ok", true}}.dump(), "application/json");
    });

    svr.Get("/api/scheduler/status", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(g_scheduler.ToJson().dump(), "application/json");
    });
    svr.Post("/api/scheduler/exit-after-stop", [](const httplib::Request& req, httplib::Response& res) {
        json body = json::parse(req.body, nullptr, false);
        bool value = !body.is_discarded() && body.value("value", false);
        g_scheduler.SetExitAfterStopOnly(value);
        res.set_content(g_scheduler.ToJson().dump(), "application/json");
    });
    svr.Post("/api/scheduler/start", [](const httplib::Request& req, httplib::Response& res) {
        json body = json::parse(req.body, nullptr, false);
        bool enabled = !body.is_discarded() && body.value("enabled", false);
        if (enabled) {
            int h = body.value("hours", 0);
            int m = body.value("minutes", 0);
            g_scheduler.SetStart(h, m);
        } else {
            g_scheduler.CancelStart();
        }
        res.set_content(g_scheduler.ToJson().dump(), "application/json");
    });
    svr.Post("/api/scheduler/stop", [](const httplib::Request& req, httplib::Response& res) {
        json body = json::parse(req.body, nullptr, false);
        bool enabled = !body.is_discarded() && body.value("enabled", false);
        if (enabled) {
            int h = body.value("hours", 0);
            int m = body.value("minutes", 0);
            bool exitAfter = body.value("exit_after_stop", false);
            g_scheduler.SetStop(h, m, exitAfter);
        } else {
            g_scheduler.CancelStop();
        }
        res.set_content(g_scheduler.ToJson().dump(), "application/json");
    });

    svr.listen("127.0.0.1", kLocalPort);
}

// ============================================================================
// WIN32 + WEBVIEW2 ISKELETI
// ============================================================================
static wil::com_ptr<ICoreWebView2Controller> g_webviewController;
static wil::com_ptr<ICoreWebView2> g_webview;

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE:
            if (g_webviewController) {
                RECT bounds;
                GetClientRect(hWnd, &bounds);
                g_webviewController->put_Bounds(bounds);
            }
            return 0;
        case WM_TIMER:
            // PARTI MODU: saniyede bir baslik (isim + sahte PID) degistir.
            // Kapaninca baslik BIR KEZ varsayilana geri alinir (wasParty ile).
            if (wParam == kPartyTimerId) {
                // bkz. g_wantKbHook yorumu - kullanici bir kisayol atadiginda
                // kancayi burada (ana pencerenin thread'inde) kur.
                if (g_wantKbHook.load() && !g_kbHook) {
                    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKbProc, GetModuleHandleW(nullptr), 0);
                    LogDiag("HOTKEY global kanca SONRADAN kuruldu (kullanici kisayol atadi)");
                }
                // GUVENILIRLIK BEKCISI (kullanici bildirdi: "kısayollar hala
                // sorunlu"): WH_KEYBOARD_LL kancasi, islem yogun anda
                // (frame yakalama/tespit sirasinda) callback'in sistem
                // zaman asimini (varsayilan 300ms) asmasi halinde Windows
                // tarafindan SESSIZCE sokulebilir - kod bunu ASLA bir hata
                // olarak GORMEZ, g_kbHook eski (artik gecersiz) handle'i
                // tutmaya devam eder. Cozum: kisayol atanmisken periyodik
                // olarak (10 saniyede bir) kancayi SOKUP YENIDEN KUR - boyle
                // sessiz bir kopma en fazla ~10sn surer, sonra kendiliginden
                // duzelir. SADECE bu iki kisayolun kancasini etkiler, baska
                // hicbir tusa/fonksiyona dokunmaz.
                static int rearmTick = 0;
                if (g_wantKbHook.load() && g_kbHook) {
                    if (++rearmTick >= 10) {
                        rearmTick = 0;
                        UnhookWindowsHookEx(g_kbHook);
                        g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKbProc, GetModuleHandleW(nullptr), 0);
                    }
                } else {
                    rearmTick = 0;
                }
                // GUARD MODU baslik guncellemesi ARTIK burada DEGIL - kullanici
                // "1ms'de falan degissin, asiri hizli olsun" istedi ve bu
                // zamanlayici saniyede 1 kez calisiyor (hotkey rearm mantigi
                // bunu gerektiriyor). Gercek hizli guncelleme icin bkz.
                // PartyModeLoop() - ayri, kendi Sleep(1) dongusune sahip thread.
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // KRITIK: config.json/desktop_ui/dovus/savas/puzzle gibi TUM dosya
    // yollari calisma dizinine (cwd) GORE aranir - ama cwd, uygulama NASIL
    // baslatildigina gore degisir (kisayoldan/Baslangic Menusunden/farkli
    // bir klasorden calistirilirsa exe'nin kendi klasorunden FARKLI olabilir,
    // bu durumda TUM dosyalar "bulunamadi" olup sayfa BOMBOS/etkilesimsiz
    // kalirdi). Bunu KOKTEN cozmek icin: cwd'yi HER ZAMAN exe'nin kendi
    // bulundugu klasore sabitliyoruz - artik NASIL baslatilirsa baslatilsin
    // ayni sekilde calisir.
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir(exePath);
    exeDir = exeDir.substr(0, exeDir.find_last_of(L"\\/"));
    SetCurrentDirectoryW(exeDir.c_str());

    // TEK ORNEK KORUMASI (KRITIK - kullanicinin bildirdigi "Sunucuya
    // baglanilamadi" hatasinin KOK NEDENI buydu): daha once ACIK kalmis
    // (ör. kapatilmadan yeni bir surum calistirilmis) bir Elbit.exe zaten
    // 127.0.0.1:8010 portunu tutuyorsa, YENI baslatilan kopyanin kendi
    // yerel sunucusu o porta HIC baglanamaz (sessizce basarisiz olur, HICBIR
    // hata gostermez) - arayuz (WebView2) o zaman TUM /api/* isteklerinde
    // "Sunucuya baglanilamadi" hatasi verir, cunku aslinda konusacagi bir
    // sunucu YOK. Bunu KOKTEN onlemek icin: adli bir mutex ile ikinci bir
    // kopyanin baslamasini TAMAMEN engelliyoruz - onceki pencere varsa onu
    // one getirip cikiyoruz, boylece ASLA "yarim/bozuk" ikinci bir kopya
    // calismaz.
    HANDLE singleInstanceMutex = CreateMutexW(nullptr, TRUE, L"ElbitOtomasyonSingleInstanceMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"ElbitOtomasyonNativeApp", nullptr);
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        } else {
            MessageBoxW(nullptr, L"Elbit zaten çalışıyor. Görev Yöneticisi'nde açık bir Elbit.exe varsa kapatıp tekrar deneyin.",
                        L"Elbit", MB_OK | MB_ICONINFORMATION);
        }
        if (singleInstanceMutex) CloseHandle(singleInstanceMutex);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // KISAYOLLAR: global (dusuk-seviye) klavye kancasi - oyun odaktayken bile
    // atanmis Baslat/Durdur tuslarini yakalar. Ana thread'in mesaj dongusu
    // (asagidaki GetMessage) kancayi besler; kanca sadece bir bayrak set eder
    // (bkz. LowLevelKbProc), oyunun tusunu TUKETMEZ.
    //
    // KRITIK (kullanici bildirdigi "Origins acar acmaz kapaniyor" hatasinin
    // KOK NEDENI - bkz. config.h global_hotkeys_enabled): bu kanca VARSAYILAN
    // olarak KURULMAZ. Macro recorder'lar da AYNI kancayi kurar ve bazi oyun
    // istemcileri global klavye kancasi gorunce makro varsayip kendini
    // kapatir. Kanca ancak kullanici config.json'da (ya da Ayarlar >
    // Kisayollar) global kisayollari ACIKCA acarsa kurulur.
    {
        Config startupCfg = Config::Load("config.json");
        if (startupCfg.global_hotkeys_enabled) {
            g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKbProc, GetModuleHandleW(nullptr), 0);
            LogDiag("HOTKEY global kanca kuruldu");
        } else {
            LogDiag("HOTKEY global kanca ATLANDI (global_hotkeys_enabled=false)");
        }
    }

    // Yerel API/dosya sunucusunu arka planda baslat - WebView2 buna baglanacak.
    // RunGuarded ile sarmalandi: sunucu is parcacigi HERHANGI bir sebeple
    // (yapisal istisna/cokme dahil) dururza, uygulamayi YIKMADAN 2sn sonra
    // KENDINI YENIDEN BASLATIR - arayuz "sunucuya baglanilamadi" gormek
    // yerine kisa bir kesintiden sonra normal calismaya doner.
    std::thread([]() { RunGuarded("RunLocalServer", RunLocalServer); }).detach();

    // Discord botu (varsa) - hesap baglama tamamlanmis olsun olmasin, token
    // config.json'da tanimliysa gateway baglantisini hemen kurar; hangi
    // kullanicidan gelen DM'lere cevap verecegine (discord_link.json) her
    // mesajda ayrica bakar - bkz. HandleDiscordCommand().
    // Hepsi istege bagli: config.json'da token/kanal yoksa hicbiri baslamaz.
    StartDiscordBotIfConfigured();
    StartTelegramBotIfConfigured();
    StartFarmPresenceLoopIfConfigured();
    std::thread([]() { RunGuarded("SchedulerLoop", SchedulerLoop); }).detach();
    std::thread([]() { RunGuarded("PartyModeLoop", PartyModeLoop); }).detach();

    HICON appIcon = ::LoadIconW(hInstance, MAKEINTRESOURCEW(101));  // kalkan+E logosu (bkz. desktop_app.rc.in)
    WNDCLASSEXW wc = {sizeof(wc), CS_HREDRAW | CS_VREDRAW, WndProc, 0L,      0L,
                       hInstance, appIcon,    nullptr, nullptr, nullptr,
                       L"ElbitOtomasyonNativeApp", appIcon};
    ::RegisterClassExW(&wc);
    // KRITIK: kullanicinin talebi - pencere kucultup/buyutup/genisletilemez
    // olmali ("kafama gore daraltiyorum genisletiyorum... pencere sabit
    // durmali"). WS_THICKFRAME (yeniden-boyutlandirma cercevesi) ve
    // WS_MAXIMIZEBOX (buyutme dugmesi) CIKARILIYOR - baslik cubugu/simge
    // menusu/kucultme/kapatma dugmeleri AYNEN kaliyor, sadece boyut SABIT.
    DWORD windowStyle = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Elbit | Premium", windowStyle, CW_USEDEFAULT,
                                 CW_USEDEFAULT, 940, 620, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return 1;

    // PARTI MODU zamanlayicisi: her zaman calisir, WndProc/WM_TIMER icinde
    // g_partyMode kontrol edilir (kapaliyken baslik degismez). Endpoint
    // /api/party sadece bayragi ceviriyor - bkz. MakePartyTitle.
    g_mainHwnd = hwnd;
    SetTimer(hwnd, kPartyTimerId, 1000, nullptr);

    // NATIVE PENCERE BASLIGINI DA UYGULAMA TEMASIYLA UYUMLU KOYU YAP: varsayilan
    // BEYAZ Windows title bar'i, koyu temali icerigin ustunde COK amator/
    // uyumsuz durur - bu, "profesyonel/modern" hissi bozan en belirgin
    // detaylardan biri. DWM (Windows 10 1809+) hem karanlik-mod bayragini hem
    // de (Windows 11 22000+) basligin TAM RENGINI (sidebar rengimizle BIREBIR
    // ayni) ayarlamamiza izin veriyor - boylece native cerceve UI'ya
    // KAYNASMIS gorunuyor. Desteklenmeyen Windows surumlerinde cagrilar
    // sessizce (hata dondurup) etkisiz kalir - crash YOK.
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
    COLORREF captionColor = RGB(13, 14, 19);  // --sidebar-bg (#0d0e13) ile BIREBIR ayni
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
    COLORREF textColor = RGB(238, 240, 243);  // --text (#eef0f3)
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));

    // WebView2 kullanici verisi (cerez/onbellek) icin - yaninda calisan
    // klasor "wv2data" olarak, exe'nin oldugu yerde tutulur.
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, L"wv2data", nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd](HRESULT, ICoreWebView2Environment* env) -> HRESULT {
                env->CreateCoreWebView2Controller(
                    hwnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                              [hwnd](HRESULT, ICoreWebView2Controller* controller) -> HRESULT {
                                  if (controller) {
                                      g_webviewController = controller;
                                      g_webviewController->get_CoreWebView2(&g_webview);
                                  }
                                  RECT bounds;
                                  GetClientRect(hwnd, &bounds);
                                  g_webviewController->put_Bounds(bounds);

                                  // F12/sag-tik "Denetle" ile DevTools acilip app.js/localStorage
                                  // incelenmesin, network sekmesinden yerel API cagrilari
                                  // izlenmesin diye TAMAMEN kapatiyoruz - lisans mantigi
                                  // (bkz. VerifyLicenseNow) zaten sunucu tarafinda olsa da,
                                  // DevTools kapaliyken normal kullanicinin "kurcalama"
                                  // yuzeyi ciddi sekilde daralir.
                                  if (g_webview) {
                                      wil::com_ptr<ICoreWebView2Settings> settings;
                                      if (SUCCEEDED(g_webview->get_Settings(&settings)) && settings) {
                                          settings->put_AreDevToolsEnabled(FALSE);
                                          settings->put_AreDefaultContextMenusEnabled(FALSE);
                                      }
                                  }

                                  wchar_t url[64];
                                  swprintf_s(url, L"http://127.0.0.1:%d/", kLocalPort);
                                  g_webview->Navigate(url);
                                  return S_OK;
                              })
                        .Get());
                return S_OK;
            })
            .Get());

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        // TEK MESAJ KORUMASI: WndProc icinde (ör. WM_TIMER/parti modu basligi,
        // WM_SIZE/webview boyutlandirma) beklenmeyen bir cokme olursa, TUM
        // pencere/uygulamayi GOTURMEDEN o TEK mesaji atlayip dongu devam eder.
        // (SEH siniri izole CallWithSehGuard'da - bkz. dosya basi, C2712'yi
        // onlemek icin bu fonksiyonun KENDISINDE __try KULLANILAMAZ, cunku
        // wWinMain COK sayida yikici-gerektiren yerel degisken barindiriyor.)
        DWORD sehCode = 0;
        bool ok = CallWithSehGuard([](void* p) { DispatchMessage(reinterpret_cast<MSG*>(p)); }, &msg, &sehCode);
        if (!ok) {
            char buf[64];
            snprintf(buf, sizeof(buf), "mesaj isleme sirasinda YAPISAL istisna, kod=0x%08lX, msg=0x%04X", sehCode,
                      msg.message);
            LogCrashGuard("WndProc/DispatchMessage", buf);
        }
    }
    g_session.Stop();
    if (g_discordBot) g_discordBot->Stop();
    if (g_telegramBot) g_telegramBot->Stop();
    return 0;
}
