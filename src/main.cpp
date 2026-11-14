// Metin2 Farm Botu (C++) - Faz 1: konsol motoru
//
// Kullanim:
//   metin2_farm_bot.exe
//
// Kisayollar:
//   L   -> baslat
//   K   -> duraklat
//   ESC -> cikis
//
// Bu, Python projesindeki src/main.py + src/bot.py'nin C++ karsiligi.
// Arayuz (WebView2) faz 2'de bu motorun ustune baglanacak.

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <chrono>
#include <iostream>
#include <thread>

#include "bot.h"
#include "config.h"

namespace {

// Cift tiklayarak acilan konsol penceresi, program bitince ANINDA kapanir -
// hata mesaji varsa okunamadan kaybolur. Bunu onlemek icin cikmadan once
// bir tusa basilmasini bekliyoruz.
void WaitForKeyPress() {
    std::cout << "\nKapatmak icin Enter'a basin...";
    std::cout.flush();
    std::cin.get();
}

int RunBot() {
    // KRITIK: uygulama DPI'a duyarli olmali. Aksi halde Windows,
    // GetWindowRect gibi API'leri OLCEKLENMIS (sahte) koordinatlarda
    // dondurur, ama WGC yakalamasi GERCEK fiziksel piksel kullanir - bu
    // uyumsuzluk, ekran olcekleme (%125/%150 vb.) acikken tiklama
    // koordinatinin sistemli sekilde kaymasina (bambaska yerlere
    // tiklamaya) sebep olur. Bu, en erken noktada (ilk pencere/DPI
    // sorgusundan once) ayarlanmali.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::cout << "Metin2 Farm Botu (C++) baslatiliyor...\n";
    std::cout << "  L = baslat, K = duraklat, ESC = cikis\n";

    winrt::init_apartment(winrt::apartment_type::single_threaded);

    Config config = Config::Load("config.json");

    MetinBot bot(config);
    bot.BindWindow();
    std::cout << "[bot] oyun penceresi bulundu ve baglandi.\n";
    if (!bot.HasTemplates()) {
        std::cout << "[uyari] hic sablon yok - template_picker.exe ile en az bir sablon ekleyin, "
                     "yoksa bot hicbir zaman tespit yapamaz.\n";
    }

    bool running = false;
    bool lWasDown = false, kWasDown = false, escWasDown = false;

    while (true) {
        bool lDown = (GetAsyncKeyState('L') & 0x8000) != 0;
        bool kDown = (GetAsyncKeyState('K') & 0x8000) != 0;
        bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;

        if (lDown && !lWasDown) {
            running = true;
            std::cout << "[bot] BASLADI\n";
        }
        if (kDown && !kWasDown) {
            running = false;
            std::cout << "[bot] DURAKLADI\n";
        }
        if (escDown && !escWasDown) {
            std::cout << "[bot] cikiliyor...\n";
            break;
        }
        lWasDown = lDown;
        kWasDown = kDown;
        escWasDown = escDown;

        bot.SetRunning(running);

        if (!running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        bool clicked = bot.Step();
        if (clicked) {
            auto stats = bot.GetStats();
            std::cout << "[bot] hedef bulundu -> burst tiklandi (tespit: " << stats.targets_found
                       << ", kesilen: " << stats.targets_completed
                       << ", toplam tiklama: " << stats.total_clicks << ")\n";
        } else if (bot.HasTarget()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
    }

    bot.Stop();
    std::cout << "[bot] kapatildi.\n";
    return 0;
}

}  // namespace

int main() {
    int result = 1;
    try {
        result = RunBot();
    } catch (const winrt::hresult_error& e) {
        std::cerr << "[hata] Windows/WinRT hatasi (0x" << std::hex << (uint32_t)e.code() << std::dec
                   << "): " << winrt::to_string(e.message()) << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[hata] " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[hata] Bilinmeyen bir hata olustu.\n";
    }

    WaitForKeyPress();
    return result;
}
