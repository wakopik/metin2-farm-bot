#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

// Oyun penceresini bulma / on plana getirme yardimci fonksiyonlari.
// Python projesindeki src/window.py'nin C++ karsiligi.
namespace win_window {

// Baslik ipucuna gore (kismi, buyuk/kucuk harf duyarsiz eslesme) gorunur
// bir pencere handle'i dondurur, bulamazsa nullptr.
HWND FindGameWindow(const std::string& titleHint);

// FindGameWindow ile AYNI eslesme mantigi, ama TEK sonuc yerine baslik
// ipucuyla eslesen TUM gorunur pencereleri dondurur - birden fazla oyun
// istemcisi (multi-client) ayni anda acikken her birini ayri ayri
// baglayabilmek icin. exclude icindeki handle'lar sonuca dahil edilmez
// (zaten baska bir bot'a baglanmis pencereleri tekrar secmemek icin).
std::vector<HWND> FindAllGameWindows(const std::string& titleHint, const std::vector<HWND>& exclude = {});

// Bos olmayan basligi olan tum gorunur pencerelerin basliklarini dondurur -
// dogru titleHint'i bulmak icin (hata mesajlarinda gosterilir).
std::vector<std::string> ListVisibleWindowTitles();

// Pencereyi on plana getirmeye CALISIR - GERCEKTE odagi degistirir mi
// degistirmez mi, cagiran taraf GetForegroundWindow() ile dogrulamali
// (bkz. bot.cpp Activate()). Arka planda calisan (mesaj dongusu OLMAYAN)
// bir is parcacigindan cagrilirsa Windows'un foreground-calma engeli
// GUVENILMEZ sekilde devreye girebiliyor - bkz. SetForegroundRequestHandler.
void BringToForeground(HWND hwnd);

// GERCEK (dogrudan API) deneme - SetForegroundRequestHandler kayitli
// olsa bile BUNU DEGIL, dogrudan denemeyi yapar. Sadece zaten bir mesaj
// dongusu olan bir is parcacigindan (ör. tepsi simgesi pencere yordami)
// cagirmak icindir - bkz. web_main.cpp.
void BringToForegroundDirect(HWND hwnd);

// HAFIF/HIZLI odak zorlama - BringToForeground'un aksine alt-tap simulasyonu/
// topmost-toggle/5-deneme bekleme dongusu YAPMAZ (Step() basinda zaten
// GetForegroundWindow()==hwnd DOGRULANMIS oluyor - bkz. bot.cpp). Sadece
// PID'i bulup (kullaniciya gosterilebilsin diye DONDURUR) AttachThreadInput
// kopruleyip SetFocus cagirir - "pencere ON PLANDA ama GERCEK klavye
// odagi (input focus) baska bir alt pencerede/hic bir yerde degil" olasi
// senaryosunu KAPATMAK icin. Her tus/tik ONCESI cagirilmasi guvenlidir
// (ucuz - Sleep/dongusu yok).
DWORD EnsureInputFocus(HWND hwnd);

// UYUMLULUK MODU (kullanici bildirimi: "Origins'de botu baslatinca kapaniyor"
// - KOK NEDEN ADAYI): EnsureInputFocus HER TEK TIKLAMA/TUS BASIMINDAN ONCE
// cagrilir ve icinde AttachThreadInput var - dakikada onlarca/yuzlerce kez
// tekrarlanan bu cagri, bazi oyun istemcilerinin (baska bir surecin kendi
// giris kuyruguna "iliserek" odak calmasi) izledigi KLASIK bir teknik.
// Acikken EnsureInputFocus VE BringToForegroundDirect, AttachThreadInput'u
// TAMAMEN ATLAR - sadece SetForegroundWindow/BringWindowToTop/SetActiveWindow/
// SetFocus ile yetinir (daha az guvenilir ama hicbir "yabanci is parcacigina
// iliskilendirme" izi birakmaz). Varsayilan KAPALI - sadece config.h
// avoid_attach_thread_input acikken (ör. Origins sunucusu secilince otomatik)
// devreye girer.
void SetAvoidAttachThreadInput(bool avoid);

// KRITIK: webapp.exe'nin ana calisma (bot) is parcaciginin KENDI mesaj
// dongusu YOK - bu, bazi sistemlerde Windows'un SetForegroundWindow'u
// GUVENILMEZ sekilde reddetmesine yol aciyordu (arka planda calisan bir
// surecin odak "calmasi" varsayilan olarak engellenir, ama bu engelin
// kesinligi GERCEK bir mesaj dongusu olan bir is parcacigindan gelen
// isteklerde daha az siki isliyor). Cozum: web_main.cpp, KENDI mesaj
// dongusune sahip olan tepsi simgesi is parcaciginin (RunTrayIcon)
// pencere yordamindan BringToForegroundDirect'i cagiran bir handler
// kaydediyor - BringToForeground cagrildiginda, kayitli bir handler
// varsa istegi O is parcacigina (PostMessage ile) yonlendirir.
void SetForegroundRequestHandler(std::function<void(HWND)> handler);

// Pencerenin su anki basligini dondurur.
std::string GetWindowTitle(HWND hwnd);

// Pencere kenarliklari/baslik cubugu haric, sadece istemci (client) alaninin
// ekran koordinatlarini dondurur (left, top, right, bottom).
RECT GetClientRectOnScreen(HWND hwnd);

// Pencerenin TAMAMININ (baslik cubugu + kenarlik DAHIL) ekran koordinatlarini
// dondurur - WindowCapture'in yakaladigi kare de tam pencereyi kapsadigi
// icin (bkz. capture.cpp), tespit koordinatindan tiklama koordinatina
// gecerken BUNU kullanmak gerekir, GetClientRectOnScreen degil - aksi
// halde baslik cubugu/kenarlik farki IKI KERE sayilir ve tiklama pencere
// disina kayar.
RECT GetWindowRectOnScreen(HWND hwnd);

// Windows Graphics Capture'in GERCEKTE yakaladigi alanin ekran dikdortgeni -
// yani pencerenin GORUNUR cercevesi (DWM genisletilmis cerceve). GetWindowRect
// ile arasinda fark vardir: o, GORUNMEZ yeniden-boyutlandirma kenarligini da
// icerir (kullanicinin logunda pencere 816x639 iken kare 802x632 idi). Kare
// uzayi <-> ekran donusumunde MUTLAKA bu kullanilmalidir; aksi halde merkezde
// sifir, kenarlarda buyuyen sistematik bir tiklama kaymasi olusur.
RECT GetCapturedFrameRectOnScreen(HWND hwnd);

// ZAMANLI DURDUR -> "Durdurduktan sonra oyundan cik" secenegi icin: verilen
// pencerenin SAHIBI process'i (oyun istemcisi) SERT sekilde sonlandirir
// (TerminateProcess) - kaydetme/kapanis diyaloguna FIRSAT VERMEZ, bu yuzden
// SADECE kullanicinin ACIKCA istedigi (checkbox isaretli) zamanlanmis
// durdurma aninda cagrilmalidir, bkz. desktop_app_main.cpp SchedulerLoop.
void KillProcessOwningWindow(HWND hwnd);

// CIHAZ KILIDI icin: bu bilgisayara ozgu, Windows kurulumlari arasinda
// PRATIKTE benzersiz ve YENIDEN BASLATMALAR/format DISINDA SABIT kalan bir
// kimlik dondurur (Windows'un kendi MachineGuid degeri - kayit defterinden
// okunur, YAZMA/yonetici yetkisi GEREKMEZ). Okunamazsa (cok nadir) bos
// string doner - bkz. desktop_app_main.cpp CheckLicenseKey, bos donduyse
// cihaz kilidi guvenlik geregi DEVRE DISI birakilir (asla "her zaman ayni"
// sabit bir deger kullanilmaz, bu TUM cihazlari ayni sayardi).
std::string GetMachineHwid();

// Lisans panelinde "Cihaz Adi" olarak gosterilir - kullanicinin Windows
// bilgisayar adi (Ayarlar > Sistem > Hakkinda). Salt bilgi amaclidir,
// HWID gibi bir kimlik dogrulama degeri DEGILDIR.
std::string GetLocalComputerName();

}  // namespace win_window
