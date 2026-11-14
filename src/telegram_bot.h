#pragma once
// TELEGRAM BOTU (long-polling istemcisi): kullanicinin bota yazdigi
// /analiz /canli /goruntu /durdur /baslat ... komutlarini dinler, cevaplari
// Bot API (https://api.telegram.org/bot<TOKEN>/...) uzerinden geri gonderir.
// Discord botuyla AYNI komut mantigini paylasir (bkz. desktop_app_main.cpp
// HandleBotCommand<Bot> sablonu) - bu sinif Discord'un webhook/embed yerine
// Telegram long-poll + HTML mesaj kullanan TASIMA katmanidir. Metot yuzeyi
// bilincli olarak DiscordBot ile ayni tutulmustur (SendEmbed embed-json'u
// Telegram HTML mesajina cevirir), boylece komut sablonu ikisini de surer.
//
// Yetki: SADECE telegram_link.json'da kayitli (uygulamadaki kodla /baglan
// yapmis) sohbetten gelen komutlara cevap verilir - bkz. HandleTelegramCommand.
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json_fwd.hpp>

class TelegramBot {
public:
    // authorId (from.id), channelId (chat.id), content (trim edilmis metin)
    using MessageHandler = std::function<void(const std::string& authorId,
                                               const std::string& channelId,
                                               const std::string& content)>;

    explicit TelegramBot(std::string botToken);
    ~TelegramBot();

    void SetMessageHandler(MessageHandler handler) { m_handler = std::move(handler); }

    // Long-polling dongusunu arka planda baslatir - cagiran taraf bloklanmaz.
    void Start();
    void Stop();

    // Bot API getMe ile botun @kullanici_adini ceker (bir kez cekilip
    // onbellege alinir) - UI'daki "Telegram'da @X'e /baglan yaz" talimati icin.
    // Token yapilandirilmamissa bos doner.
    std::string GetBotUsername();

    // --- DiscordBot ile AYNI metot yuzeyi (komut sablonu ikisini de kullanir) ---

    // Duz metin mesaj gonderir (chatId'ye).
    void SendChannelMessage(const std::string& channelId, const std::string& content);
    // "DM": Telegram'da ozel sohbette chat_id == user_id oldugu icin
    // SendChannelMessage ile aynidir - baglama onayi mesaji icin.
    void SendDirectMessage(const std::string& userId, const std::string& content);
    // Discord "embed"-json'unu (title/description/fields) Telegram HTML mesajina
    // cevirip gonderir - komut cevaplari Discord'daki ile AYNI icerikte olur.
    void SendEmbed(const std::string& channelId, const nlohmann::json& embed);

    struct PngAttachment {
        std::string filename;
        std::vector<unsigned char> pngBytes;
        std::string caption;
    };
    // Birden fazla PNG'yi TEK mesajda gonderir (sendMediaGroup; tek gorsel ise
    // sendPhoto) - /goruntu komutunda tum client'lar birlikte gorunsun diye.
    void SendMultiplePng(const std::string& channelId, const std::vector<PngAttachment>& attachments);

private:
    void PollLoop();

    std::string m_token;
    MessageHandler m_handler;

    std::atomic<bool> m_stop{false};
    std::atomic<int64_t> m_offset{0};
    std::thread m_pollThread;

    std::string m_cachedUsername;
    std::atomic<bool> m_usernameFetched{false};
};
