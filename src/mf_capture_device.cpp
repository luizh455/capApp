// src/mf_capture_device.cpp
// Real camera capture via Windows Media Foundation IMFSourceReader.
// Output format requested: MFVideoFormat_ARGB32 (32-bpp BGRA, row-major).
// MF's built-in color-converter MFT handles YUY2/NV12/MJPEG → ARGB32.

#include <capapp/capture/mf_capture_device.hpp>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <vector>

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
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devpkey.h>
#include <winioctl.h>
#include <usbioctl.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
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

static bool select_source_compression(IMFMediaSource* source,
                                      CaptureCompression requestedCompression,
                                      uint32_t targetWidth,
                                      uint32_t targetHeight,
                                      uint32_t targetFps) {
    if (requestedCompression == CaptureCompression::Auto) return true;

    const GUID requestedSubtype = requestedCompression == CaptureCompression::YUY2
        ? MFVideoFormat_YUY2 : MFVideoFormat_MJPG;
    IMFPresentationDescriptor* presentation = nullptr;
    HRESULT hr = source->CreatePresentationDescriptor(&presentation);
    if (FAILED(hr) || !presentation) return false;

    DWORD streamCount = 0;
    hr = presentation->GetStreamDescriptorCount(&streamCount);
    bool selected = false;
    for (DWORD streamIndex = 0; SUCCEEDED(hr) && streamIndex < streamCount && !selected;
         ++streamIndex) {
        BOOL isSelected = FALSE;
        IMFStreamDescriptor* stream = nullptr;
        if (FAILED(presentation->GetStreamDescriptorByIndex(
                streamIndex, &isSelected, &stream)) || !stream) {
            continue;
        }

        IMFMediaTypeHandler* handler = nullptr;
        if (SUCCEEDED(stream->GetMediaTypeHandler(&handler)) && handler) {
            IMFMediaType* bestType = nullptr;
            bool bestMatchesSize = false;
            bool bestMatchesFps = false;
            uint64_t bestPixels = 0;
            DWORD typeCount = 0;
            if (SUCCEEDED(handler->GetMediaTypeCount(&typeCount))) {
                for (DWORD typeIndex = 0; typeIndex < typeCount; ++typeIndex) {
                    IMFMediaType* type = nullptr;
                    if (FAILED(handler->GetMediaTypeByIndex(typeIndex, &type)) || !type) {
                        continue;
                    }

                    GUID majorType{};
                    GUID subtype{};
                    UINT32 width = 0, height = 0, fpsNumerator = 0, fpsDenominator = 1;
                    const bool matchesRequestedSubtype =
                        SUCCEEDED(type->GetGUID(MF_MT_MAJOR_TYPE, &majorType)) &&
                        majorType == MFMediaType_Video &&
                        SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) &&
                        subtype == requestedSubtype;
                    if (matchesRequestedSubtype) {
                        MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height);
                        MFGetAttributeRatio(type, MF_MT_FRAME_RATE,
                                            &fpsNumerator, &fpsDenominator);
                        const UINT32 fps = fpsDenominator > 0
                            ? fpsNumerator / fpsDenominator : 0;
                        const bool matchesSize = width == targetWidth && height == targetHeight;
                        const bool matchesFps = fps == targetFps;
                        const uint64_t pixels = static_cast<uint64_t>(width) * height;
                        const bool better = !bestType ||
                            (matchesSize && !bestMatchesSize) ||
                            (matchesSize == bestMatchesSize && matchesFps && !bestMatchesFps) ||
                            (matchesSize == bestMatchesSize && matchesFps == bestMatchesFps &&
                             pixels > bestPixels);
                        if (better) {
                            if (bestType) bestType->Release();
                            bestType = type;
                            bestMatchesSize = matchesSize;
                            bestMatchesFps = matchesFps;
                            bestPixels = pixels;
                            type = nullptr;
                        }
                    }
                    if (type) type->Release();
                }
            }

            if (bestType) {
                hr = handler->SetCurrentMediaType(bestType);
                if (SUCCEEDED(hr)) {
                    std::cout << "[MFCapture] Requested source compression: "
                              << capture_compression_name(requestedCompression) << "\n";
                    selected = true;
                }
                bestType->Release();
            }
            handler->Release();
        }
        stream->Release();
    }

    presentation->Release();
    if (!selected) {
        std::cerr << "[MFCapture Warning] Device does not accept requested source compression "
                  << capture_compression_name(requestedCompression)
                  << "; using automatic format selection.\n";
    }
    return selected;
}

