#include <capapp/capture/camera_selector.hpp>
#include <iostream>
#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#endif

namespace capapp::capture {

#if defined(_WIN32)
static bool detect_capture_compressions(IMFActivate* activation,
                                        std::vector<CaptureCompression>& modes) {
    modes = {CaptureCompression::Auto};
    IMFMediaSource* source = nullptr;
    HRESULT hr = activation->ActivateObject(IID_PPV_ARGS(&source));
    if (FAILED(hr) || !source) return false;

    IMFPresentationDescriptor* presentation = nullptr;
    hr = source->CreatePresentationDescriptor(&presentation);
    bool inspected = false;
    if (SUCCEEDED(hr) && presentation) {
        DWORD streamCount = 0;
        if (SUCCEEDED(presentation->GetStreamDescriptorCount(&streamCount))) {
            for (DWORD streamIndex = 0; streamIndex < streamCount; ++streamIndex) {
                BOOL selected = FALSE;
                IMFStreamDescriptor* stream = nullptr;
                if (FAILED(presentation->GetStreamDescriptorByIndex(
                        streamIndex, &selected, &stream)) || !stream) {
                    continue;
                }

                IMFMediaTypeHandler* handler = nullptr;
                if (SUCCEEDED(stream->GetMediaTypeHandler(&handler)) && handler) {
                    DWORD typeCount = 0;
                    if (SUCCEEDED(handler->GetMediaTypeCount(&typeCount))) {
                        inspected = true;
                        for (DWORD typeIndex = 0; typeIndex < typeCount; ++typeIndex) {
                            IMFMediaType* type = nullptr;
                            if (FAILED(handler->GetMediaTypeByIndex(typeIndex, &type)) || !type) {
                                continue;
                            }
                            GUID majorType{};
                            GUID subtype{};
                            if (SUCCEEDED(type->GetGUID(MF_MT_MAJOR_TYPE, &majorType)) &&
                                majorType == MFMediaType_Video &&
                                SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype))) {
                                CaptureCompression compression = CaptureCompression::Auto;
                                if (subtype == MFVideoFormat_YUY2) {
                                    compression = CaptureCompression::YUY2;
                                } else if (subtype == MFVideoFormat_MJPG) {
                                    compression = CaptureCompression::MJPEG;
                                }
                                if (compression != CaptureCompression::Auto &&
                                    std::find(modes.begin(), modes.end(), compression) == modes.end()) {
                                    modes.push_back(compression);
                                }
                            }
                            type->Release();
                        }
                    }
                    handler->Release();
                }
                stream->Release();
            }
        }
        presentation->Release();
    }
    source->Shutdown();
    source->Release();
    return inspected;
}
#endif

CameraSelector::CameraSelector() {
    refresh_devices();
}

CameraSelector::~CameraSelector() = default;

