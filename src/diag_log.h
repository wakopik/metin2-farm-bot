#pragma once
#include <chrono>
#include <ctime>
#include <fstream>
#include <string>

// PAYLASILAN tani-loglama yardimcisi - bot.cpp/input.cpp'nin KENDI (dosya-ici,
// baska .cpp'lerden erisilemeyen) LogClickDebug/LogMouseDebug kopyalariyla
// AYNI dosyaya (diag.log) AYNI formatta yazar. puzzle_solver.cpp gibi
// bu ikisine dahil olmayan dosyalarin da (desktop_app.exe konsolsuz calistigi
// icin std::cout hicbir yere gitmiyor - bkz. bot.cpp basindaki yorum) tani
// verisini KAYBETMEDEN aynı gizli dosyaya yazabilmesi icin.
inline void LogDiag(const std::string& msg) {
    std::ofstream f("diag.log", std::ios::app);
    if (!f.is_open()) return;
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
    localtime_s(&tmBuf, &t);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tmBuf);
    f << "[" << buf << "] " << msg << "\n";
}
