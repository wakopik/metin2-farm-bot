#include "orb_detector.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <opencv2/calib3d.hpp>

namespace fs = std::filesystem;

namespace {

// Sablonu (gri) ve varsa alfa kanalindan turetilen MASKEYI ayni olcekte
// dondurur - boylece hem "farkli zoom seviyeleri" hem de "sadece nesnenin
// kendi pikselleri" (arka plan degil) ayni anda korunur.
struct GrayWithMask {
    cv::Mat gray;
    cv::Mat mask;  // bos olabilir (alfasiz sablonlar icin)
};

std::vector<GrayWithMask> GenerateScaleVariants(const cv::Mat& gray, const cv::Mat& mask) {
    std::vector<GrayWithMask> variants;
    for (double scale : {1.0, 0.8, 1.25}) {
        cv::Mat scaledGray;
        cv::resize(gray, scaledGray, cv::Size(), scale, scale, cv::INTER_AREA);
        if (scaledGray.cols < 20 || scaledGray.rows < 20) continue;

        cv::Mat scaledMask;
        if (!mask.empty()) cv::resize(mask, scaledMask, scaledGray.size(), 0, 0, cv::INTER_NEAREST);

        variants.push_back({scaledGray, scaledMask});
    }
    return variants;
}

}  // namespace

OrbDetector::OrbDetector(const Config& config) : m_config(config) {
    // Daha fazla ozellik noktasi = "birebir ayni degil ama benzer" acidan
    // gelen goruntulerin de yeterli sayida ortak nokta bulma sansi artar.
    m_orb = cv::ORB::create(1500);

    std::error_code ec;
    // BIRDEN FAZLA metin turu klasoru AYNI ANDA taranabilir - her klasorden
    // yuklenen sablonlar TEK bir havuzda birlesir, Detect() hepsini birden
    // arar (hangi turden olursa olsun ilk bulunana tiklanir).
    for (const auto& dir : config.templates_dirs) {
        if (!fs::exists(dir, ec)) {
            std::cerr << "[orb_detector] '" << dir << "' klasoru yok.\n";
            continue;
        }
        // NOT: "dovus/" gibi SONUNDA ayirici (/ veya \) olan bir yol
        // verilirse fs::path(...).filename() BOS DONER (sondaki ayiraci
        // "bos son bilesen" sayar) - bu durumda TUM klasorler yanlislikla
        // AYNI (bos) kategoriye dusup asagidaki NMS duzeltmesini SESSIZCE
        // etkisiz birakirdi. parent_path().filename() ile bu durumu da kapsariz.
        std::string category = fs::path(dir).filename().string();
        if (category.empty()) category = fs::path(dir).parent_path().filename().string();

        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext != ".png") continue;
            // "_panel.png" bir tespit sablonu DEGIL, ust bilgi panelindeki
            // isim referansidir (bkz. StatusPanelDetector) - ORB havuzuna
            // asla dahil edilmemeli.
            if (entry.path().filename().string() == "_panel.png") continue;

            // IMREAD_UNCHANGED: alfa kanali (seffaf arka plan - GrabCut ile
            // otomatik kaldirilmis) varsa korunur, boylece ozellik noktalari
            // SADECE nesnenin kendi pikselerinden cikarilir, arka plandan degil.
            cv::Mat img = cv::imread(entry.path().string(), cv::IMREAD_UNCHANGED);
            if (img.empty()) continue;

            cv::Mat gray, mask;
            if (img.channels() == 4) {
                std::vector<cv::Mat> ch(4);
                cv::split(img, ch);
                cv::Mat bgr;
                cv::merge(std::vector<cv::Mat>{ch[0], ch[1], ch[2]}, bgr);
                cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
                cv::threshold(ch[3], mask, 10, 255, cv::THRESH_BINARY);
                // Kesim sinirindaki sahte kenari disarida birakmak icin iceriden asindir.
                cv::erode(mask, mask, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
            } else if (img.channels() == 3) {
                cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
            } else {
                gray = img;
            }

            std::string baseName = entry.path().stem().string();
            int addedForThis = 0;
            for (const auto& variant : GenerateScaleVariants(gray, mask)) {
                Template t;
                t.name = baseName;
                t.category = category;
                t.size = variant.gray.size();
                if (!variant.mask.empty())
                    m_orb->detectAndCompute(variant.gray, variant.mask, t.keypoints, t.descriptors);
                else
                    m_orb->detectAndCompute(variant.gray, cv::noArray(), t.keypoints, t.descriptors);
                if (t.descriptors.empty() || t.keypoints.size() < 8) continue;
                m_templates.push_back(std::move(t));
                addedForThis++;
            }

            if (addedForThis == 0) {
                std::cerr << "[orb_detector] '" << baseName << "' -> yeterli ozellik noktasi yok, atlaniyor "
                           << "(daha desenli/detayli bir kirpma deneyin).\n";
            }
        }
    }

    std::cout << "[orb_detector] " << m_templates.size()
               << " sablon varyanti yuklendi (ORB + RANSAC geometrik dogrulama, otomatik olcek cesitlemesi dahil).\n";
}

