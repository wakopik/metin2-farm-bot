#include "telegram_bot.h"

#include <httplib.h>

#include "win_tls_trust.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <fstream>
#include <mutex>

using nlohmann::json;

// desktop_app.exe konsolsuz calistigi icin std::cout hicbir yere gitmiyor -
// hatalari discord_bot.log ile ayni mantikta telegram_bot.log'a yaziyoruz.
static void Log(const std::string& msg) {
    std::ofstream f("telegram_bot.log", std::ios::app);
    if (!f.is_open()) return;
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
    localtime_s(&tmBuf, &t);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tmBuf);
    f << "[" << buf << "] " << msg << "\n";
}

// SSLClient kopyalanamaz/tasinamaz (mutex icerir) - deger dondurulemez;
// bunun yerine cagiran yerde olusturulan client'i yapilandirir.
static void SetupClient(httplib::SSLClient& cli, int readTimeoutSec) {
    // IPv4'e ZORLA. Bazi aglarda (ozellikle TR ISP'leri) api.telegram.org'un
    // IPv6 rotasi kirik/engelli - varsayilan "happy eyeballs" once IPv6'yi
    // deneyip takiliyor ve TUM istekler timeout'a dusuyor ("SSL connection
    // failed"). IPv4'e sabitleyince baglanti kararli calisiyor (olculdu: IPv4
    // 10/10, varsayilan 0/10). Discord bu sorunu yasamiyor - sadece Telegram.
    cli.set_address_family(AF_INET);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(readTimeoutSec);
    ConfigureSystemTrustStore(cli);
}

// chat_id JSON govdede sayi olarak gonderilmeli (Telegram sayi ya da
// "@kullanici" bekler) - sayiya cevrilebiliyorsa sayi, degilse string.
static json ChatIdJson(const std::string& chatId) {
    try {
        size_t pos = 0;
        long long v = std::stoll(chatId, &pos);
        if (pos == chatId.size()) return json(v);
    } catch (...) {
    }
    return json(chatId);
}

// HTML ozel karakterlerini kacisla (Telegram parse_mode=HTML icin).
static std::string EscapeHtml(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out += c;
    }
    return out;
}

// Discord markdown'unu (**kalin**, `kod`) Telegram HTML'ine cevirir. ONCE
// HTML kacisi yapilir (guvenli), SONRA isaretcilerden etiket uretilir.
static std::string MarkdownToHtml(const std::string& raw) {
    std::string s = EscapeHtml(raw);
    // **kalin** -> <b>kalin</b>
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (i + 1 < s.size() && s[i] == '*' && s[i + 1] == '*') {
            size_t end = s.find("**", i + 2);
            if (end != std::string::npos) {
                out += "<b>" + s.substr(i + 2, end - (i + 2)) + "</b>";
                i = end + 2;
                continue;
            }
        }
        if (s[i] == '`') {
            size_t end = s.find('`', i + 1);
            if (end != std::string::npos) {
                out += "<code>" + s.substr(i + 1, end - (i + 1)) + "</code>";
                i = end + 1;
                continue;
            }
        }
        out += s[i];
        ++i;
    }
    return out;
}

// Discord embed-json'unu okunakli bir Telegram HTML mesajina cevirir. Icerik
// (baslik/aciklama/alanlar) Discord'daki ile AYNI - bicim Telegram'a uygun ve
// daha modern: baslik kalin, altinda ince ayrac, alanlar "•" ile listelenir.
static std::string EmbedToHtml(const json& embed) {
    std::string html;
    bool hasTitle = false;
    if (embed.contains("title")) {
        std::string title = embed.value("title", std::string());
        if (!title.empty()) {
            html += "<b>" + MarkdownToHtml(title) + "</b>";
            hasTitle = true;
        }
    }
    if (embed.contains("description")) {
        std::string desc = embed.value("description", std::string());
        if (!desc.empty()) html += (html.empty() ? "" : "\n") + MarkdownToHtml(desc);
    }
    if (embed.contains("fields") && embed.at("fields").is_array() && !embed.at("fields").empty()) {
        // Baslik/aciklama ile alanlar arasina ince bir ayrac koy.
        if (!html.empty()) html += "\n<b>──────────</b>";
        for (const auto& fld : embed.at("fields")) {
            std::string name = fld.value("name", std::string());
            std::string value = fld.value("value", std::string());
            html += "\n• <b>" + MarkdownToHtml(name) + "</b>: " + MarkdownToHtml(value);
        }
    }
    (void)hasTitle;
    if (html.empty()) html = " ";  // Telegram bos metni reddeder
    return html;
}

