#include "discord_bot.h"

#include <httplib.h>

#include "win_tls_trust.h"
#include <nlohmann/json.hpp>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>

using nlohmann::json;

// desktop_app.exe konsolsuz (WIN32_EXECUTABLE) calistigi icin std::cout hicbir
// yere gitmiyor - Gateway/REST hatalarini gorebilmek icin ayrica calisma
// dizinindeki discord_bot.log dosyasina da yaziyoruz.
static void Log(const std::string& msg) {
    std::ofstream f("discord_bot.log", std::ios::app);
    if (!f.is_open()) return;
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
    localtime_s(&tmBuf, &t);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tmBuf);
    f << "[" << buf << "] " << msg << "\n";
}

static void EnsureNetSystemInit() {
    static std::once_flag flag;
    std::call_once(flag, [] { ix::initNetSystem(); });
}

DiscordBot::DiscordBot(std::string botToken) : m_token(std::move(botToken)) {
    EnsureNetSystemInit();
    m_ws = std::make_unique<ix::WebSocket>();
    m_ws->setUrl("wss://gateway.discord.gg/?v=10&encoding=json");
    m_ws->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Message) {
            OnGatewayText(msg->str);
        } else if (msg->type == ix::WebSocketMessageType::Open) {
            m_connected = true;
            Log("gateway baglantisi acildi");
        } else if (msg->type == ix::WebSocketMessageType::Close) {
            m_connected = false;
            m_heartbeatIntervalMs = 0;
            Log("gateway baglantisi kapandi (kod:" + std::to_string(msg->closeInfo.code) +
                " sebep:" + msg->closeInfo.reason + ")");
        } else if (msg->type == ix::WebSocketMessageType::Error) {
            Log("gateway hata: " + msg->errorInfo.reason);
        }
    });
}

DiscordBot::~DiscordBot() { Stop(); }

void DiscordBot::Start() {
    m_ws->start();
    m_stopHeartbeat = false;
    m_heartbeatThread = std::thread([this] { HeartbeatLoop(); });
}

void DiscordBot::Stop() {
    m_stopHeartbeat = true;
    if (m_heartbeatThread.joinable()) m_heartbeatThread.join();
    if (m_ws) m_ws->stop();
}

void DiscordBot::SendIdentify() {
    json identify = {
        {"op", 2},
        {"d",
         {
             {"token", m_token},
             {"intents", 4096 | 32768},  // DIRECT_MESSAGES | MESSAGE_CONTENT
             {"properties", {{"os", "windows"}, {"browser", "elbit"}, {"device", "elbit"}}},
         }},
    };
    m_ws->send(identify.dump());
}

void DiscordBot::SendHeartbeat() {
    int64_t seq = m_lastSeq.load();
    json hb{{"op", 1}, {"d", seq < 0 ? json(nullptr) : json(seq)}};
    m_ws->send(hb.dump());
}

