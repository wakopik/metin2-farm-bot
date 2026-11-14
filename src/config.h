#pragma once
#include <string>
#include <vector>

// Tek bir metin turune (ornegin "Savas Metni") ait isimlendirilmis HSV
// renk araligi. color_picker.exe ile toplanir, config.json -> color_profiles
// altinda saklanir.
struct ColorProfile {
    std::string name;
    int hsv_lower[3] = {0, 0, 0};
    int hsv_upper[3] = {179, 255, 255};
};

// Metin2 Farm Botu - ayarlar. config.json'dan yuklenir, Python
// projesindeki src/config.py ile bire bir ayni degerleri tasir.
struct Config {
    std::string window_title_hint = "Misali2";
    // window_title_hint bulunamazsa denenecek ikinci ipucu - oyun istemcisi
    // yeniden acilip basligi kendi orijinal ismine donduyse (bizim
    // yeniden adlandirmamiz kalici degil) bu sayede yine bulunur.
    std::string window_title_fallback_hint;

    // DISCORD / TELEGRAM ENTEGRASYONU (istege bagli): kendi Discord
    // uygulamanizi (Developer Portal) ve/veya Telegram botunuzu (@BotFather)
    // olusturup bilgileri config.json'a yazin. Bos birakilan entegrasyon
    // TAMAMEN devre disi kalir (bkz. desktop_app_main.cpp /api/discord/*,
    // /api/telegram/*). Token'larinizi ASLA repoya commit etmeyin.
    std::string discord_client_id;
    std::string discord_client_secret;
    std::string discord_bot_token;
    std::string telegram_bot_token;
    // RICH PRESENCE (toplam farmlayan kisi sayisi): tanimliysa, botun kendi
    // Discord durum yazisi ("Toplam N Kisi Farmliyor!") TUM calisan .exe
    // kopyalarinin (arkadaslar dahil) toplam client sayisini gosterir - bkz.
    // desktop_app_main.cpp PostFarmStatus/FetchAndBroadcastFarmPresence.
    // Bu SADECE Discord'un kendi altyapisini (paylasilan bir metin kanali)
    // kullanir - AYRI bir sunucu/backend KURULMAZ.
    std::string discord_status_channel_id;

    // Arayuzdeki "Discord Sunucusuna Git" butonu bu adresi acar (bkz.
    // /api/update/open-discord) - bos birakilirsa buton pasif kalir.
    std::string discord_invite_url;

    // Renk (HSV) tabanli tespit - artik AKTIF DEGIL (gorsel/template tespite
    // gecildi), ama kod duruyor - istenirse geri donulebilir.
    std::vector<ColorProfile> color_profiles;
    double color_min_area = 200;
    double color_max_area = 25000;
    double color_min_score = 0.60;
    int color_max_width = 130;
    int color_max_height = 130;

    // Gorsel (template) tabanli tespit - templates/ klasorundeki PNG'ler
    // (template_picker.exe ile toplanir). KRITIK: performans icin arama
    // SADECE farm alani icinde yapilir, tum ekranda degil.
    // BIRDEN FAZLA metin turu AYNI ANDA secilebilir (ör. hem "Dövüş Metni"
    // hem "Savaş Metni") - bu durumda TUM klasorlerdeki sablonlar BIRLIKTE
    // yuklenir, hangisi once bulunursa ona tiklanir (ORB/AutoColor/panel
    // dogrulamasi HER klasor icin ayri ayri calisir, sonuclar birlestirilir).
    std::vector<std::string> templates_dirs = {"templates"};

    // HP CUBUGU TABANLI KILITLENME DOGRULAMASI (isim-yazisi metin
    // eslestirmesinin YERINE gecti - bkz. status_panel_detector.cpp):
    // hedefe tiklaninca ustte cikan HP CUBUGU, hangi metin/canavar turune
    // tiklanirsa tiklansin AYNI gorunumdedir - bu yuzden KATEGORIYE GORE
    // DEGIL, TEK bir ortak referansla calisir (template_picker.exe 'h' modu
    // ile <hp_bar_dir>/_hpbar.png olarak kaydedilir). Font/olcek/parlaklik
    // farkina karsi (renk-blogu eslestirmesi oldugu icin) isim-yazisi
    // yontemine gore COK daha dayaniklidir - bkz. kullanicinin PHANTOM
    // botunun dokumantasyonundan aktardigi tasarim.
    std::string hp_bar_dir = "hp_bar";

    // HP CUBUGU ARAMA BOLGESI AYARI ARTIK YOK - bkz. status_panel_detector.cpp.
    // Once PHANTOM referansindan alinan sabit oranlar, sonra kullanicinin
    // cizdigi kutunun orani kullaniliyordu; ikisi de arayuzun pencereyle
    // ORANTILI buyudugu VARSAYIMINA dayaniyordu ve bu varsayim OLCUMLE YANLIS
    // bulundu (arayuz SABIT PIKSEL; HP cubugu 802x632 ve 1602x1049 karelerin
    // IKISINDE de 129x10 px, olcek 1.0, y=56). Artik cubuk ust-orta bantta
    // ARANARAK bulunuyor - hicbir bolge kalibrasyonu gerekmez.

    // ============ OTOMATIK PENCERE KALIBRASYONU (kullanici talebi) ============
    // "Sistem kendini oyun penceresine otomatik kalibre etmeli - her boyutta
    // sorunsuz olmali... cozunurluk degisince template_picker ile yeniden
    // almadan da stabil calismali."
    //
    // SORUNUN KOKU (KANITLANDI - kod incelemesi, IKI AYRI TUR HATA):
    // 1) HP cubugu, eldiven esyasi, Nesne Market seridi gibi sablon
    //    eslestirmeleri sabit ve DAR olcek araliklari deniyordu.
    // 2) Eldiven/Nesne Market icin bir ARA cozum olarak "sablon HANGI kare
    //    yuksekliginde yakalandiysa, o anki kare yuksekligiyle ORANTILI bir
    //    TEK olcek tahmin edip +-%10 bandinda ara" yontemi denendi - ama bu,
    //    oyun arayuzunun pencere boyutuyla DOGRU ORANTILI buyudugu
    //    VARSAYIMINA dayaniyordu. Bu varsayim dogrulanmamisti; cogu eski
    //    oyun istemcisinde arayuz elemanlari SABIT PIKSEL boyutunda kalir.
    //    Yanlissa tahmin tamamen sapar ve dar bant gercek olcegi hic
    //    yakalayamaz - "cozunurluk degisince template_picker ile yeniden
    //    yakalamadan calismiyor" sikayetinin kaynagi buydu.
    //
    // COZUM: can barinda ZATEN KANITLANMIS yontem (bkz.
    // status_panel_detector.cpp kScales) TUM sablon eslestirmelerine
    // uygulandi - tek bir "beklenen" olcek TAHMIN ETMEK yerine SABIT, GENIS
    // bir olcek listesi (bkz. bot.cpp RunEldivenSequence /
    // CheckAndDismissNesneMarket kScales) DOGRUDAN denenir, hangisi tutarsa
    // o kullanilir. Boylece arayuzun pencereyle orantili mi yoksa sabit
    // piksel mi buyudugu ONEMLI DEGIL - kod HER IKI durumda da dogru
    // olcegi kendi kendine bulur, template_picker.exe'yi cozunurluk
    // degistikce YENIDEN CALISTIRMAYA GEREK KALMAZ (sadece ILK kurulumda,
    // sablonu tanimlamak icin bir kez gerekir).

