#pragma once
#include <windows.h>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "auto_color_detector.h"
#include "capture.h"
#include "color_detector.h"
#include "config.h"
#include "input.h"
#include "name_label_detector.h"
#include "status_panel_detector.h"
#include "yolo_detector.h"

struct BotStats {
    double session_seconds = 0;
    int total_clicks = 0;
    int targets_found = 0;
    int targets_completed = 0;
    bool is_locked = false;
    bool has_last_detection = false;
    std::string last_detection_name;
    double last_detection_score = 0;
};

// Bot cekirdek mantigi - Python tarafindaki src/bot.py -> MetinBot
// sinifinin C++ karsiligi. Ayrintili aciklama icin bot.py'deki
// docstring'e bakin (farm alani / hedef kilitleme / arama davranisi
// birebir ayni).
class MetinBot {
public:
    explicit MetinBot(const Config& config);
    ~MetinBot();

    // window_title_hint/fallback_hint ile ARAYIP bulunan ilk pencereye
    // baglanir - TEK client senaryosu icin. Basarisiz olursa
    // std::runtime_error firlatir.
    void BindWindow();

    // Aramadan, DOGRUDAN verilen pencereye baglanir - COKLU client
    // senaryosunda her MetinBot'un HANGI pencereye baglanacagi disaridan
    // (win_window::FindAllGameWindows sonucundan) belirlenir, aksi halde
    // hepsi ayni (ilk bulunan) pencereye baglanmaya calisirdi. Basarisiz
    // olursa std::runtime_error firlatir.
    void BindWindow(HWND hwnd);

    // Tek bir tarama adimi. Yeni hedef bulunup burst tiklama yapildiysa true doner.
    bool Step();

    void Stop();

    BotStats GetStats() const;

    // "Sure" istatistigi SADECE bot calisirken (running=true) ilerlesin,
    // duraklatilinca donsun diye disaridan (web_main.cpp) cagirilir.
    void SetRunning(bool running);

    // COKLU CLIENT: sira BASKA bir client'a gecmeden HEMEN once, bu bot'un
    // fare hapsini (ClipCursor) acikca birakir - aksi halde yeni client
    // on plana gelip onun ilk Step()'i kendi hapsini kurana kadar (cok
    // kisa ama sifir olmayan bir sure) fare bir onceki pencerenin eski
    // sinirinda kilitli kalabilirdi. Bu, gecisi tertemiz/aninda yapar.
    void ReleaseMouseClip();

    // COKLU CLIENT: bu bot'un penceresini on plana getirir - sira bu
    // client'a gecince (bkz. web_main.cpp BotWorker) cagirilir. Boylece
    // fare/klavye o an SADECE bu pencereye etki eder (Step() zaten
    // odaklanmamis pencerelere HICBIR SEY yapmiyor - bkz. GetForegroundWindow
    // kontrolu), diger client'lar dokunulmadan beklemeye devam eder.
    void Activate();

    // COKLU CLIENT: bu bot'un aktif hedefi (metin kesildi/yanlis hedef/
    // takildi/guvenlik zaman asimi - herhangi bir sebeple) SU STEP'TE
    // biraktigini bildirir - BotWorker bunu "sirada digger client'a gec"
    // sinyali olarak kullanir. Okundugunda bayrak otomatik sifirlanir.
    bool ConsumeTargetEndedFlag();

    HWND Hwnd() const { return m_hwnd; }
    // Pencere basliginin ("Misali2 (Feda) | KarakterAdi") "|" sonrasindan
    // cikarilan karakter adi - bkz. bot.cpp ExtractCharacterName().
    const std::string& CharacterName() const { return m_characterName; }
    bool HasTarget() const { return m_target.has_value(); }

    // COKLU CLIENT ZAMAN PAYLASIMI: bu client hedefe KILITLIYKEN ama ODAKTA
    // DEGILKEN (baska bir client calisirken) cagrilir. Kilitli metnin kesimi
    // oyunda OTOMATIK devam ettigi (odak/mouse gerektirmedigi) icin, sadece
    // WGC capture + HP cubugu okumasiyla (HICBIR input yapmadan, on planda
    // olmaya GEREK duymadan) "kesim bitti mi" kontrol eder. Bittiyse hedefi
    // serbest birakir (bu client "arama" moduna gecer -> planlayici ona odagi
    // verir). Tam Step()'teki takilma/anti-takilma/kamera gibi INPUT gerektiren
    // adimlar burada YAPILMAZ - onlar client ODAKTAYKEN normal Step() ile
    // calisir. true doner: kesim bitti (hedef serbest birakildi).
    bool PollUnfocusedCut();

