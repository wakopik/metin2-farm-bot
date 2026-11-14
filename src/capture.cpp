#include "capture.h"
#include "diag_log.h"

#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <stdexcept>

using namespace winrt;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using Microsoft::WRL::ComPtr;

namespace {

IDirect3DDevice CreateWinrtDevice(ComPtr<ID3D11Device> const& d3dDevice) {
    ComPtr<IDXGIDevice> dxgiDevice;
    winrt::check_hresult(d3dDevice.As(&dxgiDevice));

    winrt::com_ptr<::IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put()));
    return inspectable.as<IDirect3DDevice>();
}

ComPtr<ID3D11Texture2D> GetTextureFromSurface(IDirect3DSurface const& surface) {
    auto access = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    ComPtr<ID3D11Texture2D> texture;
    winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&texture)));
    return texture;
}

}  // namespace

WindowCapture::WindowCapture(HWND hwnd, int minIntervalMs) : m_hwnd(hwnd), m_minIntervalMs(minIntervalMs) {
    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    winrt::check_hresult(D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
        nullptr, 0, D3D11_SDK_VERSION,
        m_d3dDevice.GetAddressOf(), nullptr, m_d3dContext.GetAddressOf()));

    m_winrtDevice = CreateWinrtDevice(m_d3dDevice);

    auto interopFactory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    winrt::com_ptr<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem> itemAbi;
    winrt::check_hresult(interopFactory->CreateForWindow(
        hwnd, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
        itemAbi.put_void()));
    m_item = itemAbi.as<GraphicsCaptureItem>();

    m_lastSize = m_item.Size();
    // CreateFreeThreaded kullaniyoruz (Create degil) - Create() metodu
    // FrameArrived olayini iletmek icin cagiran thread'de bir DispatcherQueue
    // gerektiriyor; konsol/Win32 uygulamamizda boyle bir sey yok, o yuzden
    // Create() ile hicbir zaman kare gelmiyordu (tespit hep bos donuyordu).
    m_framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
        m_winrtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, m_lastSize);

    m_frameArrivedRevoker = m_framePool.FrameArrived(
        winrt::auto_revoke, {this, &WindowCapture::OnFrameArrived});

    m_session = m_framePool.CreateCaptureSession(m_item);
    // Fare imlecini yakalamayalim - tespit/tiklama mantigini bozmasin.
    m_session.IsCursorCaptureEnabled(false);
    // KULLANICI TALEBI: Windows Graphics Capture, Windows 11'de yakalanan
    // pencerenin kenarina SARI/altin bir "kayit gostergesi" cizgisi cizer.
    // IsBorderRequired ozelligi (GraphicsCaptureSession3, sadece belirli
    // Windows surumlerinde mevcut) bunu kapatir - eski surumlerde ozellik
    // bulunmuyorsa ApiInformation kontrolu sayesinde SESSIZCE atlanir,
    // crash olmaz.
    if (winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(
            L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired")) {
        m_session.IsBorderRequired(false);
    }
    m_session.StartCapture();

    // TANI: "hicbir sey olmuyor" sikayetlerinde ilk sorulacak soru "yakalama
    // GERCEKTEN basladi mi" - bunu KANITLAMAK icin baslangic boyutunu/hwnd'yi
    // logluyoruz. Bazi sistemlerde (ozellikle RDP/uzak masaustu, bazi eski
    // GPU surucu kombinasyonlari, "Bitmap" render modu zorunlu pencereler)
    // StartCapture() istisna FIRLATMADAN basarili doner ama OnFrameArrived
    // HICBIR ZAMAN tetiklenmez - constructor asamasi TEK BASINA yeterli kanit
    // degil, bu yuzden bot.cpp tarafinda ayrica "ilk kare ne kadar surede
    // geldi" de izleniyor (bkz. Step()).
    LogDiag("CAP init hwnd=" + std::to_string((long long)(intptr_t)hwnd) +
             " size=" + std::to_string(m_lastSize.Width) + "x" + std::to_string(m_lastSize.Height));
}

WindowCapture::~WindowCapture() { Stop(); }

void WindowCapture::Stop() {
    if (m_stopped) return;
    m_stopped = true;
    m_frameArrivedRevoker.revoke();
    if (m_session) m_session.Close();
    if (m_framePool) m_framePool.Close();
}

void WindowCapture::OnFrameArrived(
    Direct3D11CaptureFramePool const& sender,
    winrt::Windows::Foundation::IInspectable const&) {
    // NOT: bos frame pool'dan kare gelmemesi NORMAL/BEKLENEN bir durumdur
    // (henuz yeni kare uretilmemis olabilir) - bunu hata sayaci olarak
    // SAYMIYORUZ, aksi halde saglikli bir sistemde bile sayac surekli artar
    // ve "gercek" arizadan ayirt edilemez hale gelirdi.
    auto frame = sender.TryGetNextFrame();
    if (!frame) return;

    auto contentSize = frame.ContentSize();
    if (contentSize.Width != m_lastSize.Width || contentSize.Height != m_lastSize.Height) {
        m_lastSize = contentSize;
        m_framePool.Recreate(m_winrtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, m_lastSize);
    }

    // KRITIK: oyunun FPS'i ile ayni hizda pahali GPU->CPU kopyalama/donusum
    // yapmayalim - bu oyunu kasabilir. Frame pool'u bosaltmak icin
    // TryGetNextFrame() zaten cagirildi (yeterli), simdi de yeterince zaman
    // gecmediyse burada dur - hicbir ek islem (texture kopyalama, Map,
    // renk donusumu) yapilmaz.
    auto now = std::chrono::steady_clock::now();
    if (m_minIntervalMs > 0) {
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastProcessed).count();
        if (elapsedMs < m_minIntervalMs) return;
    }
    m_lastProcessed = now;

    // TANI: buradan sonraki HER erken-cikis GERCEK bir ariza (GPU/surucu
    // seviyesinde) - bu, "goruntu hic guncellenmiyor" sikayetlerinin sessizce
    // kaybolmasini onlemek icin nedeniyle BIRLIKTE sayiliyor/kaydediliyor.
    auto markFail = [this](const char* reason) {
        m_failCount++;
        std::lock_guard<std::mutex> lock(m_failReasonMutex);
        m_lastFailReason = reason;
    };

    ComPtr<ID3D11Texture2D> texture;
    try {
        texture = GetTextureFromSurface(frame.Surface());
    } catch (...) {
        markFail("get-texture-from-surface-threw");
        return;
    }

    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(m_d3dDevice->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf()))) {
        markFail("create-staging-texture-failed");
        return;
    }

    m_d3dContext->CopyResource(staging.Get(), texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_d3dContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        markFail("map-staging-texture-failed");
        return;
    }

    cv::Mat bgra(desc.Height, desc.Width, CV_8UC4, mapped.pData, mapped.RowPitch);
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);

    m_d3dContext->Unmap(staging.Get(), 0);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latestFrame = bgr;
        m_lastFrameAt = std::chrono::steady_clock::now();
    }
}

std::optional<cv::Mat> WindowCapture::GetFrame() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_latestFrame.empty()) return std::nullopt;
    return m_latestFrame.clone();
}

double WindowCapture::SecondsSinceLastFrame() const {
    if (m_lastFrameAt == std::chrono::steady_clock::time_point{}) return -1.0;  // hic kare gelmedi
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_lastFrameAt).count();
}

long long WindowCapture::FailCount() const { return m_failCount.load(); }

std::string WindowCapture::LastFailReason() const {
    std::lock_guard<std::mutex> lock(m_failReasonMutex);
    return m_lastFailReason;
}