// Her komut cevabina eklenen ana menu (inline klavye). Kullanici KOMUT YAZMADAN
// butona dokunarak yonetir - dokununca callback_query gelir, PollLoop onu
// answerCallbackQuery ile onaylayip data'yi (ör. "/baslat") komut gibi handler'a
// verir. Boylece butonlar mevcut komut mantigini AYNEN kullanir.
static json MainMenuKeyboard() {
    return json{
        {"inline_keyboard",
         json::array({
             json::array({{{"text", "▶️ Başlat"}, {"callback_data", "/baslat"}},
                          {{"text", "⏹️ Durdur"}, {"callback_data", "/durdur"}}}),
             json::array({{{"text", "📊 Analiz"}, {"callback_data", "/analiz"}},
                          {{"text", "📈 Canlı Durum"}, {"callback_data", "/canli"}}}),
             json::array({{{"text", "📷 Görüntü"}, {"callback_data", "/goruntu"}},
                          {{"text", "💎 Üyelik"}, {"callback_data", "/uyelik"}}}),
             json::array({{{"text", "⏱️ Zamanlayıcı"}, {"callback_data", "/zdetay"}},
                          {{"text", "❓ Yardım"}, {"callback_data", "/yardim"}}}),
         })}};
}

TelegramBot::TelegramBot(std::string botToken) : m_token(std::move(botToken)) {}

TelegramBot::~TelegramBot() { Stop(); }

void TelegramBot::Start() {
    m_stop = false;
    m_pollThread = std::thread([this] { PollLoop(); });
}

void TelegramBot::Stop() {
    m_stop = true;
    if (m_pollThread.joinable()) m_pollThread.join();
}

std::string TelegramBot::GetBotUsername() {
    if (m_usernameFetched.load()) return m_cachedUsername;
    if (m_token.empty()) {
        m_usernameFetched = true;
        return "";
    }
    try {
        httplib::SSLClient cli("api.telegram.org");
        SetupClient(cli, 10);
        auto res = cli.Get(("/bot" + m_token + "/getMe").c_str());
        if (res && res->status == 200) {
            json j = json::parse(res->body, nullptr, false);
            if (!j.is_discarded() && j.value("ok", false)) {
                m_cachedUsername = j.at("result").value("username", "");
                m_usernameFetched = true;  // SADECE basarida onbellekle - aksi
                                           // halde gecici ag hatasinda bos deger
                                           // kalici kalirdi (bir daha denenmezdi)
            }
        } else {
            Log(std::string("getMe basarisiz: ") + (res ? std::to_string(res->status) : "baglanti hatasi"));
        }
    } catch (const std::exception& e) {
        Log(std::string("getMe hata: ") + e.what());
    }
    return m_cachedUsername;
}