    // OTO NESNE (kullanici talebi): metin KESILDIGI anda (HP cubugu kaybolunca)
    // bot hemen yeni metin aramaya gecmek yerine, 2 saniye boyunca YERINDE
    // KALIP hizli hizli Z'ye basar - boylece yere dusen TUM esyalar toplanir,
    // sonra aramaya gecer. KAPALIYKEN eski/klasik davranis aynen surer: Z
    // zaten Step() basinda loot_key_interval_sec araliklarla SUREKLI basiliyor
    // (bkz. bot.cpp), ekstra 2 saniyelik bekleme YAPILMAZ. Anasayfa ->
    // Ayarlar'dan acilir/kapatilir, /api/start govdesiyle iletilir.
    bool auto_loot_enabled = false;

    // TIKLAMA SEKLI (kullanici talebi - Fonksiyonlar > Tiklama Degistirme):
    // true  = SHIFT + SAG TIK (VARSAYILAN - cogu sunucuda boyle)
    // false = SADECE SOL TIK (Shift yok, sag tik yok)
    // Tek noktadan etkilidir:
    // InputController::ShiftRightClick (bkz. input.cpp) Shift'i bu bayraga
    // gore basar/basmaz - bot.cpp'deki tum tiklama cagri yerleri AYNI kalir.
    bool click_use_shift = true;

    // TIKLAMA YONTEMI (kullanici talebi - Fonksiyonlar > Tiklama Yontemi):
    // false (VARSAYILAN) = mevcut kanitlanmis yontem: SendMessage (senkron) +
    //   klavyede ek SendInput (DirectInput uyumlulugu icin cift gonderim).
    // true  = SADECE PostMessage (asenkron, kuyruga ekler-ve-devam eder):
    //   fare/klavye TUM WM_* mesajlari SendMessage yerine PostMessage ile
    //   gonderilir, klavyedeki ek SendInput ATLANIR. NOT: gercek imlec
    //   hareketi (SetCursorPos) bu bayraktan ETKILENMEZ - fareyle tiklamanin
    //   oyunda GECERLI sayilmasi icin SART oldugu kullanicinin kendi
    //   testleriyle KANITLANMISTI (bkz. postmessage_test_main.cpp,
    //   setcursor_sendmessage_test_main.cpp) - sadece mesaj GONDERIM sekli
    //   (senkron/asenkron) degisir.
    bool input_use_postmessage = false;

    // ---- MANUEL TARAMA (kullanici talebi) --------------------------------
    // Metin modellerini (YOLO) TAMAMEN atlayip, kullanicinin kendi kirptigi
    // KUCUK bir gorseli (ör. bir cicegin isim etiketi) ekranda arar; bulunca
    // eslesmenin merkezine gore verilen OFSET kadar kaydirip tiklar. Ofset
    // gerekli cunku aranan sey genelde ISIM ETIKETI, tiklanmasi gereken sey
    // ise onun BIRAZ ALTINDAKI nesnedir.
    bool manual_scan_enabled = false;
    // Kirpma araciyla kaydedilen sablonlarin KLASORU. Icindeki TUM gorseller
    // (png/jpg/bmp) sablon sayilir - kullanici birden fazla hedef (ör. birkac
    // farkli cicek) ekleyebilir; her karede hepsi aranip EN IYI eslesme
    // tiklanir. Dosyalar diskte durdugu icin uygulama kapanip acilinca
    // secimler KORUNUR.
    std::string manual_scan_dir = "manual_scan";
    // TIKLAMA: manuel taramada HER ZAMAN SOL TIK - ofset/tik-turu ayarlari
    // kullanici talebiyle kaldirildi (eslesmenin MERKEZINE tiklanir).
    // Hedef bu kadar sure bulunamazsa kamerayi cevirip yeni alana bak.
    double manual_scan_rotate_after_sec = 1.5;
    // Sablon eslestirme guven esigi (cv::TM_CCOEFF_NORMED, 0..1). Cok
    // dusuk olursa yanlis yere tiklar, cok yuksek olursa hic bulamaz.
    double manual_scan_threshold = 0.75;
    // Iki tiklama arasindaki en kisa sure - ayni hedefe saniyede onlarca kez
    // tiklamayi engeller.
    double manual_scan_click_interval_sec = 1.5;

    // OTO NESNE - ERKEN BASLATMA ESIGI (bkz. bot.cpp): toplama dizisi,
    // "kesildi" kararini (status_panel_lost_grace_sec) BEKLEMEDEN, HP cubugu
    // kaybolduktan bu kadar saniye sonra baslar. Tek karelik titremeleri
    // (vurus efekti/capture titremesi) elemeye yetecek kadar kucuk, ama
    // "keser kesmez basla" hissini verecek kadar kisa. 0 yapilirsa ilk kayip
    // karesinde baslar (titremelere karsi korumasiz).
    double auto_loot_start_grace_sec = 0.35;

    // ANTI-TAKILMA (kullanici talebi): AYNI HP cubugu KESINTISIZ olarak
    // anti_stuck_seconds'tan (varsayilan 2.5 dakika) uzun suredir ekrandaysa
    // ve metin hala kirilmadiysa, karakter bir yere takilmis demektir (ör.
    // agac/kaya arkasina sikismis, hedefe yol bulamiyor). Sayac HER YENI HP
    // cubugunda SIFIRDAN baslar (bkz. Target::panelFirstVisibleAt - her yeni
    // hedefte yeni bir Target olusuyor). Tetiklenince bot sirasiyla: kamerayi
    // TAM TERS yone cevirir -> ESC ile hedefi birakir (HP cubugu kapansin,
    // arama moduna gecebilsin) -> 4'er saniye W, S, D, A basarak kendini
    // sikistigi yerden kurtarmaya calisir. Kapaliyken eski/klasik davranis
    // surer (stuck_panel_timeout_sec ile 60sn'de hedefi birakir).
    bool anti_stuck_enabled = false;
    double anti_stuck_seconds = 150.0;  // 2.5 dakika
    // KULLANICI GERI BILDIRIMI: 4-5sn W sonra 4-5sn S basmak karakteri TAM
    // OLARAK takildigi yere geri getiriyordu (W ve S birbirini goturur) ve
    // arada ekran neredeyse tam tur donduruluyordu - hem zaman kaybi hem
    // faydasiz. Artik her yon 2 saniye ve sira W'den sonra ONCE YANLARA
    // (A/D) gecer (bkz. bot.cpp AntiStuckRecover) - yanal hareket gercekten
    // yer degistirdigi icin geri donus sorunu ortadan kalkar.
    double anti_stuck_move_seconds = 2.0;  // her yon tusu icin basili tutma suresi

