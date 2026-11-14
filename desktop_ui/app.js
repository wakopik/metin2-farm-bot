// ELBIT OTOMASYON - native masaustu uygulamasi mantik katmani.
// Sadece yerel (127.0.0.1) API'ye fetch() ile konusur - internet YOK.
const AUTH_KEY = "elbit-auth-key";
const THEME_KEY = "elbit-theme-color";
let pollTimer = null;  // startPolling/stopPolling'den ONCE tanimlanmali (initAuth() sayfa yuklenir yuklenmez cagirabilir)

// ---------------- Tema rengi ----------------
function hexToRgb(hex) {
  const n = parseInt(hex.slice(1), 16);
  return { r: (n >> 16) & 255, g: (n >> 8) & 255, b: n & 255 };
}
function mix(hex, withWhiteRatio) {
  const { r, g, b } = hexToRgb(hex);
  const m = (c) => Math.round(c + (255 - c) * withWhiteRatio);
  return `rgb(${m(r)}, ${m(g)}, ${m(b)})`;
}
// HSL -> hex (h: 0-360, s/l: 0-100) - rengarenk tema icin sürekli hue
// tarayarak canli renk uretmek icin.
function hslToHex(h, s, l) {
  s /= 100;
  l /= 100;
  const k = (n) => (n + h / 30) % 12;
  const a = s * Math.min(l, 1 - l);
  const f = (n) => l - a * Math.max(-1, Math.min(k(n) - 3, Math.min(9 - k(n), 1)));
  const toHex = (x) => Math.round(255 * x).toString(16).padStart(2, "0");
  return `#${toHex(f(0))}${toHex(f(8))}${toHex(f(4))}`;
}

function applyRawColor(hex) {
  const { r, g, b } = hexToRgb(hex);
  const root = document.documentElement.style;
  root.setProperty("--accent", hex);
  root.setProperty("--accent-hover", mix(hex, 0.18));
  root.setProperty("--accent-dim", `rgba(${r}, ${g}, ${b}, 0.14)`);
  root.setProperty("--accent-glow", `rgba(${r}, ${g}, ${b}, 0.16)`);
  root.setProperty("--accent-shadow", `rgba(${r}, ${g}, ${b}, 0.55)`);
}

// "Rengarenk" tema: requestAnimationFrame ile GECEN SUREYE gore (kare
// hizindan BAGIMSIZ, bu yuzden her zaman ayni hizda) surekli hue
// kaydirilarak TUM vurgu renklerinin (buton/glow/logo/vs. HEPSI var(--accent)
// ustunden turetildigi icin) ayni anda, cok smooth bir sekilde gokkusaginda
// gezinmesini saglar.
let rainbowRAF = null;
function stopRainbowTheme() {
  if (rainbowRAF) {
    cancelAnimationFrame(rainbowRAF);
    rainbowRAF = null;
  }
}
function startRainbowTheme() {
  stopRainbowTheme();
  const start = performance.now();
  const CYCLE_MS = 9000;  // bir tam gokkusagi turu - yeterince yavas/smooth
  function tick(now) {
    const hue = ((now - start) / CYCLE_MS) * 360 % 360;
    applyRawColor(hslToHex(hue, 75, 62));
    rainbowRAF = requestAnimationFrame(tick);
  }
  rainbowRAF = requestAnimationFrame(tick);
}

function applyThemeColor(value) {
  if (value === "rainbow") {
    startRainbowTheme();
  } else {
    stopRainbowTheme();
    applyRawColor(value);
  }
  document.querySelectorAll(".swatch").forEach((s) => s.classList.toggle("active", s.dataset.color === value));
}
const DEFAULT_THEME = "#e8eaed";  // siyah / beyaz / gri - varsayilan monokrom tema
(function initTheme() {
  const saved = localStorage.getItem(THEME_KEY);
  applyThemeColor(saved || DEFAULT_THEME);
})();
document.querySelectorAll(".swatch").forEach((btn) => {
  btn.addEventListener("click", () => {
    applyThemeColor(btn.dataset.color);
    localStorage.setItem(THEME_KEY, btn.dataset.color);
  });
});

// ---------------- Guard Modu (Ayarlar) ----------------
// Aciksa: pencere adi + PID'si surekli degisir (C++ tarafi, bkz. /api/party ve
// WndProc WM_TIMER). Bot calismasini HIC etkilemez.
// NOT (kullanici talebi): eskiden bu mod TEMAYI de otomatik olarak RGB
// (gokkusagi) yapiyordu - o davranis KALDIRILDI. Tema artik SADECE kullanicinin
// Ayarlar'dan sectigi renktir, bu mod ona hic dokunmaz. Depolama anahtarlari
// (elbit-party-*) ve /api/party uc noktasi BILEREK ayni birakildi: yeniden
// adlandirmak mevcut kullanicilarin kayitli ayarini sifirlardi.
const PARTY_ENABLED_KEY = "elbit-party-enabled";
// VARSAYILAN ACIK (kullanici talebi): daha once hic ayarlanmamissa ("1"/"0"
// hic yazilmamissa) acik say - sadece kullanicinin BILEREK "0" kaydettigi
// (Guard Modu'nu elle kapattigi) durumda kapali kalsin.
let partyEnabled = localStorage.getItem(PARTY_ENABLED_KEY) !== "0";
const partyEnableEl = document.getElementById("party-mode-enable");
const partyNoteEl = document.getElementById("party-mode-note");

function setPartyMode(on) {
  partyEnabled = on;
  localStorage.setItem(PARTY_ENABLED_KEY, on ? "1" : "0");
  if (partyEnableEl) partyEnableEl.checked = on;
  if (partyNoteEl) partyNoteEl.textContent = on ? t("note.guard.on") : t("note.off");

  // C++ tarafina bildir (pencere basligi zamanlayicisi bu bayragi okur).
  fetch("/api/party", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ enabled: on }),
  }).catch(() => {});
}

if (partyEnableEl) {
  partyEnableEl.checked = partyEnabled;
  partyEnableEl.addEventListener("change", () => setPartyMode(partyEnableEl.checked));
}
// Acilista: mod kayitli ACIKSA yeniden etkinlestir (C++ tarafi her baslangicta
// bayragi sifirlar, bu yuzden ona TEKRAR bildirmek gerekir).
if (partyNoteEl) partyNoteEl.textContent = partyEnabled ? t("note.guard.on") : t("note.off");
if (partyEnabled) setPartyMode(true);

// ---------------- Arka plan modu (koyu/tok vs yari saydam/glassmorphism) ----------------
const BGMODE_KEY = "elbit-bg-mode";
function applyBgMode(mode) {
  document.body.classList.toggle("bg-translucent", mode === "translucent");
  document.querySelectorAll(".bgmode-btn").forEach((b) => b.classList.toggle("active", b.dataset.bgmode === mode));
}
(function initBgMode() {
  applyBgMode(localStorage.getItem(BGMODE_KEY) || "solid");
})();
document.querySelectorAll(".bgmode-btn").forEach((btn) => {
  btn.addEventListener("click", () => {
    applyBgMode(btn.dataset.bgmode);
    localStorage.setItem(BGMODE_KEY, btn.dataset.bgmode);
  });
});

const loginScreen = document.getElementById("login-screen");
const dashboardScreen = document.getElementById("dashboard-screen");
const loginForm = document.getElementById("login-form");
const keyInput = document.getElementById("key-input");
keyInput.addEventListener("input", () => {
  keyInput.classList.remove("input-error");
  document.getElementById("login-error").classList.add("hidden");
});
const rememberInput = document.getElementById("remember-input");
const loginError = document.getElementById("login-error");

// ---------------- Anahtar goster/gizle (goz butonu) ----------------
document.getElementById("toggle-key-visibility").addEventListener("click", () => {
  const showing = keyInput.type === "text";
  keyInput.type = showing ? "password" : "text";
  document.getElementById("eye-open").classList.toggle("hidden", !showing);
  document.getElementById("eye-closed").classList.toggle("hidden", showing);
});

// EKRAN GECISI: display:none aninda oldugu icin dogrudan gecis yaparsak
// (opacity animasyonuna ragmen) sert/aniden olurdu - once mevcut ekrani
// SOLDURUP (fade-out), o bitince gizleyip digerini opaklik 0'dan 1'e
// yükselterek (fade-in) GECISI iki asamali/yumusak yapiyoruz.
function crossFade(hideEl, showEl, after) {
  hideEl.classList.add("fade-out");
  window.setTimeout(() => {
    hideEl.classList.add("hidden");
    hideEl.classList.remove("fade-out");
    showEl.classList.remove("hidden");
    showEl.classList.add("fade-out");
    // Yeniden reflow zorlayip opacity 0 -> 1 gecisinin GERCEKTEN animasyonlu
    // olmasini sagliyoruz (aksi halde tarayici iki class degisikligini
    // tek karede birlestirip "atlardi").
    void showEl.offsetWidth;
    showEl.classList.remove("fade-out");
    if (after) after();
  }, 240);
}

function showDashboard() {
  crossFade(loginScreen, dashboardScreen, () => {
    loadMetins();
    startPolling();
    setKeyDisplays();
    startExpiryCountdown();
    startRelicenseTimer();
  });
}

// ---------------- Anahtar goster/gizle (Profil sekmesi) ----------------
let keyRevealed = { profile: false };
function getStoredKey() {
  return localStorage.getItem(AUTH_KEY) || sessionStorage.getItem(AUTH_KEY) || "";
}
function maskKey(key) {
  return key.length > 2 ? key[0] + "•".repeat(Math.max(3, key.length - 2)) + key[key.length - 1] : "••••";
}
function setKeyDisplays() {
  const key = getStoredKey();
  const profileEl = document.getElementById("profile-key-value");
  if (profileEl) profileEl.textContent = keyRevealed.profile ? key || "—" : maskKey(key);
}
document.getElementById("profile-key-toggle")?.addEventListener("click", () => {
  keyRevealed.profile = !keyRevealed.profile;
  setKeyDisplays();
});

// ---------------- Kopyala butonu (anahtar) ----------------
// Profil sekmesinde anahtari elle yazip HATA yapmadan (ör. destek
// talebine yapistirmak icin) kopyalamak icin.
async function copyToClipboard(text, btn) {
  if (!text) return;
  try {
    await navigator.clipboard.writeText(text);
  } catch {
    return;  // pano izni yoksa sessizce vazgec - kritik bir islev degil
  }
  if (!btn) return;
  const original = btn.innerHTML;
  btn.innerHTML =
    '<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M20 6 9 17l-5-5"/></svg>';
  setTimeout(() => {
    btn.innerHTML = original;
  }, 1200);
}
document.getElementById("profile-key-copy")?.addEventListener("click", (e) => {
  copyToClipboard(getStoredKey(), e.currentTarget);
});
function showLogin() {
  crossFade(dashboardScreen, loginScreen, () => stopPolling());
}

// ---------------- Kalan Sure (uyelik bitis geri sayimi) ----------------
// Bitis zamani lisans sunucusunun kendisinden gelir (bkz.
// desktop_app_main.cpp /api/login) - HER basarili girişte TAZELENIR
// (Dashboard'da sure uzatilmis/degistirilmis olabilir), sadece CANLI
// (saniyede bir) geri sayim icin cihazda (localStorage) tutulur.
const EXPIRY_KEY = "elbit-membership-expiry";
const EXPIRY_UNLIMITED_KEY = "elbit-membership-unlimited";
let expiryTimer = null;
let relicenseTimer = null;
let loggingOut = false;  // updateExpiryCountdown ve relicense kontrolu AYNI anda tetiklenirse cift cikisi engeller

// Lisans panelinden anahtar iptal edilirse/suresi biterse, kullaniciyi
// oturum SIRASINDA (sadece bir sonraki girişte degil) atar: botu durdurur,
// oturumu temizler, giris ekranina donup NEDENI gosterir - bkz.
// checkLicenseWithServer / updateExpiryCountdown / periodik yeniden kontrol.
async function forceLogout(message) {
  if (loggingOut) return;
  loggingOut = true;
  // ANINDA durdur: showLogin() polling'i ancak gecis animasyonu BITTIKTEN
  // sonra (~240ms) durduruyordu, poll ise 250ms'de bir calisiyor - lisans
  // iptalinde durum hala "iptal" dondugu icin araya bir poll daha girip
  // forceLogout'u TEKRAR tetikleyebilir, bu da /api/stop'u ust uste cagiran
  // bir cikis dongusune yol acabilirdi. Once durduruyoruz.
  stopPolling();
  try {
    await fetch("/api/stop", { method: "POST" });
  } catch {}
  localStorage.removeItem(AUTH_KEY);
  sessionStorage.removeItem(AUTH_KEY);
  localStorage.removeItem(EXPIRY_KEY);
  localStorage.removeItem(EXPIRY_UNLIMITED_KEY);
  if (expiryTimer) { clearInterval(expiryTimer); expiryTimer = null; }
  if (relicenseTimer) { clearInterval(relicenseTimer); relicenseTimer = null; }
  showLogin();
  if (message) showLoginError(message);
  loggingOut = false;
}

// Lisans sunucusuna karsi anahtari yeniden dogrular (init+license, HWID
// dahil) - basarili olursa {ok:true, ...} doner ve expiry'yi TAZELER,
// basarisiz olursa false doner (cagiran taraf forceLogout etmeli).
async function checkLicenseWithServer(key) {
  try {
    const res = await fetch("/api/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ key }),
    });
    const data = await res.json();
    if (data.ok) ensureExpiryStored(data);
    return data;
  } catch {
    // Sunucuya (kendi yerel exe'mize) ulasilamiyorsa aginin/exe'nin kendisi
    // sorunlu demektir - bu durumda kullaniciyi ATMIYORUZ (gecici bir yerel
    // hata yuzunden haksiz yere disari atmamak icin).
    return { ok: true, _networkError: true };
  }
}