    // OTO BASMA: planlayici (NativeSession) bir slot'un suresi dolunca AKTIF
    // (odakta) client'in bu metoduyla ilgili tusu bastirir. Sadece bir tus
    // tap'i - bot'un farm mantigina DOKUNMAZ.
    void PressAutoKey(const std::string& key) {
        if (m_input) m_input->PressKeyTap(key);
    }

    // OTO BASMA - "Basılı Tut" (kullanici talebi): sure girmeden, tus SUREKLI
    // basili tutulsun istendiginde tap yerine bu cagrilir. down=true ise
    // basar (cagiran taraf, oyunun otomatik-tekrari fark etmesi icin
    // periyodik olarak TEKRAR true ile cagirmali - bkz. NativeSession run
    // loop), down=false ise birakir.
    void SetAutoKeyHeld(const std::string& key, bool down) {
        if (m_input) m_input->SetAutoKeyHeld(key, down);
    }

    // NESNE MARKET KAPAT (kullanici talebi): "Nesne Market" bildirim seridi
    // ekranda GORULURSE ESC basarak kapatir. Step()'ten HER KEREDE
    // cagirilir ama ICERIDE periyodik olarak calisir (bkz. config.h
    // nesne_market_check_interval_sec) - zaten yakalanmis kareyi kullanir,
    // EK bir capture YAPMAZ.
    void CheckAndDismissNesneMarket(const cv::Mat& frame);
    // Durum ekraninda "bulunan PID" kisa sureligine gosterilebilsin diye
    // (kullanici talebi) - bkz. InputController::LastFocusedPid.
    DWORD LastFocusedPid() const { return m_input ? m_input->LastFocusedPid() : 0; }
    double LastFocusedPidAgeSec() const { return m_input ? m_input->LastFocusedPidAgeSec() : -1.0; }
    // GERCEK aktif tespit yontemi isim-etiketi eslesmesidir (bkz.
    // DetectInFarmZone) - ORB tabanli eski yontem tamamen kaldirildigi icin
    // artik buraya BAKILMIYOR (dead code'du, kaldirildi).
    bool HasTemplates() const { return m_nameLabelDetector.HasTemplates() || m_colorDetector.IsReady(); }

    // MANUEL TARAMA durum yazisi (web paneli).
    const std::string& ManualStatusText() const { return m_manualStatusText; }