    // IKINCI ANTI-TAKILMA TETIKLEYICISI (kullanicinin canli gozlemi): yukaridaki
    // tetikleyici HP cubugunun GORUNUYOR olmasina dayanir. Ama gercekte cok
    // daha sinsi bir takilma sekli var: karakter bir metne dogru yuruyup
    // takiliyor AMA HP cubugu HIC ACILMIYOR (ör. arada puzzle/baska bir olay
    // araya girip hedef sirasi bozuluyor). O halde bot "arama modunda"
    // sanip baska metinlere tiklamaya devam eder, karakter takili oldugu icin
    // hicbirine ULASAMAZ ve HP cubugu asla acilmadigi icin BIRINCI tetikleyici
    // OMRU BOYUNCA calismaz - bot SONSUZA KADAR takili kalir.
    // Cozum: HP cubugu (herhangi biri) bu kadar saniyedir HIC gorulmediyse,
    // ayni kurtarma dizisini calistir. Normal farm'da HP cubugu surekli acilip
    // kapandigi icin bu sayac pratikte hic dolmaz; sadece gercekten "uzun
    // suredir hicbir seye vuramiyoruz" durumunda dolar.
    double anti_stuck_no_hp_seconds = 60.0;

    // KOR NOKTA (kullanici talebi): ekranin TAM ORTASI karakterin KENDISIDIR.
    // Oraya (ve cok yakinina) tiklamanin hicbir faydasi yok - karakterin
    // uzerine tiklanmis olur, hedef secilmez; ustelik kendi karakterinin
    // govdesi/efekti bir tespiti oraya cekerse bot ayni noktaya tiklayip
    // bosa donebilir. Bu yaricaptaki (kare YUKSEKLIGININ orani - cozunurlukten
    // bagimsiz olsun diye) bir cember icine dusen tespitler ADAY BILE
    // SAYILMAZ. "Azicik etraf": %7 ~ 1080p'de ~76px yaricap.
    // NOT: anti-takilma / A-T Pro kurtarma dizilerindeki KASITLI merkez
    // tiklari bundan ETKILENMEZ - onlar tespit havuzundan gecmez, dogrudan
    // InputController::Click cagirir (bkz. AntiStuckRecover / AtProRecover).
    // MINIK KUTU ELEMESI - VARSAYILAN OLARAK KAPALI (0.0). ACIL DUZELTME:
    // eski varsayilan (0.032) TEK BIR ekran goruntusunden kalibre edilmisti
    // (o karede kedi/kopek h=23, gerçek metin h=36-71). Gercek kullanici
    // log'unda (diag.log, kanit) BASKA bir kullanicinin GERCEK
    // metinleri h=16-22 cikti - yani onceki "hayvan" orneginden (h=23) bile
    // KUCUK. Sebep: metin kutusu boyutu karakterle metin arasindaki
    // MESAFEYE/kamera yakinlastirmasina gore degisir, cozunurlukten
    // BAGIMSIZ olarak da degisken - TEK bir evrensel esik bu ikisini ayirt
    // edemiyor (bu kullanicida gercek metin, digerinde "hayvan" sayilan
    // deger kadar KUCUKTU). Sonuc: bot HICBIR metne tiklamiyordu (butun
    // tespitler "minik" diye reddediliyordu) - tespit kacirmak, ara sira bir
    // hayvana tiklamaktan COK daha kotu. Guvenli tarafta kalmak icin
    // varsayilan KAPATILDI (0.0 = hicbir kutu boyuta gore reddedilmez).
    // Isterseniz config.json'a "min_detection_height_ratio": 0.02 gibi dusuk
    // bir deger ekleyip debug_preview.exe'de GOZLE dogrulayarak
    // ACABILIRSINIZ - ama evrensel bir varsayilan olarak guvenli degil.
    double min_detection_height_ratio = 0.0;

    double blind_spot_radius_ratio = 0.07;
    // Kor noktanin merkezi ekranin TAM ortasi degil, biraz ALTIDIR (kullanici
    // talebi): karakterin govdesi merkez noktanin ALTINA dogru uzandigi icin
    // yasaklanacak alan da oraya kaymali. Kare YUKSEKLIGININ orani - pozitif
    // deger asagi kaydirir.
    double blind_spot_offset_y_ratio = 0.04;

    // ========================= NESNE MARKET KAPAT ============================
    // (kullanici talebi) Oyunun "Nesne Market" bildirim seridi ekranda
    // GORULURSE otomatik ESC basarak kapatir. Periyodik/dusuk maliyetli
    // taranir (her Step()'te DEGIL) - zaten yakalanmis kareyi kullanir, EK
    // bir capture yapmaz.
    bool nesne_market_dismiss_enabled = false;
    std::string nesne_market_template_path = "nesne_market/_banner.png";
    // TM_CCOEFF_NORMED skalasi [-1,1] - TM_CCORR_NORMED'in [0,1] araligindan
    // FARKLI (bkz. yukaridaki KANITLANMIS duzeltme notu). KULLANICININ
    // debug_preview.exe ile OLCTUGU GERCEK degerler: banner YOKKEN skor
    // 0.824, VARKEN 1.000 - esik ikisinin ARASINA, yanlis-pozitife (ESC
    // rahatsiz edici oldugu icin) karsi guvenli tarafa yakin konuldu.
    double nesne_market_match_threshold = 0.93;
    double nesne_market_check_interval_sec = 1.5;
    // Ayni banner'a ust uste ESC SPAMLAMAMAK icin - basildiktan sonra bu
    // kadar sure yeni bir ESC daha basilmaz (kapanma animasyonuna zaman taniir).
    double nesne_market_esc_cooldown_sec = 2.5;
    // ARAMA BOLGESI AYARI ARTIK YOK - bkz. bot.cpp CheckAndDismissNesneMarket.
    // Once MUTLAK PIKSEL, sonra ORAN olarak saklaniyordu; ikisi de yanlisti:
    // mutlak piksel baska cozunurlukte bambaska yere denk geliyor, ORAN ise
    // arayuzun pencereyle orantili buyudugu VARSAYIMINA dayaniyor - bu
    // varsayim OLCUMLE YANLIS bulundu (arayuz SABIT PIKSEL boyutunda; ornek:
    // bolge 802x632'de y=133..153 iken ayni oranlar 1602x1049'da y=221..254
    // gosterir, banner orada DEGILDIR). Artik banner ust-orta bolgede
    // ARANARAK bulunuyor - hicbir kalibrasyon gerekmez, her cozunurlukte
    // kendiliginden calisir.