// YEDEK KATMAN: anahtarin iptalini/sure dolmasini yakalayan ASIL mekanizma
// artik C++ tarafindaki lisans bekcisidir (bkz. desktop_app_main.cpp
// LicenseWatchdogLoop) - 60 saniyede bir, bot calissin calismasin, webview'in
// hayatta olmasindan BAGIMSIZ olarak dogrular ve sonucu /api/status ile
// bildirir (bkz. render()). Buradaki JS zamanlayicisi sadece IKINCI bir
// guvenlik agi; bu yuzden araligi 5 dakikadan 15 dakikaya cikarildi -
// ikisi ayni anda lisans sunucusuna gidince "tek anahtar icin tek oturum" kurali
// yuzunden birbirinin oturumunu gecersiz kilip HAKSIZ "Oturum bulunamadi"
// hatasi uretebiliyordu.
function startRelicenseTimer() {
  if (relicenseTimer) return;
  relicenseTimer = setInterval(async () => {
    const key = getStoredKey();
    if (!key) return;
    const data = await checkLicenseWithServer(key);
    // data.network_error: yerel sunucuya ULASILDI ama O, lisans sunucusuna
    // ulasamadi (bkz. desktop_app_main.cpp /api/login). Bu durumda anahtarin
    // gecersiz oldugu SONUCU CIKARILAMAZ - kullaniciyi ATMIYORUZ, bir
    // sonraki turda tekrar denenir. Gecici internet kesintisinin oturumu
    // kapatmasi sorunu ("bot kendi kendine oturumu kapatiyor") buradan
    // geliyordu.
    if (!data.ok && !data._networkError && !data.network_error) {
      forceLogout(data.message || t("license.revoked"));
    }
  }, 15 * 60 * 1000);
}
function ensureExpiryStored(loginResponse) {
  // loginResponse: { unlimited: true } YA DA { expires_at: <unix saniye> } -
  // bkz. desktop_app_main.cpp /api/login yaniti.
  if (loginResponse.unlimited) {
    localStorage.setItem(EXPIRY_UNLIMITED_KEY, "1");
    localStorage.removeItem(EXPIRY_KEY);
    return;
  }
  localStorage.removeItem(EXPIRY_UNLIMITED_KEY);
  const expiresAt = loginResponse.expires_at * 1000;
  localStorage.setItem(EXPIRY_KEY, String(expiresAt));
}
function startExpiryCountdown() {
  updateExpiryCountdown();
  if (expiryTimer) return;
  expiryTimer = setInterval(updateExpiryCountdown, 1000);
}
// Kalan sure IKI yerde gosteriliyor (sidebar + Profil) - hepsini TEK bir
// yerden guncelliyoruz.
function updateExpiryCountdown() {
  const targets = [
    document.getElementById("expiry-countdown"),
    document.getElementById("profile-expiry-value"),
  ];

  if (localStorage.getItem(EXPIRY_UNLIMITED_KEY) === "1") {
    targets.forEach((el) => {
      if (!el) return;
      el.textContent = t("expiry.unlimited");
      el.classList.remove("expiry-warn", "expiry-critical");
    });
    setProfileHero("", t("profile.hero.subUnlimited"));
    return;
  }

  const stored = localStorage.getItem(EXPIRY_KEY);
  if (!stored) {
    targets.forEach((el) => el && (el.textContent = "—"));
    return;
  }
  const remainingMs = Number(stored) - Date.now();
  targets.forEach((el) => el && el.classList.remove("expiry-warn", "expiry-critical"));

  if (remainingMs <= 0) {
    targets.forEach((el) => {
      if (!el) return;
      el.textContent = t("expiry.expired");
      el.classList.add("expiry-critical");
    });
    // Sadece etiketi degistirmekle kalma - suresi dolan kullaniciyi FIILEN
    // disari at (botu durdur, oturumu kapat, giris ekranina don).
    forceLogout(t("expiry.expiredLogout"));
    return;
  }
  const totalHours = Math.floor(remainingMs / 3_600_000);
  const days = Math.floor(totalHours / 24);
  const hours = totalHours % 24;
  const label = t("expiry.remaining", { days, hours });
  targets.forEach((el) => {
    if (!el) return;
    el.textContent = label;
    if (days < 1) el.classList.add("expiry-critical");
    else if (days < 3) el.classList.add("expiry-warn");
  });
  setProfileHero(days < 1 ? "critical" : days < 3 ? "warn" : "", t("expiry.remainingSuffix", { label }));
}

// PROFIL BASLIGI: uyeligin durumunu tek bakista okunur kilar - kalan sureye
// gore renk (normal / uyari / kritik) ve kisa bir aciklama.
function setProfileHero(level, subText) {
  const pill = document.getElementById("profile-hero-pill");
  const pillText = document.getElementById("profile-hero-pill-text");
  const sub = document.getElementById("profile-hero-sub");
  if (pill) pill.className = "profile-hero-pill" + (level ? " " + level : "");
  if (pillText) {
    const pillTxt =
      level === "critical" ? t("profile.hero.pill.critical")
      : level === "warn" ? t("profile.hero.pill.warn")
      : t("profile.hero.pill.active");
    if (pillText.textContent !== pillTxt) pillText.textContent = pillTxt;
  }
  if (sub && subText && sub.textContent !== subText) sub.textContent = subText;
}

// ---------------- Zorunlu guncelleme kontrolu ----------------
// Uygulama HER ACILISTA (giris/panel ekranindan ONCE) update_check_url'deki
// JSON'u ({"version":"...", "message":"..."}) kontrol eder - gelistirici
// GitHub'daki bu dosyayi degistirip surumu yukselttiginde, ESKI surumdeki
// TUM kullanicilar bir sonraki acilista giris/panele ULASAMAZ, sadece bu
// ekrani gorur. Ag hatasi/sunucu erisilemez DURUMUNDA FAIL-OPEN davranilir
// (kullanici ENGELLENMEZ) - guncelleme kontrolunun kendisi bir kesinti
// SEBEBI OLMAMALI.
const updateDiscordBtn = document.getElementById("update-discord-btn");
updateDiscordBtn.addEventListener("click", () => {
  fetch("/api/update/open-discord", { method: "POST" }).catch(() => {});
});

// "Tekrar Dene": gelistirici surumu geri dusurdukten/duzelttikten sonra,
// kullanicinin PLQK73MN.exe'yi KAPATIP TEKRAR ACMASINA gerek kalmadan hemen
// devam edebilmesi icin - sayfayi TAMAMEN yeniler (initAuth() -> checkForUpdate()
// baştan calisir, en basit ve HATAYA en KAPALI yol).
document.getElementById("update-retry-btn").addEventListener("click", () => {
  location.reload();
});

// GUNCELLEME KONTROLU - HER ACILISTA, HER SEYDEN ONCE (lisans kontrolunden
// bile once). Amac: 3.0 yayinlandiginda 2.0 kullananlarin onune "Güncelleme
// Mevcut" ekrani KESIN ciksin. Guncelleme sunucusuna ULASILAMAZSA (TLS/ag/
// sunucu kesintisi) sunucu checked=false doner; bunu "guncelim" saymak yerine
// SESSIZCE GECMEDEN birkac kez TEKRAR DENERIZ. Ancak kalici bir kesintide
// (tum denemeler basarisiz) yine de gecise izin veririz - yoksa guncelleme
// servisi coktugunde HERKES uygulamadan kilitlenirdi.
async function checkForUpdate() {
  const MAX_TRIES = 3;
  for (let attempt = 0; attempt < MAX_TRIES; attempt++) {
    let data = null;
    try {
      const res = await fetch("/api/version-check");
      data = await res.json();
    } catch {
      data = null;  // yerel sunucu daha ayaga kalkmamis olabilir
    }

    // Ulasilamadi / gecersiz yanit -> SESSIZCE GECME, tekrar dene.
    if (!data || !data.ok || data.checked === false) {
      if (attempt < MAX_TRIES - 1) {
        await new Promise((r) => setTimeout(r, 1500));
        continue;
      }
      return false;  // kalici kesinti - kullaniciyi ENGELLEME (fail-open)
    }

    if (!data.updateRequired) return false;  // kesin: guncel
    document.getElementById("update-message").textContent =
      data.message || t("update.messageFull");
    loginScreen.classList.add("hidden");
    dashboardScreen.classList.add("hidden");
    document.getElementById("session-check-screen")?.classList.add("hidden");
    document.getElementById("update-screen").classList.remove("hidden");
    return true;
  }
  return false;
}

// Sayfa acilirken hatirlanan anahtar varsa - HER ACILISTA lisans
// sunucusuna karsi YENIDEN dogrulanir (Dashboard'da iptal edilmis/suresi
// bitmis olabilir, sadece localStorage'a guvenilmez). Dogrulama bitene
// kadar hicbir ekran gosterilmez (ne panel ne giris) - gecersizse dogrudan
// giris ekranina, geciyorsa dogrudan panele gecilir.
const sessionCheckScreen = document.getElementById("session-check-screen");

// ACILIS ANIMASYONU: guncelleme/oturum kontrolu BITTIKTEN sonra, giris veya
// panel gosterilmeden hemen once bir kez oynar. Animasyon tamamen CSS'te;
// burada sadece ekrani gosterip suresi kadar bekliyoruz. Kullanici hareket
// azaltma tercihi vermisse (isletim sistemi ayari) sahneyi kisa kesiyoruz -
// o tercih "bu animasyonu izlemek istemiyorum" demektir.
const INTRO_MS = 2980;
function playIntro() {
  const el = document.getElementById("intro-screen");
  if (!el) return Promise.resolve();
  const reduced = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  const hold = reduced ? 600 : INTRO_MS;
  el.classList.remove("hidden");
  return new Promise((resolve) => {
    setTimeout(() => {
      el.classList.add("intro-out");
      setTimeout(() => {
        el.classList.add("hidden");
        el.classList.remove("intro-out");
        resolve();
      }, 690);  // intro-screen-out suresiyle ayni
    }, hold);
  });
}

(async function initAuth() {
  const saved = localStorage.getItem(AUTH_KEY) || sessionStorage.getItem(AUTH_KEY);
  // KULLANICI TALEBI: "Beni hatırla" ile kaydedilmis bir anahtar varsa,
  // dogrulama (lisans sunucusuna karsi, ne kadar surerse sursun - yavas internette
  // uzun bile surse) BITENE KADAR bos giris formunu DEGIL, net bir "kontrol
  // ediliyor" gostergesini goster - "her acilista giris ekranina dusuyorum"
  // hissi buradan geliyordu.
  // Guncelleme kontrolu HER ACILISTA ILK IS - ve bu sirada gorunur bir
  // "kontrol ediliyor" gostergesi (kayitli anahtar olsun olmasin) ki
  // "hicbir sey olmuyor / takildi" hissi OLUSMASIN.
  const sessionCheckText = document.getElementById("session-check-text");
  const savedSessionText = sessionCheckText ? sessionCheckText.textContent : "";
  loginScreen.classList.add("hidden");
  if (sessionCheckText) sessionCheckText.textContent = t("session.updateCheck");
  sessionCheckScreen.classList.remove("hidden");

  if (await checkForUpdate()) return;

  // Guncelleme yok/gecildi -> normal oturum akisina don.
  if (sessionCheckText) sessionCheckText.textContent = savedSessionText;
  // ACIK KAYNAK SURUM: lisans/giris ekrani yok - dogrudan panele gecilir.
  const data = await checkLicenseWithServer(saved || "oss");
  sessionCheckScreen.classList.add("hidden");
  await playIntro();
  // data.network_error: lisans sunucusuna ULASILAMADI (bkz. startRelicenseTimer
  // yorumu). Anahtarin gecersiz oldugu SONUCU CIKARILAMAZ - acilista
  // internet gecici olarak yokken kullaniciyi giris ekranina ATMIYORUZ.
  if (!data.ok && !data.network_error) {
    localStorage.removeItem(AUTH_KEY);
    sessionStorage.removeItem(AUTH_KEY);
    loginScreen.classList.remove("hidden");
    showLoginError(data.message || t("login.revoked"));
    return;
  }
  dashboardScreen.classList.remove("hidden");
  loadMetins();
  startPolling();
  startExpiryCountdown();
  startRelicenseTimer();
  setKeyDisplays();
})();

// ---------------- Buton dokunma (ripple) efekti ----------------
document.addEventListener("click", (e) => {
  const btn = e.target.closest(".btn-primary, .btn-ghost");
  if (!btn || btn.disabled) return;
  const rect = btn.getBoundingClientRect();
  const size = Math.max(rect.width, rect.height);
  const ripple = document.createElement("span");
  ripple.className = "ripple";
  ripple.style.width = ripple.style.height = size + "px";
  ripple.style.left = e.clientX - rect.left - size / 2 + "px";
  ripple.style.top = e.clientY - rect.top - size / 2 + "px";
  btn.appendChild(ripple);
  ripple.addEventListener("animationend", () => ripple.remove());
});

const loginErrorText = document.getElementById("login-error-text");
const loginCard = document.querySelector(".login-card");
function showLoginError(msg) {
  loginErrorText.textContent = msg;
  loginError.classList.remove("hidden");
  keyInput.classList.add("input-error");
  loginCard.classList.remove("shake");
  void loginCard.offsetWidth;
  loginCard.classList.add("shake");
}

