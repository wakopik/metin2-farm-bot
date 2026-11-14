#pragma once
#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>  // cv::dnn::NMSBoxes icin
#include <memory>
#include <string>
#include <vector>
#include "color_detector.h"  // Detection struct

// YOLOv8 (Ultralytics, ONNX'e disa aktarilmis) tabanli tespit yontemi -
// isim-etiketi/renk sablon eslestirmesinin (bkz. NameLabelDetector/
// AutoColorDetector) aksine, TASIN/CANAVARIN KENDISINI dogrudan taniyacak
// sekilde EGITILMIS bir sinir agi kullanir. Bu, ISIK/ARKA PLAN/ACI
// farklarina karsi template matching'den KOKTEN daha dayaniklidir.
//
// NEDEN OpenCV cv::dnn DEGIL DE ONNX Runtime: ilk denemede cv::dnn::
// readNetFromONNX, Ultralytics'in (yeni PyTorch surumleriyle) urettigi
// ONNX dosyasindaki bazi Conv katmanlarini OKUYAMADI ("kernel_size not
// specified" hatasi) - bu, OpenCV'nin ONNX okuyucusunun ONNX SPEC'inin
// TAMAMINI desteklememesinden kaynaklanan, TOPLULUKTA bilinen bir sinirlama
// (basitlestirme/onnxsim ile bile COZULMEDI). ONNX Runtime, ONNX'in
// KENDI referans/resmi calisma zamani oldugu icin (Microsoft tarafindan
// gelistiriliyor, ayni ekip) BU TUR uyumluluk sorunlari yasanmaz.
//
// ONEMLI: YOLO kutusu DOGRUDAN NESNENIN KENDISINI kapsar (isim etiketini
// DEGIL) - bu yuzden NameLabelDetector'daki gibi ayri bir renk-analizi/
// ofset katmanina GEREK YOK, kutunun GEOMETRIK MERKEZI zaten dogru
// tiklama noktasidir.
//
// Model dosyasi (.onnx) bulunamazsa/yuklenemezse bu yontem TAMAMEN sessiz
// kalir (IsReady()==false) - diger tespit yontemlerini (isim etiketi/
// renk) hicbir sekilde ETKILEMEZ, sadece EK bir aday kaynagi olarak ayni
// Detection havuzuna katilir (bkz. bot.cpp DetectInFarmZone).
class YoloDetector {
public:
    // modelPath: egitilmis .onnx dosyasi. category: bu modelin tespit
    // ettigi metin turu (ör. "dovus"). inputSize: egitimde kullanilan
    // imgsz (ör. 800 - Colab egitim komutuyla AYNI olmali). classIndex:
    // modelin ic sinif listesindeki HANGI indekse guvenilecegi (Roboflow'da
    // olusturulma sirasina gore 0,1,2,...) - yanlislikla/deneme amacli
    // eklenmis BASKA bir sinifin (ör. az ornekli, zayif egitilmis) yanlis
    // kategoriye atfedilmesini ONLEMEK icin, TUM siniflar yerine SADECE bu
    // indekse bakilir. Bkz. config.h YoloModelEntry::class_index.
    YoloDetector(std::string modelPath, std::string category, int inputSize = 800,
                 float confThreshold = 0.5f, float nmsThreshold = 0.45f, int classIndex = 0);
    ~YoloDetector() = default;
    // unique_ptr uyeler zaten TASINABILIR/kopyalanamaz - bot.h'deki
    // std::vector<YoloDetector> icin bunlarin ACIKCA varsayilan olmasi
    // yeterli (Ort::Env/Session TAM tanimli oldugundan burada sorun yok).
    YoloDetector(YoloDetector&&) noexcept = default;
    YoloDetector& operator=(YoloDetector&&) noexcept = default;
    YoloDetector(const YoloDetector&) = delete;
    YoloDetector& operator=(const YoloDetector&) = delete;

    bool IsReady() const { return m_ready; }
    const std::string& GetCategory() const { return m_category; }

    std::vector<Detection> Detect(const cv::Mat& frameBgr, const cv::Rect& searchRegion) const;

private:
    // Ort::Session'in varsayilan kurucusu yok (env+model yolu SART) - model
    // yuklenemezse "hazir degil" durumunu temsil edebilmek icin pointer.
    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::Session> m_session;
    std::string m_inputName;
    std::string m_outputName;
    std::string m_category;
    int m_inputSize;
    float m_confThreshold;
    float m_nmsThreshold;
    int m_classIndex;
    bool m_ready = false;
    mutable double m_lastLogAt = 0.0;  // TANI: seyreltilmis ozet loglama icin, bkz. .cpp
};