    // ============================ A-T PRO ==================================
    // (kullanici talebi) Yukaridaki iki tetikleyici de SURE tabanlidir - takilma
    // ancak dakikalar sonra fark edilir, o zamana kadar bot bos bekler. A-T Pro
    // bunu MESAFE ile ve SANIYELER icinde yakalar:
    //
    //   merkez nokta = ekranin ortasi (= karakter)
    //   hedef nokta  = kilitli metnin tespit kutusunun merkezi
    //
    // ONEMLI (sahada olculdu): EKRAN uzerindeki mesafe, 3B mesafenin GUVENILIR
    // bir gostergesi DEGILDIR - kamera karakterin arkasinda oldugu icin, metne
    // dogru yurunurken metin ekranda merkeze yaklasmak ZORUNDA degil, perspektif
    // yuzunden asagi/disa dogru KAYABILIR (kullanicinin gozlemi: "azalip
    // artiyor"). Bu yuzden BIRINCIL ilerleme sinyali TESPIT KUTUSUNUN
    // YUKSEKLIGIDIR: acisal boyut mesafeyle TERS ORANTILIDIR (h ~ 1/d), yani
    // yaklastikca TEKDUZE buyur. Ekran mesafesi sadece IKINCIL/destek sinyaldir.
    // Tetiklenince kademeli kurtarma:
    //   FAZ 1 (at_pro_phase1_sec): KESIN SIRAYLA (bir sol bir sag: A,D,A,D...)
    //          at_pro_key_hold_sec kadar A / D basar - cogu takilma bu kucuk
    //          yanal hareketle acilir. Yon rastgele DEGIL: rastgele secim ayni
    //          yonu ust uste secip salinimi tamamen bozabiliyordu.
    //   FAZ 2 (faz 1 ise yaramazsa): ESC -> kamerayi cevir -> ekranin ortasina
    //          2-3 sol tik -> hedefi birak, arama moduna don.
    // HER IKI FAZ DA yaklasma tespit edilir edilmez ANINDA kesilir (sureyi
    // doldurmayi BEKLEMEZ) - bkz. AtProRecover().
    bool at_pro_enabled = false;
    // Bu kadar saniye boyunca HIC yaklasma olmazsa "takildi" sayilir.
    //
    // SAHADA OLCULDU (kullanicinin logundan - KANIT): 6sn COK KISAYDI, normal
    // oynanista bile ~1-2 dakikada bir yanlis tetikleniyordu ("alakasiz metin
    // keserken anti-takilma devreye giriyor" sikayeti TAM OLARAK buydu).
    // Sebep: hedef 100-250 ekran-pikseli uzaktayken (log'daki tipik tetiklenme
    // mesafesi), perspektif yuzunden kutunun OLCULEBILIR sekilde buyumesi
    // 6sn'den COK daha uzun surebiliyor - bu YANLIS pozitif, gercek takilma
    // degil. Pencere simdi cok daha genis; yine de eski (anti_stuck_seconds=
    // 150sn) mekanizmadan KAT KAT hizli.
    double at_pro_no_progress_sec = 15.0;
    // FAZ 1 suresi: artik SIRAYLA (bir sag bir sol) A/D basildigi icin - bkz.
    // bot.cpp AtProRecover - her yon icin birden fazla deneme sansi olsun
    // diye uzatildi. 12sn / 1.5sn = 8 hamle, yani 4 tam sag-sol salinimi:
    // karakter engelin hangi tarafinda sikismis olursa olsun her iki yon de
    // BIRDEN FAZLA kez denenmis olur.
    double at_pro_phase1_sec = 12.0;
    double at_pro_key_hold_sec = 1.5;
    // IKINCIL sinyal: ekran mesafesi bu kadar azaldiysa da ilerleme sayilir.
    // Tek basina KARAR VERDIRMEZ (ekran mesafesi guvenilmez - bkz. yukaridaki
    // perspektif notu). Cozunurlukten bagimsiz - bkz.
    // detection_stable_radius_ratio yorumu.
    double at_pro_min_progress_ratio = 8.0 / 600.0;
    // BIRINCIL sinyal: tespit kutusunun YUKSEKLIGI - h ~ 1/d oldugu icin
    // buyumesi yaklasma demektir.
    //
    // KRITIK DUZELTME (KANITLANDI): eskiden YUZDE (ör. %5) idi - ama sahada
    // olculen kutu yukseklikleri COK KUCUK (10-20px) cikti. Tam sayi
    // piksellerde %5 buyume (13px'te ~0.65px) OLCULEMEYECEK kadar ince bir
    // esik - bu da PRATIKTE hicbir buyume kaydedilememesine, dolayisiyla
    // surekli yanlis tetiklenmeye katkida bulunuyordu. MUTLAK PIKSEL'e
    // gecirildi - AMA bu sefer de cozunurluk degisince (kullanici talebi)
    // ayni sorun tekrarlanmasin diye kare YUKSEKLIGININ ORANI olarak
    // tutuluyor: at 800x600'de 1px = 600'de %0.167 - baska cozunurlukte
    // orantili buyuk/kucuk esik kullanilir.
    double at_pro_min_box_growth_ratio = 1.0 / 600.0;
    // Kutu tabanin bu kati ALTINA duserse "gorunum ciddi degisti" sayilir
    // (kamera dondu / karakter uzaklasti) - taban ASAGI cekilir ve sayac
    // sifirlanir, aksi halde eski/buyuk tabana kiyasla bir daha ASLA ilerleme
    // kaydedilemez ve bot kilitlenirdi.
    double at_pro_rebase_shrink = 0.80;
    // "VARDIK" ESIGI (kullanici karari): karakter metne varip VURMAYA baslayinca
    // mesafe de azalmayi birakir - bu NORMALDIR, takilma degil. Ayirt etmek icin
    // tespit kutusunun boyutuna bakiyoruz: karakter yaklastikca metin ekranda
    // BUYUR. Kutu yuksekligi kare yuksekliginin bu oranini gectiyse "vardik"
    // sayilir ve A-T Pro o hedef icin DEVRE DISI kalir (o noktadan sonrasi
    // yukaridaki klasik anti-takilmanin isidir).
    double at_pro_arrived_box_ratio = 0.42;

