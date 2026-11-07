// src/mf_capture_device.cpp
// Real camera capture via Windows Media Foundation IMFSourceReader.
// Output format requested: MFVideoFormat_ARGB32 (32-bpp BGRA, row-major).
// MF's built-in color-converter MFT handles YUY2/NV12/MJPEG → ARGB32.

#include <capapp/capture/mf_capture_device.hpp>
#include <iostream>
#include <cstring>
#include <algorithm>

// ---- Windows / Media Foundation includes (implementation only) ----------------
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <comdef.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#endif // _WIN32
// ------------------------------------------------------------------------------

namespace capapp::capture {

// ============================================================================
// Helpers
// ============================================================================

#if defined(_WIN32)

/** RAII helper: initialise / uninitialise MF on this thread. */
struct MFStartupGuard {
    HRESULT hr{E_FAIL};
    MFStartupGuard() {
        hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    }
    ~MFStartupGuard() {
        if (SUCCEEDED(hr)) MFShutdown();
    }
};

/** Process-singleton so we don't re-init MF for every device open. */
static void ensure_mf_startup() {
    // Initialise once per process; leak is intentional (process lifetime).
    static MFStartupGuard guard;
    (void)guard;
}

/** Build an IMFMediaSource for a symbolic link, then wrap it in a source reader. */
static IMFSourceReader* create_reader_from_symlink(
    const std::wstring& symLink,
    uint32_t targetWidth,
    uint32_t targetHeight)
{
    if (symLink.empty()) {
        std::cerr << "[MFCapture] No symbolic link provided — cannot open device.\n";
        return nullptr;
    }

    // ---- 1. Activate the camera as an IMFMediaSource -----
    IMFAttributes* pActivateAttrs = nullptr;
    HRESULT hr = MFCreateAttributes(&pActivateAttrs, 1);
    if (FAILED(hr)) {
        std::cerr << "[MFCapture] MFCreateAttributes failed: 0x" << std::hex << hr << "\n";
        return nullptr;
    }

    hr = pActivateAttrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                 MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) {
        pActivateAttrs->Release();
        std::cerr << "[MFCapture] SetGUID(SOURCE_TYPE) failed: 0x" << std::hex << hr << "\n";
        return nullptr;
    }

    hr = pActivateAttrs->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                                   symLink.c_str());
    if (FAILED(hr)) {
        pActivateAttrs->Release();
        std::cerr << "[MFCapture] SetString(SYMBOLIC_LINK) failed: 0x" << std::hex << hr << "\n";
        return nullptr;
    }

    IMFMediaSource* pSource = nullptr;
    hr = MFCreateDeviceSource(pActivateAttrs, &pSource);
    pActivateAttrs->Release();
    if (FAILED(hr)) {
        std::cerr << "[MFCapture] MFCreateDeviceSource failed: 0x" << std::hex << hr << "\n";
        return nullptr;
    }

    // ---- 2. Configure the source reader -------------------------
    IMFAttributes* pReaderAttrs = nullptr;
    MFCreateAttributes(&pReaderAttrs, 2);
    if (pReaderAttrs) {
        // Enable hardware transforms (colour converter MFT)
        pReaderAttrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        pReaderAttrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    }

    IMFSourceReader* pReader = nullptr;
    hr = MFCreateSourceReaderFromMediaSource(pSource, pReaderAttrs, &pReader);
    pSource->Release();
    if (pReaderAttrs) pReaderAttrs->Release();

    if (FAILED(hr)) {
        std::cerr << "[MFCapture] MFCreateSourceReaderFromMediaSource failed: 0x"
                  << std::hex << hr << "\n";
        return nullptr;
    }

    // ---- 3. Request ARGB32 output (MF colour-converts on the fly) ----
    IMFMediaType* pOutputType = nullptr;
    hr = MFCreateMediaType(&pOutputType);
    if (SUCCEEDED(hr)) {
        pOutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        pOutputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);  // = BGRA8 in memory
        // Try to set resolution; reader will clamp to what the device supports
        if (targetWidth > 0 && targetHeight > 0) {
            MFSetAttributeSize(pOutputType, MF_MT_FRAME_SIZE, targetWidth, targetHeight);
        }
        hr = pReader->SetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                nullptr, pOutputType);
        pOutputType->Release();
        if (FAILED(hr)) {
            // Fallback: let MF choose the first available video format
            std::cerr << "[MFCapture] SetCurrentMediaType(ARGB32) failed (0x"
                      << std::hex << hr << ") — trying RGB32 fallback.\n";

            IMFMediaType* pFallback = nullptr;
            if (SUCCEEDED(MFCreateMediaType(&pFallback))) {
                pFallback->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                pFallback->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
                hr = pReader->SetCurrentMediaType(
                        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                        nullptr, pFallback);
                pFallback->Release();
                if (FAILED(hr)) {
                    std::cerr << "[MFCapture] RGB32 fallback also failed (0x"
                              << std::hex << hr << ") — using native format.\n";
                }
            }
        }
    }

    // ---- 4. Query actual output frame size -------------------------
    // (May differ from requested if device doesn't support it)
    IMFMediaType* pActualType = nullptr;
    hr = pReader->GetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &pActualType);
    if (SUCCEEDED(hr) && pActualType) {
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(pActualType, MF_MT_FRAME_SIZE, &w, &h);
        pActualType->Release();
        std::cout << "[MFCapture] Negotiated frame size: " << w << "x" << h << "\n";
    }

    return pReader;
}

#endif // _WIN32

// ============================================================================
// MFCaptureDevice
// ============================================================================

MFCaptureDevice::MFCaptureDevice(std::wstring symbolicLink)
    : symbolicLink_(std::move(symbolicLink)) {}