    // DISCORD /goruntu KOMUTU: bu client'in su anki ekran goruntusunu
    // (odakli olmasi/onplan gerekmez, WGC yakalama zaten odaksiz calisir)
    // dogrudan dondurur - salt-okunur, hicbir tiklama/pencere degisikligi
    // YAPMAZ.
    std::optional<cv::Mat> CaptureFrame() const {
        if (!m_capture) return std::nullopt;
        return m_capture->GetFrame();
    }

private:
    struct Target {
        // KILITLI hedefin YOLO kategorisi. Tum modeller donusumlu (round-robin)
        // calistigi icin (bkz. bot.cpp DetectInFarmZone), hedef kilitliyken
        // IZLEMENIN kopmamasi adina HER KAREDE bu kategori calistirilir.
        std::string category;
        cv::Point2d pos;
        std::chrono::steady_clock::time_point lastSeen;
        std::chrono::steady_clock::time_point lockedAt;
        // Ust bilgi panelinde en az bir kez DOGRU (isim eslesen) panel
        // gorulduyse true - panel sonradan kaybolursa bu "metin kesildi"
        // anlamina gelir, hicbir zaman gorulmediyse "yanlis hedef" anlamina gelir.
        bool everSawValidPanel = false;
        // Isim'in EN SON gorundugu an - kaybolduktan sonra "kesildi" sayilmadan
        // once bir sure daha (status_panel_lost_grace_sec) beklenir, tek bir
        // karelik gecici kayip yanlislikla "kesildi" saymasin diye.
        std::chrono::steady_clock::time_point lastValidPanelSeen;
        // Panelin KESINTISIZ gorunur oldugu ilk an - "takilma" korumasi
        // (stuck_panel_timeout_sec) icin kullanilir. Panel kaybolup
        // (status_panel_lost_grace_sec toleransini asarak) tekrar gorununce
        // bu SIFIRLANIR (yeni bir "kesintisiz gorunme" donemi baslar).
        std::optional<std::chrono::steady_clock::time_point> panelFirstVisibleAt;
        // KORUMA (kullanici talebi: "metin kesilmeden ASLA baska bir seye
        // gitmemeli"): panel hic esigi gecmemis olsa bile, en yuksek
        // korelasyon skoru esige COK YAKINSA (sablon aslinda dogru ama
        // zayif/marjinal eslesiyor olabilir - ör. golge metninin soluk
        // render edilmesi), "yanlis hedef" kararini bir kez ertelemek icin
        // kullanilir - boylece TEK bir zayif okuma yuzunden gercek bir
        // metin erken/hatali terk edilmez. Bkz. bot.cpp wrongTarget mantigi.
        bool nearMissExtended = false;
        // AYNI koruma, "kesildi" (cutConfirmed) kararı için - panel bir kez
        // GERÇEKTEN doğrulanmış hedefte (everSawValidPanel=true) bile,
        // kaybolduktan sonra en yüksek skor eşiğe yakın kalıyorsa (tam sıfır
        // değil - yani muhtemelen hâlâ oradayız ama marjinal okunuyor),
        // "kesildi" kararını bir kez ertelemek için kullanılır. nearMissExtended
        // ile AYRI tutulur çünkü appear-phase (hiç görülmedi) ve lost-phase
        // (görülüp kayboldu) birbirinden bağımsız, aynı hedefte İKİSİ de
        // birer kez tetiklenebilmeli.
        bool lostNearMissExtended = false;
        // OTO NESNE (kullanici talebi: "metin kesilir kesilmez BASLAMALI"):
        // toplama dizisi bu hedef icin CALISTI mi. Toplama artik "kesildi"
        // KARARINI beklemiyor (o karar guvenlik icin panelin
        // status_panel_lost_grace_sec kadar KESINTISIZ kayip olmasini sartt
        // kosar - gozle gorulur bir gecikme); panel kaybolur kaybolmaz
        // (auto_loot_start_grace_sec) baslatilip bu bayrakla TEK SEFERE
        // indirgeniyor - kesim karari o sirada zaten arka planda olgunlasiyor.
        bool lootStarted = false;

        // A-T PRO (bkz. config.h at_pro_*): EN SON ONAYLANMIS ILERLEME anindaki
        // TABAN degerler. DIKKAT - bunlar "simdiye kadarki en iyi" DEGILDIR:
        // once oyleydi ve bu bir CIRCIR (ratchet) hatasi yaratiyordu - kutu her
        // minicik buyudugunde taban da yukseltildigi icin kademeli buyume asla
        // birikip esigi gecemiyor, dolayisiyla karakter metne NORMAL yururken
        // bile "ilerleme yok" saniliyordu. Taban artik SADECE gercek bir
        // ilerleme onaylandiginda (ya da gorunum ciddi degistiginde) guncellenir.
        double atProBaseDist = -1.0;
        double atProBaseBoxH = 0.0;
        // En son GERCEK ilerlemenin (yaklasmanin) yasandigi an - takilma
        // sayaci bunun uzerinden isler.
        std::chrono::steady_clock::time_point atProLastProgressAt{};
        // Kutu "vardik" esigini gectiyse (metin ekranda buyudu) bu hedef icin
        // A-T Pro TAMAMEN kapanir - karakter metne varmis, vuruyor demektir.
        bool atProArrived = false;
    };

    static std::optional<Detection> ClosestTo(const std::vector<Detection>& detections, cv::Point2d pos, double maxDist);
    bool InFarmZone(cv::Point2d pos, cv::Point2d anchor) const;
    // Panel (isim) sablonu yuklu ve burst boyunca HIC cikmadiysa false doner
    // (yanlis hedef - kilitlenmemeli). Panel cikar cikmaz burst hemen kesilir
    // (karakter zaten hedefe gidiyor, fazladan tiklamak gereksiz/manasiz).
    bool ClickBurst(const Detection& detection, int frameW, int frameH);

