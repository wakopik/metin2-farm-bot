#pragma once
#include <windows.h>
#include <chrono>
#include <string>
#include "config.h"

// Fare/klavye kontrolu - Interception surucusu TAMAMEN KALDIRILDI
// (kullanici talebi, bkz. input.cpp yorumlari): tum girdi SetCursorPos +
// SendMessage + SendInput ile yapilir, hicbir surucuye/kuruluma ihtiyac yoktur.
class InputController {
public:
    explicit InputController(const Config& config);
    ~InputController();

    // Fare hareketlerinin (ozellikle kamera cevirme/goz atma) HANGI
    // PENCERE SINIRLARINI asla asmayacagini belirler - "kesin" guvence
    // icin. Pencere degisirse (nadiren) tekrar cagirilabilir.
    void SetBounds(HWND hwnd);

    // KULLANICI TALEBI: her tik/tus GONDERIMINDEN ONCE PID uzerinden gercek
    // odagi (bkz. win_window::EnsureInputFocus) zorluyoruz - hangi PID'in
    // bulunup hedeflendigini durum ekraninda (2-3 saniyeligine) gosterebilmek
    // icin son bulunan PID'i ve NE ZAMAN bulundugunu burada saklıyoruz.
    DWORD LastFocusedPid() const { return m_lastFocusedPid; }
    double LastFocusedPidAgeSec() const {
        if (m_lastFocusedPid == 0) return -1.0;
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_lastFocusedPidAt).count();
    }

    // KRITIK GUVENLIK: Windows'un KENDI imlec-hapsetme (ClipCursor) API'sini
    // kullanarak fareyi pencere sinirlarina FIZIKSEL OLARAK hapseder - bizim
    // hesapladigimiz deltalara/hizlandirma tahminine bagli DEGIL, isletim
    // sistemi imlecin o dikdortgenin disina cikmasina IZIN VERMEZ. Sadece
    // oyun penceresi ON PLANDAYKEN cagrilmali (bkz. bot.cpp Step()).
    void UpdateClipToWindow();

    // Hapsi TAMAMEN kaldirir - fare tekrar tum ekranda serbest. Pencere
    // odagi kaybedildiginde, bot durduruldugunda VE her ihtimale karsi
    // yikici (destructor) icinde de cagrilir - fare ASLA kilitli KALMAMALI.
    void ReleaseClip();

    InputController(const InputController&) = delete;
    InputController& operator=(const InputController&) = delete;

    // Hedefe kucuk adimlarla, insansi bir sure icinde hareket eder (mutlak ekran koordinati).
    void MoveTo(int x, int y);

    void Click(int x, int y, bool rightButton = false);

    // Bu sunucuda metin kesme tiklamasi SHIFT + SAG TIK ile yapiliyor.
    void ShiftRightClick(int x, int y);

    // Ust bilgi paneli cok uzun sure (ör. karakter takilmis, ilerleme yok)
    // acik kalirsa onu kapatmak icin ESC tusuna basar - tek, kisa bir
    // basip-birakma (Shift gibi surekli basili tutma YOK, bu yuzden
    // erisilebilirlik kisayolu riski de yok).
    void PressEscape();

    // Metin kesilip yere esya/parca dustugunde bunlari toplamak icin
    // kullanilan Z tusuna TEK, kisa bir basip-birakma yapar (ESC ile ayni
    // guvenli desen - surekli basili tutma YOK).
    void PressLootKey();

    // KRITIK: bir burst icinde Shift'i HER tiklamada ayri ayri basip
    // birakmak (ShiftRightClick'i art arda cagirmak), Windows'un "Yapiskan
    // Tuslar" erisilebilirlik kisayolunu (Shift'e ~5 kez ust uste basilmasi)
    // tetikleyip bir sistem dialogu acabiliyor - bu da odagi (focus) oyundan
    // calip pencereyi arka plana atiyordu ("alt-tab gibi" davraniyordu).
    // Cozum: Shift'i TUM burst boyunca TEK SEFER basili tut, arada sadece
    // sag tikla - HoldShift(true)/HoldShift(false) arasinda Click() cagir.
    void HoldShift(bool down);

    // ANTI-TAKILMA: bir YON tusunu (W/A/S/D) verilen sure boyunca BASILI
    // TUTAR, sonra birakir - karakteri sikistigi yerden kurtarmak icin.
    // Tek/anlik basma DEGIL: karakterin gercekten yurumesi icin tusun
    // sure boyunca basili kalmasi gerekir.
    void HoldMoveKey(char key, double seconds);

    // ANTI-TAKILMA sonrasi TEMIZLIK: hicbir fare tusu (sol/sag), Shift veya
    // yon tusu (W/A/S/D) oyunun gozunde TAKILI KALMASIN diye HEPSINE birakma
    // (up) olayi gonderir. "anti-stuck sonrasi mouse tiklamiyor" belirtisinin
    // en olasi nedeni takili kalan bir SAG tus (kamera-surukleme modu) idi.
    void ReleaseAllButtonsAndKeys();

    // Imleci DOGRUDAN verilen ekran noktasina koyar (Win32 SetCursorPos) -
    // eski MoveTo Interception'a bagliydi, surucu kaldirilinca calismiyor;
    // anti-takilma kamera cevirmesinde imleci yeniden konumlandirmak icin bu
    // kullanilir.
    void PlaceCursor(int x, int y);

    // OTO BASMA: kullanicinin girdigi tus(lar)a anlik bas-birak yapar -
    // periyodik gondermek icin. SendKey'e (bot'un SHIFT/Z/ESC/WASD'i)
    // DOKUNMAZ, ayri/bagimsiz bir yol.
    //
    // Kabul edilen bicimler (buyuk/kucuk harf farketmez, bosluk tolere edilir):
    //   "X" / "5"          -> tek harf/rakam
    //   "F1".."F12"        -> fonksiyon tuslari
    //   "SPACE","ENTER",   -> isimli tuslar (bkz. input.cpp kNamedKeys)
    //   "TAB","ESC", ...
    //   "CTRL","ALT","SHIFT" -> modifier'in KENDISI (tek basina)
    //   "CTRL+X", "ALT+1", "CTRL+SHIFT+F5" -> KOMBINASYON
    // Kombinasyonda modifier'lar once BASILI TUTULUR, ana tusa tap yapilir,
    // sonra modifier'lar TERS SIRADA birakilir.
    void PressKeyTap(const std::string& key);

    // "Basılı Tut" (kullanici talebi): PressKeyTap ile AYNI tus/kombinasyon
    // sozdizimi ("CTRL+SHIFT+F5" vb.) ama tap yerine tek yonlu bas/birak -
    // down=true: modifier'lari sirayla bas + ana tusa bas (BIRAKMADAN).
    // down=false: ana tusu birak + modifier'lari ters sirada birak.
    void SetAutoKeyHeld(const std::string& key, bool down);

    // Sag tik basili tutup fareyi yatay surukleyerek kamerayi cevirir (kalici).
    // SADECE YATAY - dikey "goz atma" (eski PeekVertical) KULLANICI TALEBIYLE
    // TAMAMEN KALDIRILDI: cok kucuk bir katki icin her aramada ekstra saniyeler
    // harciyordu.
    void RotateView(int dx);

    // OTO NESNE v2: bir yon tusunu (W/A/S/D) BASILI TUTMAYA baslar / birakir -
    // HoldMoveKey'in aksine BLOKLAMAZ, boylece tus basiliyken ayni anda Z
    // spamlanabilir. Cagiran taraf ikisini de esitlemekle yukumludur.
    void MoveKeyDown(char key);
    void MoveKeyUp(char key);

