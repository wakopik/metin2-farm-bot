#pragma once
#include <windows.h>
#include <opencv2/opencv.hpp>
#include <atomic>
#include <chrono>
#include <optional>
#include <mutex>
#include <string>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <d3d11.h>
#include <wrl/client.h>

// Belirli bir pencerenin (hwnd) icerigini Windows Graphics Capture API
// ile yakalar - pencere on planda olmasa/baska bir pencere ustune gelse
// bile HER ZAMAN o pencerenin gercek icerigini gorur. Python tarafindaki
// windows-capture (src/capture.py -> WindowCapture) ile ayni mantik.
//
// KRITIK PERFORMANS NOTU: WGC, oyunun kendi FPS'inde (ornegin 60-144)
// kare gonderir. Her karede pahali bir GPU->CPU texture kopyalama +
// renk donusumu yapmak, oyunun FPS'ini dusurebilir. Bu yuzden
// minIntervalMs'den daha sik gelen kareler icin bu pahali islem
// TAMAMEN ATLANIR - sadece frame pool'u bosaltmak icin gerekli minimum
// islem yapilir.
class WindowCapture {
public:
    explicit WindowCapture(HWND hwnd, int minIntervalMs = 150);
    ~WindowCapture();

    WindowCapture(const WindowCapture&) = delete;
    WindowCapture& operator=(const WindowCapture&) = delete;

    // En guncel kareyi (BGR, tum pencere - baslik cubugu/kenarlik dahil)
    // dondurur. Henuz kare gelmediyse std::nullopt doner.
    std::optional<cv::Mat> GetFrame();

    void Stop();

    // TANI: "hicbir sey olmuyor" sikayetlerinde (bkz. capture.cpp) hic kare
    // gelmedigini/uretilemedigini KANITLAYABILMEK icin - son basarili karenin
    // uzerinden kac saniye gectigi ve OnFrameArrived'in HANGI asamada sessizce
    // basarisiz oldugunu (texture/staging/map) sayaclarla disari veriyoruz.
    double SecondsSinceLastFrame() const;
    long long FailCount() const;
    std::string LastFailReason() const;

private:
    void OnFrameArrived(
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
        winrt::Windows::Foundation::IInspectable const& args);

    HWND m_hwnd{};

    Microsoft::WRL::ComPtr<ID3D11Device> m_d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_d3dContext;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_winrtDevice{nullptr};

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::FrameArrived_revoker m_frameArrivedRevoker;

    winrt::Windows::Graphics::SizeInt32 m_lastSize{};

    int m_minIntervalMs;
    std::chrono::steady_clock::time_point m_lastProcessed{};

    std::mutex m_mutex;
    cv::Mat m_latestFrame;
    bool m_stopped = false;

    std::chrono::steady_clock::time_point m_lastFrameAt{};
    std::atomic<long long> m_failCount{0};
    mutable std::mutex m_failReasonMutex;
    std::string m_lastFailReason;
};
