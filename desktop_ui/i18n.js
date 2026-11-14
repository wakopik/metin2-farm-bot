// ============================ DIL SISTEMI (i18n) ============================
// TR/EN dil destegi. app.js'ten ONCE yuklenir (bkz. index.html) - global
// window.t / window.applyStaticI18n / window.setLanguage / window.getLang
// saglar. app.js hem statik HTML'i (data-i18n oznitelikleri) hem de kendi
// urettigi dinamik metinleri (t("anahtar") cagrilariyla) bu sozlukten ceker.
//
// Yeni metin eklerken: TR ve EN karsiliklarini asagidaki I18N sozlugune ayni
// anahtarla ekle; HTML'de <span data-i18n="anahtar">...</span>, JS'te
// t("anahtar") kullan. Eksik anahtar sessizce anahtarin kendisini dondurur
// (crash yok), boylece kismi ceviri bile UI'yi bozmaz.

const I18N = {
  tr: {
    // --- Guncelleme ekrani ---
    "update.title": "Güncelleme Mevcut!",
    "update.message": "Yazılımın yeni bir sürümü yayınlandı.",
    "update.messageFull": "Yazılımın yeni bir sürümü yayınlandı. Devam etmek için güncel sürümü indirin.",
    "update.discordBtn": "Discord Sunucusuna Git",
    "update.retryBtn": "Tekrar Dene",
    // --- Oturum kontrol ---
    "session.checkText": "Oturumunuz kontrol ediliyor, yönlendiriliyorsunuz...",
    "session.updateCheck": "Güncellemeler kontrol ediliyor...",
    // --- Giris ---
    "login.keyLabel": "Anahtar",
    "login.remember": "Beni hatırla",
    "login.submit": "Giriş Yap",
    "login.submitting": "Giriş yapılıyor...",
    "login.keyToggleAria": "Anahtarı göster/gizle",
    "login.invalidKey": "Geçersiz anahtar.",
    "login.revoked": "Lisansınız artık geçerli değil, tekrar giriş yapın.",
    "login.serverError": "Sunucuya bağlanılamadı ({detail}). Lütfen tekrar deneyin.",
    "login.badResponse": "sunucu geçersiz yanıt verdi (HTTP {status}): {body}",
    // --- Navigasyon ---
    "nav.dashboard": "Anasayfa",
    "nav.profile": "Profil",
    "nav.history": "Geçmiş",
    "nav.functions": "Fonksiyonlar",
    "nav.settings": "Ayarlar",
    "nav.manual": "Manuel Tarama",
    "manual.title": "Manuel Tarama",
    "manual.enable": "Manuel Tarama",
    "manual.enableOn": "Açık — metin modelleri çalışmaz, sadece seçtiğiniz görsel aranır",
    "manual.enableOff": "Kapalı — metin modelleri çalışır",
    "manual.step1": "Hedef Görselleri",
    "manual.pick": "Hedef Ekle",
    "manual.picking": "Kırpma aracı açık...",
    "manual.noImage": "Henüz hedef eklenmedi",
    "manual.emptyHint": "Aşağıdaki butonla oyundan bir hedef kırpın.",
    "manual.remove": "Kaldır",
    "manual.step2": "2. Tıklama Kayması (Offset)",
    "manual.step2desc": "Görselin merkezine göre nereye tıklanacağı. Örnek: isim etiketinin 70px altına tıklamak için Y = 70.",
    "manual.offsetX": "Yatay (X)",
    "manual.offsetXNote": "Artı = sağa, eksi = sola",
    "manual.offsetY": "Dikey (Y)",
    "manual.offsetYNote": "Artı = aşağı, eksi = yukarı",
    "manual.step3": "3. Tıklama Türü",
    "manual.step3desc": "Bulunan noktaya hangi tuşla tıklanacağı. Aynı anda yalnızca biri aktif olabilir.",
    "manual.left": "Sol Tık",
    "manual.right": "Sağ Tık",
    "manual.errNoWindow": "Oyun penceresi bulunamadı. Önce oyunu açın.",
    "manual.errNoFrame": "Oyun ekranı yakalanamadı.",
    "manual.errSave": "Görsel kaydedilemedi.",
    "manual.cancelled": "Kırpma iptal edildi.",
    "manual.needImage": "Önce bir görsel seçin.",
    "nav.theme": "Tema",
    "theme.title": "Tema",
    // --- Sidebar ---
    "sidebar.expiry": "Kalan Süre",
    "btn.start": "Başlat",
    "btn.pause": "Duraklat",
    "btn.resume": "Devam Et",
    "btn.stopBot": "Botu Kapat",
    "btn.stop": "Durdur",
    "btn.logout": "Çıkış Yap",
    // --- Dashboard istatistik ---
    "stat.farmTime": "Farm Süresi",
    "stat.cutText": "Kesilen Metin",
    "stat.totalClicks": "Toplam Tıklama",
    // --- Calisma kartı ---
    "work.state.waiting": "Beklemede",
    "work.state.cutting": "Metin Kesiliyor",
    "work.state.searching": "Hedef Aranıyor",
    "work.state.paused": "Duraklatıldı",
    "work.sub.default": "Başlat'a basın.",
    "work.badge.ready": "Hazır",
    "work.badge.running": "Çalışıyor",
    "work.badge.paused": "Duraklatıldı",
    "work.step.scan": "Tarama",
    "work.step.lock": "Hedef Kilidi",
    "work.step.cut": "Kesim",
    "info.serverSelect": "Sunucu Seçimi",
    "server.active": "Aktif",
    "server.maintenance": "Bakımda",
    "server.passive": "Pasif",
    "server.pickHint": "Bir sunucu seçin",
    "server.add": "Sunucu ekle",
    "server.addName": "Sunucu adı",
    "server.addNamePh": "ör. Misali2",
    "server.addTitle": "Pencere başlığı (isteğe bağlı)",
    "server.addTitlePh": "Boş bırakılırsa sunucu adı kullanılır",
    "server.addCancel": "İptal",
    "server.addSave": "Ekle",
    "server.addErrName": "Sunucu adı boş olamaz.",
    "server.addErrSave": "Sunucu kaydedilemedi.",
    "toast.started": "Bot çalışmaya başladı",
    "toast.stopped": "Bot çalışmayı durdurdu",
    "toast.noWindow": "Oyun penceresi bulunamadı",
    "toast.noWindowHint": "Aranan: {hint}",
    "info.language": "Dil Seçimi",
    "lang.tr": "Türkçe",
    "lang.en": "İngilizce",
    "info.activeClient": "Aktif Client",
    "clientMode.auto": "Otomatik",
    "clientMode.autoCount": "{n} Client (Otomatik)",
    // --- Profil ---
    "profile.title": "Profil",
    "profile.hero.title": "Elbit Üyeliği",
    "profile.hero.sub": "Lisanslı oturum",
    "profile.hero.subUnlimited": "Sınırsız üyelik",
    "profile.hero.pill.active": "Aktif",
    "profile.hero.pill.warn": "Yakında Bitiyor",
    "profile.hero.pill.critical": "Süre Azaldı",
    "profile.licenseKey": "Lisans Anahtarı",
    "profile.timeLeft": "Kalan Süre",
    "profile.clientMode": "Client Modu",
    "profile.totalCut": "Toplam Kesilen",
    "aria.showHideKey": "Anahtarı göster/gizle",
    "aria.copyKey": "Anahtarı kopyala",
    // --- Kalan sure metinleri ---
    "expiry.unlimited": "Sınırsız",
    "expiry.expired": "Süre Doldu",
    "expiry.remaining": "{days} Gün {hours} Saat",
    "expiry.remainingSuffix": "{label} kaldı",
    "expiry.expiredLogout": "Lisans süreniz doldu. Devam etmek için anahtarınızı yenileyin.",
    "license.revoked": "Lisansınız artık geçerli değil.",
    // --- Fonksiyonlar ---
    "functions.title": "Fonksiyonlar",
    "card.autoLoot.title": "Oto Nesne",
    "note.autoLoot.on": "Açık - metin kırılınca 2sn boyunca hızlı Z ile eşyalar toplanır",
    "note.autoLoot.off": "Kapalı (normal eşya toplama)",
    "card.antiStuck.title": "Anti-Takılma",
    "note.antiStuck.on": "Açık - {min}dk takılırsa kamera çevir + ESC + W/S/D/A ile kurtul",
    "note.antiStuck.off": "Kapalı (klasik 60sn'de hedefi bırakma)",
    "card.queue.title": "Sıraya Ekleme",
    "note.queue.on": "Açık - kesim sırasında da tarar",
    "card.atPro.title": "A-T Pro",
    "note.atPro.on": "Açık (mesafe sürekli izleniyor)",
    "card.humanize.title": "İnsancıllaştır",
    "note.humanize.on": "Açık (fare yumuşak kayar)",
    "note.humanize.off": "Kapalı (anında tıklama)",
    "card.autoKey.title": "Oto Basma",
    "note.autoKey.on": "Açık - {n} tuş tanımlı",
    "note.autoKey.onEmpty": "Açık ama tuş girilmemiş",
    "autoKey.holdLabel": "Basılı Tut",
    "autoKey.every": "her",
    "unit.sec": "sn",
    "autoKey.keyPlaceholder": "Tuşa bas",
    "autoKey.keyTitle": "Kutuya tıklayıp istediğin tuşa (ya da Ctrl+X gibi bir kombinasyona) bas. Temizlemek için Delete/Backspace.",
    "autoKey.holdTitle": "Süre girmeden tuşu sürekli basılı tutar",
    "btn.autoKeyFarm": "Oto Basma Farmı",
    "autoKeyFarm.title": "Hedef arama/tıklama yapmadan, sadece pencereye geçip yukarıdaki Oto Basma tuşlarını çalıştırır",
    "card.scheduler.title": "Zamanlayıcı",
    "sched.start": "Zamanlı Başlat",
    "sched.stop": "Zamanlı Durdur",
    "sched.willStart": "{time}'de başlayacak ({remaining} sonra)",
    "sched.willStop": "{time}'de duracak ({remaining} sonra)",
    "sched.remaining": "{h} sa {m} dk",
    "sched.exitAfter": "Zamanlı Durdur devreye girince oyundan da çık",
    "sched.minError": "Lütfen en az 1 dakikalık bir süre girin.",
    "card.clickMode.title": "Tıklama Değiştirme",
    "clickMode.shift": "Shift + Sağ Tık",
    "clickMode.plain": "Sadece Sol Tık",
    "card.inputMethod.title": "Tıklama Yöntemi",
    "inputMethod.default": "Varsayılan",
    "inputMethod.postmessage": "PostMessage",
    "card.guard.title": "Guard Modu",
    "note.guard.on": "Açık - pencere adı/PID değişiyor",
    "note.off": "Kapalı",
    "unit.min": "dk",
    "unit.hour": "sa",
    // --- Ayarlar ---
    "settings.title": "Ayarlar",
    "card.calib.title": "Kalibrasyon",
    "btn.calibrate": "Kalibre Et",
    "calib.starting": "Başlatılıyor...",
    "calib.running": "Kalibre ediliyor...",
    "calib.done": "Tamamlandı",
    "calib.failed": "Başarısız",
    "calib.notStarted": "Başlatılamadı",
    "calib.errPrefix": "HATA ",
    "calib.errStart": "Kalibrasyon başlatılamadı.",
    "calib.errConn": "HATA Sunucuya bağlanılamadı.",
    "card.theme.title": "Tema Rengi",
    "swatch.bw": "Siyah / Beyaz / Gri",
    "swatch.blue": "Mavi",
    "swatch.green": "Yeşil",
    "swatch.purple": "Mor",
    "swatch.pink": "Pembe",
    "swatch.orange": "Turuncu",
    "swatch.red": "Kırmızı",
    "swatch.rainbow": "Rengarenk",
    "card.bg.title": "Arka Plan",
    "bg.solid": "Koyu (Tok)",
    "bg.translucent": "Yarı Saydam",
    "card.hotkeys.title": "Kısayollar",
    "hotkey.startKey": "Başlat tuşu",
    "hotkey.stopKey": "Durdur tuşu",
    "btn.assign": "Ata",
    "hotkey.pressKey": "Bir tuşa basın...",
    // --- Discord ---
    "discord.notConnected": "Discord bağlı değil",
    "discord.sub": "Uzaktan durum bildirimi ve komutlar için hesabınızı bağlayın.",
    "discord.connected": "{user} | Bağlandı",
    "discord.subConnected": "Uzaktan komutlar ve durum bildirimleri aktif.",
    "btn.discordConnect": "Discord'a Bağlan",
    "btn.discordDisconnect": "Bağlantıyı Kaldır",
    "discord.notConfigured": "Discord entegrasyonu henüz yapılandırılmadı. (config.json içinde discord_client_id ayarlanmalı.)",
    "telegram.notConnected": "Telegram bağlı değil",
    "telegram.sub": "Uzaktan durum bildirimi ve komutlar için Telegram hesabınızı bağlayın.",
    "telegram.connected": "Telegram | Bağlandı",
    "telegram.subConnected": "Uzaktan komutlar ve durum bildirimleri aktif.",
    "telegram.soon": "Telegram (yakında)",
    "telegram.notConfigured": "Telegram entegrasyonu henüz yapılandırılmadı. (config.json içindeki telegram_bot_token alanını doldurun.)",
    "telegram.codeInstruction": "Telegram'da <b>{bot}</b> botuna şunu yaz: <b>/baglan {code}</b> (kod 15 dk geçerli)",
    "telegram.botFallback": "botunuz",
    "btn.telegramConnect": "Telegram'a Bağlan",
    "btn.telegramDisconnect": "Bağlantıyı Kaldır",
    // --- Gecmis ---
    "history.title": "Geçmiş",
    "history.empty": "Henüz kaydedilmiş bir oturum yok.",
    "history.cut": "KESİLEN",
    "history.farmDuration": "Farm süresi: {dur}",
    "history.noChar": "Karakter tespit edilemedi",
    "history.client": "Client {n} —",
    "history.locale": "tr-TR",
    // --- Metin turleri ---
    "metin.dovus": "Dövüş Metni",
    "metin.savas": "Savaş Metni",
    "metin.golge": "Gölge Metni",
    "metin.kiskanclik": "Kıskançlık Metni",
    "metin.karanlik": "Karanlık Metin",
    "metin.ruh": "Ruh Metni",
    "metin.dayaniklilik": "Dayanıklılık Metni",
    "metin.genel": "Genel Metin",
    // --- Cesitli ---
    "misc.focusedPid": "Odaklanılan PID: {pid}",
  },
  en: {
    "update.title": "Update Available!",
    "update.message": "A new version of the software has been released.",
    "update.messageFull": "A new version has been released. Download the latest version to continue.",
    "update.discordBtn": "Go to Discord Server",
    "update.retryBtn": "Retry",
    "session.checkText": "Checking your session, redirecting...",
    "session.updateCheck": "Checking for updates...",
    "login.keyLabel": "Key",
    "login.remember": "Remember me",
    "login.submit": "Sign In",
    "login.submitting": "Signing in...",
    "login.keyToggleAria": "Show/hide key",
    "login.invalidKey": "Invalid key.",
    "login.revoked": "Your license is no longer valid, please sign in again.",
    "login.serverError": "Could not connect to server ({detail}). Please try again.",
    "login.badResponse": "server returned an invalid response (HTTP {status}): {body}",
    "nav.dashboard": "Home",
    "nav.profile": "Profile",
    "nav.history": "History",
    "nav.functions": "Functions",
    "nav.settings": "Settings",
    "nav.manual": "Manual Scan",
    "manual.title": "Manual Scan",
    "manual.enable": "Manual Scan",
    "manual.enableOn": "On — metin models are skipped, only your image is searched",
    "manual.enableOff": "Off — metin models are used",
    "manual.step1": "Target Images",
    "manual.pick": "Add Target",
    "manual.picking": "Crop tool open...",
    "manual.noImage": "No targets added yet",
    "manual.emptyHint": "Use the button below to crop a target from the game.",
    "manual.remove": "Remove",
    "manual.step2": "2. Click Offset",
    "manual.step2desc": "Where to click relative to the image center. Example: to click 70px below a name label, set Y = 70.",
    "manual.offsetX": "Horizontal (X)",
    "manual.offsetXNote": "Positive = right, negative = left",
    "manual.offsetY": "Vertical (Y)",
    "manual.offsetYNote": "Positive = down, negative = up",
    "manual.step3": "3. Click Type",
    "manual.step3desc": "Which button to click with. Only one can be active at a time.",
    "manual.left": "Left Click",
    "manual.right": "Right Click",
    "manual.errNoWindow": "Game window not found. Open the game first.",
    "manual.errNoFrame": "Could not capture the game screen.",
    "manual.errSave": "Could not save the image.",
    "manual.cancelled": "Crop cancelled.",
    "manual.needImage": "Select an image first.",
    "nav.theme": "Theme",
    "theme.title": "Theme",
    "sidebar.expiry": "Time Left",
    "btn.start": "Start",
    "btn.pause": "Pause",
    "btn.resume": "Resume",
    "btn.stopBot": "Stop Bot",
    "btn.stop": "Stop",
    "btn.logout": "Log Out",
    "stat.farmTime": "Farm Time",
    "stat.cutText": "Metins Cut",
    "stat.totalClicks": "Total Clicks",
    "work.state.waiting": "Idle",
    "work.state.cutting": "Cutting Metin",
    "work.state.searching": "Searching Target",
    "work.state.paused": "Paused",
    "work.sub.default": "Press Start.",
    "work.badge.ready": "Ready",
    "work.badge.running": "Running",
    "work.badge.paused": "Paused",
    "work.step.scan": "Scanning",
    "work.step.lock": "Target Lock",
    "work.step.cut": "Cutting",
    "info.serverSelect": "Server Selection",
    "server.active": "Active",
    "server.maintenance": "Maintenance",
    "server.passive": "Passive",
    "server.pickHint": "Select a server",
    "server.add": "Add server",
    "server.addName": "Server name",
    "server.addNamePh": "e.g. Misali2",
    "server.addTitle": "Window title (optional)",
    "server.addTitlePh": "Server name is used if left empty",
    "server.addCancel": "Cancel",
    "server.addSave": "Add",
    "server.addErrName": "Server name cannot be empty.",
    "server.addErrSave": "Could not save the server.",
    "toast.started": "Bot started running",
    "toast.stopped": "Bot stopped running",
    "toast.noWindow": "Game window not found",
    "toast.noWindowHint": "Searched: {hint}",
    "info.language": "Language Selection",
    "lang.tr": "Turkish",
    "lang.en": "English",
    "info.activeClient": "Active Client",
    "clientMode.auto": "Automatic",
    "clientMode.autoCount": "{n} Clients (Auto)",
    "profile.title": "Profile",
    "profile.hero.title": "Elbit Membership",
    "profile.hero.sub": "Licensed session",
    "profile.hero.subUnlimited": "Unlimited membership",
    "profile.hero.pill.active": "Active",
    "profile.hero.pill.warn": "Expiring Soon",
    "profile.hero.pill.critical": "Running Low",
    "profile.licenseKey": "License Key",
    "profile.timeLeft": "Time Left",
    "profile.clientMode": "Client Mode",
    "profile.totalCut": "Total Cut",
    "aria.showHideKey": "Show/hide key",
    "aria.copyKey": "Copy key",
    "expiry.unlimited": "Unlimited",
    "expiry.expired": "Expired",
    "expiry.remaining": "{days} Days {hours} Hours",
    "expiry.remainingSuffix": "{label} left",
    "expiry.expiredLogout": "Your license has expired. Renew your key to continue.",
    "license.revoked": "Your license is no longer valid.",
    "functions.title": "Functions",
    "card.autoLoot.title": "Auto Loot",
    "note.autoLoot.on": "On - after a metin breaks, items are collected with fast Z for 2s",
    "note.autoLoot.off": "Off (normal looting)",
    "card.antiStuck.title": "Anti-Stuck",
    "note.antiStuck.on": "On - if stuck for {min}min, rotate camera + ESC + W/S/D/A to recover",
    "note.antiStuck.off": "Off (classic drop target at 60s)",
    "card.queue.title": "Queueing",
    "note.queue.on": "On - scans during cutting too",
    "card.atPro.title": "A-T Pro",
    "note.atPro.on": "On (distance continuously monitored)",
    "card.humanize.title": "Humanize",
    "note.humanize.on": "On (mouse glides smoothly)",
    "note.humanize.off": "Off (instant click)",
    "card.autoKey.title": "Auto Press",
    "note.autoKey.on": "On - {n} keys defined",
    "note.autoKey.onEmpty": "On but no key entered",
    "autoKey.holdLabel": "Hold",
    "autoKey.every": "every",
    "unit.sec": "s",
    "autoKey.keyPlaceholder": "Press a key",
    "autoKey.keyTitle": "Click the box and press the key you want (or a combo like Ctrl+X). Press Delete/Backspace to clear.",
    "autoKey.holdTitle": "Holds the key down indefinitely without an interval",
    "btn.autoKeyFarm": "Auto Press Farm",
    "autoKeyFarm.title": "Without searching/clicking targets, just switches to the window and runs the Auto Press keys above",
    "card.scheduler.title": "Scheduler",
    "sched.start": "Scheduled Start",
    "sched.stop": "Scheduled Stop",
    "sched.willStart": "will start at {time} (in {remaining})",
    "sched.willStop": "will stop at {time} (in {remaining})",
    "sched.remaining": "{h}h {m}m",
    "sched.exitAfter": "Also exit the game when Scheduled Stop triggers",
    "sched.minError": "Please enter at least 1 minute.",
    "card.clickMode.title": "Click Mode",
    "clickMode.shift": "Shift + Right Click",
    "clickMode.plain": "Left Click Only",
    "card.inputMethod.title": "Click Method",
    "inputMethod.default": "Default",
    "inputMethod.postmessage": "PostMessage",
    "card.guard.title": "Guard Mode",
    "note.guard.on": "On - window name/PID changing",
    "note.off": "Off",
    "unit.min": "min",
    "unit.hour": "h",
    "settings.title": "Settings",
    "card.calib.title": "Calibration",
    "btn.calibrate": "Calibrate",
    "calib.starting": "Starting...",
    "calib.running": "Calibrating...",
    "calib.done": "Done",
    "calib.failed": "Failed",
    "calib.notStarted": "Could not start",
    "calib.errPrefix": "ERROR ",
    "calib.errStart": "Could not start calibration.",
    "calib.errConn": "ERROR Could not connect to server.",
    "card.theme.title": "Theme Color",
    "swatch.bw": "Black / White / Gray",
    "swatch.blue": "Blue",
    "swatch.green": "Green",
    "swatch.purple": "Purple",
    "swatch.pink": "Pink",
    "swatch.orange": "Orange",
    "swatch.red": "Red",
    "swatch.rainbow": "Rainbow",
    "card.bg.title": "Background",
    "bg.solid": "Dark (Solid)",
    "bg.translucent": "Translucent",
    "card.hotkeys.title": "Shortcuts",
    "hotkey.startKey": "Start key",
    "hotkey.stopKey": "Stop key",
    "btn.assign": "Assign",
    "hotkey.pressKey": "Press a key...",
    "discord.notConnected": "Discord not connected",
    "discord.sub": "Link your account for remote status notifications and commands.",
    "discord.connected": "{user} | Connected",
    "discord.subConnected": "Remote commands and status notifications are active.",
    "btn.discordConnect": "Connect Discord",
    "btn.discordDisconnect": "Disconnect",
    "discord.notConfigured": "Discord integration is not configured yet. (discord_client_id must be set in config.json.)",
    "telegram.notConnected": "Telegram not connected",
    "telegram.sub": "Link your Telegram account for remote status notifications and commands.",
    "telegram.connected": "Telegram | Connected",
    "telegram.subConnected": "Remote commands and status notifications are active.",
    "telegram.soon": "Telegram (coming soon)",
    "telegram.notConfigured": "Telegram integration is not configured yet. (Set telegram_bot_token in config.json.)",
    "telegram.codeInstruction": "In Telegram, send this to <b>{bot}</b>: <b>/baglan {code}</b> (code valid for 15 min)",
    "telegram.botFallback": "your bot",
    "btn.telegramConnect": "Connect Telegram",
    "btn.telegramDisconnect": "Disconnect",
    "history.title": "History",
    "history.empty": "No saved sessions yet.",
    "history.cut": "CUT",
    "history.farmDuration": "Farm time: {dur}",
    "history.noChar": "No character detected",
    "history.client": "Client {n} —",
    "history.locale": "en-GB",
    "metin.dovus": "Fight Metin",
    "metin.savas": "War Metin",
    "metin.golge": "Shadow Metin",
    "metin.kiskanclik": "Envy Metin",
    "metin.karanlik": "Dark Metin",
    "metin.ruh": "Soul Metin",
    "metin.dayaniklilik": "Endurance Metin",
    "metin.genel": "General Metin",
    "misc.focusedPid": "Focused PID: {pid}",
  },
};