loginForm.addEventListener("submit", async (e) => {
  e.preventDefault();
  const key = keyInput.value.trim();
  if (!key) return;
  loginError.classList.add("hidden");
  keyInput.classList.remove("input-error");

  const submitBtn = loginForm.querySelector("button[type='submit']");
  const origBtnText = submitBtn ? submitBtn.textContent : t("login.submit");
  if (submitBtn) {
    submitBtn.disabled = true;
    submitBtn.textContent = t("login.submitting");
  }

  try {
    let data = null;
    // 2 yerine 3 deneme + ARTAN bekleme: yerel sunucu (kendi exe'miz) cok
    // nadiren de olsa ilk anda mesgul olabilir; sessizce pes etmek yerine
    // biraz daha israrci olmak kullaniciyi bosuna giris ekranina dusurmez.
    const backoffMs = [400, 1200];
    for (let attempt = 0; attempt < 3; attempt++) {
      try {
        const res = await fetch("/api/login", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ key }),
        });
        // KRITIK TANI: yaniti once DUZ METIN olarak aliyoruz. Onceden dogrudan
        // res.json() cagriliyordu - sunucu (herhangi bir sebeple) JSON DISI
        // bir sey donerse bu sessizce patliyor ve disaridaki catch "Sunucuya
        // baglanilamadi" gibi TAMAMEN YANILTICI bir mesaj gosteriyordu (oysa
        // sunucuya ULASILMISTI). Artik HTTP durum kodunu ve ham yanitin
        // basini hataya koyuyoruz - bir daha olursa sebep EKRANDA yazacak.
        const raw = await res.text();
        try {
          data = JSON.parse(raw);
        } catch {
          throw new Error(t("login.badResponse", { status: res.status, body: raw.slice(0, 120) }));
        }
        break;
      } catch (err) {
        lastErr = err;
        if (attempt < 2) {
          await new Promise((r) => setTimeout(r, backoffMs[attempt]));
          continue;
        }
        throw err;
      }
    }

    if (!data || !data.ok) {
      showLoginError((data && data.message) || t("login.invalidKey"));
      return;
    }
    if (rememberInput.checked) localStorage.setItem(AUTH_KEY, key);
    else sessionStorage.setItem(AUTH_KEY, key);
    ensureExpiryStored(data);
    showDashboard();
  } catch (err) {
    // Gercek sebebi ARTIK GIZLEMIYORUZ - "Failed to fetch" (baglanti hic
    // kurulamadi) ile "sunucu gecersiz yanit verdi" (baglanti kuruldu ama
    // yanit bozuk) BAMBASKA sorunlardir ve ayni genel mesajin altina
    // saklanmalari teshisi imkansiz kiliyordu.
    const detail = err && err.message ? String(err.message) : String(err);
    showLoginError(t("login.serverError", { detail }));
  } finally {
    if (submitBtn) {
      submitBtn.disabled = false;
      submitBtn.textContent = origBtnText;
    }
  }
});

document.getElementById("logout-btn").addEventListener("click", () => forceLogout());

// ---------------- Sekme gecisi ----------------
document.querySelectorAll(".nav-item").forEach((btn) => {
  btn.addEventListener("click", () => {
    document.querySelectorAll(".nav-item").forEach((b) => b.classList.remove("active"));
    document.querySelectorAll(".tab-panel").forEach((p) => p.classList.add("hidden"));
    btn.classList.add("active");
    const panel = document.getElementById("tab-" + btn.dataset.tab);
    panel.classList.remove("hidden");
    if (btn.dataset.tab === "profil") renderHistory();
    if (btn.dataset.tab === "ayarlar") { refreshDiscordStatus(); refreshTelegramStatus(); }
    if (btn.dataset.tab === "manuel") refreshManualTemplate();
    // Gecis animasyonunu HER tiklamada yeniden tetikle - CSS animasyonu
    // varsayilan olarak sadece elemente ILK class eklendiginde oynar,
    // ayni panele tekrar tekrar geri donulunce (reflow zorlanmadan) sessiz
    // kalirdi.
    panel.style.animation = "none";
    void panel.offsetWidth;
    panel.style.animation = "";
  });
});

// ---------------- Sunucu secimi (kart-secici) ----------------
// Liste /api/servers'tan (lisans sunucusu servers.php) gelir; ulasilamazsa
// asagidaki gomulu varsayilan listeye duser (uygulama cevrimdisi de calissin).
// window_title -> bot.cpp/win_window'un aradigi gercek oyun penceresi basligi.
const DEFAULT_SERVERS = [
  { slug: "misali2",    name: "Misali2",      window_title: "Misali2",      status: "active", logo: null, note: null },
  { slug: "ageofmenor", name: "Age Of Menor", window_title: "Age Of Menor", status: "active", logo: null, note: null },
  { slug: "harbi2",     name: "Harbi2",       window_title: "Harbi2",       status: "active", logo: null, note: null },
  { slug: "kuzey2",     name: "Kuzey2",       window_title: "Kuzey2",       status: "active", logo: null, note: null },
  { slug: "m2hype",     name: "M2Hype",       window_title: "M2Hype",       status: "active", logo: null, note: null },
  { slug: "m2end",      name: "M2End",        window_title: "M2End",        status: "active", logo: null, note: null },
  { slug: "origins",    name: "Origins",      window_title: "Origins",      status: "active", logo: null, note: null },
];
let serverList = DEFAULT_SERVERS.slice();
let selectedServer = (function () { try { return localStorage.getItem("elbit-server") || "misali2"; } catch { return "misali2"; } })();
let selectedServerWindowTitle = "Misali2";
let serverPickerLocked = false;

const spTrigger = document.getElementById("sp-trigger");
const spPanel = document.getElementById("sp-panel");
const spGrid = document.getElementById("server-grid");
const spTriggerName = document.getElementById("sp-trigger-name");
const spTriggerLogo = document.getElementById("sp-trigger-logo");
let serverCloseTimer = null;

function spEscapeHtml(s) {
  return String(s == null ? "" : s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}
function serverStatusLabel(st) {
  return st === "maintenance" ? t("server.maintenance") : st === "passive" ? t("server.passive") : t("server.active");
}
function serverLogoHtml(sv) {
  if (sv.logo) return `<img src="${spEscapeHtml(sv.logo)}" alt="">`;
  const ch = (sv.name || "?").trim().charAt(0).toUpperCase();
  return `<span class="sp-logo-fallback">${spEscapeHtml(ch)}</span>`;
}

function renderServerGrid() {
  if (!spGrid) return;
  spGrid.innerHTML = serverList
    .map((sv) => {
      const selectable = sv.status === "active";
      const isSel = sv.slug === selectedServer;
      const cls = `server-card status-${sv.status}` + (isSel ? " selected" : "") + (selectable ? "" : " disabled");

      // DURUM ROZETI sadece AKTIF OLMAYANLARDA cikar. Her kartta "AKTIF"
      // yazmak 50 kartlik izgarada bilgi degil gurultu uretiyordu; aktif
      // olan zaten varsayilan durum.
      const state = selectable
        ? ""
        : `<span class="sc-state">${spEscapeHtml(serverStatusLabel(sv.status))}</span>`;
      const badge = selectable ? "" : `<span class="sc-badge">${statusBadgeIcon(sv.status)}</span>`;
      const tip = sv.note ? ` title="${spEscapeHtml(sv.note)}"` : "";

      return `<button type="button" class="${cls}" data-slug="${spEscapeHtml(sv.slug)}"${tip} ${selectable ? "" : 'aria-disabled="true"'}>
        <span class="sc-logo">${serverLogoHtml(sv)}${badge}</span>
        <span class="sc-name">${spEscapeHtml(sv.name)}</span>
        ${state}
      </button>`;
    })
    .join("");

  spGrid.querySelectorAll(".server-card:not(.disabled)").forEach((el) => {
    el.addEventListener("click", (e) => { e.stopPropagation(); selectServer(el.dataset.slug); });
  });
}

function statusBadgeIcon(st) {
  // Bakim: anahtar. Pasif: durdurma. Metin yerine ikon - kart kalabaliklasmiyor.
  if (st === "maintenance") {
    return '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"><path d="M14.7 6.3a4 4 0 0 1-5 5L4 17v3h3l5.7-5.7a4 4 0 0 1 5-5l2.6-2.6-2.6-2.6Z"/></svg>';
  }
  return '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/><path d="m8 8 8 8"/></svg>';
}

function updateServerTrigger() {
  const sv = serverList.find((s) => s.slug === selectedServer) || serverList[0];
  if (!sv) return;
  selectedServer = sv.slug;
  selectedServerWindowTitle = sv.window_title || sv.name || sv.slug;
  if (spTriggerName) spTriggerName.textContent = sv.name;
  if (spTriggerLogo) spTriggerLogo.innerHTML = serverLogoHtml(sv);
}

function selectServer(slug) {
  const sv = serverList.find((s) => s.slug === slug);
  if (!sv || sv.status !== "active") return;
  selectedServer = slug;
  try { localStorage.setItem("elbit-server", slug); } catch {}
  updateServerTrigger();
  renderServerGrid();
  // ANINDA kapatmiyoruz: tiklanan kartin "secildi" haline gecisi (tik rozeti +
  // accent halka) hic gorunmeden panel kapanirsa secim onaylanmamis gibi
  // hissettiriyor. Once o gecis oynasin, sonra panel yumusakca kapansin.
  if (serverCloseTimer) clearTimeout(serverCloseTimer);
  serverCloseTimer = setTimeout(() => { serverCloseTimer = null; closeServerPanel(); }, 260);
}

// ACILIM/KAPANIS: yuksekligi JS'te OLCUP piksel olarak veriyoruz.
// Onceki "grid-template-rows: 0fr -> 1fr" yaklasimi, icerigin yuksekligi
// kesinlesene kadar (sunucu logolari yuklenirken) once takilip sonra birden
// dusuyordu. scrollHeight ile olculen deger bu belirsizligi tamamen kaldirir.
// Acilis bitince height:auto'ya donuyoruz ki pencere yeniden boyutlaninca
// veya liste degisince panel kirpilmasin.
let spHeightTimer = null;

function openServerPanel() {
  if (serverPickerLocked || !spPanel || spPanel.classList.contains("open")) return;
  const wrap = document.getElementById("server-picker");
  spPanel.classList.add("open");
  if (wrap) wrap.classList.add("open");
  spTrigger.setAttribute("aria-expanded", "true");

  spPanel.style.height = spPanel.scrollHeight + "px";
  clearTimeout(spHeightTimer);
  spHeightTimer = setTimeout(() => {
    if (spPanel.classList.contains("open")) spPanel.style.height = "auto";
  }, 360);  // CSS'teki .34s gecisten hemen sonra
}

function closeServerPanel() {
  if (!spPanel || !spPanel.classList.contains("open")) return;
  const wrap = document.getElementById("server-picker");
  clearTimeout(spHeightTimer);

  // height:auto'dan 0'a gecis animasyon URETMEZ - once mevcut yuksekligi
  // piksel olarak sabitleyip reflow zorluyoruz, sonra 0'a cekiyoruz.
  spPanel.style.height = spPanel.getBoundingClientRect().height + "px";
  void spPanel.offsetHeight;

  spPanel.classList.remove("open");
  if (wrap) wrap.classList.remove("open");
  spTrigger.setAttribute("aria-expanded", "false");
  spPanel.style.height = "0px";
}
function toggleServerPanel() {
  if (!spPanel) return;
  spPanel.classList.contains("open") ? closeServerPanel() : openServerPanel();
}
function setServerPickerLocked(locked) {
  serverPickerLocked = locked;
  if (spTrigger) spTrigger.classList.toggle("locked", locked);
  if (locked) closeServerPanel();
}

if (spTrigger) spTrigger.addEventListener("click", toggleServerPanel);
document.addEventListener("click", (e) => {
  const p = document.getElementById("server-picker");
  if (p && !p.contains(e.target)) closeServerPanel();
});

async function loadServers() {
  try {
    // UYEYE OZEL SUNUCU: lisans anahtarini gonder - sunucu tarafi herkese
    // acik olanlara EK olarak bu uyeye ozel sunuculari da dondurur.
    let key = "";
    try { key = localStorage.getItem(AUTH_KEY) || ""; } catch {}
    const res = await fetch("/api/servers" + (key ? "?key=" + encodeURIComponent(key) : ""));
    const data = await res.json();
    if (data && data.success && Array.isArray(data.servers) && data.servers.length) {
      serverList = data.servers;
      // Sunucu bir "default" bildiriyorsa (cevrimdisi surum: Baerim) ve kullanici
      // daha once kendi secimini yapmadiysa ilk acilista onu sec.
      let kayitli = null;
      try { kayitli = localStorage.getItem("elbit-server"); } catch {}
      if (data.default && !kayitli) selectedServer = data.default;
    }
  } catch {
    /* lisans sunucusu ulasilamadi - gomulu varsayilan liste kullanilir */
  }
  // Secili sunucu listede yoksa YA DA artik secilemez (bakim/pasif) durumdaysa,
  // ilk AKTIF sunucuya dus - kullanici pasif bir sunucuyla baslatamasin.
  const cur = serverList.find((s) => s.slug === selectedServer);
  if (!cur || cur.status !== "active") {
    const firstActive = serverList.find((s) => s.status === "active");
    if (firstActive) selectedServer = firstActive.slug;
  }
  updateServerTrigger();
  renderServerGrid();
}
loadServers();

// ---------------- Oto Nesne (metin kirilinca 2sn yerinde kalip hizli
// hizli Z basarak yerdeki esyalari toplama - Ayarlar sekmesi) ----------------
// ---------------- Tiklama Degistirme (Fonksiyonlar sekmesi) ----------------
// Iki switch AYNI ayarin iki secenegi: Shift + Sag Tik (varsayilan) veya
// SADECE Sol Tik. Radyo gibi davranirlar - biri acilinca digeri kapanir ve
// ikisi birden kapali BIRAKILAMAZ (tiklama bir sekilde yapilmali). Secim
// /api/start govdesinde click_use_shift olarak gider.
const CLICK_USE_SHIFT_KEY = "elbit-click-use-shift";
let clickUseShift = localStorage.getItem(CLICK_USE_SHIFT_KEY) !== "0"; // varsayilan: Shift + Sag Tik
const clickModeShiftEl = document.getElementById("click-mode-shift");
const clickModePlainEl = document.getElementById("click-mode-plain");

function syncClickModeSwitches() {
  if (clickModeShiftEl) clickModeShiftEl.checked = clickUseShift;
  if (clickModePlainEl) clickModePlainEl.checked = !clickUseShift;
}
function setClickUseShift(useShift) {
  clickUseShift = !!useShift;
  try { localStorage.setItem(CLICK_USE_SHIFT_KEY, clickUseShift ? "1" : "0"); } catch {}
  syncClickModeSwitches();
}
// Her switch KENDI secenegini secer. Zaten acik olani kapatmaya calismak
// bir sey degistirmez (syncClickModeSwitches onu geri isaretler) - boylece
// "ikisi de kapali" durumu hic olusmaz.
if (clickModeShiftEl) {
  clickModeShiftEl.addEventListener("change", () => setClickUseShift(true));
}
if (clickModePlainEl) {
  clickModePlainEl.addEventListener("change", () => setClickUseShift(false));
}
syncClickModeSwitches();

// ---------------- Tiklama Yontemi (Fonksiyonlar sekmesi) ----------------
// Tiklama Degistirme ile AYNI radyo-switch deseni: Varsayilan (mevcut
// kanitlanmis yontem) veya PostMessage (tum fare/klavye mesajlari
// PostMessage ile gonderilir). /api/start govdesinde input_use_postmessage
// olarak gider.
const INPUT_USE_POSTMESSAGE_KEY = "elbit-input-use-postmessage";
let inputUsePostmessage = localStorage.getItem(INPUT_USE_POSTMESSAGE_KEY) === "1";
const inputMethodDefaultEl = document.getElementById("input-method-default");
const inputMethodPostmsgEl = document.getElementById("input-method-postmessage");

function syncInputMethodSwitches() {
  if (inputMethodDefaultEl) inputMethodDefaultEl.checked = !inputUsePostmessage;
  if (inputMethodPostmsgEl) inputMethodPostmsgEl.checked = inputUsePostmessage;
}
function setInputUsePostmessage(usePostmessage) {
  inputUsePostmessage = !!usePostmessage;
  try { localStorage.setItem(INPUT_USE_POSTMESSAGE_KEY, inputUsePostmessage ? "1" : "0"); } catch {}
  syncInputMethodSwitches();
}
// Her switch KENDI secenegini secer; acik olani kapatmaya calismak bir sey
// degistirmez (sync geri isaretler) - ikisi birden kapali kalamaz.
if (inputMethodDefaultEl) {
  inputMethodDefaultEl.addEventListener("change", () => setInputUsePostmessage(false));
}
if (inputMethodPostmsgEl) {
  inputMethodPostmsgEl.addEventListener("change", () => setInputUsePostmessage(true));
}
syncInputMethodSwitches();

// ---------------- Manuel Tarama (kendi gorselini ara + ofsetli tikla) ------
// Akis: switch ac -> "Gorsel Sec" (C++ tarafi cv::selectROI ile kirpma araci
// acar, PNG kaydeder) -> ofset gir -> tiklama turu sec -> Baslat.
// Ayarlar /api/start govdesinde manual_scan_* olarak gider.
const MANUAL_ENABLED_KEY = "elbit-manual-enabled";
let manualEnabled = localStorage.getItem(MANUAL_ENABLED_KEY) === "1";
let manualHasTemplate = false;

const manualEnableEl = document.getElementById("manual-enable");
const manualEnableNoteEl = document.getElementById("manual-enable-note");
const manualStepsEl = document.getElementById("manual-steps");
const manualCaptureBtn = document.getElementById("manual-capture-btn");
const manualGalleryEl = document.getElementById("manual-gallery");
const manualEmptyEl = document.getElementById("manual-empty");

function syncManualUi() {
  if (manualEnableEl) manualEnableEl.checked = manualEnabled;
  if (manualEnableNoteEl) manualEnableNoteEl.textContent = manualEnabled ? t("manual.enableOn") : t("manual.enableOff");
  if (manualStepsEl) manualStepsEl.classList.toggle("is-disabled", !manualEnabled);
}
function saveManual() {
  try { localStorage.setItem(MANUAL_ENABLED_KEY, manualEnabled ? "1" : "0"); } catch {}
}

async function refreshManualTemplate() {
  if (!manualGalleryEl) return;
  let items = [];
  try {
    const res = await fetch("/api/manual/list");
    const d = await res.json();
    items = Array.isArray(d.items) ? d.items : [];
  } catch {
    return;  // yerel sunucu hazir degil - sessizce gec
  }
  manualHasTemplate = items.length > 0;
  if (manualEmptyEl) manualEmptyEl.classList.toggle("hidden", manualHasTemplate);
  manualGalleryEl.innerHTML = items
    .map((it, i) => {
      const id = String(it.id).replace(/"/g, "&quot;");
      return `<div class="mt-card">
        <button class="mt-del" data-id="${id}" title="${t("manual.remove")}" aria-label="${t("manual.remove")}">
          <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"><path d="M18 6 6 18M6 6l12 12"/></svg>
        </button>
        <div class="mt-card-img"><img src="/api/manual/image?id=${encodeURIComponent(it.id)}&t=${Date.now()}" alt=""></div>
        <div class="mt-card-meta"><span class="mt-card-idx">#${i + 1}</span><span>${it.w}x${it.h}</span></div>
      </div>`;
    })
    .join("");
  manualGalleryEl.querySelectorAll(".mt-del").forEach((btn) => {
    btn.addEventListener("click", async () => {
      await fetch("/api/manual/delete", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ id: btn.dataset.id }),
      });
      refreshManualTemplate();
    });
  });
}

