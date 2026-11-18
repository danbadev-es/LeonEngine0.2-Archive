#include "RHI/FPlatformMemory.hpp"
#include "RHI/IRHIDevice.hpp"

#ifdef _WIN32
#include <windows.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <psapi.h>
#endif

namespace Leon {

    FMemoryStats FPlatformMemory::GetMemoryStats() {
        FMemoryStats stats;

#ifdef _WIN32
        // 1. Process Physical RAM (Working Set)
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
            stats.WorkingSetBytes = pmc.WorkingSetSize;
            stats.PeakWorkingSetBytes = pmc.PeakWorkingSetSize;
        }

        // 2. Total System Installed Physical RAM
        MEMORYSTATUSEX memInfo{};
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            stats.TotalPhysicalBytes = memInfo.ullTotalPhys;
            stats.AvailablePhysicalBytes = memInfo.ullAvailPhys;
        }
#endif

        // 3. Dedicated memory of the adapter the device runs on. Usage comes from DXGI below.
        if (IRHIDevice* device = FRHI::GetDevice())
            stats.DedicatedVideoMemoryBytes = static_cast<size_t>(device->GetInfo().DedicatedVideoMemoryBytes);

#ifdef _WIN32
        // 4. DXGI Fallback / Supplement for AMD / Intel / Windows Universal GPUs
        if (stats.DedicatedVideoMemoryBytes == 0 || stats.UsedVideoMemoryBytes == 0) {
            IDXGIFactory* pFactory = nullptr;
            if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory))) {
                IDXGIAdapter* pAdapter = nullptr;
                if (SUCCEEDED(pFactory->EnumAdapters(0, &pAdapter))) {
                    DXGI_ADAPTER_DESC desc;
                    if (SUCCEEDED(pAdapter->GetDesc(&desc))) {
                        if (stats.DedicatedVideoMemoryBytes == 0) {
                            stats.DedicatedVideoMemoryBytes = desc.DedicatedVideoMemory;
                        }
                    }

                    // Query DXGI 1.4 for real-time video memory usage
                    IDXGIAdapter3* pAdapter3 = nullptr;
                    if (SUCCEEDED(pAdapter->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&pAdapter3))) {
                        DXGI_QUERY_VIDEO_MEMORY_INFO vMemInfo;
                        if (SUCCEEDED(pAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vMemInfo))) {
                            stats.UsedVideoMemoryBytes = vMemInfo.CurrentUsage;
                            if (stats.DedicatedVideoMemoryBytes == 0 && vMemInfo.Budget > 0) {
                                stats.DedicatedVideoMemoryBytes = vMemInfo.Budget;
                            }
                        }
                        pAdapter3->Release();
                    }
                    pAdapter->Release();
                }
                pFactory->Release();
            }
        }
#endif

        return stats;
    }

    FGPUInfo FPlatformMemory::GetGPUInfo() {
        FGPUInfo info;
        if (IRHIDevice* device = FRHI::GetDevice()) {
            const FRHIDeviceInfo& deviceInfo = device->GetInfo();
            info.Renderer = deviceInfo.AdapterName;
            info.Version = deviceInfo.Backend == ERHIBackend::D3D12 ? "Direct3D 12" : "Null device";
            info.ShadingLanguageVersion = "HLSL 2021";
        }
        return info;
    }

} // namespace Leon