const LANG_KEY = "elbit-lang";
let currentLang = (function () {
  try {
    const saved = localStorage.getItem(LANG_KEY);
    if (saved === "tr" || saved === "en") return saved;
  } catch (e) {}
  return "tr";  // varsayilan Turkce
})();

function getLang() {
  return currentLang;
}

// t("anahtar", {isim: deger}) - {isim} yer tutucularini doldurur. Anahtar
// yoksa anahtarin kendisini dondurur (crash yerine gorulur bir iz).
function t(key, params) {
  const table = I18N[currentLang] || I18N.tr;
  let s = table[key];
  if (s === undefined) s = (I18N.tr[key] !== undefined ? I18N.tr[key] : key);
  if (params) {
    for (const k in params) {
      s = s.replace(new RegExp("\\{" + k + "\\}", "g"), params[k]);
    }
  }
  return s;
}

// Statik HTML'i cevirir: data-i18n (textContent), data-i18n-ph (placeholder),
// data-i18n-title (title), data-i18n-aria (aria-label).
function applyStaticI18n(root) {
  const scope = root || document;
  scope.querySelectorAll("[data-i18n]").forEach((el) => {
    el.textContent = t(el.getAttribute("data-i18n"));
  });
  scope.querySelectorAll("[data-i18n-ph]").forEach((el) => {
    el.setAttribute("placeholder", t(el.getAttribute("data-i18n-ph")));
  });
  scope.querySelectorAll("[data-i18n-title]").forEach((el) => {
    el.setAttribute("title", t(el.getAttribute("data-i18n-title")));
  });
  scope.querySelectorAll("[data-i18n-aria]").forEach((el) => {
    el.setAttribute("aria-label", t(el.getAttribute("data-i18n-aria")));
  });
  document.documentElement.setAttribute("lang", currentLang);
}

