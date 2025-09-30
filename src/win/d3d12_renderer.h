/**
 * @file d3d12_renderer.h
 * @brief Direct3D 12 GPU-accelerated path tracer for Pong scene
 * 
 * Phase 5: GPU acceleration using D3D12 compute shaders (Windows 10+ built-in API)
 * 
 * Architecture:
 *  - GPU does path tracing (80% of frame time)
 *  - CPU does post-processing: temporal accumulation + tone mapping (20%)
 *  - Zero external runtime dependencies (d3d12.dll, dxgi.dll built into Windows 10+)
 * 
 * Expected Performance: 10-50x over Phase 4 CPU renderer
 */

#pragma once
#ifdef _WIN32

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>
#include "../core/game_core.h"

// Import existing SRConfig and SRStats from soft_renderer
#include "soft_renderer.h"

using Microsoft::WRL::ComPtr;

class D3D12Renderer {
public:
    D3D12Renderer();
    ~D3D12Renderer();

    // Initialize D3D12 device and infrastructure
    // Returns true on success, false if D3D12 unavailable (caller should fallback to CPU)
    bool initialize();

    // Configuration and sizing
    void configure(const SRConfig &cfg);
    void resize(int w, int h);
    void resetHistory();

    // Render using GPU compute shader
    void render(const GameState &gs);

    // Stats and output
    const SRStats &stats() const { return stats_; }
    const BITMAPINFO &getBitmapInfo() const { return bmpInfo_; }
    const uint32_t *pixels() const { return reinterpret_cast<const uint32_t*>(outputPixels_.data()); }

    // Check if D3D12 successfully initialized
    bool isInitialized() const { return initialized_; }

private:
    // Initialization state
    bool initialized_ = false;

    // Output configuration
    int outW_ = 0, outH_ = 0;      // Window size
    int rtW_ = 0, rtH_ = 0;        // Internal render resolution
    SRConfig config_{};
    BITMAPINFO bmpInfo_{};         // Top-down 32bpp DIB header for GDI
    std::vector<uint32_t> outputPixels_; // Packed BGRA for GDI output
    SRStats stats_{};
    unsigned frameCounter_ = 0;
    bool haveHistory_ = false;

    // D3D12 Core Objects
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> commandQueue_;
    ComPtr<ID3D12CommandAllocator> commandAllocator_;
    ComPtr<ID3D12GraphicsCommandList> commandList_;
    ComPtr<ID3D12Fence> fence_;
    UINT64 fenceValue_ = 0;
    UINT64 lastCompletedFenceValue_ = 0;  // Track last signaled fence for synchronization
    HANDLE fenceEvent_ = nullptr;

    // Descriptor Heaps
    ComPtr<ID3D12DescriptorHeap> srvUavHeap_;  // For UAVs and SRVs
    UINT srvUavDescriptorSize_ = 0;

    // Shader Resources
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipelineState_;
    std::vector<UINT8> shaderBytecode_;

    // GPU Buffers
    ComPtr<ID3D12Resource> outputTexture_;      // UAV: GPU writes path tracing output here
    ComPtr<ID3D12Resource> accumTexture_;       // UAV: GPU-side accumulation buffer
    ComPtr<ID3D12Resource> readbackBuffer_;     // Readback heap for CPU access
    ComPtr<ID3D12Resource> sceneDataBuffer_;    // SRV: Sphere/box data
    ComPtr<ID3D12Resource> paramsBuffer_;       // CBV: Render parameters
    ComPtr<ID3D12Resource> uploadBuffer_;       // Upload heap for CPU->GPU transfers

    // Helper Methods
    bool createDevice();
    bool createCommandObjects();
    bool createDescriptorHeaps();
    bool loadAndCompileShader();
    bool createRootSignature();
    bool createPipelineState();
    bool createBuffers();
    void updateInternalResolution();
    void waitForGPU();
    void executeCommandList();
    void updateSceneData(const GameState &gs);
    void dispatchCompute();
    void readbackResults();
    void postProcess(); // Temporal accumulation + tone mapping on CPU
};

#endif // _WIN32