MFCaptureDevice::~MFCaptureDevice() {
    close();
}

bool MFCaptureDevice::open(const CaptureDeviceConfig& config) {
    close(); // release previous state
    config_ = config;
    stats_ = {};
    sequenceCounter_ = 0;

#if defined(_WIN32)
    ensure_mf_startup();

    auto* pReader = create_reader_from_symlink(
            symbolicLink_, config_.targetWidth, config_.targetHeight);
    if (!pReader) {
        std::cerr << "[MFCapture] Failed to open camera: " << config_.deviceName << "\n";
        return false;
    }
    pReader_ = pReader;

    // Determine actual output frame size from the reader
    auto* reader = static_cast<IMFSourceReader*>(pReader_);
    IMFMediaType* pType = nullptr;
    if (SUCCEEDED(reader->GetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &pType)) && pType) {
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &w, &h);
        pType->Release();
        frameWidth_  = (w > 0) ? w : config_.targetWidth;
        frameHeight_ = (h > 0) ? h : config_.targetHeight;
    } else {
        frameWidth_  = config_.targetWidth;
        frameHeight_ = config_.targetHeight;
    }

    // Pre-allocate staging buffer (BGRA = 4 bytes/pixel)
    stagingBuffer_.resize(static_cast<size_t>(frameWidth_) * frameHeight_ * 4u, 0);

    std::cout << "[MFCapture] Opened camera \"" << config_.deviceName
              << "\" at " << frameWidth_ << "x" << frameHeight_ << "\n";
    open_ = true;
    return true;

#else
    // Non-Windows fallback: nothing to open
    (void)config;
    return false;
#endif
}

void MFCaptureDevice::close() noexcept {
    stop_stream();
    open_ = false;
#if defined(_WIN32)
    if (pReader_) {
        static_cast<IMFSourceReader*>(pReader_)->Release();
        pReader_ = nullptr;
    }
#endif
    stagingBuffer_.clear();
    frameWidth_ = 0;
    frameHeight_ = 0;
}

bool MFCaptureDevice::is_open() const noexcept {
    return open_;
}

bool MFCaptureDevice::start_stream() {
    if (!open_) return false;
    streaming_ = true;
    return true;
}

void MFCaptureDevice::stop_stream() noexcept {
    streaming_ = false;
}

bool MFCaptureDevice::is_streaming() const noexcept {
    return streaming_;
}

bool MFCaptureDevice::acquire_frame(Frame& outFrame, uint32_t /*timeoutMs*/) {
    if (!streaming_) return false;

#if defined(_WIN32)
    if (!pReader_) return false;

    auto* reader = static_cast<IMFSourceReader*>(pReader_);

    DWORD streamIndex = 0;
    DWORD flags = 0;
    LONGLONG llTimestamp = 0;
    IMFSample* pSample = nullptr;

    HRESULT hr = reader->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            0,
            &streamIndex,
            &flags,
            &llTimestamp,
            &pSample);

    if (FAILED(hr)) {
        std::cerr << "[MFCapture] ReadSample failed: 0x" << std::hex << hr << "\n";
        return false;
    }

    // Stream changed / end-of-stream: skip this sample
    if (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) {
        if (pSample) pSample->Release();
        return false;
    }

    if (!pSample) {
        // Can happen on format-change events; not an error
        return false;
    }

    // ---- Get the contiguous buffer from the sample ----
    IMFMediaBuffer* pBuffer = nullptr;
    hr = pSample->ConvertToContiguousBuffer(&pBuffer);
    if (FAILED(hr) || !pBuffer) {
        pSample->Release();
        return false;
    }

    BYTE* pData = nullptr;
    DWORD maxLen = 0;
    DWORD curLen = 0;
    hr = pBuffer->Lock(&pData, &maxLen, &curLen);
    if (SUCCEEDED(hr) && pData && curLen > 0) {
        // Copy into our staging buffer (resize if needed, e.g. on resolution change)
        size_t needed = static_cast<size_t>(frameWidth_) * frameHeight_ * 4u;
        if (stagingBuffer_.size() < needed) {
            stagingBuffer_.resize(needed, 0);
        }
        size_t toCopy = std::min(static_cast<size_t>(curLen), stagingBuffer_.size());
        std::memcpy(stagingBuffer_.data(), pData, toCopy);
        pBuffer->Unlock();

        // Fill the Frame struct
        outFrame.data            = stagingBuffer_.data();
        outFrame.width           = frameWidth_;
        outFrame.height          = frameHeight_;
        outFrame.stride          = frameWidth_ * 4u;
        outFrame.format          = PixelFormat::BGRA8;
        outFrame.timestampNs     = static_cast<TimestampNs>(llTimestamp) * 100ULL; // 100-ns → ns
        outFrame.sequenceNumber  = ++sequenceCounter_;
        outFrame.isKeyframe      = true; // uncompressed raw frame
        outFrame.vkResourceHandle = nullptr;

        stats_.framesCaptured++;
        stats_.lastCaptureTimestampNs = outFrame.timestampNs;
        stats_.averageFps = static_cast<double>(config_.targetFps);
    } else {
        pBuffer->Release();
        pSample->Release();
        return false;
    }

    pBuffer->Release();
    pSample->Release();
    return true;

#else
    (void)outFrame;
    return false;
#endif
}

void MFCaptureDevice::release_frame(Frame& frame) noexcept {
    // Data already copied to stagingBuffer_; just null the pointer
    frame.data = nullptr;
}

CaptureDeviceStats MFCaptureDevice::stats() const noexcept {
    return stats_;
}

} // namespace capapp::capture