    // INSANCILLASTIR (kullanici talebi): ACIKSA tiklamadan ONCE imlec hedefe
    // ANINDA isinlamak yerine (SetCursorPos), kisa bir sure icinde YUMUSAKCA
    // (ease-in-out + hafif rastgelelik) kayarak gider - "tak tak" robotik
    // his yerine insan-benzeri hareket. Interception KULLANILMAZ (tamamen
    // SetCursorPos ile). KAPALIYKEN davranis BIREBIR eskisi gibi (aninda
    // konumlanip tiklar) - mevcut sistem hic etkilenmez.
    bool humanize_enabled = false;

    // OPSIYONEL/TEST EDILMEMIS (VARSAYILAN KAPALI): aciksa, her tiklamada
    // WM_*BUTTONDOWN/UP mesajlarinin YANINDA/HEMEN SONRASINDA bir de SendInput
    // fare-tus olayi gonderilir (bkz. input.cpp SendMouseButtonRaw) - amac
    // GetAsyncKeyState/raw-input okuyan bir mekanizmaya da GERCEK/global
    // buton durumunu yansitmak. UYARI: bu sunucuda SendInput fare tiklamasi
    // daha once TEK BASINA reddedilmisti (bkz. Click() yorumu); burada
    // SendMessage'in YANINDA denendigi icin farkli bir kombinasyon ama
    // sunucunun buna nasil tepki verecegi BILINMIYOR - acmadan once izole
    // test edin, sorun cikarsa hemen false'a donun.
    bool click_dual_sendinput_enabled = false;

    // YOLO (Ultralytics, ONNX'e disa aktarilmis) TABANLI TESPIT: her giris
    // {category, path} - hangi metin turunun HANGI egitilmis model dosyasiyla
    // tespit edilecegini belirtir (ör. {"dovus", "models/dovus_metni.onnx"}).
    // Bkz. yolo_detector.h - opsiyonel/EK bir yontemdir, bos birakilirsa ya
    // da dosya bulunamazsa mevcut isim-etiketi/renk tespiti hicbir sekilde
    // ETKILENMEZ.
    struct YoloModelEntry {
        std::string category;
        std::string path;
        // Roboflow projesinde YANLISLIKLA/deneme amacli eklenmis, cok az
        // (ör. 1) ornekle "egitilmis" sayilan bir sinif varsa, model
        // TUM siniflar arasinda "en yuksek skor" alirsa o zayif/yanlis
        // sinif da (nadiren de olsa) tetiklenip YANLIS bir kategoriye
        // (bu modelin category'sine) atfedilebilirdi. Bu yuzden HANGI
        // sinif indeksine (Roboflow'daki sira - ilk olusturulan sinif=0)
        // guvenilecegini ACIKCA belirtiyoruz - varsayilan 0 (ilk/asil
        // sinif). Bkz. yolo_detector.cpp.
        int class_index = 0;
        // Bu modele OZEL guven esigi - global yolo_conf_threshold'u
        // EZER (override). Negatifse (varsayilan) global deger kullanilir.
        // Bazi kategoriler (az/dusuk kaliteli egitim verisiyle egitilmis
        // modeller) genel esikte hic tespit edemeyebilir - ama esigi
        // GLOBAL dusurmek DIGER (zaten iyi calisan) kategorilerde yanlis-
        // pozitif riskini artirir, bu yuzden per-model override gerekli.
        double conf_threshold = -1.0;
    };
    std::vector<YoloModelEntry> yolo_models;
    // Egitimde kullanilan imgsz (Colab: model.train(..., imgsz=800)) - TUM
    // yolo_models girisleri icin ORTAK (farkli modeller farkli boyutla
    // egitildiyse bu deger yanlis sonuc verir, o zaman per-model alana
    // tasinmasi gerekir - simdilik TEK model oldugu icin bu yeterli).
    // TUM MODELLERI HER KAREDE CALISTIR (varsayilan: KAPALI).
    // Kapaliyken modeller donusumlu calisir ve sonuclari birlestirilir (bkz.
    // bot.cpp DetectInFarmZone) - maliyet tek model kadardir (~125 ms) ve
    // kamera, TUM kategoriler taranmadan donmez. Acikken eski davranis:
    // her karede TUM modeller calisir - tespit tazeligi en yuksek olur AMA
    // maliyet OLCULDU: 7 model = 861 ms/kare (bot belirgin sekilde yavaslar).
    bool yolo_all_models_every_frame = false;

    // GLOBAL KISAYOL KANCASI (VARSAYILAN: KAPALI - kullanici bildirdigi
    // "Origins acar acmaz kapaniyor" sorununun KOK NEDENI): global baslat/
    // durdur kisayollari icin sistem geneli bir WH_KEYBOARD_LL kancasi
    // kuruluyordu. Macro recorder'lar da TAM OLARAK ayni kancayi kurar; bazi
    // oyun istemcileri (ör. Origins) calisan global klavye kancalarini bir
    // makro belirtisi sayip KENDINI kapatir - bu yuzden Elbit acilinca da,
    // bir macro recorder acilinca da oyun kapaniyordu (girdi/enjeksiyon
    // sebebi DEGIL, sadece kancanin VARLIGI). Artik kanca VARSAYILAN olarak
    // KURULMAZ; sadece kullanici global kisayol atamak isterse (Ayarlar >
    // Kisayollar) acilir. Kapaliyken kisayollar arayuz odaktayken calisir.
    bool global_hotkeys_enabled = false;

    // UYUMLULUK MODU - AttachThreadInput KULLANMA (VARSAYILAN: KAPALI -
    // kullanici bildirdigi "Origins'de kanca kapatildiktan SONRA da, botu
    // BASLATINCA yine kapaniyor" sorununun kok neden ADAYI): win_window.cpp
    // EnsureInputFocus fonksiyonu HER TEK tiklama/tus basimindan once
    // cagriliyor ve icinde AttachThreadInput var - farming sirasinda dakikada
    // onlarca/yuzlerce kez tekrarlanan bu cagri, "baska bir surecin kendi
    // giris kuyruguna ilismesi" seklinde ozetlenebilecek, bazi oyun
    // istemcilerinin izledigi bilinen bir teknik. Bu, global klavye
    // kancasindan (tek seferlik, acilista) cok daha SIK tekrarlanan bir
    // imza oldugu icin daha guclu bir aday. Acikken hem EnsureInputFocus hem
    // BringToForegroundDirect (bkz. win_window.cpp) AttachThreadInput'u
    // TAMAMEN atlar, sadece SetForegroundWindow/BringWindowToTop/
    // SetActiveWindow/SetFocus + Alt-tap hilesine guvenir. Sunucu secimi
    // Origins ise (bkz. desktop_app_main.cpp) OTOMATIK acilir.
    bool avoid_attach_thread_input = false;

