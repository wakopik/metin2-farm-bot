// Sablon (template) Toplama Araci - Python tarafindaki capture_template.py'nin
// C++ karsiligi. Oyunda bir metnin uzerine kutu cizersin, o bolge
// templates/<isim>.png olarak kaydedilir. metin2_farm_bot.exe bunlari
// otomatik yukler (bkz. template_detector.cpp).
//
// Kullanim:
//   template_picker.exe [pencere_basligi]
// Pencere basligi verilmezse config.json -> window_title_hint kullanilir.
// Ornek (M2Hype sunucusunda ornek toplamak icin): template_picker.exe M2Hype

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <thread>

#include "capture.h"
#include "config.h"
#include "win_window.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {
// BOLGE KAYDETME KALDIRILDI (HP cubugu ve Nesne Market icin).
// Once mutlak piksel, sonra ORAN olarak kaydediliyordu; ikisi de baska bir
// cozunurlukte YANLIS yeri isaret ediyordu - cunku oyun arayuzu pencereyle
// ORANTILI BUYUMUYOR, SABIT PIKSEL boyutunda kaliyor (OLCULDU: HP cubugu
// 802x632 ve 1602x1049 karelerin IKISINDE de 129x10 px, olcek 1.0). Artik
// her iki oge de bot tarafinda ARANARAK bulunuyor (bkz.
// status_panel_detector.cpp ve bot.cpp CheckAndDismissNesneMarket), bu
// yuzden bolge kalibrasyonuna HIC gerek yok - sadece SABLONU yakalamak
// yeterli ve o sablon her cozunurlukte gecerlidir.

}  // namespace

