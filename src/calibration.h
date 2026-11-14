#pragma once
#include <mutex>
#include <string>
#include <vector>

// ============================ KALIBRASYON =================================
// AMAC (kullanici talebi): "cihazdan cihaza esikler milimetrik farklarla
// degisiyor ve sorun yasiyoruz - kimse icin ayri analiz yapmaya gerek
// kalmasin". Bu modul, oyun ekranini ~40 saniye HICBIR TIKLAMA YAPMADAN
// izler (sadece kamerayi cevirir), olcum toplar ve esikleri O CIHAZA gore
// config.json'a yazar.
//
// NEYI OLCEBILIR, NEYI OLCEMEZ - DURUST SINIR:
//   OLCULEBILIR: bir sinyal EKRANDA YOKKEN aldigi en yuksek skor, yani o
//   cihazin "yanlis pozitif tabani". Esik bunun UZERINE ayarlanir. HP
//   cubugu, Nesne Market bildirimi ve bulmaca basligi icin bu YETERLIDIR,
//   cunku uc sinyalin de POZITIF tarafi (gercekten ekrandayken aldigi skor)
//   daha once GERCEK goruntulerle olculdu ve buraya UST SINIR olarak
//   gomuldu (bkz. kHp*/kNm*/kPuz* sabitleri).
//   OLCULEMEZ: bir YOLO tespitinin gercek metin mi yoksa hayvan mi oldugu -
//   bunun icin etiketli veri gerekir. Bu yuzden kalibrasyon
//   min_detection_height_ratio'yu DEGISTIRMEZ, sadece gozlenen kutu
//   yuksekligi dagilimini RAPOR EDER. (Bu ayari tek bir ornekten tahmin
//   etmek gercek bir kullanicida TUM tespitleri elemis ve botu tamamen
//   durdurmustu - bkz. config.h min_detection_height_ratio.)

struct CalibrationStatus {
    bool running = false;
    bool done = false;
    int progressPct = 0;
    std::string phase;             // kullaniciya gosterilen asama metni
    std::string error;             // bos degilse basarisiz
    std::vector<std::string> lines;  // sonuc raporu (arayuzde listelenir)
};

// Kalibrasyonu ARKA PLANDA baslatir. Zaten calisiyorsa hicbir sey yapmaz.
// windowTitleHint bos ise config.json'daki deger kullanilir. lang ("tr"/"en")
// ciktinin (satirlar + faz basliklari) dilini belirler - arayuz diliyle uyar.
void StartCalibration(const std::string& windowTitleHint, const std::string& lang = "tr");

// Anlik durum (arayuz 250ms'de bir sorgular).
CalibrationStatus GetCalibrationStatus();