if (manualEnableEl) {
  manualEnableEl.addEventListener("change", () => {
    manualEnabled = manualEnableEl.checked;
    saveManual();
    syncManualUi();
    poll();  // Baslat butonunun kilidi manuel moda gore tazelensin
  });
}

if (manualCaptureBtn) {
  manualCaptureBtn.addEventListener("click", async () => {
    const oldText = manualCaptureBtn.textContent;
    manualCaptureBtn.disabled = true;
    manualCaptureBtn.textContent = t("manual.picking");
    try {
      // C++ tarafi cv::selectROI ile BLOKLAR - kullanici ENTER'a basana kadar
      // bu istek donmez, bu normaldir.
      // Secili sunucunun pencere basligini GONDER - config.json'daki
      // varsayilan baslik secilen sunucudan farkli olabilir (bkz. C++ tarafi).
      const res = await fetch("/api/manual/capture", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ window_title_hint: selectedServerWindowTitle, lang: (typeof getLang === "function" ? getLang() : "tr") }),
      });
      const d = await res.json();
      if (!d.ok) {
        // Hangi baslikla arandigini da goster - "bulunamadi" sikayetinin
        // nedeni genelde yanlis sunucu secimi oluyor.
        const msg = d.error === "no_window" ? t("manual.errNoWindow") + (d.hint ? " (Aranan: " + d.hint + ")" : "")
                  : d.error === "no_frame" ? t("manual.errNoFrame")
                  : d.error === "cancelled" ? t("manual.cancelled")
                  : t("manual.errSave");
        alert(msg);
      }
    } catch {
      alert(t("manual.errNoFrame"));
    }
    manualCaptureBtn.disabled = false;
    manualCaptureBtn.textContent = oldText;
    refreshManualTemplate();
  });
}
syncManualUi();
refreshManualTemplate();

const AUTO_LOOT_ENABLED_KEY = "elbit-auto-loot-enabled";
let autoLootEnabled = localStorage.getItem(AUTO_LOOT_ENABLED_KEY) === "1";
const autoLootEnableEl = document.getElementById("auto-loot-enable");
const autoLootNoteEl = document.getElementById("auto-loot-note");
function updateAutoLootNote() {
  if (!autoLootNoteEl) return;
  autoLootNoteEl.textContent = autoLootEnabled ? t("note.autoLoot.on") : t("note.autoLoot.off");
}
if (autoLootEnableEl) {
  autoLootEnableEl.checked = autoLootEnabled;
  autoLootEnableEl.addEventListener("change", () => {
    autoLootEnabled = autoLootEnableEl.checked;
    localStorage.setItem(AUTO_LOOT_ENABLED_KEY, autoLootEnabled ? "1" : "0");
    updateAutoLootNote();
  });
}
updateAutoLootNote();

// ---------------- Anti-Takilma (ayni HP cubugu 2.5 dakikadir ekrandaysa
// kurtarma dizisi calistir - Fonksiyonlar sekmesi) ----------------
const ANTI_STUCK_ENABLED_KEY = "elbit-anti-stuck-enabled";
const ANTI_STUCK_MINUTES_KEY = "elbit-anti-stuck-minutes";
let antiStuckEnabled = localStorage.getItem(ANTI_STUCK_ENABLED_KEY) === "1";
let antiStuckMinutes = Number(localStorage.getItem(ANTI_STUCK_MINUTES_KEY)) || 2.5;
const antiStuckEnableEl = document.getElementById("anti-stuck-enable");
const antiStuckMinutesEl = document.getElementById("anti-stuck-minutes");
const antiStuckNoteEl = document.getElementById("anti-stuck-note");
function updateAntiStuckNote() {
  if (!antiStuckNoteEl) return;
  antiStuckNoteEl.textContent = antiStuckEnabled
    ? t("note.antiStuck.on", { min: antiStuckMinutes })
    : t("note.antiStuck.off");
}
if (antiStuckEnableEl) {
  antiStuckEnableEl.checked = antiStuckEnabled;
  antiStuckEnableEl.addEventListener("change", () => {
    antiStuckEnabled = antiStuckEnableEl.checked;
    localStorage.setItem(ANTI_STUCK_ENABLED_KEY, antiStuckEnabled ? "1" : "0");
    updateAntiStuckNote();
  });
}
if (antiStuckMinutesEl) {
  antiStuckMinutesEl.value = antiStuckMinutes;
  antiStuckMinutesEl.addEventListener("change", () => {
    antiStuckMinutes = Math.min(30, Math.max(0.5, Number(antiStuckMinutesEl.value) || 2.5));
    antiStuckMinutesEl.value = antiStuckMinutes;
    localStorage.setItem(ANTI_STUCK_MINUTES_KEY, String(antiStuckMinutes));
    updateAntiStuckNote();
  });
}
updateAntiStuckNote();

// ---------------- İnsancıllaştır (yumuşak fare) ----------------
const HUMANIZE_ENABLED_KEY = "elbit-humanize-enabled";
let humanizeEnabled = localStorage.getItem(HUMANIZE_ENABLED_KEY) === "1";
const humanizeEnableEl = document.getElementById("humanize-enable");
const humanizeNoteEl = document.getElementById("humanize-note");
function updateHumanizeNote() {
  if (!humanizeNoteEl) return;
  humanizeNoteEl.textContent = humanizeEnabled ? t("note.humanize.on") : t("note.humanize.off");
}
if (humanizeEnableEl) {
  humanizeEnableEl.checked = humanizeEnabled;
  humanizeEnableEl.addEventListener("change", () => {
    humanizeEnabled = humanizeEnableEl.checked;
    localStorage.setItem(HUMANIZE_ENABLED_KEY, humanizeEnabled ? "1" : "0");
    updateHumanizeNote();
  });
}
updateHumanizeNote();

// ---------------- A-T Pro (mesafe tabanli takilma tespiti) ----------------
// Bkz. config.h at_pro_* / bot.cpp AtProRecover - sure girmeye gerek yok,
// merkez nokta (ekran ortasi = karakter) ile hedef nokta (tespit edilen metin)
// arasindaki mesafe azalmiyorsa takilma varsayilir.
const AT_PRO_ENABLED_KEY = "elbit-at-pro-enabled";
let atProEnabled = localStorage.getItem(AT_PRO_ENABLED_KEY) === "1";
const atProEnableEl = document.getElementById("at-pro-enable");
const atProNoteEl = document.getElementById("at-pro-note");
function updateAtProNote() {
  if (!atProNoteEl) return;
  atProNoteEl.textContent = atProEnabled ? t("note.atPro.on") : t("note.off");
}
if (atProEnableEl) {
  atProEnableEl.checked = atProEnabled;
  atProEnableEl.addEventListener("change", () => {
    atProEnabled = atProEnableEl.checked;
    localStorage.setItem(AT_PRO_ENABLED_KEY, atProEnabled ? "1" : "0");
    updateAtProNote();
  });
}
updateAtProNote();

// NESNE MARKET arayuz kontrolu KALDIRILDI (kullanici talebi) - artik
// ANTI-TAKILMA acikken otomatik devrede (bkz. desktop_app_main.cpp).
// Geriye donuk uyumluluk icin bayrak false gonderilir; backend zaten
// anti-takilma ile OR'layarak aciyor.
const nesneMarketEnabled = false;