// Dili degistir: kaydet, statik metinleri yeniden yaz, app.js'e dinamik
// metinleri tazelemesini bildir (window.onLanguageChanged).
function setLanguage(lang) {
  if (lang !== "tr" && lang !== "en") return;
  currentLang = lang;
  try { localStorage.setItem(LANG_KEY, lang); } catch (e) {}
  applyStaticI18n();
  updateLangSwitcherUI();
  if (typeof window.onLanguageChanged === "function") {
    try { window.onLanguageChanged(); } catch (e) {}
  }
}

function updateLangSwitcherUI() {
  document.querySelectorAll(".lang-btn").forEach((b) => {
    b.classList.toggle("active", b.getAttribute("data-lang") === currentLang);
  });
}

// Dil butonlarini bagla + acilista statik cevirileri uygula. DOM hazir
// oldugunda calisir (bu script defer/normal yuklendigi icin body sonunda).
function initI18n() {
  document.querySelectorAll(".lang-btn").forEach((b) => {
    b.addEventListener("click", () => setLanguage(b.getAttribute("data-lang")));
  });
  applyStaticI18n();
  updateLangSwitcherUI();
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", initI18n);
} else {
  initI18n();
}

// Global eris (app.js icin)
window.t = t;
window.getLang = getLang;
window.setLanguage = setLanguage;
window.applyStaticI18n = applyStaticI18n;
