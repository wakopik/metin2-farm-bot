# Mimari ve Tasarım Notları

Bu belge, kodu ilk kez okuyan bir geliştiricinin "neden böyle yapılmış"
sorusuna cevap bulması için yazıldı. Kaynak kodun yorumları (çoğu Türkçe ve
ASCII) her kararın ölçüme dayalı gerekçesini ayrıca içerir.

## Genel akış

```
            ┌────────────────────────── desktop_app.exe ─────────────────────────┐
            │                                                                    │
 WebView2 ──┤  desktop_ui/ (HTML/JS)  ──fetch──▶  yerel HTTP sunucusu            │
 penceresi  │                                    127.0.0.1:8010 (cpp-httplib)    │
            │                                          │                         │
            │                                   NativeSession                    │
            │                         (çoklu istemci round-robin, zamanlayıcı)   │
            │                                          │                         │
            │                                  MetinBot::Step()                  │
            │      ┌───────────────┬───────────────────┼──────────────┐          │
            │  WindowCapture   YoloDetector(ler)   StatusPanel     InputController│
            │   (WGC kare)     (ONNX Runtime)      Detector        (Win32 girdi)  │
            └────────────────────────────────────────────────────────────────────┘
```

1. **Yakalama:** `WindowCapture` (`src/capture.cpp`) Windows Graphics Capture
   ile oyun penceresinden kare alır. WGC pencere odakta olmasa bile çalıştığı
   için çoklu istemci modunun temelidir. `capture_min_interval_ms` kare
   alımını seyreltir.
2. **Tespit:** `MetinBot::DetectInFarmZone` (`src/bot.cpp`) yalnızca karakter
   etrafındaki elips farm alanının sınırlayıcı dikdörtgeninde arama yapar.
   Seçili her kategori için ayrı bir `YoloDetector` çalışır.
   `yolo_only_mode: true` iken eski şablon / renk / ORB yöntemleri devre dışıdır,
   ama kodları referans olarak durur.
3. **Hedef seçimi:** En yakın kararlı tespit hedef olur.
   `detection_stable_frames` ve `*_radius_ratio` değerleri tüm yarıçapları
   **kare boyutuna oran** olarak tutar, böylece davranış çözünürlükten bağımsız kalır.
4. **Tıklama:** `ClickBurst` hedefe `click_burst_count` kez Shift+sağ tık
   yapar. Bütün tıklamalar `FrameToScreenPoint` üzerinden geçer. Bu tek nokta
   alt arayüz bandını (`bottom_ui_exclude_px`) ve ekran sınırlarını kelepçeler.
5. **Doğrulama:** `StatusPanelDetector` ekranın üstündeki hedef panelini
   (isim + HP barı) okuyarak hedefin gerçekten kilitlendiğini ve kesildiğini
   doğrular. Panel kaybolursa `status_panel_lost_grace_sec` sonra hedef
   "kesildi" sayılır.
6. **Arama:** Hedef yoksa `SearchAction` kamerayı döndürür. Art arda en fazla
   iki kez aynı yöne döner, sonra yön zorla ters çevrilir ("pervane" etkisini önler).
7. **Takılma önleme:** `AntiStuckRecover` HP barı uzun süre değişmezse ya da
   hiç görünmezse kamera döndürme, ESC, kısa yürüme ve rastgele tıklamalardan
   oluşan bir kurtarma dizisi çalıştırır.

## Girdi (fare / klavye)

`src/input.cpp`, bazı oyun istemcilerinin sentetik girdiyi farklı yollardan
reddetmesi yüzünden birkaç yöntemi birlikte kullanır:

- **Fare:** `SetCursorPos(x, y)` + `SendMessageW(hwnd, WM_*BUTTONDOWN/UP)`.
  Tek başına `SendInput` fare tıklaması bazı istemcilerde reddedildi.
- **Klavye:** `SendMessageW(WM_KEYDOWN/UP)` **ve** donanım tarama kodlu
  (`KEYEVENTF_SCANCODE`) `SendInput` birlikte gönderilir. DirectInput okuyan
  istemciler sanal tuş kodlu `SendInput`'u görmez.
- **Kamera:** `RotateView`, sağ tuş basılıyken göreli fare hareketi yapar.
  `timeBeginPeriod(1)` ile zamanlayıcı çözünürlüğü 1 ms'ye çekilir.
- Uygulama **Yönetici** olarak çalışır. Oyun istemcisi Yönetici ise Windows
  UIPI, düşük yetkili bir süreçten gelen pencere geçişini sessizce engeller.

## YOLO tespiti