// ---------------- Siraya Ekleme (kesim sirasinda kuyruklama) ----------------
const QUEUE_OTHERS_KEY = "elbit-queue-others";
let queueOthersEnabled = localStorage.getItem(QUEUE_OTHERS_KEY) === "1";
const queueOthersEl = document.getElementById("queue-others-enable");
const queueOthersNoteEl = document.getElementById("queue-others-note");
function updateQueueOthersNote() {
  if (queueOthersNoteEl)
    queueOthersNoteEl.textContent = queueOthersEnabled ? t("note.queue.on") : t("note.off");
}
if (queueOthersEl) {
  queueOthersEl.checked = queueOthersEnabled;
  queueOthersEl.addEventListener("change", () => {
    queueOthersEnabled = queueOthersEl.checked;
    localStorage.setItem(QUEUE_OTHERS_KEY, queueOthersEnabled ? "1" : "0");
    updateQueueOthersNote();
  });
}
updateQueueOthersNote();


// ---------------- Oto Basma (4 slot: tus + kendi araligi) ----------------
const AUTO_KEY_ENABLED_KEY = "elbit-auto-key-enabled";
const AUTO_KEY_SLOTS_KEY = "elbit-auto-key-slots";
const AUTO_KEY_SLOT_COUNT = 4;
let autoKeyEnabled = localStorage.getItem(AUTO_KEY_ENABLED_KEY) === "1";
let autoKeySlots = [];
try {
  autoKeySlots = JSON.parse(localStorage.getItem(AUTO_KEY_SLOTS_KEY) || "[]");
} catch {
  autoKeySlots = [];
}
while (autoKeySlots.length < AUTO_KEY_SLOT_COUNT) autoKeySlots.push({ key: "", interval: 3, unit: "min", hold: false });
autoKeySlots = autoKeySlots.slice(0, AUTO_KEY_SLOT_COUNT);

const autoKeyRowsEl = document.getElementById("auto-key-rows");
const autoKeyEnableEl = document.getElementById("auto-key-enable");
const autoKeyNoteEl = document.getElementById("auto-key-note");

function saveAutoKeySlots() {
  localStorage.setItem(AUTO_KEY_SLOTS_KEY, JSON.stringify(autoKeySlots));
}

// Bir klavye olayini C++ tarafinin (input.cpp PressKeyTap) anladigi metne
// cevirir: "X", "F5", "SPACE", "CTRL+X", "CTRL+SHIFT+F1"...
// Sadece modifier'a basildiysa (ör. yalniz Ctrl) onun KENDISI dondurulur -
// kullanici gercekten tek basina Ctrl gondermek isteyebilir.
const AUTO_KEY_NAMED = {
  " ": "SPACE",
  Spacebar: "SPACE",
  Enter: "ENTER",
  Escape: "ESC",
  Tab: "TAB",
  Backspace: "BACKSPACE",
  Delete: "DELETE",
  Insert: "INSERT",
  Home: "HOME",
  End: "END",
  PageUp: "PAGEUP",
  PageDown: "PAGEDOWN",
  ArrowUp: "UP",
  ArrowDown: "DOWN",
  ArrowLeft: "LEFT",
  ArrowRight: "RIGHT",
};
function comboFromKeyEvent(e) {
  const mods = [];
  if (e.ctrlKey) mods.push("CTRL");
  if (e.altKey) mods.push("ALT");
  if (e.shiftKey) mods.push("SHIFT");

  const k = e.key;
  // Modifier tusunun KENDISINE basildi (henuz ana tus yok).
  if (k === "Control" || k === "Alt" || k === "Shift") {
    return mods.length ? mods.join("+") : null;
  }

  let base = null;
  if (AUTO_KEY_NAMED[k]) base = AUTO_KEY_NAMED[k];
  else if (/^F([1-9]|1[0-2])$/.test(k)) base = k.toUpperCase();
  else if (k.length === 1 && /[a-z0-9]/i.test(k)) base = k.toUpperCase();
  // Shift+2 gibi durumlarda e.key '"' olur - fiziksel tusu e.code'dan al.
  else if (/^(Key|Digit)[A-Z0-9]$/.test(e.code || "")) base = e.code.slice(-1);

  if (!base) return null;
  return mods.concat(base).join("+");
}
function updateAutoKeyNote() {
  if (!autoKeyNoteEl) return;
  const active = autoKeySlots.filter((s) => s.key && String(s.key).trim());
  autoKeyNoteEl.textContent = autoKeyEnabled
    ? active.length > 0
      ? t("note.autoKey.on", { n: active.length })
      : t("note.autoKey.onEmpty")
    : t("note.off");
}
function buildAutoKeyRows() {
  if (!autoKeyRowsEl) return;
  autoKeyRowsEl.innerHTML = "";
  autoKeySlots.forEach((slot, idx) => {
    const row = document.createElement("div");
    row.className = "sched-row";
    row.style.cssText = "gap:8px; margin-bottom:8px";
    row.innerHTML =
      `<input type="text" class="autokey-key scheduler-num" readonly placeholder="${t("autoKey.keyPlaceholder")}" title="${t("autoKey.keyTitle")}" style="width:110px;height:30px;padding:0;text-align:center;text-transform:uppercase;cursor:pointer" value="${(slot.key || "").replace(/"/g, "")}" />` +
      `<span class="muted autokey-every-label" style="font-size:12px">${t("autoKey.every")}</span>` +
      `<input type="number" class="autokey-int scheduler-num" min="1" max="999" value="${Number(slot.interval) || 3}" />` +
      `<select class="autokey-unit scheduler-num" style="width:auto;padding:6px 8px">` +
      `<option value="sec"${slot.unit === "sec" ? " selected" : ""}>${t("unit.sec")}</option>` +
      `<option value="min"${slot.unit !== "sec" ? " selected" : ""}>${t("unit.min")}</option>` +
      `</select>` +
      `<label style="display:flex;align-items:center;gap:5px;font-size:12px;cursor:pointer;margin-left:4px" title="${t("autoKey.holdTitle")}">` +
      `<input type="checkbox" class="autokey-hold" style="width:auto"${slot.hold ? " checked" : ""} />${t("autoKey.holdLabel")}</label>`;
    const keyEl = row.querySelector(".autokey-key");
    const intEl = row.querySelector(".autokey-int");
    const unitEl = row.querySelector(".autokey-unit");
    const holdEl = row.querySelector(".autokey-hold");
    const everyLabelEl = row.querySelector(".autokey-every-label");
    function applyHoldVisibility() {
      const held = !!autoKeySlots[idx].hold;
      intEl.style.display = held ? "none" : "";
      unitEl.style.display = held ? "none" : "";
      everyLabelEl.style.display = held ? "none" : "";
    }
    applyHoldVisibility();
    holdEl.addEventListener("change", () => {
      autoKeySlots[idx].hold = holdEl.checked;
      applyHoldVisibility();
      saveAutoKeySlots();
    });
    // Tus KAYDETME: kutu salt-okunur, kullanici tiklayip fiziksel olarak
    // basiyor. Boylece "CTRL+X" gibi kombinasyonlar yazim hatasi olmadan,
    // C++ tarafinin (input.cpp PressKeyTap) bekledigi bicimde uretilir.
    keyEl.addEventListener("keydown", (e) => {
      e.preventDefault();
      if (e.key === "Delete" || e.key === "Backspace") {
        autoKeySlots[idx].key = "";
        keyEl.value = "";
        saveAutoKeySlots();
        updateAutoKeyNote();
        return;
      }
      const combo = comboFromKeyEvent(e);
      if (!combo) return;
      autoKeySlots[idx].key = combo;
      keyEl.value = combo;
      saveAutoKeySlots();
      updateAutoKeyNote();
    });
    intEl.addEventListener("change", () => {
      autoKeySlots[idx].interval = Math.max(1, Number(intEl.value) || 1);
      intEl.value = autoKeySlots[idx].interval;
      saveAutoKeySlots();
    });
    unitEl.addEventListener("change", () => {
      autoKeySlots[idx].unit = unitEl.value;
      saveAutoKeySlots();
    });
    autoKeyRowsEl.appendChild(row);
  });
}
if (autoKeyEnableEl) {
  autoKeyEnableEl.checked = autoKeyEnabled;
  autoKeyEnableEl.addEventListener("change", () => {
    autoKeyEnabled = autoKeyEnableEl.checked;
    localStorage.setItem(AUTO_KEY_ENABLED_KEY, autoKeyEnabled ? "1" : "0");
    updateAutoKeyNote();
  });
}
buildAutoKeyRows();
updateAutoKeyNote();

// /api/start govdesine gonderilecek auto_key_slots dizisini (saniyeye
// cevrilmis, sadece tusu dolu olanlar) uretir.
function getAutoKeySlotsForRequest() {
  return autoKeySlots
    .filter((s) => s.key && String(s.key).trim())
    .map((s) => ({
      key: String(s.key).trim(),
      interval_sec: s.hold ? 0 : (Number(s.interval) || 1) * (s.unit === "sec" ? 1 : 60),
      hold: !!s.hold,
    }));
}

// ---------------- Kısayollar (global hotkey: Başlat / Durdur) ----------------
const HOTKEY_START_KEY = "elbit-hotkey-start";
const HOTKEY_STOP_KEY = "elbit-hotkey-stop";
let hotkeyStartCfg = null;
let hotkeyStopCfg = null;
try {
  hotkeyStartCfg = JSON.parse(localStorage.getItem(HOTKEY_START_KEY) || "null");
  hotkeyStopCfg = JSON.parse(localStorage.getItem(HOTKEY_STOP_KEY) || "null");
} catch {
  hotkeyStartCfg = hotkeyStopCfg = null;
}
// VARSAYILAN KISAYOLLAR (kullanici talebi): daha once HIC atanmamissa
// (localStorage'da kayit yoksa) Baslat=F5, Durdur=F6 olarak varsayilan
// atansin - kullanici Ayarlar'dan istedigi zaman degistirebilir. Kullanici
// BILEREK bir tus atadiysa (kayit varsa) buraya hic dokunulmaz.
if (!hotkeyStartCfg) {
  hotkeyStartCfg = { vk: 116, name: "F5" };
  localStorage.setItem(HOTKEY_START_KEY, JSON.stringify(hotkeyStartCfg));
}
if (!hotkeyStopCfg) {
  hotkeyStopCfg = { vk: 117, name: "F6" };
  localStorage.setItem(HOTKEY_STOP_KEY, JSON.stringify(hotkeyStopCfg));
}
const hotkeyStartBtn = document.getElementById("hotkey-start-btn");
const hotkeyStopBtn = document.getElementById("hotkey-stop-btn");
let hotkeyCapturing = null; // "start" | "stop" | null

function hotkeyLabel(cfg) {
  return cfg && cfg.name ? cfg.name : t("btn.assign");
}
function renderHotkeyButtons() {
  if (hotkeyStartBtn) hotkeyStartBtn.textContent = hotkeyCapturing === "start" ? t("hotkey.pressKey") : hotkeyLabel(hotkeyStartCfg);
  if (hotkeyStopBtn) hotkeyStopBtn.textContent = hotkeyCapturing === "stop" ? t("hotkey.pressKey") : hotkeyLabel(hotkeyStopCfg);
}
// KRITIK DUZELTME (kullanici bildirdi: "kisayol tuslari calismiyor"): bu
// fonksiyon acilista TEK SEFER cagriliyordu ve basarisiz olursa (ör. WebView2
// sayfasi, yerel httplib sunucusu daha DINLEMEYE baslamadan JS'i calistirirsa
// - baslangicta olasi bir yaris durumu) SESSIZCE hicbir zaman tekrar
// denemiyordu. Sonuc: arayuzde tus "atanmis" gorunuyor (localStorage'da
// duruyor) ama C++ tarafi (g_startHotkeyVk/g_stopHotkeyVk) hicbir zaman
// haberdar olmuyor, tusa basinca hicbir sey olmuyordu. Artik basarisizlikta
// kisa bir aralikla birkac kez tekrar deniyor; ayrica asagidaki poll()
// dongusunde periyodik olarak da yeniden gonderiliyor (defans hatti - HER
// TURLU baslangic/yeniden-baslama yarisini kendiliginden duzeltir).
async function sendHotkeysToCore(retriesLeft = 5) {
  try {
    const res = await fetch("/api/hotkeys", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        start_vk: hotkeyStartCfg ? hotkeyStartCfg.vk : 0,
        stop_vk: hotkeyStopCfg ? hotkeyStopCfg.vk : 0,
      }),
    });
    if (!res.ok) throw new Error("hotkeys post not ok");
  } catch {
    if (retriesLeft > 0) setTimeout(() => sendHotkeysToCore(retriesLeft - 1), 500);
  }
}
function beginHotkeyCapture(which) {
  hotkeyCapturing = which;
  renderHotkeyButtons();
}
// Yakalama sirasinda TEK bir keydown'i al: e.keyCode = Windows VK kodu (harf/
// rakam/F-tuslari icin birebir ayni), e.key = gosterim adi.
window.addEventListener("keydown", (e) => {
  if (!hotkeyCapturing) return;
  e.preventDefault();
  if (e.key === "Escape") {
    hotkeyCapturing = null;
    renderHotkeyButtons();
    return;
  }
  const vk = e.keyCode || e.which;
  if (!vk) return;
  let name = e.key.length === 1 ? e.key.toUpperCase() : e.key; // "a"->"A", "F9"->"F9"
  const cfg = { vk, name };
  if (hotkeyCapturing === "start") {
    hotkeyStartCfg = cfg;
    localStorage.setItem(HOTKEY_START_KEY, JSON.stringify(cfg));
  } else {
    hotkeyStopCfg = cfg;
    localStorage.setItem(HOTKEY_STOP_KEY, JSON.stringify(cfg));
  }
  hotkeyCapturing = null;
  renderHotkeyButtons();
  sendHotkeysToCore();
});
if (hotkeyStartBtn) hotkeyStartBtn.addEventListener("click", () => beginHotkeyCapture("start"));
if (hotkeyStopBtn) hotkeyStopBtn.addEventListener("click", () => beginHotkeyCapture("stop"));
renderHotkeyButtons();
sendHotkeysToCore(); // acilista C++ tarafina bildir

