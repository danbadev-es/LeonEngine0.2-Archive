#include "RHI/IRHIDevice.hpp"
#include "RHI/FRHIFrame.hpp"

#include "Core/FLog.hpp"

#include <map>

namespace Leon {

    uint32_t RHIFormatBytesPerPixel(ERHIFormat InFormat) {
        switch (InFormat) {
        case ERHIFormat::R8_UNorm:
            return 1;
        case ERHIFormat::RGBA8_UNorm:
        case ERHIFormat::RGBA8_UNorm_sRGB:
        case ERHIFormat::BGRA8_UNorm:
        case ERHIFormat::RG16_Float:
        case ERHIFormat::R32_Float:
        case ERHIFormat::R32_SInt:
        case ERHIFormat::D32_Float:
        case ERHIFormat::D24_UNorm_S8_UInt:
            return 4;
        case ERHIFormat::RGBA16_Float:
            return 8;
        case ERHIFormat::RGBA32_Float:
            return 16;
        case ERHIFormat::BC1_UNorm:
        case ERHIFormat::BC1_UNorm_sRGB:
        case ERHIFormat::BC3_UNorm:
        case ERHIFormat::BC3_UNorm_sRGB:
        case ERHIFormat::BC4_UNorm:
        case ERHIFormat::BC5_UNorm:
        case ERHIFormat::Unknown:
            return 0;
        }
        return 0;
    }

    uint32_t RHIFormatBytesPerBlock(ERHIFormat InFormat) {
        switch (InFormat) {
        case ERHIFormat::BC1_UNorm:
        case ERHIFormat::BC1_UNorm_sRGB:
        case ERHIFormat::BC4_UNorm:
            return 8;
        case ERHIFormat::BC3_UNorm:
        case ERHIFormat::BC3_UNorm_sRGB:
        case ERHIFormat::BC5_UNorm:
            return 16;
        default:
            return 0;
        }
    }

    uint32_t RHIFormatRowBytes(ERHIFormat InFormat, uint32_t InWidth) {
        if (const uint32_t block = RHIFormatBytesPerBlock(InFormat))
            return ((InWidth + 3) / 4 > 0 ? (InWidth + 3) / 4 : 1) * block;
        return InWidth * RHIFormatBytesPerPixel(InFormat);
    }

    uint32_t RHIFormatRowCount(ERHIFormat InFormat, uint32_t InHeight) {
        if (RHIFormatBytesPerBlock(InFormat))
            return (InHeight + 3) / 4 > 0 ? (InHeight + 3) / 4 : 1;
        return InHeight;
    }

    bool RHIFormatIsDepth(ERHIFormat InFormat) {
        return InFormat == ERHIFormat::D32_Float || InFormat == ERHIFormat::D24_UNorm_S8_UInt;
    }

    namespace {

        std::map<ERHIBackend, FRHI::FDeviceFactory>& Factories() {
            static std::map<ERHIBackend, FRHI::FDeviceFactory> factories;
            return factories;
        }

        TScope<IRHIDevice>& DeviceSlot() {
            static TScope<IRHIDevice> device;
            return device;
        }

        const char* BackendName(ERHIBackend InBackend) {
            switch (InBackend) {
            case ERHIBackend::Null:
                return "Null";
            case ERHIBackend::D3D12:
                return "Direct3D 12";
            }
            return "?";
        }

    } // namespace

    void FRHI::RegisterBackend(ERHIBackend InBackend, FDeviceFactory InFactory) {
        Factories()[InBackend] = std::move(InFactory);
    }

    bool FRHI::IsBackendRegistered(ERHIBackend InBackend) {
        return Factories().count(InBackend) > 0;
    }

    bool FRHI::Init(ERHIBackend InBackend, const FRHIInitDesc& InDesc) {
        if (DeviceSlot()) {
            LE_CORE_WARN("FRHI::Init: a device already exists; call Shutdown first");
            return false;
        }
        const auto found = Factories().find(InBackend);
        if (found == Factories().end()) {
            LE_CORE_ERROR("FRHI::Init: the {0} backend is not registered", BackendName(InBackend));
            return false;
        }
        DeviceSlot() = found->second(InDesc);
        if (!DeviceSlot()) {
            LE_CORE_ERROR("FRHI::Init: the {0} backend could not create a device", BackendName(InBackend));
            return false;
        }
        const FRHIDeviceInfo& info = DeviceSlot()->GetInfo();
        LE_CORE_INFO("RHI: {0} on '{1}'{2}", BackendName(info.Backend), info.AdapterName,
                     info.bSoftwareAdapter ? " (software)" : "");
        return true;
    }

    void FRHI::Shutdown() {
        if (DeviceSlot()) {
            DeviceSlot()->WaitIdle();
            DeviceSlot().reset();
        }
    }

    bool FRHI::IsInitialized() {
        return DeviceSlot() != nullptr;
    }

    IRHIDevice* FRHI::GetDevice() {
        return DeviceSlot().get();
    }

    namespace {
        IRHICommandList* GFrameCommands = nullptr;
        IRHITexture* GFrameOutput = nullptr;
    } // namespace

    void FRHIFrame::Begin(IRHICommandList& InCommands, IRHITexture& InOutput) {
        GFrameCommands = &InCommands;
        GFrameOutput = &InOutput;
    }

    void FRHIFrame::End() {
        GFrameCommands = nullptr;
        GFrameOutput = nullptr;
    }

    IRHICommandList* FRHIFrame::GetCommands() {
        return GFrameCommands;
    }

    IRHITexture* FRHIFrame::GetOutput() {
        return GFrameOutput;
    }

} // namespace Leon