    // OTO NESNE v2: 1sn W + 1sn S yururken KESINTISIZ Z spamlar (esyalar
    // karakterin onune/arkasina dusmus olabilir). Bloklar (~2sn) ve kac kez
    // Z basildigini dondurur. auto_loot_enabled kapaliysa cagrilmaz.
    int RunAutoLoot();

    void SearchAction();

    // ANTI-TAKILMA kurtarma dizisi: kamerayi tam ters yone cevir -> (pressEsc
    // ise) ESC ile hedefi birak -> 4'er saniye W/S/D/A -> orta alana rastgele
    // sol tiklar. Bkz. config.h anti_stuck_* yorumu.
    //
    // pressEsc=false SADECE ikinci tetikleyici (anti_stuck_no_hp_seconds -
    // "hic HP cubugu gorulmedi") icin kullanilir: bu tetikleyici SADECE hedef
    // KILITLI DEGILKEN (arama modunda) calisir (bkz. bot.cpp cagri noktasi),
    // yani ESC ile birakilacak bir hedef ZATEN yok - ESC basmak burada
    // anlamsiz/gereksiz (kullanici talebi: bu durumda ESC'ye BASILMAMALI).
    // Birinci tetikleyici (ayni HP cubugu uzun suredir ekranda - hedef
    // KILITLI) icin pressEsc HER ZAMAN true kalir, orada ESC gercekten
    // kilitli hedefi birakmak icin gerekli.
    void AntiStuckRecover(bool pressEsc = true);

    // A-T PRO (bkz. config.h at_pro_*): kilitli hedefin O ANKI olcumu - taze
    // bir kare alip hedefi yeniden bulur, merkeze olan mesafesini ve tespit
    // kutusunun alanini/yuksekligini dondurur. Hedef o karede bulunamazsa
    // nullopt (cagiran taraf bunu "ilerleme yok" DEGIL, "bilgi yok" sayar).
    struct AtProSample {
        double dist = 0.0;    // merkez nokta -> hedef nokta (piksel)
        double boxArea = 0.0;
        double boxH = 0.0;
        double frameH = 0.0;
    };
    std::optional<AtProSample> AtProSampleTarget();

    // A-T PRO kademeli kurtarma. FAZ 1: rastgele A/D basimlari. FAZ 2: ESC ->
    // kamera cevir -> merkeze 2-3 sol tik. KRITIK (kullanici talebi): her iki
    // fazda da yaklasma tespit edilir edilmez ANINDA cikar, sureyi doldurmayi
    // beklemez. true doner: kurtarma faz 2'ye kadar gitti ve hedef BIRAKILMALI
    // (arama moduna donulmeli). false doner: yaklasma basladi, hedef KORUNUR.
    bool AtProRecover();

    void BindWindowCore(HWND hwnd);

    // Kare-uzayindaki bir noktayi (piksel), pencerenin GERCEK ekran
    // sinirlarina ORANTILI OLCEKLEME ile ekran koordinatina cevirir - hem
    // ClickBurst hem QueueOtherVisibleMetins AYNI formulu kullanir (bkz.
    // ClickBurst yorumu - DPI/kenarlik farklarina karsi).
    POINT FrameToScreenPoint(cv::Point2d framePt, int frameW, int frameH) const;

    // MANUEL TARAMA (bkz. config.h manual_scan_*): kullanicinin kirptigi
    // sablonu karede arar, bulursa ofsetli noktaya tiklar. true donerse bu
    // adimda normal metin/farm mantigina GIRILMEZ.
    bool ManualScanStep(const cv::Mat& frame);

    // Step()'in basindaki ORB+AutoColor tespit + farm-alani filtreleme
    // mantiginin ORTAK (Step() ve QueueOtherVisibleMetins tarafindan
    // paylasilan) hali - davranis Step() ile BIREBIR ayni kalsin diye
    // buraya cikarildi.
    std::vector<Detection> DetectInFarmZone(const cv::Mat& frame, cv::Point2d anchor,
                                             std::chrono::steady_clock::time_point now) const;