    int yolo_input_size = 800;
    double yolo_conf_threshold = 0.5;
    double yolo_nms_threshold = 0.45;
    // TANI/TEST AMACLI: true ise isim-etiketi + renk tespiti (eski sistem)
    // TAMAMEN devre disi kalir, tespit havuzu SADECE YOLO'dan gelir - YOLO'yu
    // eski sistemin etkisi olmadan izole test etmek icin. Varsayilan false
    // (normal calisma - iki sistem BIRLIKTE, eskisi hicbir sekilde
    // kaldirilmadan calisir).
    bool yolo_only_mode = false;

    double template_match_threshold = 0.45;
    bool template_multi_scale = false;
    double template_scale_min = 0.9;
    double template_scale_max = 1.1;
    int template_scale_steps = 3;
    int canny_low = 50;
    int canny_high = 150;

    // ORB ozellik eslestirme (feature matching) - templates/ klasorundeki
    // gorsellerin AYIRT EDICI NOKTALARINI (kose/desen) sahnedeki noktalarla
    // eslestirir. Tek bir global benzerlik sayisina guvenmedigi icin kenar/
    // sablon korelasyonundan cok daha az yanlis pozitif verir ve olcek/aci
    // degisimine daha dayaniklidir.
    int orb_min_matches = 10;       // Bir tespit sayilmasi icin gereken minimum "iyi" eslesme sayisi
    double orb_ratio_thresh = 0.75;  // Lowe's ratio test esigi (dusuk = daha siki/az false positive)

    // Yakalama - oyunun FPS'inde surekli pahali GPU->CPU kopyalama/donusum
    // yapmamak icin: bir kare en fazla bu araliktan (ms) daha sik islenir.
    int capture_min_interval_ms = 150;

    // Tiklama davranisi
    double mouse_move_duration_ms_min = 120;
    double mouse_move_duration_ms_max = 260;
    int pre_click_jitter_px = 3;
    double loop_idle_delay_sec_min = 0.25;
    double loop_idle_delay_sec_max = 0.5;
    // Panel gorunene kadar (en fazla bu kadar) tiklamaya devam edilir -
    // her tiklamadan sonra panel kontrol edilir, CIKAR CIKMAZ burst hemen
    // kesilir (bkz. ClickBurst). Boylece hem "tek seferde tam vurmama"
    // ihtimaline karsi güvenlik agi olur, hem de dogru vurulunca fazladan
    // tiklama atilmaz.
    int click_burst_count = 10;
    double click_burst_delay_sec_min = 0.15;
    double click_burst_delay_sec_max = 0.3;
    int click_jitter_px = 4;

    // Her tiklamadan SONRA, panelin ekranda belirmesi icin (oyunun render
    // gecikmesi olabilir) kisa birkac kez kontrol ediyoruz - sirf capture/
    // render zamanlama gecikmesi yuzunden dogru vurusu "yanlis" sanip bir
    // sonraki tiklamaya erken gecmeyi onler.
    int click_verify_checks = 2;
    double click_verify_check_delay_sec = 0.12;

    // KARARLILIK KAPISI: bir tespide gore tiklamadan once, ayni noktanin
    // (target_match_radius_px degil, bu ayri/daha siki bir yaricap) art
    // arda kac karede DOGRULANMASI gerektigi. 1 = eski davranis (ilk
    // gorunce hemen tikla), 2+ = kararsiz/gurultulu ilk okumaya gore kotu
    // tiklamayi onler.
    int detection_stable_frames = 2;
    // KRITIK DUZELTME (kullanici talebi: "her cozunurlukte/cihazda farkli
    // davraniyor, sapitiyor"): bu ve asagidaki benzer alanlar eskiden SABIT
    // PIKSEL degerlerdi - 800x600'de elle ayarlanmislardi. Baska bir
    // cozunurlukte (ör. 1200x800) kare bambaska boyutta oldugu icin bu sabit
    // pikseller ARTIK YANLIS ANLAMA geliyordu: buyuk cozunurlukte "aynı
    // hedef" esiği orantisiz KUCUK kalip botun surekli "farkli hedef" sanip
    // yeniden tespit/tiklama yapmasina, kucuk cozunurlukte ise orantisiz
    // BUYUK kalip yakin ama FARKLI iki metni "ayni" sanmasina yol aciyordu.
    // Artik kare YUKSEKLIGININ ORANI olarak tutuluyor - hangi cozunurlukte
    // calisirsa calissin AYNI GORSEL BUYUKLUGU ifade eder. Varsayilanlar,
    // "sorunsuz calisan" 800x600 referans alinarak (eski_px / 600) ile
    // hesaplandi - yani ayni davranisi TUM cozunurluklerde verir.
    double detection_stable_radius_ratio = 12.0 / 600.0;

    // REDDEDILEN BOLGE HAFIZASI: bir burst'un TAMAMI (click_burst_count
    // tiklama) atildigi halde panel hic cikmazsa (yanlis hedef - ornegin
    // dag/tas/duvar dokusu metin sanilip tiklanmis), o ekran bolgesi bir
    // sure "reddedildi" olarak isaretlenir ve tekrar SECILMEZ - aksi halde
    // bot aynı yanlis noktaya sonsuz dongude tikleyip TAKILI kalabiliyordu.
    // Ayrica kamera dondurulup arayis moduna girilir ki gorunum degissin.
    // Cozunurlukten bagimsiz - bkz. detection_stable_radius_ratio yorumu.
    double reject_spot_radius_ratio = 60.0 / 600.0;
    double reject_spot_expire_sec = 20.0;