// KRITIK: Discord'un istedigi araligi (heartbeat_interval, bkz. Hello/op10)
// TAM olarak beklemek yerine 200ms'lik dilimlere bolup bekliyoruz - boylece
// Stop() cagrildiginda thread en fazla 200ms icinde (uzun araligin sonunu
// beklemeden) sonlanip join edilebilir.
void DiscordBot::HeartbeatLoop() {
    while (!m_stopHeartbeat) {
        int interval = m_heartbeatIntervalMs.load();
        if (m_connected && interval > 0) {
            SendHeartbeat();
            for (int waited = 0; waited < interval && !m_stopHeartbeat; waited += 200) {
                std::this_thread::sleep_for(std::chrono::milliseconds(std::min(200, interval - waited)));
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

void DiscordBot::OnGatewayText(const std::string& text) {
    // TUM govde try/catch icinde: bu fonksiyon ixwebsocket'in KENDI ic
    // thread'inden cagriliyor - buradan disariya YAKALANMAMIS bir istisna
    // sizarsa (ör. m_handler icindeki bir komut isleyicisinde beklenmedik
    // bir veri turu/durum), std::terminate() cagrilip TUM PLQK73MN.exe (farm
    // dahil) COKER. Tek bir Discord komutundaki bir hata, calisan farm
    // oturumunu ASLA etkilememeli.
    try {
        json msg = json::parse(text, nullptr, false);
        if (msg.is_discarded() || !msg.contains("op")) return;
        int op = msg.at("op").get<int>();
        if (msg.contains("s") && !msg.at("s").is_null()) {
            m_lastSeq = msg.at("s").get<int64_t>();
        }

        if (op == 10) {  // Hello
            int interval = msg.at("d").value("heartbeat_interval", 41250);
            m_heartbeatIntervalMs = interval;
            Log("Hello alindi (heartbeat_interval=" + std::to_string(interval) + "), Identify gonderiliyor");
            SendIdentify();
        } else if (op == 1) {  // sunucu erken heartbeat istiyor
            SendHeartbeat();
        } else if (op == 7) {  // Reconnect - sunucu baglantiyi KAPATIP yeniden kurmamizi istiyor
            Log("Reconnect istegi (op7) alindi - baglanti yeniden kurulacak");
            if (m_ws) m_ws->close();  // ixwebsocket otomatik yeniden baglanir, Hello'da tekrar Identify gonderilir
        } else if (op == 9) {  // Invalid Session - token/intents reddedildi ya da oturum gecersiz
            Log("Invalid Session (op9) - token veya intents reddedilmis olabilir. Ham mesaj: " + text);
        } else if (op == 0) {  // Dispatch
            std::string t = msg.value("t", "");
            if (t == "READY") {
                Log("bot hazir (READY)");
            } else if (t == "MESSAGE_CREATE") {
                const json& d = msg.at("d");
                bool isBot = d.contains("author") && d.at("author").value("bot", false);
                bool isDm = !d.contains("guild_id");
                if (!isBot && isDm && m_handler) {
                    std::string authorId = d.at("author").value("id", "");
                    std::string channelId = d.value("channel_id", "");
                    std::string content = d.value("content", "");
                    size_t start = content.find_first_not_of(" \t\r\n");
                    size_t end = content.find_last_not_of(" \t\r\n");
                    content = (start == std::string::npos) ? "" : content.substr(start, end - start + 1);
                    if (!content.empty()) {
                        Log("DM alindi (author:" + authorId + "): " + content);
                        m_handler(authorId, channelId, content);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        Log(std::string("gateway mesaji islenirken beklenmeyen hata (yakalandi, YOKSAYILDI): ") + e.what());
    } catch (...) {
        Log("gateway mesaji islenirken bilinmeyen bir hata (yakalandi, YOKSAYILDI)");
    }
}

void DiscordBot::SendChannelMessage(const std::string& channelId, const std::string& content) {
    httplib::SSLClient cli("discord.com");
    cli.set_connection_timeout(10);
    cli.set_read_timeout(10);
    ConfigureSystemTrustStore(cli);
    httplib::Headers headers{{"Authorization", "Bot " + m_token}};
    json body{{"content", content}};
    auto res = cli.Post(("/api/v10/channels/" + channelId + "/messages").c_str(), headers, body.dump(), "application/json");
    if (!res) {
        Log("mesaj gonderilemedi: baglanti hatasi (" + httplib::to_string(res.error()) + ")");
    } else if (res->status != 200 && res->status != 201) {
        Log("mesaj gonderilemedi: HTTP " + std::to_string(res->status) + " " + res->body);
    }
}

void DiscordBot::SendDirectMessage(const std::string& userId, const std::string& content) {
    httplib::SSLClient cli("discord.com");
    cli.set_connection_timeout(10);
    cli.set_read_timeout(10);
    ConfigureSystemTrustStore(cli);
    httplib::Headers headers{{"Authorization", "Bot " + m_token}, {"Content-Type", "application/json"}};

    // ADIM 1: DM kanali ac (kullanici zaten bir DM kanali actiysa Discord
    // AYNI kanal ID'sini geri doner - guvenle her seferinde cagirilabilir).
    json openBody{{"recipient_id", userId}};
    auto openRes = cli.Post("/api/v10/users/@me/channels", headers, openBody.dump(), "application/json");
    if (!openRes) {
        Log("hosgeldin DM'i icin kanal acilamadi: baglanti hatasi (" + httplib::to_string(openRes.error()) + ")");
        return;
    }
    if (openRes->status != 200 && openRes->status != 201) {
        Log("hosgeldin DM'i icin kanal acilamadi: HTTP " + std::to_string(openRes->status) + " " + openRes->body);
        return;
    }
    json openJson = json::parse(openRes->body, nullptr, false);
    if (openJson.is_discarded() || !openJson.contains("id")) {
        Log("hosgeldin DM'i icin kanal yaniti beklenmeyen formatta: " + openRes->body);
        return;
    }
    std::string dmChannelId = openJson.at("id").get<std::string>();

    // ADIM 2: o kanala mesaji gonder.
    SendChannelMessage(dmChannelId, content);
}

void DiscordBot::SendEmbed(const std::string& channelId, const json& embed) {
    httplib::SSLClient cli("discord.com");
    cli.set_connection_timeout(10);
    cli.set_read_timeout(10);
    ConfigureSystemTrustStore(cli);
    httplib::Headers headers{{"Authorization", "Bot " + m_token}};
    json body{{"embeds", json::array({embed})}};
    auto res = cli.Post(("/api/v10/channels/" + channelId + "/messages").c_str(), headers, body.dump(), "application/json");
    if (!res) {
        Log("embed gonderilemedi: baglanti hatasi (" + httplib::to_string(res.error()) + ")");
    } else if (res->status != 200 && res->status != 201) {
        Log("embed gonderilemedi: HTTP " + std::to_string(res->status) + " " + res->body);
    }
}

void DiscordBot::SetPresence(const std::string& text) {
    if (!m_connected) return;
    json presence{
        {"op", 3},
        {"d",
         {
             {"since", nullptr},
             {"activities", json::array({{{"name", text}, {"type", 3}}})},  // type 3 = "... izliyor"
             {"status", "online"},
             {"afk", false},
         }},
    };
    m_ws->send(presence.dump());
}

void DiscordBot::SendPng(const std::string& channelId, const std::string& filename,
                          const std::vector<unsigned char>& pngBytes, const std::string& caption) {
    httplib::SSLClient cli("discord.com");
    cli.set_connection_timeout(10);
    cli.set_read_timeout(15);
    ConfigureSystemTrustStore(cli);
    httplib::Headers headers{{"Authorization", "Bot " + m_token}};
    json embed{
        {"title", "📷 " + caption},
        {"color", 0x5b8def},
        {"image", {{"url", "attachment://" + filename}}},
        {"footer", {{"text", "Elbit | Premium"}}},
    };
    json payload{{"embeds", json::array({embed})}};
    httplib::UploadFormDataItems items = {
        {"payload_json", payload.dump(), "", "application/json"},
        {"files[0]", std::string(pngBytes.begin(), pngBytes.end()), filename, "image/png"},
    };
    cli.Post(("/api/v10/channels/" + channelId + "/messages").c_str(), headers, items);
}

void DiscordBot::SendMultiplePng(const std::string& channelId, const std::vector<PngAttachment>& attachments) {
    if (attachments.empty()) return;
    httplib::SSLClient cli("discord.com");
    cli.set_connection_timeout(10);
    cli.set_read_timeout(20);
    ConfigureSystemTrustStore(cli);
    httplib::Headers headers{{"Authorization", "Bot " + m_token}};

    json embeds = json::array();
    for (const auto& a : attachments) {
        embeds.push_back({
            {"title", "📷 " + a.caption},
            {"color", 0x5b8def},
            {"image", {{"url", "attachment://" + a.filename}}},
            {"footer", {{"text", "Elbit | Premium"}}},
        });
    }
    json payload{{"embeds", embeds}};

    httplib::UploadFormDataItems items = {
        {"payload_json", payload.dump(), "", "application/json"},
    };
    for (size_t i = 0; i < attachments.size(); ++i) {
        items.push_back({"files[" + std::to_string(i) + "]",
                          std::string(attachments[i].pngBytes.begin(), attachments[i].pngBytes.end()),
                          attachments[i].filename, "image/png"});
    }
    auto res = cli.Post(("/api/v10/channels/" + channelId + "/messages").c_str(), headers, items);
    if (!res) {
        Log("coklu png gonderilemedi: baglanti hatasi (" + httplib::to_string(res.error()) + ")");
    } else if (res->status != 200 && res->status != 201) {
        Log("coklu png gonderilemedi: HTTP " + std::to_string(res->status) + " " + res->body);
    }
}