// ---------------- Client sayisi ----------------
// KULLANICI TALEBI: sayi girme alani TAMAMEN kaldirildi - artik kullanici
// hicbir sayi girmiyor. Secilen sunucu basligiyla eslesen TUM pencereler
// otomatik bulunup kullanilir (bkz. desktop_app_main.cpp - max_clients
// backend'de artik cok yuksek bir tavan, pratikte "hepsi"). maxClients
// buradan HER ZAMAN "sinirsiz" (backend'deki gercek tavan) olarak gonderilir;
// gercek client sayisi calisirken /api/status.client_count'tan okunup
// asagida profil/uye ekranlarina yansitilir.
const maxClients = 9999;
function updateClientModeDisplays(actualCount) {
  const label = actualCount > 0 ? t("clientMode.autoCount", { n: actualCount }) : t("clientMode.auto");
  const profileEl = document.getElementById("profile-client-mode");
  const memberEl = document.getElementById("member-client-mode");
  if (profileEl) profileEl.textContent = label;
  if (memberEl) memberEl.textContent = label;
}
updateClientModeDisplays(0);

// ---------------- Zamanlayici (Zamanli Baslat / Zamanli Durdur) ----------------
// Sunucu (C++ tarafi, bkz. desktop_app_main.cpp SchedulerState) zaten TEK
// dogru kaynak - burada SADECE onun durumunu gosterip kullanicinin
// checkbox/sayi degisikliklerini ilgili /api/scheduler/* uc noktalarina
// iletiyoruz. Zamanlanmis bir islem TETIKLENINCE sunucu kendi tarafinda
// otomatik "enabled=false" yapar - bir sonraki refreshSchedulerStatus()
// cagrisinda checkbox KENDILIGINDEN geri isaretsiz hale gelir.
function formatSchedulerTarget(epochMs) {
  const d = new Date(epochMs);
  return `${String(d.getHours()).padStart(2, "0")}:${String(d.getMinutes()).padStart(2, "0")}`;
}
function formatSchedulerRemaining(epochMs) {
  const totalMin = Math.max(0, Math.round((epochMs - Date.now()) / 60000));
  return t("sched.remaining", { h: Math.floor(totalMin / 60), m: totalMin % 60 });
}

const schedStartEnable = document.getElementById("sched-start-enable");
const schedStartH = document.getElementById("sched-start-h");
const schedStartM = document.getElementById("sched-start-m");
const schedStartNote = document.getElementById("sched-start-note");
const schedStopEnable = document.getElementById("sched-stop-enable");
const schedStopH = document.getElementById("sched-stop-h");
const schedStopM = document.getElementById("sched-stop-m");
const schedExitAfter = document.getElementById("sched-exit-after");
const schedStopNote = document.getElementById("sched-stop-note");

// Sunucudan gelen durumu checkbox'lara YANSITIRKEN, o checkbox'in KENDI
// "change" olay dinleyicisini (sanki kullanici tikladi gibi) TEKRAR
// tetiklememesi icin - aksi halde sonsuz bir geri-bildirim donguru olurdu.
let schedSyncing = false;

async function refreshSchedulerStatus() {
  try {
    const res = await fetch("/api/scheduler/status");
    const data = await res.json();
    schedSyncing = true;
    schedStartEnable.checked = !!data.start_enabled;
    schedStopEnable.checked = !!data.stop_enabled;
    schedExitAfter.checked = !!data.exit_after_stop;
    schedSyncing = false;

    schedStartNote.textContent =
      data.start_enabled && data.start_at_ms
        ? t("sched.willStart", {
            time: formatSchedulerTarget(data.start_at_ms),
            remaining: formatSchedulerRemaining(data.start_at_ms),
          })
        : t("note.off");
    schedStopNote.textContent =
      data.stop_enabled && data.stop_at_ms
        ? t("sched.willStop", {
            time: formatSchedulerTarget(data.stop_at_ms),
            remaining: formatSchedulerRemaining(data.stop_at_ms),
          })
        : t("note.off");
  } catch {
    /* yerel sunucu henuz hazir degil - sessizce tekrar dene */
  }
}

schedStartEnable.addEventListener("change", async () => {
  if (schedSyncing) return;
  if (schedStartEnable.checked) {
    const h = parseInt(schedStartH.value, 10) || 0;
    const m = parseInt(schedStartM.value, 10) || 0;
    if (h === 0 && m === 0) {
      alert(t("sched.minError"));
      schedStartEnable.checked = false;
      return;
    }
    await fetch("/api/scheduler/start", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ enabled: true, hours: h, minutes: m }),
    });
  } else {
    await fetch("/api/scheduler/start", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ enabled: false }),
    });
  }
  refreshSchedulerStatus();
});

schedStopEnable.addEventListener("change", async () => {
  if (schedSyncing) return;
  if (schedStopEnable.checked) {
    const h = parseInt(schedStopH.value, 10) || 0;
    const m = parseInt(schedStopM.value, 10) || 0;
    if (h === 0 && m === 0) {
      alert(t("sched.minError"));
      schedStopEnable.checked = false;
      return;
    }
    await fetch("/api/scheduler/stop", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ enabled: true, hours: h, minutes: m, exit_after_stop: schedExitAfter.checked }),
    });
  } else {
    await fetch("/api/scheduler/stop", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ enabled: false }),
    });
  }
  refreshSchedulerStatus();
});

schedExitAfter.addEventListener("change", async () => {
  if (schedSyncing) return;
  await fetch("/api/scheduler/exit-after-stop", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ value: schedExitAfter.checked }),
  });
});

// ---------------- Metin secimi ----------------
let selectedMetins = [];
// Karisik (obfuscate) klasor adi -> i18n metin anahtari. Gorunen ad t() ile
// secili dilden gelir (bkz. i18n.js "metin.*").
const METIN_I18N_KEYS = {
  dovus: "metin.dovus",
  savas: "metin.savas",
  golge: "metin.golge",
  kiskanclik: "metin.kiskanclik",
  karanlik: "metin.karanlik",
  ruh: "metin.ruh",
  dayaniklilik: "metin.dayaniklilik",
  genel: "metin.genel",
};
function displayMetinName(name) {
  const key = METIN_I18N_KEYS[name];
  return key ? t(key) : name;
}

// KRITIK ("HEDEF SEC listesi BOMBOS" hatasinin KOK NEDENI): yerel HTTP
// sunucusu AYRI bir is parcaciginda baslatilir (desktop_app_main.cpp
// RunLocalServer -> svr.listen) ama WebView2 sayfayi BEKLEMEDEN yukler -
// yani app.js, sunucu 8010 portunu daha BAGLAMADAN /api/metins isteyebilir.
// Bu istek basarisiz olunca liste BOS kaliyor ve BIR DAHA HIC denenmiyordu
// (poll() sadece /api/status'u tazeler, loadMetins'i CAGIRMAZ) - yani liste
// uygulama kapatilip acilana kadar SONSUZA DEK bos kalirdi. Ustelik oturum
// geri yuklenirken checkLicenseWithServer() ag hatasinda BILEREK {ok:true}
// donduruyor (kullaniciyi haksiz yere atmamak icin), bu yuzden sunucu daha
// hazir DEGILKEN bile panele giriliyor ve hata tamamen GORUNMEZ oluyordu.
// Cozum: liste GERCEKTEN dolana kadar kisa araliklarla tekrar dene.
let metinRetryTimer = null;

// Metin turleri arayuzde ARTIK GOSTERILMEZ (kullanici talebi: "orada bos bos
// durmasinlar") - secim zaten yoktu, TUM turler her zaman aktif. Liste yalnizca
// arka planda alinip /api/start'a gonderilir.
async function loadMetins() {
  let rendered = 0;
  try {
    const res = await fetch("/api/metins");
    const list = await res.json();
    if (Array.isArray(list)) {
      selectedMetins = list.map((m) => m.name);
      rendered = list.length;
    }
  } catch {
    /* sunucu henuz hazir degil - asagidaki zamanlayici tekrar dener */
  }

  if (rendered > 0) {
    if (metinRetryTimer) {
      clearTimeout(metinRetryTimer);
      metinRetryTimer = null;
    }
    return;
  }
  // Hic hedef cizilemedi (sunucu hazir degil ya da yanit bos) - tek bir
  // deneme zinciri calistir, liste dolunca kendiliginden durur.
  if (!metinRetryTimer) {
    metinRetryTimer = setTimeout(() => {
      metinRetryTimer = null;
      loadMetins();
    }, 500);
  }
}

// ---------------- Baslat / Duraklat / Durdur ----------------
const startBtn = document.getElementById("start-btn");
// AYRI "Botu Kapat" butonu KALDIRILDI (kullanici talebi) - Baslat butonunun
// KENDISI calisirken "Durdur"a donusur (bkz. stopBot / durum guncelleme).

async function performStart() {
  // MANUEL TARAMA acikken metin listesi gerekmez (metin modelleri calismaz).
  if (selectedMetins.length === 0 && !manualEnabled) return null;
  const res = await fetch("/api/start", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      metins: selectedMetins,
      max_clients: maxClients,
      window_title_hint: selectedServerWindowTitle,
      auto_loot_enabled: autoLootEnabled,
      click_use_shift: clickUseShift,
      input_use_postmessage: inputUsePostmessage,
      manual_scan_enabled: manualEnabled,
      anti_stuck_enabled: antiStuckEnabled,
      anti_stuck_minutes: antiStuckMinutes,
      humanize_enabled: humanizeEnabled,
      at_pro_enabled: atProEnabled,
      queue_others_enabled: queueOthersEnabled,
      nesne_market_dismiss_enabled: nesneMarketEnabled,
      auto_key_enabled: autoKeyEnabled,
      auto_key_slots: getAutoKeySlotsForRequest(),
    }),
  });
  try { return await res.json(); } catch { return null; }
}

// Bot calisiyorken AYNI buton "Durdur" olur ve basinca DURDURUR
// (eskiden duraklatiyordu ve altta ayri bir "Botu Kapat" butonu vardi).
async function stopBot() {
  saveHistoryEntry();
  await fetch("/api/stop", { method: "POST" });
  showToast(t("toast.stopped"), "info");
  poll();
}

// Baslat + bildirim. Hem butondan hem kisayoldan cagrilir ki iki yol da
// ayni geri bildirimi versin.
async function startAndNotify() {
  const d = await performStart();
  if (!d) return;
  // Oyun penceresi yoksa baslatmayi ENGELLEMIYORUZ (oyun birazdan acilabilir,
  // zamanlayici da ayni ucu kullaniyor) - ama kullanici "basladi ama hicbir
  // sey olmuyor" diye beklemesin diye uyariyoruz. Aranan basligi da
  // gosteriyoruz: "bulunamadi" sikayetlerinin cogu yanlis sunucu seciminden.
  if (d.window_found === false) {
    const ek = d.hint ? " - " + t("toast.noWindowHint", { hint: d.hint }) : "";
    showToast(t("toast.noWindow") + ek, "warn", 5200);
  } else {
    showToast(t("toast.started"), "ok");
  }
}

startBtn.addEventListener("click", async () => {
  if (startBtn.dataset.state === "active") {
    await stopBot();
    return;
  }
  await startAndNotify();
  poll();
});

// OTO BASMA FARMI (kullanici talebi): hedef arama/tiklama YAPMADAN, sadece
// pencereye gecip Oto Basma'yi calistirir - performStart() ile AYNI govde,
// ama metins BOS gonderilir (bos oldugu icin normal performStart() bu
// isteği hic atmazdı, o yuzden ayri bir fonksiyon) ve auto_key_only=true
// eklenir (bkz. desktop_app_main.cpp autoKeyOnlyMode).
const autoKeyFarmBtn = document.getElementById("auto-key-farm-btn");
if (autoKeyFarmBtn) {
  autoKeyFarmBtn.addEventListener("click", async () => {
    await fetch("/api/start", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        metins: [],
        max_clients: 1,
        window_title_hint: selectedServerWindowTitle,
        auto_key_enabled: true,
        auto_key_slots: getAutoKeySlotsForRequest(),
        auto_key_only: true,
        // Bu modda tiklama yapilmaz ama govdede GONDERILMEZSE sunucu tarafi
        // varsayilana (Shift + Sag Tik) doner ve kullanicinin secimi silinirdi.
        click_use_shift: clickUseShift,
        input_use_postmessage: inputUsePostmessage,
      }),
    });
    poll();
  });
}


// KISAYOL eylemleri (global hotkey /api/status uzerinden gelir). Baslat tusu:
// bosta baslat, duraklatilmissa devam et (calisirken hicbir sey yapmaz -
// kazayla durdurmasin). Durdur tusu: durdur.
async function hotkeyStart() {
  const st = startBtn.dataset.state;
  if (st === "running") return;
  if (st === "paused") await fetch("/api/resume", { method: "POST" });
  else await startAndNotify();
  poll();
}
async function hotkeyStop() {
  if (!startBtn || startBtn.dataset.state === "idle") return;  // zaten durmus
  saveHistoryEntry();
  await fetch("/api/stop", { method: "POST" });
  showToast(t("toast.stopped"), "info");
  poll();
}