private:
    void SendRelativeMove(int dx, int dy);
    void SendMouseButton(bool down, bool rightButton);
    // OPSIYONEL IKILI GONDERIM (bkz. config.h click_dual_sendinput_enabled,
    // input.cpp icindeki uyari yorumu) - SendMessage'a EK bir SendInput
    // fare-tus olayi, imlecin o anki konumunda.
    void SendMouseButtonRaw(bool down, bool rightButton);
    // INSANCILLASTIR: imleci hedefe yumusakca kaydirir (bkz. Click).
    void GlideCursorTo(int x, int y);
    void SendKey(unsigned short scancode, bool down);
    void DragRelative(int totalDx, int totalDy, double durationMs);

    // Click() ile AYNI kanitlanmis yontem (SetCursorPos + SendMessage) -
    // kamera cevirme/goz atma da GERCEK imlec surukleyerek yapildigi icin
    // (sag tik basili + hareket) bu ikisi kullanilir. m_hasBounds false ise
    // (bagimsiz araclarda SetBounds hic cagrilmamissa) SendInput tabanli
    // guvenlik agi yolu kalir (surucu gerekmez).
    void SendButtonViaMessage(bool down, bool rightButton);
    void DragViaMessage(int totalDx, int totalDy, double durationMs);

    // TIKLAMA YONTEMI (bkz. config.h input_use_postmessage): m_boundsHwnd'ye
    // giden TUM WM_* mesajlarinin TEK gecis noktasi - bayrak false ise
    // SendMessage (senkron, mevcut kanitlanmis davranis), true ise
    // PostMessage (asenkron, kuyruga ekler) kullanir. Boylece iki yontem
    // arasinda gecis TEK bir yerden yapilir, cagiran kod degismez.
    void DispatchMsg(UINT msg, WPARAM wp, LPARAM lp);

    // KRITIK GUVENLIK AGI: Windows'un fare hizlandirma egrisi (pointer
    // ballistics) goreli hareketleri bizim gonderdigimizden FARKLI (bazen
    // daha fazla) piksel kaydirabilir - matematigimiz dogru olsa bile
    // gercek imlec ara sira sinirin disina tasabiliyordu. Bu fonksiyon
    // GERCEK imlec konumunu (GetCursorPos) olcer, pencere sinirlarinin
    // disindaysa aninda duzeltici bir hareketle GERI CEKER - tahmine degil
    // olculen sonuca dayanir, bu yuzden hicbir kaynaktan (hizlanma,
    // yuvarlama hatasi vb.) kaynaklanan sapmayi kacirmaz.
    void ClampCursorToBounds();

    // Verilen bir baslangic noktasindan "delta" kadar gidilirse [minB,maxB]
    // sinirinin disina cikacaksa, sinira tam oturacak sekilde delta'yi kirpar.
    static int ClampDelta(int start, int delta, int minB, int maxB);

    const Config& m_config;

    // KRITIK: SendKey artik SendMessage(WM_KEYDOWN) ile CALISIYOR - bu,
    // GERCEK/global klavye durumunu (GetKeyState) DEGISTIRMEZ, sadece
    // pencereye mesaj yollar. Bu yuzden ShiftRightClick sirasinda "Shift
    // basili mi" bilgisini GetKeyState'ten OKUYAMAYIZ artik - HoldShift()
    // tarafindan guncellenen bu bayraga bakiyoruz (bkz. Click()/
    // SendButtonViaMessage() icindeki MK_SHIFT kontrolu).
    bool m_shiftDown = false;

    DWORD m_lastFocusedPid = 0;
    std::chrono::steady_clock::time_point m_lastFocusedPidAt{};
    int m_lastX = 0;
    int m_lastY = 0;

    HWND m_boundsHwnd = nullptr;
    bool m_hasBounds = false;

    // timeBeginPeriod(1) alindi mi - yikicida SADECE aldiysak birakmak icin
    // (bkz. kurucudaki akicilik aciklamasi).
    bool m_timerResolutionSet = false;
};
