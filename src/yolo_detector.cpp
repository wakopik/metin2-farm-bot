#include "yolo_detector.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>

namespace {
// desktop_app.exe konsolsuz (WIN32_EXECUTABLE) calistigi icin std::cout
// HICBIR YERE gitmiyor (normal cift-tiklama ile acilinca kaybolur) -
// discord_bot.cpp'deki AYNI desen: modelin GERCEKTEN
// yuklenip yuklenmedigini/calisirken ne buldugunu DOSYAYA da yaz ki
// "gercekten baglandik mi" sorusu her zaman kontrol edilebilsin.
void Log(const std::string& msg) {
    std::ofstream f("yolo_detector.log", std::ios::app);
    if (!f.is_open()) return;
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
    localtime_s(&tmBuf, &t);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tmBuf);
    f << "[" << buf << "] " << msg << "\n";
}

// ONNX Runtime Windows'ta model yolunu wchar_t* (ORTCHAR_T) olarak ister.
std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), len);
    return w;
}
}  // namespace

YoloDetector::YoloDetector(std::string modelPath, std::string category, int inputSize, float confThreshold,
                            float nmsThreshold, int classIndex)
    : m_category(std::move(category)),
      m_inputSize(inputSize),
      m_confThreshold(confThreshold),
      m_nmsThreshold(nmsThreshold),
      m_classIndex(classIndex) {
    try {
        m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "yolo_detector");
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        std::wstring wpath = ToWide(modelPath);
        m_session = std::make_unique<Ort::Session>(*m_env, wpath.c_str(), options);

        Ort::AllocatorWithDefaultOptions allocator;
        auto inputNameAlloc = m_session->GetInputNameAllocated(0, allocator);
        auto outputNameAlloc = m_session->GetOutputNameAllocated(0, allocator);
        m_inputName = inputNameAlloc.get();
        m_outputName = outputNameAlloc.get();

        m_ready = true;
        std::string msg = "'" + modelPath + "' modeli YUKLENDI (kategori='" + m_category +
                           "', girdi boyutu=" + std::to_string(m_inputSize) +
                           "px, ONNX Runtime) - YOLO tabanli EK tespit AKTIF.";
        std::cout << "[yolo] " << msg << "\n";
        Log(msg);
    } catch (const Ort::Exception& e) {
        std::string msg = "UYARI: '" + modelPath + "' yuklenemedi (ONNX Runtime): " + e.what();
        std::cout << "[yolo] " << msg << "\n";
        Log(msg);
        m_session.reset();
        m_env.reset();
    } catch (const std::exception& e) {
        std::string msg = "UYARI: '" + modelPath + "' yuklenemedi: " + e.what();
        std::cout << "[yolo] " << msg << "\n";
        Log(msg);
        m_session.reset();
        m_env.reset();
    }
}