    // SIRAYA EKLEME: birincil hedef kilitlendikten (ClickBurst basarili)
    // HEMEN sonra, kadrajda GORUNEN BASKA metinler varsa (yani zaten
    // arastirilmayan/kilitlenmemis olanlar), her birine TEK bir SHIFT+SAG
    // TIK atarak oyunun KENDI hedef kuyruguna ekler - bu sunucuda ayni
    // eylemi bir hedefe IKINCI kez uygulamak onu kuyruktan CIKARDIGI icin
    // (bkz. config.h queue_spot_* yorumu), her nokta SADECE BIR KEZ
    // (queue_spot_expire_sec suresince) kuyruklanir. Coklu client
    // senaryosunda bu, "diger cliente gecmeden hemen once" gerceklesmis
    // olur - cunku Step() true donunce (bu fonksiyon zaten cagrilmis
    // olacagindan) disaridaki round-robin dongusu HEMEN baska bir client'a
    // geciyor (bkz. desktop_app_main.cpp/web_main.cpp RunLoop).
    void QueueOtherVisibleMetins(const Detection& primary);
    bool IsQueuedSpot(cv::Point2d pos, std::chrono::steady_clock::time_point now) const;

    const Config& m_config;
    HWND m_hwnd = nullptr;
    std::string m_originalTitle;
    std::string m_characterName;
    std::unique_ptr<WindowCapture> m_capture;
    std::unique_ptr<InputController> m_input;
    AutoColorDetector m_colorDetector;       // ek yontem: sablonlardan cikarilan renk - "kesin bulma" icin
    NameLabelDetector m_nameLabelDetector;   // ek yontem: tasin ustundeki yuzen isim yazisi - acidan bagimsiz
    // YOLO TABANLI TESPIT: config.h yolo_models listesindeki HER giris icin
    // (bir kategori = bir egitilmis model) ayri bir YoloDetector - dosyasi
    // bulunamayan/yuklenemeyen girisler IsReady()==false kalir, sessizce
    // atlanir (bkz. bot.cpp constructor + DetectInFarmZone).
    std::vector<YoloDetector> m_yoloDetectors;
    StatusPanelDetector m_panelDetector;     // cift dogrulama: ust bilgi paneli (isim + can barina gore)
    // MANUEL TARAMA durumu: sablon bir kez yuklenir (her karede diskten
    // okumamak icin), son tiklama zamani ile tiklama araligi korunur.
    // COKLU SABLON (kullanici talebi: "birden fazla cicek"): manual_scan
    // klasorundeki TUM gorseller yuklenir, her karede hepsi aranir ve EN IYI
    // eslesen tiklanir. Klasor imzasi (dosya adi+boyut+zaman) degisince
    // otomatik yeniden yuklenir - kullanici panelden gorsel ekleyip
    // silerken botu yeniden baslatmasi GEREKMEZ.
    struct ManualTemplate {
        std::string name;
        cv::Mat img;
    };
    std::vector<ManualTemplate> m_manualTemplates;
    std::string m_manualTemplatesSignature;   // klasorun son bilinen imzasi
    std::chrono::steady_clock::time_point m_lastManualReloadCheckAt{};
    // Hedefin ne zamandir bulunamadigi - bu sure asilinca kamera RASTGELE
    // saga/sola cevrilir (bkz. config.h manual_scan_rotate_after_sec).
    std::chrono::steady_clock::time_point m_manualNoHitSince{};
    std::chrono::steady_clock::time_point m_lastManualClickAt{};
    std::chrono::steady_clock::time_point m_lastManualLogAt{};
    std::string m_manualStatusText;

    // Yere dusen esyalari (loot) KACIRMAMAK icin, bot calisirken (bu
    // client aktif/foreground oldugu her Step() cagrisinda) periyodik
    // olarak Z tusuna basilir - hedef kilitli olsun olmasin, kesintisiz.
    // bkz. Step() basi ve config.h loot_key_interval_sec.
    std::chrono::steady_clock::time_point m_lastLootKeyAt{};

    std::optional<Detection> m_lastDetection;
    std::optional<Target> m_target;
    std::chrono::steady_clock::time_point m_nextSearchAt{};

    // IKINCI ANTI-TAKILMA TETIKLEYICISI (bkz. config.h anti_stuck_no_hp_seconds):
    // HP cubugunun EN SON ne zaman gorulduğu. Hedef kilitliyken zaten her
    // Step()'te panel okunuyor; ARAMA modunda ise seyreltilmis olarak
    // (m_lastSearchPanelCheckAt) okunur - "hic HP cubugu gormeden gecen sure"
    // yalnizca boyle guvenilir olcülebilir.
    std::chrono::steady_clock::time_point m_lastHpBarSeenAt{};
    std::chrono::steady_clock::time_point m_lastSearchPanelCheckAt{};