// Long-polling: getUpdates'i uzun timeout ile cagirir; her mesaji handler'a
// verir. TUM govde try/catch icinde - handler'daki beklenmedik bir hata
// (ör. bir komut isleyicisi) POLL thread'ini ve dolayisiyla botu COKERTMEMELI
// (bkz. discord_bot.cpp OnGatewayText'teki ayni gerekce).
void TelegramBot::PollLoop() {
    while (!m_stop) {
        try {
            httplib::SSLClient cli("api.telegram.org");
            SetupClient(cli, 65);  // long-poll timeout=50 -> read timeout daha uzun
            std::string path = "/bot" + m_token + "/getUpdates?timeout=50&offset=" +
                               std::to_string(m_offset.load());
            auto res = cli.Get(path.c_str());
            if (!res) {
                Log("getUpdates baglanti hatasi (" + httplib::to_string(res.error()) + ")");
                std::this_thread::sleep_for(std::chrono::seconds(3));
                continue;
            }
            if (res->status != 200) {
                Log("getUpdates HTTP " + std::to_string(res->status) + " " + res->body);
                std::this_thread::sleep_for(std::chrono::seconds(3));
                continue;
            }
            json j = json::parse(res->body, nullptr, false);
            if (j.is_discarded() || !j.value("ok", false)) continue;
            for (const auto& upd : j.value("result", json::array())) {
                int64_t updId = upd.value("update_id", (int64_t)0);
                if (updId >= m_offset.load()) m_offset = updId + 1;

                // BUTON DOKUNMA (callback_query): kullanici inline menude bir
                // butona basti. Once "yukleniyor" halkasini answerCallbackQuery
                // ile kapat, sonra callback data'sini (ör. "/baslat") KOMUT gibi
                // handler'a ver - butonlar mevcut komut mantigini aynen kullanir.
                if (upd.contains("callback_query")) {
                    const json& cq = upd.at("callback_query");
                    std::string cbId = cq.value("id", "");
                    std::string data = cq.value("data", "");
                    const json& cm = cq.value("message", json::object());
                    std::string chatId = cm.contains("chat") ? std::to_string(cm.at("chat").value("id", (int64_t)0)) : "";
                    std::string authorId = cq.contains("from") ? std::to_string(cq.at("from").value("id", (int64_t)0)) : "";
                    if (!cbId.empty()) {
                        try {
                            httplib::SSLClient cli2("api.telegram.org");
                            SetupClient(cli2, 10);
                            json ans{{"callback_query_id", cbId}};
                            cli2.Post(("/bot" + m_token + "/answerCallbackQuery").c_str(), ans.dump(), "application/json");
                        } catch (...) {
                        }
                    }
                    if (!data.empty() && !chatId.empty() && m_handler) {
                        Log("buton (chat:" + chatId + "): " + data);
                        m_handler(authorId, chatId, data);
                    }
                    continue;
                }

                if (!upd.contains("message")) continue;
                const json& m = upd.at("message");
                if (!m.contains("text")) continue;
                bool isBot = m.contains("from") && m.at("from").value("is_bot", false);
                if (isBot) continue;
                std::string authorId = m.contains("from") ? std::to_string(m.at("from").value("id", (int64_t)0)) : "";
                std::string chatId = m.contains("chat") ? std::to_string(m.at("chat").value("id", (int64_t)0)) : "";
                std::string content = m.value("text", "");
                size_t s = content.find_first_not_of(" \t\r\n");
                size_t e = content.find_last_not_of(" \t\r\n");
                content = (s == std::string::npos) ? "" : content.substr(s, e - s + 1);
                if (!content.empty() && m_handler) {
                    Log("mesaj alindi (chat:" + chatId + "): " + content);
                    m_handler(authorId, chatId, content);
                }
            }
        } catch (const std::exception& e) {
            Log(std::string("poll dongusunde hata (yakalandi, YOKSAYILDI): ") + e.what());
            std::this_thread::sleep_for(std::chrono::seconds(3));
        } catch (...) {
            Log("poll dongusunde bilinmeyen hata (yakalandi, YOKSAYILDI)");
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }
}

void TelegramBot::SendChannelMessage(const std::string& channelId, const std::string& content) {
    try {
        httplib::SSLClient cli("api.telegram.org");
        SetupClient(cli, 10);
        json body{{"chat_id", ChatIdJson(channelId)}, {"text", content}, {"parse_mode", "HTML"}};
        auto res = cli.Post(("/bot" + m_token + "/sendMessage").c_str(), body.dump(), "application/json");
        if (!res) {
            Log("mesaj gonderilemedi: baglanti hatasi (" + httplib::to_string(res.error()) + ")");
        } else if (res->status != 200) {
            Log("mesaj gonderilemedi: HTTP " + std::to_string(res->status) + " " + res->body);
        }
    } catch (const std::exception& e) {
        Log(std::string("SendChannelMessage hata: ") + e.what());
    }
}

void TelegramBot::SendDirectMessage(const std::string& userId, const std::string& content) {
    // Ozel sohbette chat_id == user_id.
    SendChannelMessage(userId, content);
}

void TelegramBot::SendEmbed(const std::string& channelId, const json& embed) {
    // Her embed cevabina ana menu butonlarini ekle - kullanici komut yazmadan
    // dokunarak yonetsin (modern buton arayuzu).
    try {
        httplib::SSLClient cli("api.telegram.org");
        SetupClient(cli, 10);
        json body{{"chat_id", ChatIdJson(channelId)},
                  {"text", EmbedToHtml(embed)},
                  {"parse_mode", "HTML"},
                  {"reply_markup", MainMenuKeyboard()}};
        auto res = cli.Post(("/bot" + m_token + "/sendMessage").c_str(), body.dump(), "application/json");
        if (!res || res->status != 200) {
            Log("embed(menu) gonderilemedi: " + (res ? std::to_string(res->status) + " " + res->body : "baglanti hatasi"));
        }
    } catch (const std::exception& e) {
        Log(std::string("SendEmbed hata: ") + e.what());
    }
}

void TelegramBot::SendMultiplePng(const std::string& channelId, const std::vector<PngAttachment>& attachments) {
    if (attachments.empty()) return;
    try {
        httplib::SSLClient cli("api.telegram.org");
        SetupClient(cli, 25);
        if (attachments.size() == 1) {
            // Tek gorsel: sendPhoto (sendMediaGroup en az 2 gorsel ister).
            const auto& a = attachments[0];
            httplib::UploadFormDataItems items = {
                {"chat_id", channelId, "", ""},
                {"caption", a.caption, "", ""},
                {"photo", std::string(a.pngBytes.begin(), a.pngBytes.end()), a.filename, "image/png"},
            };
            auto res = cli.Post(("/bot" + m_token + "/sendPhoto").c_str(), items);
            if (!res || res->status != 200) {
                Log("sendPhoto basarisiz: " + (res ? std::to_string(res->status) + " " + res->body : "baglanti hatasi"));
            }
            return;
        }
        // 2..10 gorsel: sendMediaGroup - hepsi TEK mesajda birlikte gorunur.
        json media = json::array();
        httplib::UploadFormDataItems items = {{"chat_id", channelId, "", ""}};
        size_t count = attachments.size() > 10 ? 10 : attachments.size();
        for (size_t i = 0; i < count; ++i) {
            std::string field = "file" + std::to_string(i);
            media.push_back({{"type", "photo"}, {"media", "attach://" + field}, {"caption", attachments[i].caption}});
            items.push_back({field, std::string(attachments[i].pngBytes.begin(), attachments[i].pngBytes.end()),
                             attachments[i].filename, "image/png"});
        }
        items.insert(items.begin() + 1, {"media", media.dump(), "", ""});
        auto res = cli.Post(("/bot" + m_token + "/sendMediaGroup").c_str(), items);
        if (!res || res->status != 200) {
            Log("sendMediaGroup basarisiz: " + (res ? std::to_string(res->status) + " " + res->body : "baglanti hatasi"));
        }
    } catch (const std::exception& e) {
        Log(std::string("SendMultiplePng hata: ") + e.what());
    }
}