    // SIRAYA EKLEME (kuyruk) HAFIZASI: bu sunucuda SHIFT+SAG TIK, halihazirda
    // baska bir hedefe kilitliyken BASKA bir metine uygulanirsa onu oyunun
    // KENDI hedef kuyruguna ekler - AYNI metine TEKRAR shift+sag tik atmak
    // ise onu kuyruktan CIKARIR (aç/kapa - "toggle"). Bu yuzden bir metini
    // kuyruga bir kez ekledikten sonra ayni oturumda TEKRAR eklemeye
    // calismamak icin, hangi noktalara zaten kuyruk komutu gonderdigimizi
    // (reject_spot mantiginin ayni sekilde) bir sure hatirlariz - bkz.
    // bot.cpp QueueOtherVisibleMetins().
    // Cozunurlukten bagimsiz - bkz. detection_stable_radius_ratio yorumu.
    double queue_spot_radius_ratio = 70.0 / 600.0;
    // "Bir kere tikladigina BIR DAHA basmamali" (kullanici talebi) - bu sure
    // boyunca o nokta kuyrukta sayilir ve tekrar tiklanmaz. Surekli kuyruklama
    // acikken tarama her birkac saniyede bir tekrarlandigi icin bu sure, tek
    // bir kesim doneminden RAHATCA uzun olmali; aksi halde sure dolar ve ayni
    // metne ikinci tik atilip kuyruktan CIKARDI.
    double queue_spot_expire_sec = 180.0;
    // KRITIK GUVENLIK ANAHTARI (varsayilan KAPALI): QueueOtherVisibleMetins,
    // hedefe YENI kilitlenildiginde ClickBurst'ten (birkac saniye surebilen)
    // BAGIMSIZ, YENI bir kare yakalayip TEKRAR tespit calistiriyordu - bu
    // sure icinde karakter hedefe dogru YURUDUGU/kamera kaydigi icin
    // "birincil hedef" karsilastirmasi ESKI (kilitlenme anindaki) konuma
    // gore yapiliyor, YENI karede AYNI metin bile birkac piksil kayabiliyordu.
    // Bu, KANITLANMIS iki kritik hataya yol aciyordu: (1) hali hazirda
    // kesilmekte olan metne YANLISLIKLA IKINCI bir shift+sag-tik atilip
    // kesimin IPTAL olmasi (karakter savunmasiz kalip hasar aliyor), (2)
    // metin oyunun kendi kuyruguna ERKEN eklendigi icin, kesim biter bitmez
    // (Z ile) loot toplamadan HEMEN digerine gecilmesi. Kullanicidan gelen
    // acil/kritik rapor uzerine varsayilan KAPATILDI - "bir sonraki metni
    // onceden kuyruga ekleme" hiz kazanimi, bu iki hatanin riskine DEGMEZ.
    bool queue_other_visible_metins = false;
    // SUREKLI KUYRUKLAMA (kullanici talebi): yukaridaki kuyruklama eskiden
    // SADECE yeni bir hedefe kilitlenildigi an BIR KEZ calisiyordu. Artik
    // AKTIF KESIM SIRASINDA da (HP cubugu ekrandayken) belirli araliklarla
    // tekrar taranir - boylece kesim suruyorken kadraja giren yeni metinler de
    // oyunun kendi kuyruguna eklenir ve karakter bosa beklemez.
    // "Ayni metne IKINCI kez tiklamama" garantisi iki katmanla saglanir:
    //   1) KOR NOKTA - hali hazirda kesilen metin ekran merkezindedir ve kor
    //      nokta cemberi onu tespit havuzundan zaten cikarir.
    //   2) m_queuedSpots - bir kez tiklanan nokta queue_spot_expire_sec
    //      boyunca hatirlanir (bkz. IsQueuedSpot).
    double queue_scan_interval_sec = 2.5;
    // KAMERA TARAMASI (kullanici talebi - kanitlandi: "bazen tiklamiyor" un
    // kok sebebi, o anki kamera acisinda baska metin GORUNMEMESIYDI). Her
    // kuyruklama taramasinda, mevcut goruntuye EK olarak kamerayi kucuk
    // adimlarla cevirip HER acida da tarama yapar - boylece o an baktigi
    // yonun disindaki metinler de bulunup kuyruklanabilir.
    bool queue_scan_rotate_enabled = true;
    // Arama modundaki (SearchAction) tam donuslerden KUCUK - amac genis bir
    // "kesif" degil, mevcut goruntunun hemen disini de kontrol etmek.
    double queue_scan_rotate_px_min = 60.0;
    double queue_scan_rotate_px_max = 110.0;
    // Ilk (donmeden) taramaya EK olarak kac kucuk donus yapilacak - her
    // donus SONRASI ayrica taranir. 2 = sag+sol, ~3 acidan kontrol.
    int queue_scan_rotate_steps = 2;

    // Hedef kilitleme
    double target_lost_grace_sec = 6.0;
    // Cozunurlukten bagimsiz - bkz. detection_stable_radius_ratio yorumu.
    double target_match_radius_ratio = 90.0 / 600.0;
    // MUTLAK son guvenlik agi - stuck_panel_timeout_sec'ten (60sn) daha
    // BUYUK olmali, aksi halde bu, yeni "takilma" mekanizmasina hic sira
    // gelmeden devreye girip onu anlamsizlastirir. Normalde status_panel
    // (kesildi/yanlis hedef/takildi) cok daha once devreye girer.
    double max_target_wait_sec = 90;

    // Ust bilgi paneli (metin secilince ekranda beliren "Lv.X (Y.Seviye) <isim>" yazisi)
    // ile CIFT DOGRULAMA: templates_dir/_panel.png (template_picker.exe'de
    // 'p' ile alinir) SADECE isim/seviye yazisidir - dogrudan ekranin ust
    // seridinde araniyor. Bulunursa dogru hedefe kilitliyiz demektir,
    // kayboldugunda metin KESILMIS demektir - sabit bir sureye (40 sn)
    // gerek kalmadan aninda anlasilir.
    double status_panel_appear_grace_sec = 2.5;   // isim bu sure icinde hic cikmazsa "yanlis hedef" sayilir
    // Isim daha once dogrulanmisken bir anlik (vurus efekti, capture
    // zamanlama titremesi vb.) kaybolmasi TEK BASINA "kesildi" sayilmamali -
    // GERCEKTEN bu sure boyunca kesintisiz kaybolmus olmali.
    double status_panel_lost_grace_sec = 1.5;
    // HP cubugu eslesme esigi (0-1). DEGER OLCUMLE belirlendi (tahmin DEGIL):
    // kullanicinin 1602x1049 GERCEK ekran goruntusu uzerinde, can seviyesi
    // simule edilerek StatusPanelDetector calistirildi:
    //     can %100 -> 0.987 | %75 -> 0.737 | %50 -> 0.686 | %25 -> 0.686 | %10 -> 0.694
    //     PANEL YOK -> 0.341
    // Yani en kotu GERCEK eslesme 0.686, en yuksek YANLIS eslesme 0.341.
    // 0.50, ikisinin tam ortasi: yanlis pozitife 0.16, en kotu gercek
    // eslesmeye 0.19 pay birakir. bot.cpp'deki "yakin esik" toleranslari
    // (0.08 / 0.10) ile taban 0.40'a iner - yine de 0.341'in USTUNDE kalir.
    // NOT: ESKI deger 0.3 idi ve artik TEHLIKELI olurdu (panel YOKKEN skor
    // 0.341 cikiyor) - eski dar/oranli bolgeye gore ayarlanmisti, yeni
    // konum-aramali yontemde gecerli degil (bkz. status_panel_detector.cpp).
    double status_panel_name_match_threshold = 0.50;

    // Yere dusen esyalari (loot) KACIRMAMAK icin, bot calisirken (client
    // foreground oldugu surece) bu araliklarla SUREKLI Z tusuna basilir -
    // hedef kilitli olsun olmasin, kesintisiz. Her basis TEK, kisa bir
    // basip-birakma (ESC/Shift ile ayni guvenli desen - asla basili tutma).
    double loot_key_interval_sec = 0.2;