    // KARARLILIK KAPISI: ilk goruldugu anda hemen tiklamak yerine, ayni
    // noktanin art arda birkac karede DOGRULANMASINI bekliyoruz - kamera
    // henuz oturmamisken alinan gurultulu/kararsiz bir tespide gore kotu
    // bir yere tiklamayi onler (once tikla-sonra-duzelt yerine).
    std::optional<cv::Point2d> m_pendingPoint;
    int m_pendingStableCount = 0;

    // REDDEDILEN BOLGELER: burst'un tamami atildigi halde panel hic
    // cikmayan (yanlis hedef - dag/tas vb.) ekran konumlari, tekrar
    // secilmesin diye bir sure burada tutulur (bkz. config.h
    // reject_spot_radius_px / reject_spot_expire_sec).
    struct RejectedSpot {
        cv::Point2d pos;
        std::chrono::steady_clock::time_point rejectedAt;
    };
    std::vector<RejectedSpot> m_rejectedSpots;
    bool IsRejectedSpot(cv::Point2d pos, std::chrono::steady_clock::time_point now) const;

    // SIRAYA EKLENEN BOLGELER: bkz. QueueOtherVisibleMetins() - bir noktaya
    // zaten shift+sag-tik ile kuyruk komutu gonderildiyse, queue_spot_expire_sec
    // suresince buraya kaydedilir ki AYNI noktaya TEKRAR gonderilmesin (aksi
    // halde ikinci shift+sag-tik onu kuyruktan CIKARIRDI).
    struct QueuedSpot {
        cv::Point2d pos;
        std::chrono::steady_clock::time_point queuedAt;
    };
    std::vector<QueuedSpot> m_queuedSpots;

    bool m_sessionStarted = false;
    std::chrono::steady_clock::time_point m_sessionStart{};

    // "Sure" sadece calisirken ilerlesin diye: duraklatilan surelerin
    // toplami biriktirilir, GetStats() bunu toplam gecen sureden dusurur.
    bool m_running = false;
    std::chrono::steady_clock::time_point m_lastResumeAt{};
    double m_activeSeconds = 0;

    int m_totalClicks = 0;
    int m_targetsFound = 0;
    int m_targetsCompleted = 0;

    // COKLU CLIENT gecis sinyali - bkz. ConsumeTargetEndedFlag().
    bool m_targetEndedFlag = false;

    // TANI (diagnostic): pencere odakli olmadigi icin Step()'in "hicbir sey
    // yapmadan" cikmasi COK SIK olabilir (COKLU CLIENT'ta pasif bekleyen
    // client icin normal) - her seferinde loglamak yerine en fazla birkac
    // saniyede bir loglayip webapp_log.txt'i bogmadan tani koymayi saglar.
    std::chrono::steady_clock::time_point m_lastNotForegroundLog{};

    // ARAMA (kamera cevirme) durumu: pervane gibi surekli ayni yone donmemek
    // icin son yonu ve ard arda ayni yone kac kez donuldugunu tutar - bkz.
    // SearchAction().
    int m_lastSearchDir = 0;        // -1 sol, +1 sag, 0 henuz donmedi
    int m_searchSameDirCount = 0;   // ard arda ayni yone donme sayisi

    // TANI: capture bos kare donerse (m_capture->GetFrame() == nullopt) veya
    // aktifken hicbir tespit bulunmuyorsa (surekli "araniyor" durumu) bunu
    // ayni sekilde birkac saniyede bir loglar - "hicbir sey olmuyor"
    // sikayetlerinde HANGI asamada takildigini (pencere odaksiz mi, kare hic
    // gelmiyor mu, kare geliyor ama tespit sifir mi) ayirt etmek icin.
    std::chrono::steady_clock::time_point m_lastCaptureFailLog{};
    std::chrono::steady_clock::time_point m_lastIdleSearchLog{};

    // A-T PRO tani logu seyreltmesi (bkz. Step) - saniyede bir yazilir.
    std::chrono::steady_clock::time_point m_lastAtProLogAt{};