std::vector<Detection> YoloDetector::Detect(const cv::Mat& frameBgr, const cv::Rect& searchRegionIn) const {
    std::vector<Detection> dets;
    if (!m_ready || frameBgr.empty()) return dets;

    cv::Rect frameBounds(0, 0, frameBgr.cols, frameBgr.rows);
    cv::Rect region = searchRegionIn & frameBounds;
    if (region.width <= 8 || region.height <= 8) return dets;
    cv::Mat crop = frameBgr(region);

    // LETTERBOX: en-boy oranini BOZMADAN kare bir tuvale (inputSize x
    // inputSize) yerlestir - Ultralytics egitim sirasinda AYNI yontemi
    // kullanir (kenarlara 114,114,114 gri dolgu ekler, GERMEZ/DISTORTE
    // ETMEZ) - training/inference arasinda TUTARLILIK sart.
    int srcW = crop.cols, srcH = crop.rows;
    float scale = std::min((float)m_inputSize / srcW, (float)m_inputSize / srcH);
    int newW = std::max(1, (int)std::round(srcW * scale));
    int newH = std::max(1, (int)std::round(srcH * scale));
    cv::Mat resized;
    cv::resize(crop, resized, cv::Size(newW, newH));
    int padX = (m_inputSize - newW) / 2;
    int padY = (m_inputSize - newH) / 2;
    cv::Mat canvas(m_inputSize, m_inputSize, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(canvas(cv::Rect(padX, padY, newW, newH)));

    // ON ISLEME: BGR->RGB, [0,1] normalizasyon, HWC->CHW (Ultralytics'in
    // KENDI on isleme sekliyle AYNI - bkz. yolo_detector.h neden ONNX
    // Runtime'a gecildigi yorumu).
    cv::Mat rgb;
    cv::cvtColor(canvas, rgb, cv::COLOR_BGR2RGB);
    rgb.convertTo(rgb, CV_32FC3, 1.0 / 255.0);
    std::vector<cv::Mat> channels(3);
    cv::split(rgb, channels);
    std::vector<float> inputTensorValues((size_t)3 * m_inputSize * m_inputSize);
    size_t planeSize = (size_t)m_inputSize * m_inputSize;
    for (int c = 0; c < 3; ++c) {
        std::memcpy(inputTensorValues.data() + (size_t)c * planeSize, channels[c].ptr<float>(),
                    planeSize * sizeof(float));
    }

    std::array<int64_t, 4> inputShape{1, 3, m_inputSize, m_inputSize};
    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(memInfo, inputTensorValues.data(),
                                                               inputTensorValues.size(), inputShape.data(),
                                                               inputShape.size());

    const char* inputNames[] = {m_inputName.c_str()};
    const char* outputNames[] = {m_outputName.c_str()};
    std::vector<Ort::Value> outputTensors;
    try {
        outputTensors = m_session->Run(Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);
    } catch (const Ort::Exception& e) {
        Log(std::string("tespit basarisiz (Run hatasi): ") + e.what());
        return dets;
    }
    if (outputTensors.empty()) return dets;

    auto outShape = outputTensors[0].GetTensorTypeAndShapeInfo().GetShape();  // [1, 4+sinifSayisi, tahminSayisi]
    if (outShape.size() != 3 || outShape[0] != 1) return dets;
    int dims = (int)outShape[1];
    int numPred = (int)outShape[2];
    int numClasses = dims - 4;
    if (numClasses < 1) return dets;
    const float* outData = outputTensors[0].GetTensorData<float>();

    // YOLOv8 ONNX ciktisi (1, 4+sinifSayisi, tahminSayisi) seklinde -
    // [cx, cy, w, h, sinif0_skor, ...] SATIR DEGIL SUTUN olarak durur -
    // manuel olarak "tahmin basina" erisim icin index hesapliyoruz
    // (transpoze icin ekstra bellek kopyalamaya gerek yok).
    // SADECE m_classIndex'e bakiyoruz (TUM siniflarin en yuksegine DEGIL) -
    // Roboflow projesinde yanlislikla/deneme amacli eklenmis, cok az
    // ornekle "egitilmis" zayif bir sinif varsa, o siniftaki nadir bir
    // yanlis-pozitif ARTIK bu modelin category'sine (ör. "dovus")
    // sizamaz - bkz. YoloDetector kurucusu yorumu.
    int classIdx = std::clamp(m_classIndex, 0, numClasses - 1);
    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    // TANI: esigi HICBIR tahmin gecemediginde ("neredeyse hic tespit yok"
    // sikayetlerinde) modelin GERCEKTE ne kadar yaklastigini gorebilmek icin -
    // esik oncesi TUM tahminlerdeki en yuksek ham skoru da ayrica tutuyoruz.
    float maxRawScore = 0.0f;
    for (int i = 0; i < numPred; ++i) {
        float bestScore = outData[(size_t)(4 + classIdx) * numPred + i];
        if (bestScore > maxRawScore) maxRawScore = bestScore;
        if (bestScore < m_confThreshold) continue;

        float cx = outData[0 * (size_t)numPred + i];
        float cy = outData[1 * (size_t)numPred + i];
        float w = outData[2 * (size_t)numPred + i];
        float h = outData[3 * (size_t)numPred + i];
        // Letterbox (kare tuval) koordinatindan CROP koordinatina geri cevir.
        float x0 = (cx - w / 2.0f - padX) / scale;
        float y0 = (cy - h / 2.0f - padY) / scale;
        float bw = w / scale;
        float bh = h / scale;
        boxes.emplace_back((int)std::round(x0), (int)std::round(y0), (int)std::round(bw), (int)std::round(bh));
        scores.push_back(bestScore);
    }

    std::vector<int> keep;
    cv::dnn::NMSBoxes(boxes, scores, m_confThreshold, m_nmsThreshold, keep);
    cv::Rect cropBounds(0, 0, crop.cols, crop.rows);
    for (int idx : keep) {
        cv::Rect b = boxes[idx] & cropBounds;
        if (b.width <= 0 || b.height <= 0) continue;

        Detection d;
        d.x = region.x + b.x;
        d.y = region.y + b.y;
        d.w = b.width;
        d.h = b.height;
        d.score = scores[idx];
        d.name = "yolo";
        d.category = m_category;
        // YOLO kutusu DOGRUDAN NESNENIN KENDISINI kapsar (isim etiketini
        // DEGIL) - bu yuzden ekstra bir renk-analizi/ofset katmanina GEREK
        // YOK, kutunun GEOMETRIK MERKEZI (varsayilan Detection::Center())
        // zaten dogru tiklama noktasidir.
        dets.push_back(d);
    }

    // TANI (seyreltilmis): her Step()'te (saniyede onlarca kez) DEGIL,
    // en fazla 2 saniyede bir - modelin CANLI oyunda GERCEKTEN bir sey
    // bulup bulmadigini (ve hangi guvenle) yolo_detector.log'dan kontrol
    // edebilmek icin.
    auto nowSteady = std::chrono::steady_clock::now();
    double nowSec = std::chrono::duration<double>(nowSteady.time_since_epoch()).count();
    if (nowSec - m_lastLogAt > 2.0) {
        m_lastLogAt = nowSec;
        std::string msg = "[" + m_category + "] bu karede " + std::to_string(dets.size()) + " tespit";
        if (!dets.empty()) msg += " (en yuksek guven=" + std::to_string(dets.front().score) + ")";
        msg += " | esik=" + std::to_string(m_confThreshold) +
               " esik-oncesi en yuksek ham skor=" + std::to_string(maxRawScore);
        Log(msg);
    }
    return dets;
}