    // 3. MEKANIZMA - "TAKILMA" KORUMASI: panel (isim yazisi) KESINTISIZ
    // olarak bu sureden UZUN acik kalirsa (yani metin ne kesiliyor ne de
    // yanlis hedef sayiliyor, sadece sonsuza kadar "kesiliyor" gorunuyor),
    // karakterin bir sekilde takildigi (ör. yol bulamama, canavar engeli)
    // varsayilir: ESC basilip panel kapatilir, hedef BASARISIZ sayilarak
    // birakilir (tamamlandi SAYILMAZ) ve kamera dondurulup yeni hedef aranir.
    double stuck_panel_timeout_sec = 60.0;

    // Farm alani (karakter etrafi elips). Cozunurlukten bagimsiz - bkz.
    // detection_stable_radius_ratio yorumu. X kare GENISLIGININ, Y kare
    // YUKSEKLIGININ orani (800x600 referans alindi).
    double farm_zone_radius_x_ratio = 380.0 / 800.0;
    double farm_zone_radius_y_ratio = 220.0 / 600.0;

    // Ekranin en altindaki sabit yetenek/envanter kisayol cubugu ASLA arama
    // alanina girmemeli (bkz. bot.cpp Step()) - kamera yonunden bagimsiz
    // hep ayni yerde durdugu icin, bir sablon ona yanlislikla benzerse bot
    // surekli o sabit noktaya tiklayip hem gercek hedef bulamaz hem de
    // istenmeyen oyun ici eylemler (yetenek/esya kullanimi) tetikleyebilirdi.
    int bottom_ui_exclude_px = 130;

    // OLCEKLENME DUZELTMESI (kullanici: "bazen sistem alttaki alana tiklayip
    // kendini bozuyor"): yukaridaki deger SABIT PIKSEL - referans pencerede
    // (~800px yukseklik) dogru, ama oyun penceresi BUYUDUKCE alttaki arayuz
    // cubugu da ORANTILI buyudugu icin 130px ARTIK YETMIYOR ve tiklama
    // cubugun ustune dusebiliyordu (puzzle'daki olcek hatasiyla AYNI sinif
    // hata). Artik dislama yuksekligi = max(sabit px, kare yuksekligi * bu
    // oran) - kucuk pencerede eski davranis aynen korunur, buyuk pencerede
    // otomatik buyur. 130/800 = 0.1625.
    double bottom_ui_exclude_ratio = 0.1625;

    // ISIM ETIKETI TESPITI: metin tasinin USTUNDE/yaninda YUZEN isim yazisi
    // (ör. "Dovus Metni") kameraya HER ZAMAN duz/okunakli goruntulenir
    // (billboard) - ORB'un aksine ACIDAN BAGIMSIZ calisir, TEK bir referans
    // gorselle (template_picker.exe'de 'n' ile alinir, <klasor>/_name.png)
    // genis bir mesafe araliginda calisabilir. Opsiyonel/EK bir yontemdir -
    // referans yoksa sessizce devre disi kalir, ORB/AutoColor etkilenmez.
    double name_label_match_threshold = 0.7;
    // KENAR-TABANLI (Canny) eslesme icin AYRI bir esik - kenar haritalari
    // seyrek/ikili oldugundan TM_CCOEFF_NORMED skorlari duz gri-ton
    // eslesmesinden DOGAL olarak daha dusuk cikar, bu yuzden ayni esik
    // kullanilamaz. Bkz. name_label_detector.cpp.
    double name_label_edge_match_threshold = 0.35;
    // MASKELI (arka plan HARIC) gri-ton eslesmesi icin esik - TM_CCORR_NORMED
    // (maskeli eslesme SADECE bu ve TM_SQDIFF'te desteklenir, TM_CCOEFF_NORMED
    // DEGIL) kullanildigi icin skor karakteristigi diger ikisinden farkli,
    // AYRI bir esik gerekir. Bkz. name_label_detector.cpp.
    double name_label_masked_match_threshold = 0.75;
    // KRITIK: yazi tasin USTUNDE yuzuyor, gercek tiklanacak nesne (tas)
    // biraz ASAGISINDA - bu katsayi, bulunan yazinin KENDI (o anki olculu)
    // YUKSEKLIGININ kac kati kadar asagisina tiklanacagini belirler. Sabit
    // piksel DEGIL - boylece yakinda/uzakta (yazi buyuk/kucuk gorunse de)
    // ORANTILI olarak dogru noktaya duser. BAŞLANGIÇ DEGERI TAHMINIDIR -
    // debug_preview.exe'deki CYAN isaretciyi gorup gerekirse ayarlayin.
    double name_label_click_offset_factor = 1.2;

    // Arama davranisi
    double camera_rotate_px_min = 150;
    double camera_rotate_px_max = 300;

    // KURTARMA KAMERA CEVIRMELERI - FARE PIKSELI cinsinden (yukaridaki arama
    // degerleriyle AYNI birim). KRITIK DUZELTME: bu iki deger ESKIDEN pencere
    // GENISLIGININ orani olarak hesaplaniyordu (winW*0.55 ve winW*0.15). Ama
    // RotateView'in aldigi sayi bir FARE HAREKETI miktaridir; oyun her fare
    // pikseli icin SABIT derece doner - pencere boyutuyla ILGISI YOKTUR.
    // Dolayisiyla 802px genisliginde 441px donen kamera, 1602px genisliginde
    // 881px yani IKI KAT fazla donuyordu; kurtarma davranisi her cozunurlukte
    // FARKLI oluyordu. Artik sabit piksel - her pencerede AYNI aci.
    // Degerler referans (802px) davranisiyla birebir ayni olacak sekilde
    // secildi: 802*0.55 ~= 440, 802*0.15 ~= 120.
    double recover_rotate_px = 440.0;        // A-T Pro faz 2: buyuk donus
    double recover_small_rotate_px = 120.0;  // Anti-takilma sonu: kucuk donus
    double camera_rotate_duration_ms_min = 260;
    double camera_rotate_duration_ms_max = 420;
    double search_action_wait_sec_min = 0.8;
    double search_action_wait_sec_max = 1.4;

    // config.json'u okur. Bulunamazsa/bozuksa varsayilan degerlerle devam eder.
    static Config Load(const std::string& path);

    // Sadece color_profiles alanini gunceller, dosyadaki diger tum ayarlari
    // OLDUGU GIBI korur. color_picker.exe yeni ornek topladikca bunu cagirir.
    static void SaveColorProfiles(const std::string& path, const std::vector<ColorProfile>& profiles);
};