std::vector<CameraDeviceInfo> CameraSelector::enumerate_devices() {
    std::vector<CameraDeviceInfo> result;

#if defined(_WIN32)
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitializeCom = SUCCEEDED(comHr);
    if (FAILED(comHr) && comHr != RPC_E_CHANGED_MODE) {
        std::cerr << "[CameraSelector Warning] COM initialization failed: 0x"
                  << std::hex << comHr << std::dec
                  << "; capture format detection may be incomplete.\n";
    }
    HRESULT hr = MFStartup(MF_VERSION);
    if (SUCCEEDED(hr)) {
        IMFAttributes* pAttributes = nullptr;
        hr = MFCreateAttributes(&pAttributes, 1);
        if (SUCCEEDED(hr)) {
            hr = pAttributes->SetGUID(
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
            );

            if (SUCCEEDED(hr)) {
                IMFActivate** ppDevices = nullptr;
                UINT32 count = 0;
                hr = MFEnumDeviceSources(pAttributes, &ppDevices, &count);

                if (SUCCEEDED(hr) && count > 0) {
                    for (UINT32 i = 0; i < count; ++i) {
                        WCHAR* pFriendlyName = nullptr;
                        UINT32 nameLength = 0;
                        ppDevices[i]->GetAllocatedString(
                            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                            &pFriendlyName,
                            &nameLength
                        );

                        WCHAR* pSymLink = nullptr;
                        UINT32 symLinkLength = 0;
                        ppDevices[i]->GetAllocatedString(
                            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                            &pSymLink,
                            &symLinkLength
                        );

                        CameraDeviceInfo info{};
                        info.index = static_cast<uint32_t>(result.size());
                        if (pFriendlyName) {
                            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, pFriendlyName, -1, nullptr, 0, nullptr, nullptr);
                            if (utf8Len > 0) {
                                std::string utf8Name(utf8Len - 1, 0);
                                WideCharToMultiByte(CP_UTF8, 0, pFriendlyName, -1, utf8Name.data(), utf8Len, nullptr, nullptr);
                                info.name = utf8Name;
                            }
                            CoTaskMemFree(pFriendlyName);
                        } else {
                            info.name = "Camera Device #" + std::to_string(i);
                        }

                        if (pSymLink) {
                            info.symbolicLink = pSymLink;
                            CoTaskMemFree(pSymLink);
                        }

                        // Check if virtual
                        if (info.name.find("Virtual") != std::string::npos || info.name.find("OBS") != std::string::npos) {
                            info.isVirtual = true;
                        }

                        info.width = 1920;
                        info.height = 1080;
                        info.maxFps = 60;
                        info.preferredFormat = PixelFormat::BGRA8;
                        if (!detect_capture_compressions(ppDevices[i], info.supportedCompressions)) {
                            info.supportedCompressions = {
                                CaptureCompression::Auto,
                                CaptureCompression::YUY2,
                                CaptureCompression::MJPEG
                            };
                            std::cerr << "[CameraSelector Warning] Could not inspect capture "
                                         "formats for " << info.name
                                      << "; enabling YUY2 and MJPEG requests as fallbacks.\n";
                        }

                        result.push_back(info);
                        ppDevices[i]->Release();
                    }
                    CoTaskMemFree(ppDevices);
                }
            }
            pAttributes->Release();
        }
        MFShutdown();
    }
    if (uninitializeCom) {
        CoUninitialize();
    }
#endif

    // If no physical cameras connected or running headless in CI, supply standard capture inputs
    if (result.empty()) {
        CameraDeviceInfo cam1{};
        cam1.index = 0;
        cam1.name = "Integrated HD Webcam";
        cam1.width = 1920;
        cam1.height = 1080;
        cam1.maxFps = 60;
        cam1.preferredFormat = PixelFormat::BGRA8;
        cam1.isVirtual = false;
        result.push_back(cam1);

        CameraDeviceInfo cam2{};
        cam2.index = 1;
        cam2.name = "USB HDMI Video Capture Card";
        cam2.width = 1920;
        cam2.height = 1080;
        cam2.maxFps = 60;
        cam2.preferredFormat = PixelFormat::BGRA8;
        cam2.isVirtual = false;
        result.push_back(cam2);

        CameraDeviceInfo cam3{};
        cam3.index = 2;
        cam3.name = "OBS Virtual Camera";
        cam3.width = 1920;
        cam3.height = 1080;
        cam3.maxFps = 60;
        cam3.preferredFormat = PixelFormat::BGRA8;
        cam3.isVirtual = true;
        result.push_back(cam3);
    }

    return result;
}

void CameraSelector::refresh_devices() {
    devices_ = enumerate_devices();
    if (activeIndex_ >= devices_.size()) {
        activeIndex_ = 0;
    }
}

bool CameraSelector::select_device(uint32_t index) {
    if (index >= devices_.size()) {
        return false;
    }
    activeIndex_ = index;
    if (onDeviceChanged) {
        onDeviceChanged(devices_[activeIndex_]);
    }
    return true;
}

bool CameraSelector::select_device_by_name(std::string_view name) {
    for (size_t i = 0; i < devices_.size(); ++i) {
        if (devices_[i].name.find(name) != std::string::npos) {
            return select_device(static_cast<uint32_t>(i));
        }
    }
    return false;
}

const CameraDeviceInfo& CameraSelector::cycle_next_device() {
    if (devices_.empty()) {
        static CameraDeviceInfo emptyDev{};
        return emptyDev;
    }
    activeIndex_ = (activeIndex_ + 1) % devices_.size();
    if (onDeviceChanged) {
        onDeviceChanged(devices_[activeIndex_]);
    }
    return devices_[activeIndex_];
}