// ---------------- Gecmis (oturum kayitlari) ----------------
// Backend'de bir "gecmis" veritabani/API'si YOK - en son bilinen durumu
// (her pollingde guncellenen lastSnapshot) "Botu Kapat"a basildigi anda
// yakalayip TARAYICI TARAFINDA (localStorage) saklıyoruz. Basit ama
// istenen "oturum bitince ozet gorunsun" ihtiyacini tam karsiliyor.
const HISTORY_KEY = "elbit-history";
let lastSnapshot = null;
// KOPYA KAYIT DUZELTMESI (kullanici sikayeti: "gecmis kismina saçmaliyor
// kopya istatistik gonderiyor"): Durdur tiklamasi VE hotkeyStop() AYNI
// oturumu durdururken IKISI DE saveHistoryEntry() cagirabiliyordu (ör.
// kullanici Durdur'a tiklarken ayni anda/hemen ardindan atanmis Durdur
// kisayolunu da basarsa, ya da butona hizli CIFT tiklarsa) - her ikisi de
// HENUZ guncellenmemis AYNI lastSnapshot'i kullandigi icin gecmise IKI KEZ,
// birebir ayni istatistiklerle kayit dusuyordu. Cozum: oturum basina TEK
// kayit garantisi - bayrak sadece YENI bir oturum GERCEKTEN basladiginda
// (bkz. render() active gecisi) sifirlanir.
let historySavedForSession = false;

function saveHistoryEntry() {
  if (historySavedForSession) return;
  if (!lastSnapshot) return;
  const names = (lastSnapshot.character_names || []).filter((n) => n && n.trim());
  const completed = lastSnapshot.stats?.targets_completed ?? 0;
  // Hicbir sey olmamis (baglanamadan hemen kapatildiysa) bos bir kayit EKLEME.
  if (names.length === 0 && completed === 0) return;
  historySavedForSession = true;

  const history = JSON.parse(localStorage.getItem(HISTORY_KEY) || "[]");
  history.unshift({
    names,
    completed,
    sessionSeconds: lastSnapshot.stats?.session_seconds ?? 0,
    endedAt: Date.now(),
  });
  localStorage.setItem(HISTORY_KEY, JSON.stringify(history.slice(0, 50)));  // en fazla 50 kayit
}

function renderHistory() {
  const list = document.getElementById("history-list");
  const empty = document.getElementById("history-empty");
  const history = JSON.parse(localStorage.getItem(HISTORY_KEY) || "[]");
  if (history.length === 0) {
    list.innerHTML = "";
    empty.classList.remove("hidden");
    return;
  }
  empty.classList.add("hidden");
  list.innerHTML = history
    .map((h, i) => {
      const date = new Date(h.endedAt);
      const loc = t("history.locale");
      const dateStr = date.toLocaleDateString(loc) + " " + date.toLocaleTimeString(loc, { hour: "2-digit", minute: "2-digit" });
      const nameStr = h.names.length > 0 ? h.names.join(", ") : t("history.noChar");
      return `
      <div class="history-card fade-up" style="--delay:${Math.min(i, 8) * 40}ms">
        <div class="history-icon">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="8" r="4"/><path d="M4 21c0-4.4 3.6-8 8-8s8 3.6 8 8"/></svg>
        </div>
        <div class="history-body">
          <div class="history-title">${nameStr}</div>
          <div class="history-sub">${dateStr} · ${t("history.farmDuration", { dur: formatDuration(h.sessionSeconds) })}</div>
        </div>
        <div>
          <div class="history-count">${h.completed}</div>
          <div class="history-count-label">${t("history.cut")}</div>
        </div>
      </div>`;
    })
    .join("");
}

// ---------------- Durum sorgulama (polling) ----------------
function formatDuration(seconds) {
  const total = Math.floor(seconds || 0);
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = total % 60;
  const pad = (n) => String(n).padStart(2, "0");
  return h > 0 ? `${pad(h)}:${pad(m)}:${pad(s)}` : `${pad(m)}:${pad(s)}`;
}

function setStatValue(elId, value) {
  const el = document.getElementById(elId);
  if (el.textContent !== String(value)) {
    el.textContent = value;
    el.classList.remove("bump");
    void el.offsetWidth;
    el.classList.add("bump");
  }
}

function startPolling() {
  if (pollTimer) return;
  poll();
  pollTimer = setInterval(poll, 250);
}
function stopPolling() {
  if (pollTimer) clearInterval(pollTimer);
  pollTimer = null;
}

let pollTickCount = 0;
async function poll() {
  refreshSchedulerStatus();
  try {
    const res = await fetch("/api/status");
    const data = await res.json();
    render(data);
    // KISAYOL: C++ tarafi (global klavye kancasi) bekleyen bir eylem
    // bildirdiyse burada uygula. Oyun odaktayken bile calisir cunku bu poll
    // dongusu WebView2 arka planda olsa da isler.
    if (data.hotkey_action === "start") hotkeyStart();
    else if (data.hotkey_action === "stop") hotkeyStop();

    // KISAYOL SENKRONIZASYONU (defans hatti): 250ms'lik bu donguden 40
    // turda bir (~10sn) atanmis tuslari C++ tarafina YENIDEN gonderiyoruz.
    // sendHotkeysToCore zaten kendi ici retry'a sahip ama bu, ONGORULEMEYEN
    // her turlu senaryoyu (ör. yerel sunucunun RunGuarded tarafindan sessizce
    // yeniden baslatilmasi) da kapatan ucuz/basit bir ek guvence.
    pollTickCount++;
    if (pollTickCount % 40 === 0 && (hotkeyStartCfg || hotkeyStopCfg)) sendHotkeysToCore();
  } catch {
    /* yerel sunucu henuz hazir degil - sessizce tekrar dene */
  }
}

// ---------------- Bildirimler (toast) ----------------
// Sag ust kosede yiginlanan kisa bildirimler. Metin t() uzerinden gecer,
// yani secili dile gore cikar. Ayni anda ekrani doldurmasin diye en fazla
// 4 tanesi durur; fazlasi en eskisinden dusurulur.
const TOAST_ICONS = {
  ok: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"><path d="M20 6 9 17l-5-5"/></svg>',
  warn: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 9v4M12 17h.01"/><path d="M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0Z"/></svg>',
  info: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/><path d="M12 16v-4M12 8h.01"/></svg>',
};

function toastStack() {
  let el = document.getElementById("toast-stack");
  if (!el) {
    el = document.createElement("div");
    el.id = "toast-stack";
    el.className = "toast-stack";
    document.body.appendChild(el);
  }
  return el;
}

function showToast(text, kind = "info", ms = 3200) {
  const stack = toastStack();
  const el = document.createElement("div");
  el.className = "toast toast-" + kind;
  el.innerHTML =
    `<span class="toast-icon">${TOAST_ICONS[kind] || TOAST_ICONS.info}</span>` +
    `<span class="toast-text"></span>`;
  el.querySelector(".toast-text").textContent = text;
  stack.appendChild(el);

  while (stack.children.length > 4) dismissToast(stack.firstElementChild);
  setTimeout(() => dismissToast(el), ms);
  return el;
}

function dismissToast(el) {
  if (!el || el.classList.contains("toast-out")) return;
  el.classList.add("toast-out");
  // Animasyon bitince DOM'dan cikar. animationend gelmezse (ör. sekme arka
  // planda, kare uretilmiyor) yedek zamanlayici temizler - aksi halde
  // bildirimler gorunmez sekilde birikirdi.
  let temizlendi = false;
  const sil = () => { if (!temizlendi) { temizlendi = true; el.remove(); } };
  el.addEventListener("animationend", sil, { once: true });
  setTimeout(sil, 700);
}

let lastToastPid = null;
function showFocusedPidToast(pid) {
  let el = document.getElementById("focused-pid-toast");
  if (!el) {
    el = document.createElement("div");
    el.id = "focused-pid-toast";
    el.style.cssText =
      "position:fixed;bottom:16px;right:16px;background:rgba(20,20,24,0.92);color:#fff;" +
      "padding:8px 14px;border-radius:8px;font-size:12px;font-family:monospace;z-index:9999;" +
      "opacity:0;transition:opacity 0.2s;pointer-events:none;border:1px solid rgba(255,255,255,0.12)";
    document.body.appendChild(el);
  }
  if (!pid) {
    el.style.opacity = "0";
    lastToastPid = null;
    return;
  }
  if (pid !== lastToastPid) {
    el.textContent = t("misc.focusedPid", { pid });
    lastToastPid = pid;
  }
  el.style.opacity = "1";
}

function render(data) {
  // YENI oturum GERCEKTEN basladiginda (aktif degilken aktif oldu) kopya-
  // kayit korumasini sifirla - bkz. historySavedForSession yorumu yukarida.
  if (data.active && !(lastSnapshot && lastSnapshot.active)) historySavedForSession = false;
  lastSnapshot = data;  // "Botu Kapat"a basilinca gecmise kaydedecegimiz en son bilinen durum

  // LISANS IPTALI - HER SEYDEN ONCE. Sunucudaki lisans bekcisi (bkz.
  // desktop_app_main.cpp LicenseWatchdogLoop) anahtarin lisans panelinden
  // silindigini/iptal edildigini ya da suresinin doldugunu tespit ettiyse
  // burayi isaretler. Bu fonksiyon 250ms'de bir calistigi icin kullanici
  // en gec birkac yuz milisaniye icinde disari alinir - eskiden bu SADECE
  // 5 dakikalik bir JS zamanlayicisina bagliydi ve o zamanlayici da tarayici
  // arka plandayken kisilabiliyordu; "uygulamayi kapatip acana kadar lisans
  // devam ediyor" sorununun kok sebebi buydu.
  if (data.license_revoked) {
    forceLogout(data.license_message || t("license.revoked"));
    return;
  }

  // KALAN SURE: tek yetkili kaynak lisans sunucusu. Sunucu her basarili kontrolde
  // bunu tazeledigi icin, panelden sure UZATILIRSA uygulamayi yeniden
  // baslatmaya gerek kalmadan guncel sure gorunur (eskiden sure yalnizca
  // giris aninda alinip localStorage'da donuyordu).
  if (data.license_unlimited) {
    localStorage.setItem(EXPIRY_UNLIMITED_KEY, "1");
    localStorage.removeItem(EXPIRY_KEY);
  } else if (data.license_expires_at) {
    localStorage.removeItem(EXPIRY_UNLIMITED_KEY);
    localStorage.setItem(EXPIRY_KEY, String(data.license_expires_at * 1000));
  }

  const running = data.active && data.running;

  // Baslat/Duraklat/Durdur butonlari
  // (Client sayisi girisi kaldirildi, Hedef secimi kaldirildi - devre disi
  // birakilacak ek giris kalmadi.)
  setServerPickerLocked(!!data.active);

  if (data.active) {
    // Calisiyor (ya da duraklatilmis) - buton DURDUR olur.
    startBtn.dataset.state = "active";
    startBtn.textContent = t("btn.stop");
    startBtn.disabled = false;
  } else {
    startBtn.dataset.state = "idle";
    startBtn.textContent = t("btn.start");
    // MANUEL TARAMA acikken metin listesi gerekmez - buton kilitlenmesin.
    startBtn.disabled = selectedMetins.length === 0 && !manualEnabled;
  }



  // KULLANICI TALEBI: her tik/tus oncesi bulunan PID'i, bulundugu andan
  // itibaren 2-3 saniyeligine kucuk bir kabarcik (toast) olarak goster -
  // C++ tarafi zaten sadece yas < 3sn ise "focused_pid" alanini dolduruyor
  // (bkz. desktop_app_main.cpp ToStatusJson), burada sadece gorunurlugu
  // yonetiyoruz.
  showFocusedPidToast(data.focused_pid);

  // NOT: eski ust "durum banner'i" KALDIRILDI (kullanici talebi) - tasidigi
  // bilgi (status_text/hata) artik Calisma Sureci kartinin alt satirinda
  // gosteriliyor, bkz. asagidaki updateWorkCard cagrisi.

  // Istatistikler - deger GERCEKTEN degistiyse kisa bir "zip" animasyonuyla
  // vurgula (surekli guncellenen sure haric - o zaten her saniye degisiyor,
  // sürekli ziplamasi rahatsiz edici olurdu).
  const stats = data.stats || {};
  document.getElementById("stat-duration").textContent = formatDuration(stats.session_seconds);
  setStatValue("stat-completed", stats.targets_completed ?? 0);

  // Karaktere Baglandi: pencere basligindan ("Misali2 (Feda) | KarakterAdi")
  // C++ tarafinin cikardigi gercek karakter adi/adlari - cift client'ta
  // ikisi de listelenir. KRITIK: innerHTML'i SADECE isimler GERCEKTEN
  // degistiyse yeniden yaziyoruz - onceden HER pollingde (800ms'de bir)
  // aninin AYNISI olsa bile yeniden olusturuluyordu, bu da uzerindeki
  // "fade-up" giris animasyonunun SUREKLI yeniden tetiklenip surekli
  // gorunup-kaybolan bir "titreme" gibi gorunmesine sebep oluyordu.
  // KULLANICI TALEBI: anasayfadaki "Karaktere Bağlandı" bloğu kaldırıldı -
  // element her zaman gizli tutulur (DOM'dan silmek yerine gizlemek, ileride
  // geri istenirse tek satırla geri açılabilmesi için tercih edildi).
  const charInfo = document.getElementById("character-info");
  if (charInfo) charInfo.classList.add("hidden");

  const clientInfo = document.getElementById("client-info");
  if (data.client_count > 1) {
    clientInfo.classList.remove("hidden");
    clientInfo.innerHTML = `<span class="info-label">${t("info.activeClient")}</span><span class="badge">${(data.active_client ?? 0) + 1} / ${data.client_count}</span>`;
  } else {
    clientInfo.classList.add("hidden");
  }
  updateClientModeDisplays(data.client_count ?? 0);

  // NOT: "Kilit Durumu" rozeti kullanici talebiyle Anasayfa'dan KALDIRILDI -
  // ayni bilgi zaten Calisma Sureci kartinda (Hedef Kilidi adimi) gosteriliyor.
  updateWorkCard(data);

  // ---- Profil sekmesi + mini-istatistikler ----
  // NOT: "Toplam Tıklama" karti kullanici talebiyle Anasayfa'dan KALDIRILDI
  // (2 kart kalsin) - onu besleyen mini-stat-clicks yazimi da cikarildi.
  const totalCompleted = stats.targets_completed ?? 0;
  setStatValue("profile-total-completed", totalCompleted);
  // NOT: "Dakika Basi" karti kullanici talebiyle KALDIRILDI - onu besleyen
  // dakika/oran hesabi da buradan cikarildi (baska kullanani yoktu).
}