std::vector<Detection> OrbDetector::Detect(const cv::Mat& frameBgr, const cv::Rect& searchRegionIn) const {
    std::vector<Detection> dets;
    if (frameBgr.empty() || m_templates.empty()) return dets;

    cv::Rect frameBounds(0, 0, frameBgr.cols, frameBgr.rows);
    cv::Rect region = searchRegionIn & frameBounds;
    if (region.width <= 8 || region.height <= 8) return dets;

    cv::Mat gray;
    cv::cvtColor(frameBgr(region), gray, cv::COLOR_BGR2GRAY);

    std::vector<cv::KeyPoint> sceneKp;
    cv::Mat sceneDesc;
    m_orb->detectAndCompute(gray, cv::noArray(), sceneKp, sceneDesc);
    if (sceneDesc.empty()) return dets;

    cv::BFMatcher matcher(cv::NORM_HAMMING);

    for (const auto& tmpl : m_templates) {
        std::vector<std::vector<cv::DMatch>> knn;
        matcher.knnMatch(tmpl.descriptors, sceneDesc, knn, 2);

        std::vector<cv::Point2f> objPts, scenePts;
        for (auto& pair : knn) {
            if (pair.size() < 2) continue;
            if (pair[0].distance < m_config.orb_ratio_thresh * pair[1].distance) {
                objPts.push_back(tmpl.keypoints[pair[0].queryIdx].pt);
                scenePts.push_back(sceneKp[pair[0].trainIdx].pt);
            }
        }

        // Homografi icin en az 4 nokta sart, ama anlamli bir tespit icin
        // orb_min_matches kadar "iyi" eslesme de istiyoruz.
        if ((int)objPts.size() < std::max(4, m_config.orb_min_matches)) continue;

        std::vector<uchar> inlierMask;
        // Reprojeksiyon esigi biraz gevsetildi (5.0->7.0) - "birebir ayni
        // degil ama benzer" acidan gelen hafif perspektif farkina tolerans.
        cv::Mat H = cv::findHomography(objPts, scenePts, cv::RANSAC, 7.0, inlierMask);
        if (H.empty()) continue;

        int inlierCount = cv::countNonZero(inlierMask);
        // GEOMETRIK DOGRULAMA: sadece RANSAC'in "tek bir tutarli donusumle
        // aciklanabilir" dedigi (inlier) noktalar sayilir - aykiri/rastgele
        // eslesmeler burada elenir.
        if (inlierCount < m_config.orb_min_matches) continue;

        std::vector<cv::Point2f> objCorners = {
            {0, 0}, {(float)tmpl.size.width, 0},
            {(float)tmpl.size.width, (float)tmpl.size.height}, {0, (float)tmpl.size.height}};
        std::vector<cv::Point2f> sceneCorners;
        cv::perspectiveTransform(objCorners, sceneCorners, H);

        cv::Rect box = cv::boundingRect(sceneCorners);
        // Homografi noktalari kirpilan bolgenin disina tasabilir (asiri
        // egik/bozuk donusum) - yerel bolge sinirlarina kirp.
        box = box & cv::Rect(0, 0, region.width, region.height);
        if (box.width <= 0 || box.height <= 0) continue;

        // GUVENLIK: kutu sablonun kendi boyutunun makul bir katindan
        // (olcek/aci toleransi) BUYUK olamaz - devasa/yamuk homografi
        // sonucu cikan "buyuk kutu = sahte" durumunu sert biciimde eler.
        const double kMaxSizeFactor = 2.2;
        if (box.width > tmpl.size.width * kMaxSizeFactor || box.height > tmpl.size.height * kMaxSizeFactor) continue;
        if (box.width < 6 || box.height < 6) continue;

        // Tiklama noktasi olarak kutunun GEOMETRIK MERKEZI yerine, GERCEKTEN
        // eslesen (inlier) noktalarin MEDYANINI kullan - bu noktalar
        // nesnenin (tasin) kendi dokusu uzerinde, kutunun bos/arka plan
        // kosina degil. ORTALAMA (mean) yerine MEDYAN: nesnenin bir
        // tarafinda daha yogun desen/doku varsa ortalama o tarafa kayar,
        // medyan bu tur carpik kumelenmelere karsi cok daha dayanikli ve
        // gercek merkeze daha yakin kalir.
        std::vector<double> inlierX, inlierY;
        for (size_t i = 0; i < scenePts.size(); ++i) {
            if (inlierMask[i]) {
                inlierX.push_back(scenePts[i].x);
                inlierY.push_back(scenePts[i].y);
            }
        }

        Detection d;
        d.x = region.x + box.x;
        d.y = region.y + box.y;
        d.w = box.width;
        d.h = box.height;
        d.score = inlierCount;
        d.name = tmpl.name;
        d.category = tmpl.category;
        if (!inlierX.empty()) {
            std::sort(inlierX.begin(), inlierX.end());
            std::sort(inlierY.begin(), inlierY.end());
            double medianX = inlierX[inlierX.size() / 2];
            double medianY = inlierY[inlierY.size() / 2];

            // SIKILASTIRMA (IKI ASAMALI): ilk medyandan kutunun capina gore
            // ASIRI uzak kalan (nesnenin kenarina/disina tasan yanlis
            // eslesme) noktalari eleyip medyani DAHA SIKI/merkezi bir alt
            // kume uzerinden TEKRAR hesapliyoruz - once orta sikilikta (0.30),
            // sonra o SONUCUN etrafinda bir kez daha, cok daha siki (0.16)
            // bir kume ile - her tur gercek merkeze biraz daha yaklasir.
            // Amac: TEK tikta tam merkeze oturma ihtimalini maksimize etmek.
            // Yeterli nokta kalmayan bir turda, bir onceki (daha genis) turun
            // medyaninda kalinir - asiri sikilastirma sonucu nokta sayisi
            // cok azalip guvenilirligi dusurmesin.
            for (double trimFactor : {0.35, 0.22}) {
                double trimRadius = std::max(box.width, box.height) * trimFactor;
                std::vector<double> trimmedX, trimmedY;
                for (size_t i = 0; i < scenePts.size(); ++i) {
                    if (!inlierMask[i]) continue;
                    double dx = scenePts[i].x - medianX;
                    double dy = scenePts[i].y - medianY;
                    if (std::hypot(dx, dy) <= trimRadius) {
                        trimmedX.push_back(scenePts[i].x);
                        trimmedY.push_back(scenePts[i].y);
                    }
                }
                if (trimmedX.size() < 3) break;  // daha fazla siki(la)mayi durdur, mevcut medyanda kal
                std::sort(trimmedX.begin(), trimmedX.end());
                std::sort(trimmedY.begin(), trimmedY.end());
                medianX = trimmedX[trimmedX.size() / 2];
                medianY = trimmedY[trimmedY.size() / 2];
            }

            d.hasPrecisePoint = true;
            d.precisePoint = cv::Point2d(region.x + medianX, region.y + medianY);
        }
        dets.push_back(d);
    }

    return NonMaxSuppression(std::move(dets), 0.4);
}

