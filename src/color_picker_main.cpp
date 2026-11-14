// Renk Profili Toplama Araci - Python tarafindaki pick_color.py'nin C++
// karsiligi, ama BIRDEN FAZLA isimli profil (metin turu basina bir HSV
// araligi) destekler.
//
// Kullanim:
//   color_picker.exe
//
// Her profil icin: bir isim girersin (ornegin "Savas Metni"), sonra
// oyunda o turden bir metnin uzerine SIKI bir kutu cizersin (fare ile
// suruklersin, ENTER ile onaylarsin). Istersen ayni profil icin farkli
// isik/aci/mesafeden birkac kutu daha eklersin (ne kadar cok, o kadar
// saglam aralik). Bitirince config.json -> color_profiles OTOMATIK
// guncellenir; ana bot (metin2_farm_bot.exe) bir sonraki calistirmada
// bunu kullanir.

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "capture.h"
#include "config.h"
#include "win_window.h"

namespace {

// Python tarafindaki EXTRA_PADDING ile ayni ruh - hesaplanan araliga
// eklenen tolerans (H, S, V).
constexpr int PADDING[3] = {10, 40, 40};

int Percentile(std::vector<int>& values, double pct) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    size_t idx = (size_t)std::clamp((pct / 100.0) * (values.size() - 1), 0.0, (double)(values.size() - 1));
    return values[idx];
}

std::string AskLine(const std::string& prompt) {
    std::cout << prompt;
    std::string line;
    std::getline(std::cin, line);
    return line;
}

bool AskYesNo(const std::string& prompt) {
    std::string ans = AskLine(prompt);
    return !ans.empty() && (ans[0] == 'e' || ans[0] == 'E' || ans[0] == 'y' || ans[0] == 'Y');
}

}  // namespace

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::cout << "=== Metin2 Farm Botu - Renk Profili Toplama Araci ===\n";
    std::cout << "Her profil icin bir isim girip, oyunda o metnin uzerine kutu cizeceksin.\n\n";

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

        WindowCapture capture(hwnd);
        std::cout << "[capture] pencereye baglandi, kare bekleniyor...\n";

        std::optional<cv::Mat> frameOpt;
        for (int i = 0; i < 100 && !frameOpt; ++i) {
            frameOpt = capture.GetFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!frameOpt) {
            std::cerr << "[hata] Ekran goruntusu alinamadi.\n";
            std::cout << "\nKapatmak icin Enter'a basin...";
            std::cin.get();
            return 1;
        }
        std::cout << "[capture] hazir.\n\n";

        std::vector<ColorProfile> profiles = config.color_profiles;

        while (true) {
            std::string name = AskLine(
                "Yeni bir profil (metin turu) adi gir (bos birakip Enter'a basarsan bitirir): ");
            if (name.empty()) break;

            std::vector<int> hPool, sPool, vPool;
            int boxCount = 0;
            std::string windowTitle = "Kutu sec: " + name + "  (ENTER=onayla, C=iptal)";

            while (true) {
                auto frame = capture.GetFrame();
                if (!frame) {
                    std::cout << "[uyari] kare alinamadi, tekrar deneniyor...\n";
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                cv::Rect roi = cv::selectROI(windowTitle, *frame, true, false);
                cv::destroyWindow(windowTitle);

                if (roi.width > 0 && roi.height > 0) {
                    cv::Mat crop = (*frame)(roi);
                    cv::Mat hsv;
                    cv::cvtColor(crop, hsv, cv::COLOR_BGR2HSV);
                    for (int y = 0; y < hsv.rows; ++y) {
                        for (int x = 0; x < hsv.cols; ++x) {
                            cv::Vec3b px = hsv.at<cv::Vec3b>(y, x);
                            hPool.push_back(px[0]);
                            sPool.push_back(px[1]);
                            vPool.push_back(px[2]);
                        }
                    }
                    boxCount++;
                    std::cout << "[eklendi] " << roi.width << "x" << roi.height << " = "
                               << (roi.width * roi.height) << " piksel. Toplam kutu: " << boxCount
                               << ", " << hPool.size() << " piksel.\n";
                } else {
                    std::cout << "[atlandi] kutu secilmedi.\n";
                }

                if (!AskYesNo("Ayni profil icin baska bir kutu eklemek ister misin (farkli isik/aci)? (E/H): "))
                    break;
            }

            if (hPool.empty()) {
                std::cout << "[uyari] hic piksel toplanamadi, '" << name << "' atlaniyor.\n\n";
                continue;
            }

            ColorProfile profile;
            profile.name = name;
            profile.hsv_lower[0] = std::max(0, Percentile(hPool, 5) - PADDING[0]);
            profile.hsv_lower[1] = std::max(0, Percentile(sPool, 5) - PADDING[1]);
            profile.hsv_lower[2] = std::max(0, Percentile(vPool, 5) - PADDING[2]);
            profile.hsv_upper[0] = std::min(179, Percentile(hPool, 95) + PADDING[0]);
            profile.hsv_upper[1] = std::min(255, Percentile(sPool, 95) + PADDING[1]);
            profile.hsv_upper[2] = std::min(255, Percentile(vPool, 95) + PADDING[2]);

            std::cout << "[sonuc] '" << name << "' -> alt=[" << profile.hsv_lower[0] << ","
                       << profile.hsv_lower[1] << "," << profile.hsv_lower[2] << "] ust=["
                       << profile.hsv_upper[0] << "," << profile.hsv_upper[1] << ","
                       << profile.hsv_upper[2] << "]\n";

            auto it = std::find_if(profiles.begin(), profiles.end(),
                                    [&](const ColorProfile& p) { return p.name == name; });
            if (it != profiles.end())
                *it = profile;
            else
                profiles.push_back(profile);

            Config::SaveColorProfiles("config.json", profiles);
            std::cout << "[kaydedildi] config.json guncellendi (" << profiles.size() << " profil).\n\n";
        }

        capture.Stop();
        std::cout << "\nTamamlandi (" << profiles.size() << " profil kayitli). Kapatmak icin Enter'a basin...";
        std::cin.get();
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