namespace {

// Kullanicinin cizdigi kutunun icindeki nesneyi GrabCut ile arka plandan
// otomatik ayirir ve SEFFAF ARKA PLANLI (RGBA) bir goruntu dondurur -
// "arka plan kaldirma" sitelerine yukleme ihtiyacini ortadan kaldirir,
// tamamen yerel ve aninda calisir. Basarisiz olursa (nesne bulunamazsa)
// bos bir Mat doner.
cv::Mat RemoveBackgroundGrabCut(const cv::Mat& frame, cv::Rect roi) {
    // GrabCut'in cevreyi de gorup ayirt edebilmesi icin kutunun etrafina
    // pay birakarak daha genis bir alan kirpiyoruz.
    int padX = std::max(10, (int)(roi.width * 0.4));
    int padY = std::max(10, (int)(roi.height * 0.4));
    cv::Rect padded(roi.x - padX, roi.y - padY, roi.width + 2 * padX, roi.height + 2 * padY);
    padded = padded & cv::Rect(0, 0, frame.cols, frame.rows);
    if (padded.width < 10 || padded.height < 10) return cv::Mat();

    cv::Mat crop = frame(padded).clone();
    cv::Rect rectInCrop = cv::Rect(roi.x - padded.x, roi.y - padded.y, roi.width, roi.height) &
                           cv::Rect(0, 0, crop.cols, crop.rows);
    if (rectInCrop.width < 4 || rectInCrop.height < 4) return cv::Mat();

    cv::Mat mask(crop.size(), CV_8UC1, cv::Scalar(cv::GC_BGD));
    cv::Mat bgdModel, fgdModel;
    try {
        cv::grabCut(crop, mask, rectInCrop, bgdModel, fgdModel, 5, cv::GC_INIT_WITH_RECT);
    } catch (const cv::Exception&) {
        return cv::Mat();
    }

    cv::Mat alpha = ((mask == cv::GC_FGD) | (mask == cv::GC_PR_FGD));
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(alpha, alpha, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(alpha, alpha, cv::MORPH_CLOSE, kernel);

    std::vector<cv::Point> pts;
    cv::findNonZero(alpha, pts);
    if (pts.empty()) return cv::Mat();

    // Sadece nesnenin (alfa) kapladigi siki sinira kirp - "site cikti"
    // gibi, bos kenarsiz bir gorsel elde ederiz.
    cv::Rect tightBox = cv::boundingRect(pts);
    cv::Mat finalBgr = crop(tightBox);
    cv::Mat finalAlpha = alpha(tightBox);

    std::vector<cv::Mat> ch;
    cv::split(finalBgr, ch);
    ch.push_back(finalAlpha);
    cv::Mat rgba;
    cv::merge(ch, rgba);
    return rgba;
}

// Rastgele 8 karakterli, dosya adi olarak guvenli bir isim uretir - her
// karede kullaniciya isim sordurmamak icin (hizli, ardisik yakalama).
std::string RandomName() {
    static const char kAlphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, (int)sizeof(kAlphabet) - 2);
    std::string s = "s_";
    for (int i = 0; i < 8; ++i) s += kAlphabet[dist(rng)];
    return s;
}

// KRITIK BUG DUZELTMESI: cv::selectROI kullanildiginda, farkli program
// calistirmalari (PID'leri TAMAMEN FARKLI, yani surec-ici bir "bayat
// statik degisken" ihtimali DISLANMIS oluyor) arasinda bile BIREBIR AYNI
// (byte-byte ozdes MD5) goruntuler kaydedildigi tespit edildi - kullanici
// her seferinde FARKLI bir kutu cizdigini belirtse bile. Pencereyi odaga
// ZORLAMA denemesi (BringToForegroundDirect) bu davranisi DEGISTIRMEDI -
// yani sorun odak degil, cv::selectROI'nin KENDI ic uygulamasindaki
// guvenilmez/dokumante olmayan bir davranistan kaynaklaniyor olmali.
// COZUM: cv::selectROI'ye HIC guvenmiyoruz - fare surukleme mantigini
// SIFIRDAN, TAMAMEN kendi kontrolumuzde (cv::setMouseCallback ile) yeniden
// yaziyoruz. Boylece "gizli/bayat ic durum" ihtimali TAMAMEN ortadan kalkar
// - her cagrida taze bir yerel (static OLMAYAN) durum kullanilir.
struct RoiDragState {
    bool dragging = false;
    bool hasSelection = false;
    cv::Point start, current;
};

void OnRoiMouse(int event, int x, int y, int, void* userdata) {
    auto* state = reinterpret_cast<RoiDragState*>(userdata);
    if (event == cv::EVENT_LBUTTONDOWN) {
        state->dragging = true;
        state->hasSelection = true;
        state->start = cv::Point(x, y);
        state->current = state->start;
    } else if (event == cv::EVENT_MOUSEMOVE && state->dragging) {
        state->current = cv::Point(x, y);
    } else if (event == cv::EVENT_LBUTTONUP && state->dragging) {
        state->dragging = false;
        state->current = cv::Point(x, y);
    }
}

cv::Rect SelectRoiFocused(const std::string& windowTitle, const cv::Mat& frame) {
    cv::namedWindow(windowTitle, cv::WINDOW_NORMAL);
    HWND roiHwnd = FindWindowA(nullptr, windowTitle.c_str());
    if (roiHwnd) win_window::BringToForegroundDirect(roiHwnd);  // zarasi yok, tutuyoruz ama artik TEK basina guvenmiyoruz

    RoiDragState state;  // HER cagrida TAZE/yerel - onceki cagridan HICBIR sey tasimaz
    cv::setMouseCallback(windowTitle, OnRoiMouse, &state);

    cv::Rect result;
    while (true) {
        cv::Mat display = frame.clone();
        cv::putText(display, "Fare ile SURUKLE (sol tik basili tut), ENTER=onayla, C=iptal",
                    cv::Point(10, 25), cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0, 255, 255), 2);
        if (state.hasSelection) {
            cv::rectangle(display, state.start, state.current, cv::Scalar(0, 255, 0), 2);
        }
        cv::imshow(windowTitle, display);
        int key = cv::waitKey(15) & 0xFF;
        if (key == 13 || key == 10) {  // ENTER
            if (state.hasSelection) {
                int x = std::min(state.start.x, state.current.x);
                int y = std::min(state.start.y, state.current.y);
                int w = std::abs(state.current.x - state.start.x);
                int h = std::abs(state.current.y - state.start.y);
                result = cv::Rect(x, y, w, h) & cv::Rect(0, 0, frame.cols, frame.rows);
            }
            break;
        }
        if (key == 'c' || key == 'C' || key == 27) {  // C veya ESC = iptal
            result = cv::Rect();
            break;
        }
    }
    cv::destroyWindow(windowTitle);
    return result;
}

}  // namespace