static const char* media_subtype_name(const GUID& subtype) noexcept {
    if (subtype == MFVideoFormat_YUY2) return "YUY2";
    if (subtype == MFVideoFormat_MJPG) return "MJPEG";
    if (subtype == MFVideoFormat_NV12) return "NV12";
    if (subtype == MFVideoFormat_ARGB32) return "ARGB32";
    return "other";
}

static std::vector<std::wstring> get_location_paths(DEVINST node) {
    static constexpr DEVPROPKEY property{
        {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}},
        37};
    DEVPROPTYPE propertyType = 0;
    ULONG byteCount = 0;
    if (CM_Get_DevNode_PropertyW(node, &property, &propertyType, nullptr,
                                 &byteCount, 0) != CR_BUFFER_SMALL ||
        propertyType != DEVPROP_TYPE_STRING_LIST || byteCount < sizeof(wchar_t) * 2) {
        return {};
    }

    std::vector<wchar_t> buffer(byteCount / sizeof(wchar_t));
    if (CM_Get_DevNode_PropertyW(node, &property, &propertyType,
                                 reinterpret_cast<PBYTE>(buffer.data()),
                                 &byteCount, 0) != CR_SUCCESS) {
        return {};
    }

    std::vector<std::wstring> paths;
    for (const wchar_t* path = buffer.data(); *path; path += wcslen(path) + 1) {
        paths.emplace_back(path);
    }
    return paths;
}

static std::wstring normalize_device_id(std::wstring id) {
    std::transform(id.begin(), id.end(), id.begin(), towupper);
    std::replace(id.begin(), id.end(), L'#', L'\\');
    return id;
}