std::vector<Detection> OrbDetector::NonMaxSuppression(std::vector<Detection> dets, double overlapThresh) {
    // Ayni fiziksel nesne icin BIRDEN FAZLA sablon eslesebilir (farkli
    // acidan/isikta cekilmis ayni tas gibi) - cakisanlari birlestirip en
    // yuksek skorluyu tutuyoruz, aksi halde bot hangisini sececegini
    // karistirip yanlis (hafifce kaymis) noktaya tiklayabilir.
    // KRITIK: bu bastirma SADECE AYNI kategori (metin turu/klasor) icinde
    // yapilir. AYNI ANDA birden fazla tur secildiginde (ör. hem "dovus" hem
    // "savas"), FARKLI turlerin kutulari cakissa BILE birbirini bastirmaz -
    // aksi halde bir tur baska bir turden daha "guclu" (daha yuksek inlier
    // sayisiyla) yanlislikla eslesirse, o konumdaki DIGER turun GECERLI
    // tespidini sessizce SILERDI (ör. "savas metni" hicbir zaman
    // gorunmuyormus gibi davranmasina sebep olabilecek tam da bu turden bir
    // hata).
    if (dets.size() <= 1) return dets;
    std::sort(dets.begin(), dets.end(), [](const Detection& a, const Detection& b) { return a.score > b.score; });

    std::vector<Detection> kept;
    std::vector<bool> suppressed(dets.size(), false);

    for (size_t i = 0; i < dets.size(); ++i) {
        if (suppressed[i]) continue;
        kept.push_back(dets[i]);
        cv::Rect a(dets[i].x, dets[i].y, dets[i].w, dets[i].h);
        for (size_t j = i + 1; j < dets.size(); ++j) {
            if (suppressed[j]) continue;
            if (dets[i].category != dets[j].category) continue;
            cv::Rect b(dets[j].x, dets[j].y, dets[j].w, dets[j].h);
            double interArea = (a & b).area();
            double unionArea = a.area() + b.area() - interArea;
            if (unionArea > 0 && (interArea / unionArea) > overlapThresh) suppressed[j] = true;
        }
    }
    return kept;
}