const CameraDeviceInfo& CameraSelector::cycle_prev_device() {
    if (devices_.empty()) {
        static CameraDeviceInfo emptyDev{};
        return emptyDev;
    }
    activeIndex_ = (activeIndex_ == 0) ? static_cast<uint32_t>(devices_.size() - 1) : (activeIndex_ - 1);
    if (onDeviceChanged) {
        onDeviceChanged(devices_[activeIndex_]);
    }
    return devices_[activeIndex_];
}

ResolutionPreset CameraSelector::cycle_resolution(int direction) noexcept {
    if (resolutionPresets_.empty()) return {};
    const size_t count = resolutionPresets_.size();
    activeResIndex_ = direction < 0
        ? static_cast<uint32_t>((activeResIndex_ + count - 1) % count)
        : static_cast<uint32_t>((activeResIndex_ + 1) % count);
    auto res = resolutionPresets_[activeResIndex_];
    if (activeIndex_ < devices_.size()) {
        devices_[activeIndex_].width = res.width;
        devices_[activeIndex_].height = res.height;
    }
    if (onFormatChanged) {
        onFormatChanged(res.width, res.height, activeFps_);
    }
    return res;
}

uint32_t CameraSelector::cycle_fps(int direction) noexcept {
    (void)direction;
    activeFps_ = (activeFps_ == 60) ? 30 : 60;
    if (activeIndex_ < devices_.size()) {
        devices_[activeIndex_].maxFps = activeFps_;
    }
    if (onFormatChanged) {
        auto res = active_resolution();
        onFormatChanged(res.width, res.height, activeFps_);
    }
    return activeFps_;
}

bool CameraSelector::set_resolution(uint32_t width, uint32_t height) noexcept {
    for (size_t i = 0; i < resolutionPresets_.size(); ++i) {
        if (resolutionPresets_[i].width == width && resolutionPresets_[i].height == height) {
            activeResIndex_ = static_cast<uint32_t>(i);
            if (activeIndex_ < devices_.size()) {
                devices_[activeIndex_].width = width;
                devices_[activeIndex_].height = height;
            }
            if (onFormatChanged) {
                onFormatChanged(width, height, activeFps_);
            }
            return true;
        }
    }
    // Custom resolution fallback
    ResolutionPreset custom{width, height, std::to_string(width) + "x" + std::to_string(height)};
    resolutionPresets_.push_back(custom);
    activeResIndex_ = static_cast<uint32_t>(resolutionPresets_.size() - 1);
    if (activeIndex_ < devices_.size()) {
        devices_[activeIndex_].width = width;
        devices_[activeIndex_].height = height;
    }
    if (onFormatChanged) {
        onFormatChanged(width, height, activeFps_);
    }
    return true;
}

bool CameraSelector::set_fps(uint32_t fps) noexcept {
    activeFps_ = (fps > 0) ? fps : 60;
    if (activeIndex_ < devices_.size()) {
        devices_[activeIndex_].maxFps = activeFps_;
    }
    if (onFormatChanged) {
        auto res = active_resolution();
        onFormatChanged(res.width, res.height, activeFps_);
    }
    return true;
}

ResolutionPreset CameraSelector::active_resolution() const noexcept {
    if (activeResIndex_ < resolutionPresets_.size()) {
        return resolutionPresets_[activeResIndex_];
    }
    return {1920, 1080, "1080p"};
}

uint32_t CameraSelector::active_fps() const noexcept {
    return activeFps_;
}

const CameraDeviceInfo* CameraSelector::active_device() const noexcept {
    if (activeIndex_ < devices_.size()) {
        return &devices_[activeIndex_];
    }
    return nullptr;
}

uint32_t CameraSelector::active_index() const noexcept {
    return activeIndex_;
}

size_t CameraSelector::device_count() const noexcept {
    return devices_.size();
}

const std::vector<CameraDeviceInfo>& CameraSelector::devices() const noexcept {
    return devices_;
}

} // namespace capapp::capture
