#pragma once
// DISCORD BOTU (Gateway/WebSocket istemcisi): kullanicinin DM'den attigi
// /analiz /canli /goruntu /durdur /baslat komutlarini dinler, cevaplari
// REST API uzerinden geri gonderir. Sadece discord_link.json'daki (OAuth
// ile eslesmis) kullanicidan gelen DM'lere cevap verir - baskasi botu
// kontrol edemez.
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace ix {
class WebSocket;
}

class DiscordBot {
public:
    // authorId, channelId, content (trim edilmis, orijinal buyuk/kucuk harfle)
    using MessageHandler = std::function<void(const std::string& authorId,
                                               const std::string& channelId,
                                               const std::string& content)>;

    explicit DiscordBot(std::string botToken);
    ~DiscordBot();

    void SetMessageHandler(MessageHandler handler) { m_handler = std::move(handler); }

    // Gateway baglantisini baslatir (arka planda, ixwebsocket kendi
    // thread'inde calisir) - cagiran taraf bloklanmaz.
    void Start();
    void Stop();

    // REST: DM kanalina duz metin cevap gonderir.
    // ISIM NOTU: "SendMessage" degil "SendChannelMessage" - <windows.h>
    // SendMessage'i bir makro olarak tanimliyor (SendMessageA/W'ye genisliyor),
    // ayni isimde bir uye fonksiyon TUM cagiran/derlenen dosyalarda (bu sinifi
    // kullanan her yerde windows.h'nin dahil edilme sirasina gore) sessizce
    // bozulurdu - bkz. discord_bot.cpp'deki ilk derleme hatasi.
    void SendChannelMessage(const std::string& channelId, const std::string& content);

    // REST: verilen Discord kullanici ID'sine (authorId DEGIL, ham Discord
    // user id) DOGRUDAN MESAJ (DM) gonderir - once bir DM kanali acar
    // (POST /users/@me/channels {recipient_id}), sonra o kanala yazar.
    // Discord OAuth baglama BASARILI oldugunda "hosgeldiniz" mesaji icin
    // kullanilir (bkz. desktop_app_main.cpp ExchangeDiscordCode).
    void SendDirectMessage(const std::string& userId, const std::string& content);

    // REST: DM kanalina TEK bir zengin "embed" karti gonderir (baslik/aciklama/
    // renk/alanlar/altbilgi) - duz metinden cok daha okunakli komut cevaplari
    // icin (bkz. desktop_app_main.cpp HandleDiscordCommand).
    void SendEmbed(const std::string& channelId, const nlohmann::json& embed);
    // REST: DM kanalina bir PNG eki (ekran goruntusu) gonderir.
    void SendPng(const std::string& channelId, const std::string& filename,
                 const std::vector<unsigned char>& pngBytes, const std::string& caption);

    struct PngAttachment {
        std::string filename;
        std::vector<unsigned char> pngBytes;
        std::string caption;
    };
    // REST: BIRDEN FAZLA PNG'yi TEK bir mesajda (tek istekte, ayni anda
    // gorunecek sekilde) gonderir - /goruntu komutu cift client'ta ONCEDEN
    // her client icin AYRI/ardisik mesaj gonderiyordu (bkz. kullanicinin
    // "2 clienti de aynı anda tek karede atsın" istegi); Discord'un TEK
    // mesajda birden fazla embed+dosya ekini desteklemesinden faydalanilir.
    void SendMultiplePng(const std::string& channelId, const std::vector<PngAttachment>& attachments);

    // GATEWAY: botun kendi profilindeki durum yazisini ("... izliyor") gunceller
    // - bkz. desktop_app_main.cpp FetchAndBroadcastFarmPresence (toplam
    // farmlayan kisi sayisi).
    void SetPresence(const std::string& text);

private:
    void OnGatewayText(const std::string& text);
    void SendIdentify();
    void SendHeartbeat();
    void HeartbeatLoop();

    std::string m_token;
    MessageHandler m_handler;

    std::unique_ptr<ix::WebSocket> m_ws;
    std::atomic<bool> m_connected{false};
    std::atomic<int> m_heartbeatIntervalMs{0};
    std::atomic<int64_t> m_lastSeq{-1};

    std::atomic<bool> m_stopHeartbeat{false};
    std::thread m_heartbeatThread;
};