    // NESNE MARKET: sablon TEMBEL yuklenir. Kontrol
    // ve ESC-cooldown zamanlari periyodik seyreltme icin ayri tutulur.
    bool m_nesneMarketTemplateLoaded = false;
    cv::Mat m_nesneMarketTemplateBgr;
    cv::Mat m_nesneMarketTemplateMask;
    std::chrono::steady_clock::time_point m_lastNesneMarketCheckAt{};
    std::chrono::steady_clock::time_point m_lastNesneMarketEscAt{};

    // SUREKLI KUYRUKLAMA: kesim sirasinda yapilan periyodik taramanin son
    // zamani (bkz. config.h queue_scan_interval_sec).
    std::chrono::steady_clock::time_point m_lastQueueScanAt{};

    // COZUNURLUKTEN BAGIMSIZ ESIKLER (kullanici talebi: "her cozunurlukte
    // sorunsuz olmali"): InFarmZone/IsRejectedSpot/IsQueuedSpot gibi kare
    // parametresi ALMAYAN yardimci fonksiyonlarin, yine de "kare
    // YUKSEKLIGI/GENISLIGININ orani" seklinde tutulan esikleri (bkz. config.h
    // detection_stable_radius_ratio yorumu) piksele cevirebilmesi icin, EN
    // SON yakalanan karenin boyutu burada onbelleklenir - Step() ve digerleri
    // yeni bir kare aldiklarinda gunceller (bkz. UpdateFrameSize).
    // mutable: DetectInFarmZone gibi const metotlar da, ellerindeki TAZE
    // karenin boyutuyla bu onbellegi guncelleyebilsin diye (salt-okunur bir
    // metotta "en son gorulen kare boyutu" onbellegini tazelemek, metodun
    // mantiksal const'ligini bozmaz - disaridan gozlemlenebilir bir durum
    // degistirmiyor).
    // Tum metin modelleri donusumlu calisir - bkz. DetectInFarmZone.
    mutable size_t m_yoloRoundRobin = 0;

    // TESPIT HAFIZASI (kullanici talebi: "tespiti guclendir").
    // Modeller donusumlu calistigi icin TEK bir kare yalnizca BIR kategoriyi
    // gorur. Bu haliyle "bu karede tespit yok" karari (bkz. bot.cpp
    // detections.empty() -> SearchAction) kamerayi, BASKA bir kategoriden tam
    // karsidaki bir metinden UZAGA cevirebilirdi. Metinler SABIT nesneler
    // oldugu icin, son kamera hareketinden bu yana taranmis TUM kategorilerin
    // sonuclari birlestirilerek dondurulur - yani bot pratikte TUM modelleri
    // ayni anda gormus gibi davranir, maliyet ise tek model kadar kalir.
    struct CategoryDets {
        std::string category;
        std::vector<Detection> dets;
        std::chrono::steady_clock::time_point at;
    };
    mutable std::vector<CategoryDets> m_detMemory;
    // Son kamera hareketinden bu yana TARANMIS kategoriler. Bot, HEPSI
    // taranmadan "burada metin yok" deyip kamerayi cevirmemeli (bkz.
    // bot.cpp: aksi halde tam karsidaki bir metin, o an sirasi gelmemis bir
    // modele ait oldugu icin GORULMEDEN geride birakiliyordu).
    mutable std::vector<std::string> m_scannedSinceRotate;
    mutable std::chrono::steady_clock::time_point m_lastRotateAt{};
    // Yuklu TUM kategoriler en az bir kez tarandi mi?
    bool AllCategoriesScanned() const {
        size_t readyCount = 0;
        for (const auto& y : m_yoloDetectors) {
            if (y.IsReady()) ++readyCount;
        }
        return readyCount == 0 || m_scannedSinceRotate.size() >= readyCount;
    }
    // Kamera dondugunde ya da karakter yer degistirdiginde hafiza GECERSIZDIR.
    void ClearDetectionMemory() const {
        m_detMemory.clear();
        m_scannedSinceRotate.clear();
        m_lastRotateAt = std::chrono::steady_clock::now();
    }

    mutable int m_frameW = 800;
    mutable int m_frameH = 600;
    void UpdateFrameSize(const cv::Mat& frame) const {
        if (frame.cols > 0) m_frameW = frame.cols;
        if (frame.rows > 0) m_frameH = frame.rows;
    }
    double RadiusPxX(double ratio) const { return m_frameW * ratio; }
    double RadiusPxY(double ratio) const { return m_frameH * ratio; }
};