static bool query_usb_link(const std::wstring& symbolicLink,
                           CaptureStreamInfo& streamInfo) {
    const size_t prefix = symbolicLink.find(L"\\\\?\\");
    const size_t suffix = symbolicLink.find(L"#{");
    if (prefix != 0 || suffix == std::wstring::npos || suffix <= 4) return false;
    const std::wstring deviceId = normalize_device_id(symbolicLink.substr(4, suffix - 4));

    DEVINST deviceNode = 0;
    if (CM_Locate_DevNodeW(&deviceNode, const_cast<wchar_t*>(deviceId.c_str()),
                           CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
        return false;
    }

    std::vector<DEVINST> ancestors;
    for (DEVINST node = deviceNode; node != 0; ) {
        ancestors.push_back(node);
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) break;
        node = parent;
    }
    if (ancestors.size() < 2) return false;

    std::vector<std::wstring> ancestorIds;
    for (DEVINST node : ancestors) {
        wchar_t id[MAX_DEVICE_ID_LEN]{};
        if (CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
            ancestorIds.push_back(normalize_device_id(id));
        }
    }
    const auto cameraPaths = get_location_paths(deviceNode);
    if (cameraPaths.empty()) return false;

    static constexpr GUID kUsbHubInterface{
        0xf18a0e88, 0xc30c, 0x11d0, {0x88, 0x15, 0x00, 0xa0, 0xc9, 0x06, 0xbe, 0xd8}};
    HDEVINFO devices = SetupDiGetClassDevsW(&kUsbHubInterface, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) return false;

    std::wstring hubPath;
    ULONG portNumber = 0;
    size_t longestPrefix = 0;
    for (DWORD index = 0; ; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &kUsbHubInterface,
                                         index, &interfaceData)) {
            break;
        }

        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, nullptr, 0,
                                         &requiredSize, nullptr);
        if (requiredSize < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
        std::vector<BYTE> detailBuffer(requiredSize);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
        detail->cbSize = sizeof(*detail);
        SP_DEVINFO_DATA infoData{};
        infoData.cbSize = sizeof(infoData);
        if (!SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, detail,
                                               requiredSize, nullptr, &infoData)) {
            continue;
        }

        wchar_t hubId[MAX_DEVICE_ID_LEN]{};
        if (CM_Get_Device_IDW(infoData.DevInst, hubId, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS ||
            std::find(ancestorIds.begin(), ancestorIds.end(),
                      normalize_device_id(hubId)) == ancestorIds.end()) {
            continue;
        }

        const auto hubPaths = get_location_paths(infoData.DevInst);
        for (const auto& cameraPath : cameraPaths) {
            for (const auto& path : hubPaths) {
                if (path.size() <= longestPrefix || cameraPath.size() <= path.size() ||
                    _wcsnicmp(cameraPath.c_str(), path.c_str(), path.size()) != 0 ||
                    cameraPath[path.size()] != L'#') {
                    continue;
                }
                const std::wstring portSegment = cameraPath.substr(path.size() + 1);
                if (portSegment.size() < 5 || _wcsnicmp(portSegment.c_str(), L"USB(", 4) != 0) {
                    continue;
                }
                const size_t end = portSegment.find(L')');
                if (end == std::wstring::npos) continue;
                try {
                    portNumber = std::stoul(portSegment.substr(4, end - 4));
                } catch (const std::exception&) {
                    continue;
                }
                hubPath = detail->DevicePath;
                longestPrefix = path.size();
            }
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    if (hubPath.empty() || portNumber == 0) return false;

    HANDLE hub = CreateFileW(hubPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hub == INVALID_HANDLE_VALUE) return false;

    std::vector<BYTE> connectionBuffer(4096);
    auto* connection = reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX*>(
        connectionBuffer.data());
    connection->ConnectionIndex = portNumber;
    DWORD bytesReturned = 0;
    const BOOL queried = DeviceIoControl(hub,
        IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX, connectionBuffer.data(),
        static_cast<DWORD>(connectionBuffer.size()), connectionBuffer.data(),
        static_cast<DWORD>(connectionBuffer.size()), &bytesReturned, nullptr);

    std::string speed = "Unknown";
    uint32_t maxMbps = 0;
    if (queried && connection->ConnectionStatus == DeviceConnected) {
        switch (connection->Speed) {
            case UsbLowSpeed: speed = "USB Low Speed"; maxMbps = 12; break;
            case UsbFullSpeed: speed = "USB Full Speed"; maxMbps = 12; break;
            case UsbHighSpeed: speed = "USB High Speed"; maxMbps = 480; break;
            case UsbSuperSpeed: speed = "USB SuperSpeed"; maxMbps = 5000; break;
            default: break;
        }
#if defined(IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2)
        USB_NODE_CONNECTION_INFORMATION_EX_V2 v2{};
        v2.ConnectionIndex = portNumber;
        v2.Length = sizeof(v2);
        v2.SupportedUsbProtocols.Usb300 = 1;
        if (DeviceIoControl(hub, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2,
                            &v2, sizeof(v2), &v2, sizeof(v2), &bytesReturned, nullptr) &&
            v2.Flags.DeviceIsOperatingAtSuperSpeedPlusOrHigher) {
            speed = "USB SuperSpeed+";
            maxMbps = 10000;
        }
#endif
    }
    CloseHandle(hub);
    if (maxMbps == 0) return false;

    streamInfo.usbLinkSpeed = std::move(speed);
    streamInfo.usbMaxMbps = maxMbps;
    return true;
}

/** Build an IMFMediaSource for a symbolic link, then wrap it in a source reader. */
static IMFSourceReader* create_reader_from_symlink(
    const std::wstring& symLink,
    uint32_t targetWidth,
    uint32_t targetHeight,
    uint32_t targetFps,
    CaptureCompression compression,
    CaptureStreamInfo& streamInfo)
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

    select_source_compression(pSource, compression, targetWidth, targetHeight, targetFps);

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

    // ---- 3. Auto-negotiate native device media type -----
    DWORD dwTypeIndex = 0;
    IMFMediaType* pNativeType = nullptr;
    UINT32 bestWidth = 0, bestHeight = 0, bestFps = 0;
    GUID bestSubtype = GUID_NULL;
    bool bestMatchesCompression = false;
    bool bestMatchesTargetFps = false;
    bool bestMatchesTargetSize = false;

    while (SUCCEEDED(pReader->GetNativeMediaType(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), dwTypeIndex, &pNativeType))) {
        UINT32 w = 0, h = 0, fpsNum = 0, fpsDen = 1;
        GUID subtype = GUID_NULL;
        MFGetAttributeSize(pNativeType, MF_MT_FRAME_SIZE, &w, &h);
        MFGetAttributeRatio(pNativeType, MF_MT_FRAME_RATE, &fpsNum, &fpsDen);
        pNativeType->GetGUID(MF_MT_SUBTYPE, &subtype);
        UINT32 fps = fpsDen > 0 ? (fpsNum / fpsDen) : 30;

        const GUID requestedSubtype = compression == CaptureCompression::YUY2
            ? MFVideoFormat_YUY2 : MFVideoFormat_MJPG;
        const bool matchesCompression = compression == CaptureCompression::Auto ||
            subtype == requestedSubtype;
        const bool matchesTargetSize = w == targetWidth && h == targetHeight;
        const bool matchesTargetFps = fps == targetFps;
        const uint64_t candidatePixels = static_cast<uint64_t>(w) * h;
        const uint64_t bestPixels = static_cast<uint64_t>(bestWidth) * bestHeight;
        const bool selectType = bestWidth == 0 ||
            (matchesCompression && !bestMatchesCompression) ||
            (matchesCompression == bestMatchesCompression &&
             matchesTargetSize && !bestMatchesTargetSize) ||
            (matchesCompression == bestMatchesCompression &&
             matchesTargetSize == bestMatchesTargetSize &&
             matchesTargetFps && !bestMatchesTargetFps) ||
            (matchesCompression == bestMatchesCompression &&
             matchesTargetSize == bestMatchesTargetSize &&
             matchesTargetFps == bestMatchesTargetFps &&
             candidatePixels > bestPixels) ||
            (matchesCompression == bestMatchesCompression &&
             matchesTargetSize == bestMatchesTargetSize &&
             matchesTargetFps == bestMatchesTargetFps &&
             candidatePixels == bestPixels && fps > bestFps);

        if (selectType) {
            bestWidth = w;
            bestHeight = h;
            bestFps = fps;
            bestSubtype = subtype;
            bestMatchesCompression = matchesCompression;
            bestMatchesTargetFps = matchesTargetFps;
            bestMatchesTargetSize = matchesTargetSize;
        }
        pNativeType->Release();
        if (bestMatchesCompression && bestMatchesTargetSize && bestMatchesTargetFps) {
            break;
        }
        dwTypeIndex++;
    }

    std::cout << "[MFCapture] Selected source format: "
              << media_subtype_name(bestSubtype) << " "
              << bestWidth << "x" << bestHeight << " @" << bestFps << " FPS\n";
    streamInfo.sourceWidth = bestWidth;
    streamInfo.sourceHeight = bestHeight;
    streamInfo.sourceFps = bestFps;
    streamInfo.sourceFormat = media_subtype_name(bestSubtype);
    if (bestSubtype == MFVideoFormat_YUY2 && bestWidth > 0 && bestHeight > 0 && bestFps > 0) {
        streamInfo.nominalInputMbps =
            static_cast<double>(bestWidth) * bestHeight * 16.0 * bestFps / 1'000'000.0;
        streamInfo.nominalInputBandwidthKnown = true;
    }
    query_usb_link(symLink, streamInfo);

    uint32_t finalW = (bestWidth > 0) ? bestWidth : (targetWidth > 0 ? targetWidth : 1920);
    uint32_t finalH = (bestHeight > 0) ? bestHeight : (targetHeight > 0 ? targetHeight : 1080);
    uint32_t finalFps = (bestFps > 0) ? bestFps : (targetFps > 0 ? targetFps : 60);

    // ---- 4. Request ARGB32 output at negotiated resolution & FPS ----
    IMFMediaType* pOutputType = nullptr;
    hr = MFCreateMediaType(&pOutputType);
    if (SUCCEEDED(hr)) {
        pOutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        pOutputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);  // = BGRA8 in memory
        MFSetAttributeSize(pOutputType, MF_MT_FRAME_SIZE, finalW, finalH);
        MFSetAttributeRatio(pOutputType, MF_MT_FRAME_RATE, finalFps, 1);

        hr = pReader->SetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                nullptr, pOutputType);
        pOutputType->Release();
        if (FAILED(hr)) {
            std::cerr << "[MFCapture] SetCurrentMediaType(ARGB32) fallback to native mode.\n";
        }
    }

    // ---- 5. Query actual output frame size -------------------------
    IMFMediaType* pActualType = nullptr;
    hr = pReader->GetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &pActualType);
    if (SUCCEEDED(hr) && pActualType) {
        UINT32 w = 0, h = 0, fpsNum = 0, fpsDen = 1;
        GUID actualSubtype = GUID_NULL;
        MFGetAttributeSize(pActualType, MF_MT_FRAME_SIZE, &w, &h);
        MFGetAttributeRatio(pActualType, MF_MT_FRAME_RATE, &fpsNum, &fpsDen);
        pActualType->GetGUID(MF_MT_SUBTYPE, &actualSubtype);
        const UINT32 actualFps = fpsDen > 0 ? (fpsNum / fpsDen) : 0;
        streamInfo.outputFormat = media_subtype_name(actualSubtype);
        pActualType->Release();
        std::cout << "[MFCapture] Auto-negotiated stream: " << w << "x" << h
                  << " @" << actualFps << " FPS\n";
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
    streamInfo_ = {};
    sequenceCounter_ = 0;

#if defined(_WIN32)
    ensure_mf_startup();

    auto* pReader = create_reader_from_symlink(
            symbolicLink_, config_.targetWidth, config_.targetHeight, config_.targetFps,
            config_.compression, streamInfo_);
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
    if (pLockedBuffer_) {
        auto* buffer = static_cast<IMFMediaBuffer*>(pLockedBuffer_);
        buffer->Unlock();
        buffer->Release();
        pLockedBuffer_ = nullptr;
    }
    if (pReader_) {
        static_cast<IMFSourceReader*>(pReader_)->Release();
        pReader_ = nullptr;
    }
#endif
    pLockedBuffer_ = nullptr;
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
        auto* previousBuffer = static_cast<IMFMediaBuffer*>(pLockedBuffer_);
        if (previousBuffer == pBuffer) {
            pBuffer->Unlock();
            pBuffer->Release();
        } else {
            pLockedBuffer_ = pBuffer;
            if (previousBuffer) {
                previousBuffer->Unlock();
                previousBuffer->Release();
            }
        }
        outFrame.data            = pData;
        outFrame.width           = frameWidth_;
        outFrame.height          = frameHeight_;
        outFrame.stride          = frameWidth_ * 4u;
        outFrame.dataSize        = curLen;
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

    pSample->Release();
    return true;

#else
    (void)outFrame;
    return false;
#endif
}

void MFCaptureDevice::release_frame(Frame& frame) noexcept {
    // Keep the locked buffer valid through rendering; it is released by the next acquire.
    (void)frame;
}

CaptureDeviceStats MFCaptureDevice::stats() const noexcept {
    return stats_;
}

CaptureStreamInfo MFCaptureDevice::stream_info() const {
    return streamInfo_;
}

} // namespace capapp::capture