- `src/yolo_detector.cpp`: ONNX Runtime, letterbox ön işleme
  (`yolo_input_size`, varsayılan 800), YOLOv8 çıktı tensörünün çözümü ve NMS.
- OpenCV DNN yeni Ultralytics ONNX çıktılarındaki `Conv kernel_shape`
  özniteliğini okuyamadığı için ONNX Runtime seçildi.
- **IR sürümü:** Yeni `onnxscript` exporter'ı IR 13 üretebiliyor. Projedeki
  ONNX Runtime daha eski bir IR'a kadar destekliyorsa Python'da şöyle düzeltin:

  ```python
  import onnx
  m = onnx.load("best.onnx"); m.ir_version = 12; onnx.save(m, "best.onnx")
  ```

- `min_detection_height_ratio` çok küçük (uzak) tespitleri eler.
  `debug_preview` bu elemeleri ekranda ayrıca sayar.

## Masaüstü uygulaması

- `src/desktop_app_main.cpp`: Tek pencereli bir WebView2 kabuğu. Arayüz dosyaları
  (`desktop_ui/`) exe'nin yanından okunur, yani HTML/JS/CSS değişikliği için
  yeniden derleme gerekmez.
- Yerel API yalnızca `127.0.0.1`'e bağlanır. Başlıca uç noktalar:
  `/api/status`, `/api/start`, `/api/stop`, `/api/metins`, `/api/servers`,
  `/api/calibrate/*`, `/api/discord/*`, `/api/telegram/*`.
- **Çökme koruması:** `RunGuarded(name, fn)` arka plan thread'lerini hem C++
  istisnalarına hem SEH (donanım) istisnalarına karşı sarar. Biri çökerse
  bütün uygulama değil, yalnızca o thread durur ve 2 sn sonra yeniden başlar.
  `__try/__except` bloğu MSVC'nin C2712 kısıtı yüzünden ayrı, POD-only bir
  yardımcıda (`CallWithSehGuard`) durur. Bu yüzden `desktop_app_main.cpp`
  `/EHa` ile, görüntü işleme kodu ise `/EHsc` ile derlenir.
- **Çoklu istemci:** `NativeSession::RunLoop` aynı başlıktaki birden fazla
  oyun penceresi arasında sırayla geçer. Aktif istemci arama modundayken
  odağı belirli bir süre elinde tutar, böylece geçiş maliyeti (Activate) amortize olur.
- **Zamanlayıcı:** `SchedulerLoop` saniyede bir zamanlı başlat / durdur
  isteklerini kontrol eder. İsteğe bağlı olarak durdurduktan sonra oyunu kapatır.

## Kalibrasyon

`src/calibration.cpp` oyunu ~40 sn **tıklamadan** izler, HP barı ve bildirim
şeridi gibi şablonların "yokken" skor dağılımını ölçer ve eşikleri o cihaza
göre (gürültü tabanı + pay) önerip `config.json`'a yazar. Neyin ölçülebildiği
ve neyin ölçülemediği dosyanın başında açıkça yazılıdır.

## Uzaktan kontrol (isteğe bağlı)

- `src/discord_bot.cpp`: IXWebSocket ile Discord Gateway bağlantısı ve REST
  üzerinden mesaj / embed gönderimi. Kullanıcı hesabı OAuth ile bağlanır.
  Bağlantı bilgisi `discord_link.json` dosyasında tutulur.
- `src/telegram_bot.cpp`: Long-polling. Bağlama, uygulamada üretilen 6 haneli
  tek kullanımlık kodla yapılır.
- Komutlar tek bir ortak işleyicide toplanır (`HandleBotCommand`): `/analiz`,
  `/canli`, `/goruntu`, `/baslat`, `/durdur`, `/zkur`, `/dkur`, ...
- Token'lar `config.json` içinden okunur. Boş bırakılırsa entegrasyon hiç başlamaz.

## Yeni bir metin türü eklemek

1. `models/<tur>.onnx` dosyasını yerleştirin ve `config.json` → `yolo_models`
   listesine `{ "category": "<tur>", "path": "models/<tur>.onnx" }` ekleyin.
2. Kökte `<tur>/` klasörü açıp içine en az bir `.png` koyun (ör. `_panel.png`).
   Arayüz içinde `.png` bulunan klasörleri listeler.
3. `desktop_ui/app.js` içindeki görünen ad tablosuna (`metin.<tur>`) ve
   `desktop_ui/i18n.js` içine çeviriyi ekleyin.
4. `CMakeLists.txt` → `ELBIT_DATA_DIRS` listesine klasör adını ekleyin
   (derleme çıktısına kopyalansın diye).