// Komut satirindan verilen pencere basligi (bkz. asagida) - bos ise
// config.json icindeki window_title_hint kullanilir.
static std::string g_windowTitleOverride;

int main(int argc, char** argv) {
    if (argc >= 2) g_windowTitleOverride = argv[1];
    std::cout << "=== Metin2 Farm Botu - Sablon Toplama Araci ===\n";
    std::cout << "Her ENTER'da YENI bir kare yakalanir, kutu cizersin, arka plan otomatik\n";
    std::cout << "kaldirilip rastgele bir isimle kaydedilir.\n";
    std::cout << "'p' + ENTER: metni sectiginizde ekranda beliren UST BILGI PANELINDEKI SADECE metin\n";
    std::cout << "ADINI (ör. 'Dovus Metini') kutu icine al - 'Lv.X (Y.Seviye)' KISMINI KESINLIKLE ICINE\n";
    std::cout << "ALMA! O kisim HER canlida (baska bir mob/oyuncu dahil) AYNI gorunur, dahil edilirse\n";
    std::cout << "yakinda BASKA bir seyin paneli bile YANLISLIKLA eslesip botu 'hala kilitli' sanip\n";
    std::cout << "takili birakabilir. Sadece isim yazisinin kendisini SIKI bir kutuya al - cift\n";
    std::cout << "dogrulama icin kullanilir (isim gorunuyorsa dogru hedef, kaybolunca metin kesilmis olur).\n";
    std::cout << "'n' + ENTER: metin tasinin USTUNDE/yaninda YUZEN isim yazisini (ör. 'Dovus Metni')\n";
    std::cout << "SIKI bir kutu icine alip kaydeder - bu yazi kameraya HER ZAMAN duz gorundugu icin\n";
    std::cout << "(acidan bagimsiz) TEK bir gorsel COK ISE YARAR, birden fazla acidan almana GEREK YOK.\n";
    std::cout << "'h' + ENTER: bir hedefe/metine tiklayip ustte HP CUBUGU cikinca, SADECE cubugun\n";
    std::cout << "DOLU/renkli kismini + sabit cubuk alanini DAR bir kutuya al - isim yazisi/ikon/bos\n";
    std::cout << "alan/karakter HP-MP barini KESINLIKLE dahil ETME. Bu, TEK bir ortak 'hp_bar/_hpbar.png'\n";
    std::cout << "olarak kaydedilir - hangi metin/canavar turune tiklarsan tikla AYNI dosya kullanilir,\n";
    std::cout << "kategoriye gore ayri ayri yakalamaya GEREK YOK. CIZDIGIN KUTUNUN TAM KONUMU DA\n";
    std::cout << "otomatik olarak config.json'a yazilir - bot artik SADECE o konumda arayacak,\n";
    std::cout << "elle koordinat girmene GEREK YOK.\n";
    std::cout << "'m' + ENTER: \"Nesne Market\" bildirim seridi ekranda cikinca SIKI bir kutuya al - HP\n";
    std::cout << "cubugu ile AYNI mantik: sectigin kutunun TAM konumu otomatik config.json'a yazilir,\n";
    std::cout << "bot artik SADECE o bolgede arar, elle koordinat hesaplamana GEREK YOK.\n";
    std::cout << "'d' + ENTER: kayit klasorunu 'dovus' yapar (yeniden baslatmaya GEREK YOK).\n";
    std::cout << "'s' + ENTER: kayit klasorunu 'savas' yapar (yeniden baslatmaya GEREK YOK).\n";
    std::cout << "'f' + ENTER: TAM EKRAN OTOMATIK YAKALAMA modunu ac/kapat - YOLO/Roboflow egitim verisi\n";
    std::cout << "toplamak icindir (kutu SECMEZ, hicbir sey KIRPMAZ) - mod acikken SADECE ENTER'a basarak\n";
    std::cout << "(harf yazmadan) o anki TAM ekran goruntusunu 'metin/' klasorune kaydedersin. Karakterle\n";
    std::cout << "farkli mesafe/bolgelerden art arda ENTER'a basarak hizlica coklu ornek toplayabilirsin.\n";
    std::cout << "Cikmak icin 'q' yazip Enter'a bas.\n\n";

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);

        Config config = Config::Load("config.json");
        // Bu arac TEK SEFERDE TEK bir metin turu icin sablon toplar - birden
        // fazla klasor secili olsa bile (config.json -> templates_dirs),
        // BURADA ilkini kullaniyoruz; hangi turu toplamak istiyorsaniz
        // config.json'da o klasoru TEK basina yazip calistirin.
        std::string targetDir = config.templates_dirs.empty() ? "templates" : config.templates_dirs.front();
        fs::create_directories(targetDir);
        std::cout << "!!! KAYIT KLASORU: '" << targetDir
                   << "' (config.json -> templates_dirs[0]). Web panelde SECILI olan metin turuyle "
                      "AYNI olmali, degilse bot bu dosyalari GORMEZ !!!\n\n";

        // PENCERE BASLIGI KOMUT SATIRINDAN GECILEBILIR: config.json'daki
        // window_title_hint tek bir sunucuya sabitlenmis oluyor; farkli bir
        // sunucuda (ör. M2Hype) sablon/ornek toplamak icin config.json'u elle
        // duzenlemek gerekiyordu. Artik:  template_picker.exe M2Hype
        if (!g_windowTitleOverride.empty()) config.window_title_hint = g_windowTitleOverride;

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

        std::optional<cv::Mat> ready;
        for (int i = 0; i < 100 && !ready; ++i) {
            ready = capture.GetFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!ready) {
            std::cerr << "[hata] Ekran goruntusu alinamadi.\n";
            std::cout << "\nKapatmak icin Enter'a basin...";
            std::cin.get();
            return 1;
        }
        std::cout << "[capture] hazir.\n\n";

        int savedCount = 0;
        int fullFrameCount = 0;
        bool fullFrameMode = false;
        cv::Mat lastSavedRoi;  // GUVENLIK AGI: bir onceki secimle AYNI cikan bir secim varsa uyar (bkz. asagida)
        while (true) {
            if (fullFrameMode) {
                std::cout << "[TAM EKRAN MODU | " << fullFrameCount << " kaydedildi] ENTER=yakala, 'f'=modu kapat, 'q'=cik: ";
            } else {
                std::cout << "[" << (savedCount + 1) << " | klasor:" << targetDir
                           << "] Devam etmek icin ENTER'a bas (cikmak icin q+ENTER, klasor icin d/s+ENTER): ";
            }
            std::string line;
            std::getline(std::cin, line);
            if (!line.empty() && (line[0] == 'q' || line[0] == 'Q')) break;

            if (!line.empty() && (line[0] == 'f' || line[0] == 'F')) {
                fullFrameMode = !fullFrameMode;
                if (fullFrameMode) {
                    fs::create_directories("metin");
                    std::cout << "\n[secim] TAM EKRAN OTOMATIK YAKALAMA modu ACIK - 'metin/' klasorune kaydedilecek. "
                                 "Simdi SADECE ENTER'a basarak (harf yazmadan) art arda yakala. Kapatmak icin "
                                 "yine 'f' yaz.\n\n";
                } else {
                    std::cout << "\n[secim] tam ekran otomatik yakalama modu KAPATILDI.\n\n";
                }
                continue;
            }
            if (fullFrameMode) {
                if (!line.empty()) {
                    std::cout << "[uyari] tam ekran modundasin, sadece bos ENTER'a bas (ya da 'f'/'q').\n";
                    continue;
                }
                auto frame = capture.GetFrame();
                if (!frame) {
                    std::cout << "[uyari] kare alinamadi, tekrar dene.\n";
                    continue;
                }
                std::string outPath = (fs::path("metin") / (RandomName() + ".png")).string();
                cv::imwrite(outPath, *frame);
                fullFrameCount++;
                std::cout << "[kaydedildi] " << outPath << "  (" << frame->cols << "x" << frame->rows
                           << "px, tam ekran - kirpilmadi)\n";
                continue;
            }

            // CANLI KLASOR DEGISTIRME: onceden hangi turu toplamak istersen
            // istersen config.json'u elle degistirip PROGRAMI YENIDEN
            // BASLATMAK gerekiyordu - bu hem yavas hem de (bkz. gecmiste
            // yasanan olay) config.json'un YANLIS deger tasidigi bir anda
            // capture alinirsa YANLIS klasore (mevcut bir _name.png'nin
            // UZERINE) kaydedip fark edilmeden bozukluk yaratma riski
            // tasiyordu. Artik programi kapatmadan 'd'/'s' ile aninda
            // klasor degistirilebilir - ekrandaki "KAYIT KLASORU" satiri
            // HER degisiklikte guncellenir, bu yuzden hangi klasore
            // yazdigini HER ZAMAN acikca goruyorsun.
            if (!line.empty() && (line[0] == 'd' || line[0] == 'D')) {
                targetDir = "dovus";
                fs::create_directories(targetDir);
                std::cout << "[secim] KAYIT KLASORU artik '" << targetDir << "'\n\n";
                continue;
            }
            if (!line.empty() && (line[0] == 's' || line[0] == 'S')) {
                targetDir = "savas";
                fs::create_directories(targetDir);
                std::cout << "[secim] KAYIT KLASORU artik '" << targetDir << "'\n\n";
                continue;
            }

            bool panelMode = !line.empty() && (line[0] == 'p' || line[0] == 'P');
            bool nameLabelMode = !line.empty() && (line[0] == 'n' || line[0] == 'N');
            bool hpBarMode = !line.empty() && (line[0] == 'h' || line[0] == 'H');
            bool nesneMarketMode = !line.empty() && (line[0] == 'm' || line[0] == 'M');

            // ONEMLI: her seferinde YENI bir kare aliniyor - onceki (eski)
            // kareyi tekrar kullanmiyoruz, boylece art arda ENTER'a basip
            // farkli anlardan/acilardan hizlica ornek toplayabilirsin.
            auto frame = capture.GetFrame();
            if (!frame) {
                std::cout << "[uyari] kare alinamadi, tekrar dene.\n";
                continue;
            }

            if (panelMode) {
                std::string panelWindowTitle = "Panel isim/seviye satirini sec (ENTER=onayla, C=iptal)";
                cv::Rect panelRoi = SelectRoiFocused(panelWindowTitle, *frame);
                if (panelRoi.width <= 0 || panelRoi.height <= 0) {
                    std::cout << "[atlandi] kutu secilmedi.\n\n";
                    continue;
                }
                cv::Mat gray;
                cv::cvtColor((*frame)(panelRoi), gray, cv::COLOR_BGR2GRAY);
                std::string panelOutPath = (fs::path(targetDir) / "_panel.png").string();
                cv::imwrite(panelOutPath, gray);
                std::cout << "[kaydedildi] " << panelOutPath << "  (" << gray.cols << "x" << gray.rows
                           << "px, cift dogrulama referansi)\n\n";
                continue;
            }

            if (hpBarMode) {
                // KATEGORIDEN BAGIMSIZ: targetDir'e degil, SABIT 'hp_bar/'
                // klasorune kaydedilir - HP cubugu hangi metin/canavar
                // turune tiklarsan tikla AYNI gorunur (bkz. status_panel_
                // detector.h yorumu, kullanicinin PHANTOM aktarimindan
                // gelen tasarim).
                std::string hpBarWindowTitle =
                    "HP cubugunun SADECE dolu/renkli kismini sec (ENTER=onayla, C=iptal)";
                cv::Rect hpBarRoi = SelectRoiFocused(hpBarWindowTitle, *frame);
                if (hpBarRoi.width <= 0 || hpBarRoi.height <= 0) {
                    std::cout << "[atlandi] kutu secilmedi.\n\n";
                    continue;
                }
                fs::create_directories("hp_bar");
                cv::Mat gray;
                cv::cvtColor((*frame)(hpBarRoi), gray, cv::COLOR_BGR2GRAY);
                std::string hpBarOutPath = (fs::path("hp_bar") / "_hpbar.png").string();
                cv::imwrite(hpBarOutPath, gray);
                std::cout << "[kaydedildi] " << hpBarOutPath << "  (" << gray.cols << "x" << gray.rows
                           << "px, TUM kategoriler icin ortak HP cubugu referansi)\n\n";
                // NOT: bolge KAYDEDILMIYOR (bkz. dosya basindaki aciklama) -
                // bot HP cubugunu ust-orta bantta ARAYARAK bulur, bu yuzden
                // sablonun kendisi her cozunurlukte yeterlidir.
                continue;
            }

            if (nesneMarketMode) {
                // AYNI desen (kullanici talebi: "HP barinda yaptigimiz
                // sistemi yap") - sabit 'nesne_market/' klasorune kaydedilir,
                // GRI DEGIL (hp_bar'in aksine) - Nesne Market seridi renkli
                // bir arayuz ogesi, renk bilgisi eslesmeyi guclendirir.
                std::string nesneMarketWindowTitle =
                    "\"Nesne Market\" bildirim seridini SIKI bir kutuya al (ENTER=onayla, C=iptal)";
                cv::Rect nesneMarketRoi = SelectRoiFocused(nesneMarketWindowTitle, *frame);
                if (nesneMarketRoi.width <= 0 || nesneMarketRoi.height <= 0) {
                    std::cout << "[atlandi] kutu secilmedi.\n\n";
                    continue;
                }
                fs::create_directories("nesne_market");
                std::string nesneMarketOutPath = (fs::path("nesne_market") / "_banner.png").string();
                cv::imwrite(nesneMarketOutPath, (*frame)(nesneMarketRoi));
                std::cout << "[kaydedildi] " << nesneMarketOutPath << "  (" << nesneMarketRoi.width << "x"
                           << nesneMarketRoi.height << "px)\n";
                // NOT: bolge KAYDEDILMIYOR - bot banner'i ust-orta bantta
                // ARAYARAK bulur (bkz. bot.cpp CheckAndDismissNesneMarket),
                // bu yuzden sablon her cozunurlukte tek basina yeterlidir.
                continue;
            }

            if (nameLabelMode) {
                // Metnin USTUNDE/yaninda YUZEN isim yazisini (ör. "Dovus Metni")
                // SIKI/DAR bir kutu icine al - sadece yazinin kendisini, etrafinda
                // gereksiz bosluk/arkaplan OLMASIN (matchTemplate hassasiyetini
                // artirir). GrabCut/arka plan kaldirma YOK - duz gri tonlamali
                // kirpma yeterli (bkz. name_label_detector.cpp - matchTemplate
                // zaten piksel-piksel karsilastirma yapiyor).
                // NOT: bir ara ExtractTextForeground ile arka plan otomatik
                // kaldirilip SEFFAF (RGBA) kaydedilmeye calisildi - kullanici
                // CANLI TESTTE sonucun (gurultulu/bozuk gorunum) KALITEYI
                // DUSURDUGUNU bildirdi, bu yuzden GERI ALINDI. name_label_
                // detector.cpp maskeli eslesme yolunu hala DESTEKLIYOR (mask
                // bossa sessizce atlar) - ileride daha iyi bir arka plan
                // ayirma yontemi bulunursa kod tarafinda ek degisiklik
                // gerekmeden devreye alinabilir.
                std::string nameWindowTitle = "Isim etiketini (ör. 'Dovus Metni' yazisi) sec (ENTER=onayla, C=iptal)";
                cv::Rect nameRoi = SelectRoiFocused(nameWindowTitle, *frame);
                if (nameRoi.width <= 0 || nameRoi.height <= 0) {
                    std::cout << "[atlandi] kutu secilmedi.\n\n";
                    continue;
                }
                cv::Mat gray;
                cv::cvtColor((*frame)(nameRoi), gray, cv::COLOR_BGR2GRAY);
                std::string nameOutPath = (fs::path(targetDir) / "_name.png").string();
                cv::imwrite(nameOutPath, gray);
                std::cout << "[kaydedildi] " << nameOutPath << "  (" << gray.cols << "x" << gray.rows
                           << "px, acidan bagimsiz isim etiketi referansi)\n\n";
                continue;
            }

            std::string windowTitle = "Kutu sec (ENTER=onayla, C=iptal)";
            cv::Rect roi = SelectRoiFocused(windowTitle, *frame);

            if (roi.width <= 0 || roi.height <= 0) {
                std::cout << "[atlandi] kutu secilmedi.\n\n";
                continue;
            }

            // GUVENLIK AGI: secim penceresi odak alamayip fare suruklemesi
            // ULASMAZSA, cv::selectROI onceki BAYAT dikdortgeni sessizce
            // geri dondurebilir (bkz. SelectRoiFocused yorumu) - boyle bir
            // durumu ARTIK SESSIZCE GECMIYORUZ, acikca uyariyoruz.
            cv::Mat curRoi = (*frame)(roi);
            if (!lastSavedRoi.empty() && lastSavedRoi.size() == curRoi.size() &&
                lastSavedRoi.type() == curRoi.type() && cv::countNonZero(lastSavedRoi != curRoi) == 0) {
                std::cout << "[UYARI] Bu kutu bir ONCEKI secimle TAMAMEN AYNI cikti - secim penceresi odak "
                              "almamis OLABILIR (fare suruklemen algilanmamis olabilir). Yine de kaydediliyor, "
                              "ama muhtemelen istemedigin bir yinelenen kayittir - dikkatli kontrol et.\n";
            }
            lastSavedRoi = curRoi.clone();

            std::string name = RandomName();
            std::cout << "[isleniyor] arka plan otomatik kaldiriliyor (GrabCut)...\n";
            cv::Mat rgba = RemoveBackgroundGrabCut(*frame, roi);

            std::string outPath = (fs::path(targetDir) / (name + ".png")).string();
            if (!rgba.empty()) {
                cv::imwrite(outPath, rgba);
                std::cout << "[kaydedildi] " << outPath << "  (" << rgba.cols << "x" << rgba.rows
                           << "px, arka plan seffaf)\n\n";
            } else {
                // GrabCut basarisiz olursa (nadiren), eski davranisa (duz kirpma) don.
                cv::Mat crop = (*frame)(roi).clone();
                cv::imwrite(outPath, crop);
                std::cout << "[uyari] arka plan ayirilamadi, duz kirpma kaydedildi.\n";
                std::cout << "[kaydedildi] " << outPath << "  (" << roi.width << "x" << roi.height << "px)\n\n";
            }
            savedCount++;
        }

        capture.Stop();
        std::cout << "\nTamamlandi (" << savedCount << " sablon kaydedildi). Kapatmak icin Enter'a basin...";
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