// ============================================================================
// CALISMA SURECI KARTI
// ----------------------------------------------------------------------------
// Eski "Farm Nabzi" (canvas + requestAnimationFrame ile SANIYEDE 60+ kez
// yeniden cizilen EKG dalgasi) KALDIRILDI: gorsel olarak bir sey anlatmiyordu
// ama surekli islemci yakiyordu. Yerine gelen bu kart, botun GERCEKTEN hangi
// asamada oldugunu gosterir ve TEK BIR JS islemi bile animasyon icin
// harcamaz - tum hareket CSS'te (transform/opacity, GPU katmani). Burasi
// sadece durum sinifini/metni gunceller, o da yalnizca DEGISTIGINDE.
// ============================================================================
function updateWorkCard(data) {
  const card = document.getElementById("work-card");
  if (!card) return;

  const farming = !!(data.active && data.running);
  const paused = !!data.active && !data.running;
  const locked = !!data.is_locked;

  // Durum sinifi (nokta rengini/nefes animasyonunu bu tetikler).
  const stateClass = "run-status" + (farming ? " is-working" : paused ? " is-paused" : "");
  if (card.className !== stateClass) card.className = stateClass;

  let stateText;
  if (farming) {
    stateText = locked ? t("work.state.cutting") : t("work.state.searching");
  } else if (paused) {
    stateText = t("work.state.paused");
  } else {
    stateText = t("work.state.waiting");
  }

  // Alt-metin: hata varsa O oncelikli, yoksa durum metni, yoksa varsayilan.
  const subText = data.error || data.status_text || t("work.sub.default");

  setTextIfChanged("work-state", stateText);
  setTextIfChanged("work-sub", subText);
}

// Gereksiz DOM yazmalarini onler - metin AYNIYSA hic dokunma (bu fonksiyon
// 800ms'de bir calisiyor; her seferinde textContent yazmak, degismese bile
// tarayiciyi bosuna yeniden-boyama isine sokabiliyordu).
function setTextIfChanged(id, text) {
  const el = document.getElementById(id);
  if (el && el.textContent !== text) el.textContent = text;
}

// ---------------- Discord entegrasyonu ----------------
async function refreshDiscordStatus() {
  const card = document.getElementById("discord-card");
  const title = document.getElementById("discord-title");
  const sub = document.getElementById("discord-sub");
  const btn = document.getElementById("discord-connect-btn");
  try {
    const res = await fetch("/api/discord/status");
    const data = await res.json();
    if (data.connected) {
      card.classList.add("connected");
      title.innerHTML = `${t("discord.connected", { user: data.username })} <svg class="discord-check" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M20 6 9 17l-5-5"/></svg>`;
      sub.textContent = t("discord.subConnected");
      btn.textContent = t("btn.discordDisconnect");
      btn.classList.add("connected-state");
    } else {
      card.classList.remove("connected");
      title.textContent = t("discord.notConnected");
      sub.textContent = t("discord.sub");
      btn.textContent = t("btn.discordConnect");
      btn.classList.remove("connected-state");
    }
  } catch {
    /* yerel sunucu henuz hazir degil - sessizce vazgec */
  }
}

document.getElementById("discord-connect-btn").addEventListener("click", async () => {
  const btn = document.getElementById("discord-connect-btn");
  if (btn.classList.contains("connected-state")) {
    await fetch("/api/discord/disconnect", { method: "POST" });
    refreshDiscordStatus();
    return;
  }
  const res = await fetch("/api/discord/connect", { method: "POST" });
  const data = await res.json();
  if (!data.ok && data.error === "not_configured") {
    alert(t("discord.notConfigured"));
  }
});

// ---------------- Telegram entegrasyonu ----------------
// Discord'daki OAuth yerine: "Bağlan"a basınca backend 6 haneli bir kod üretir,
// kullanıcı Telegram'da bota "/baglan KOD" yazar. Aynı komutların hepsi çalışır.
async function refreshTelegramStatus() {
  const card = document.getElementById("telegram-card");
  const title = document.getElementById("telegram-title");
  const sub = document.getElementById("telegram-sub");
  const btn = document.getElementById("telegram-connect-btn");
  if (!card) return;
  try {
    const res = await fetch("/api/telegram/status");
    const data = await res.json();
    if (!data.configured) {
      card.classList.remove("connected");
      title.textContent = t("telegram.soon");
      sub.textContent = t("telegram.notConfigured");
      btn.textContent = t("btn.telegramConnect");
      btn.classList.remove("connected-state");
      btn.disabled = true;
      return;
    }
    btn.disabled = false;
    if (data.connected) {
      card.classList.add("connected");
      title.innerHTML = `${t("telegram.connected")} <svg class="discord-check" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M20 6 9 17l-5-5"/></svg>`;
      sub.textContent = t("telegram.subConnected");
      btn.textContent = t("btn.telegramDisconnect");
      btn.classList.add("connected-state");
    } else {
      card.classList.remove("connected");
      title.textContent = t("telegram.notConnected");
      sub.textContent = t("telegram.sub");
      btn.textContent = t("btn.telegramConnect");
      btn.classList.remove("connected-state");
    }
  } catch {
    /* yerel sunucu henuz hazir degil - sessizce vazgec */
  }
}

document.getElementById("telegram-connect-btn").addEventListener("click", async () => {
  const btn = document.getElementById("telegram-connect-btn");
  const sub = document.getElementById("telegram-sub");
  if (btn.classList.contains("connected-state")) {
    await fetch("/api/telegram/disconnect", { method: "POST" });
    refreshTelegramStatus();
    return;
  }
  const res = await fetch("/api/telegram/code", { method: "POST" });
  const data = await res.json();
  if (!data.ok) {
    alert(t("telegram.notConfigured"));
    return;
  }
  // Kodu ve talimatı kartın alt satırında göster; bot adı biliniyorsa t.me linki aç.
  const uname = data.bot_username ? "@" + data.bot_username : t("telegram.botFallback");
  sub.innerHTML = t("telegram.codeInstruction", { code: data.code, bot: uname });
  if (data.bot_username) {
    window.open("https://t.me/" + data.bot_username, "_blank");
  }
});

// ---------------- Kalibrasyon (Ayarlar sekmesi) ----------------
// Oyun ekranini ~40 sn izleyip esikleri BU BILGISAYARA gore ayarlar.
// TIKLAMA YAPMAZ - sadece kamerayi cevirip olcum alir. Sonuclar hem burada
// listelenir hem de config.json'a yazilir.
const calibBtn = document.getElementById("calib-btn");
const calibWrap = document.getElementById("calib-progress-wrap");
const calibPhase = document.getElementById("calib-phase");
const calibFill = document.getElementById("calib-bar-fill");
const calibResult = document.getElementById("calib-result");
let calibTimer = null;

function renderCalibLines(lines) {
  if (!calibResult) return;
  calibResult.classList.remove("hidden");
  calibResult.innerHTML = lines
    .map((l) => {
      const cls = (l.startsWith("UYARI") || l.startsWith("WARN")) ? "cal-warn"
        : (l.startsWith("HATA") || l.startsWith("ERROR")) ? "cal-err" : "cal-ok";
      const safe = l.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
      return `<div class="${cls}">${safe}</div>`;
    })
    .join("");
}

async function pollCalibration() {
  try {
    const res = await fetch("/api/calibrate/status");
    const st = await res.json();
    if (calibPhase) calibPhase.textContent = st.phase || "";
    if (calibFill) calibFill.style.width = (st.progress || 0) + "%";
    if (Array.isArray(st.lines) && st.lines.length) renderCalibLines(st.lines);
    if (st.error) {
      renderCalibLines([...(st.lines || []), t("calib.errPrefix") + st.error]);
    }
    if (!st.running) {
      clearInterval(calibTimer);
      calibTimer = null;
      if (calibBtn) {
        calibBtn.disabled = false;
        calibBtn.textContent = t("btn.calibrate");
      }
      if (calibPhase) calibPhase.textContent = st.error ? t("calib.failed") : t("calib.done");
    }
  } catch {
    /* sunucu gecici olarak yanit vermiyor - bir sonraki turda tekrar denenir */
  }
}

if (calibBtn) {
  calibBtn.addEventListener("click", async () => {
    calibBtn.disabled = true;
    calibBtn.textContent = t("calib.running");
    if (calibWrap) calibWrap.classList.remove("hidden");
    if (calibResult) calibResult.classList.add("hidden");
    if (calibFill) calibFill.style.width = "0%";
    try {
      const res = await fetch("/api/calibrate/start", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          window_title_hint: selectedServerWindowTitle,
          lang: getLang(),
        }),
      });
      const data = await res.json();
      if (!data.ok) {
        renderCalibLines([t("calib.errPrefix") + (data.message || t("calib.errStart"))]);
        calibBtn.disabled = false;
        calibBtn.textContent = t("btn.calibrate");
        if (calibPhase) calibPhase.textContent = t("calib.notStarted");
        return;
      }
    } catch {
      renderCalibLines([t("calib.errConn")]);
      calibBtn.disabled = false;
      calibBtn.textContent = t("btn.calibrate");
      return;
    }
    if (calibTimer) clearInterval(calibTimer);
    calibTimer = setInterval(pollCalibration, 400);
  });
}

// ---------------- Dil degisiminde dinamik metinleri tazele ----------------
// i18n.js setLanguage() ONCE statik HTML'i (data-i18n) cevirir, SONRA bunu
// cagirir - burada app.js'in KENDI urettigi (state'e bagli) dinamik metinleri
// secili dile gore yeniden yaziyoruz. Yerel state'e dayanan not'lar aninda,
// sunucuya dayananlar (durum/zamanlayici/discord) ise bir sonraki poll'da
// kesinlesir; buradaki cagriler ilk tazelemeyi HEMEN yapar.
window.onLanguageChanged = function () {
  try {
    // Fonksiyon kartlarindaki durum notlari (yerel state - aninda).
    if (typeof updateAutoLootNote === "function") updateAutoLootNote();
    if (typeof updateAntiStuckNote === "function") updateAntiStuckNote();
    if (typeof updateHumanizeNote === "function") updateHumanizeNote();
    if (typeof updateAtProNote === "function") updateAtProNote();
    if (typeof updateQueueOthersNote === "function") updateQueueOthersNote();
    if (typeof updateAutoKeyNote === "function") updateAutoKeyNote();
    if (typeof buildAutoKeyRows === "function") buildAutoKeyRows();  // satir ici placeholder/etiketler
    // Guard modu notu (yerel).
    if (partyNoteEl) partyNoteEl.textContent = partyEnabled ? t("note.guard.on") : t("note.off");
    // Kisayol butonlari (yerel).
    if (typeof renderHotkeyButtons === "function") renderHotkeyButtons();
    // Kalan sure + profil basligi (yerel - localStorage'dan hesaplar).
    if (typeof updateExpiryCountdown === "function") updateExpiryCountdown();
    // Gecmis (yerel - localStorage).
    if (typeof renderHistory === "function") renderHistory();
    // Sunucuya bagli olanlar - hemen bir tazeleme tetikle.
    if (typeof refreshSchedulerStatus === "function") refreshSchedulerStatus();
    if (typeof refreshDiscordStatus === "function") refreshDiscordStatus();
    if (typeof refreshTelegramStatus === "function") refreshTelegramStatus();
    // Sunucu secici durum etiketleri (Aktif/Bakım/Pasif) secili dile gore.
    if (typeof updateServerTrigger === "function") updateServerTrigger();
    if (typeof renderServerGrid === "function") renderServerGrid();
    if (typeof syncManualUi === "function") syncManualUi();
    // Panel goruntudeyse durum/is-karti/baslat butonu/client sayisi icin poll.
    const dash = document.getElementById("dashboard-screen");
    if (dash && !dash.classList.contains("hidden") && typeof poll === "function") poll();
  } catch (e) {
    /* tek bir tazeleme hatasi dil degisimini bozmasin */
  }
};

// Dil secici - bayrakli toggle (sidebar ustu + giris karti sag ust).
// Tum .lang-btn'leri dinler; aktif dili isaretler ve iki switcher'i senkron tutar.
function syncLangButtons() {
  const cur = getLang();
  document.querySelectorAll(".lang-btn").forEach((btn) => {
    btn.classList.toggle("active", btn.dataset.lang === cur);
  });
}
document.querySelectorAll(".lang-btn").forEach((btn) => {
  btn.addEventListener("click", () => {
    setLanguage(btn.dataset.lang);
    syncLangButtons();
  });
});
syncLangButtons();
