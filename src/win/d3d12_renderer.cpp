/**
 * @file d3d12_renderer.cpp
 * @brief Direct3D 12 GPU-accelerated path tracer implementation
 * 
 * Phase 5: GPU acceleration using D3D12 compute shaders
 */

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "d3d12_renderer.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <array>
#include <fstream>

// File logging helper
static void LogToFile(const char* message) {
    std::ofstream logFile("pong_renderer.log", std::ios::app);
    if (logFile.is_open()) {
        logFile << "[D3D12] " << message << std::endl;
        logFile.close();
    }
}

// Helper macro for D3D12 error checking
#define D3D_CHECK(hr, msg) \
    if (FAILED(hr)) { \
        OutputDebugStringA("[D3D12Renderer] ERROR: "); \
        OutputDebugStringA(msg); \
        OutputDebugStringA("\n"); \
        LogToFile("ERROR: "); \
        LogToFile(msg); \
        return false; \
    }

// Helper macro for logging
#define D3D_LOG(msg) \
    OutputDebugStringA("[D3D12Renderer] "); \
    OutputDebugStringA(msg); \
    OutputDebugStringA("\n"); \
    LogToFile(msg);

struct CD3DX12_DESCRIPTOR_RANGE1 : public D3D12_DESCRIPTOR_RANGE1 {
    CD3DX12_DESCRIPTOR_RANGE1() = default;
    explicit CD3DX12_DESCRIPTOR_RANGE1(const D3D12_DESCRIPTOR_RANGE1 &o) : D3D12_DESCRIPTOR_RANGE1(o) {}
    CD3DX12_DESCRIPTOR_RANGE1(
        D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
        UINT numDescriptors,
        UINT baseShaderRegister,
        UINT registerSpace = 0,
        D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
        UINT offsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
    {
        Init(rangeType, numDescriptors, baseShaderRegister, registerSpace, flags, offsetInDescriptorsFromTableStart);
    }
    
    inline void Init(
        D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
        UINT numDescriptors,
        UINT baseShaderRegister,
        UINT registerSpace = 0,
        D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
        UINT offsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
    {
        RangeType = rangeType;
        NumDescriptors = numDescriptors;
        BaseShaderRegister = baseShaderRegister;
        RegisterSpace = registerSpace;
        Flags = flags;
        OffsetInDescriptorsFromTableStart = offsetInDescriptorsFromTableStart;
    }
};

struct CD3DX12_ROOT_PARAMETER1 : public D3D12_ROOT_PARAMETER1 {
    CD3DX12_ROOT_PARAMETER1() = default;
    explicit CD3DX12_ROOT_PARAMETER1(const D3D12_ROOT_PARAMETER1 &o) : D3D12_ROOT_PARAMETER1(o) {}
    
    inline void InitAsDescriptorTable(
        UINT numDescriptorRanges,
        const D3D12_DESCRIPTOR_RANGE1* pDescriptorRanges,
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
    {
        ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ShaderVisibility = visibility;
        DescriptorTable.NumDescriptorRanges = numDescriptorRanges;
        DescriptorTable.pDescriptorRanges = pDescriptorRanges;
    }
};

struct CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC : public D3D12_VERSIONED_ROOT_SIGNATURE_DESC {
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC() = default;
    explicit CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC &o) : D3D12_VERSIONED_ROOT_SIGNATURE_DESC(o) {}
    
    inline void Init_1_1(
        UINT numParameters,
        const D3D12_ROOT_PARAMETER1* pParameters,
        UINT numStaticSamplers,
        const D3D12_STATIC_SAMPLER_DESC* pStaticSamplers,
        D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE)
    {
        Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        Desc_1_1.NumParameters = numParameters;
        Desc_1_1.pParameters = pParameters;
        Desc_1_1.NumStaticSamplers = numStaticSamplers;
        Desc_1_1.pStaticSamplers = pStaticSamplers;
        Desc_1_1.Flags = flags;
    }
};

inline HRESULT D3DX12SerializeVersionedRootSignature(
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pRootSignature,
    D3D_ROOT_SIGNATURE_VERSION MaxVersion,
    ID3DBlob** ppBlob,
    ID3DBlob** ppErrorBlob)
{
    return D3D12SerializeVersionedRootSignature(pRootSignature, ppBlob, ppErrorBlob);
}

// Link against D3D12 and DXGI libraries
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Helper macro for D3D12 error checking with HRESULT logging
#define D3D_CHECK(hr, msg) \
    if (FAILED(hr)) { \
        char errBuf[512]; \
        sprintf_s(errBuf, "ERROR: %s (HRESULT: 0x%08X)", msg, (unsigned int)hr); \
        OutputDebugStringA("[D3D12Renderer] "); \
        OutputDebugStringA(errBuf); \
        OutputDebugStringA("\n"); \
        LogToFile(errBuf); \
        return false; \
    }

// Helper macro for logging
#define D3D_LOG(msg) \
    OutputDebugStringA("[D3D12Renderer] "); \
    OutputDebugStringA(msg); \
    OutputDebugStringA("\n"); \
    LogToFile(msg);

D3D12Renderer::D3D12Renderer() {
    D3D_LOG("Constructor called");
}

D3D12Renderer::~D3D12Renderer() {
    if (initialized_) {
        // Wait for GPU to finish all work before destroying resources
        waitForGPU();
        
        if (fenceEvent_) {
            CloseHandle(fenceEvent_);
            fenceEvent_ = nullptr;
        }
    }
    D3D_LOG("Destructor called");
}

bool D3D12Renderer::initialize() {
    D3D_LOG("=== Starting D3D12 Initialization ===");
    LogToFile("=== D3D12Renderer::initialize() called ===");

    if (initialized_) {
        D3D_LOG("Already initialized");
        return true;
    }

    // Create device
    LogToFile("Step 1/8: Creating D3D12 device...");
    if (!createDevice()) {
        D3D_LOG("FAILED: Device creation failed");
        LogToFile(">>> FAILED at Step 1: createDevice()");
        return false;
    }
    LogToFile(">>> SUCCESS: Device created");

    // Create command objects (queue, allocator, list)
    LogToFile("Step 2/8: Creating command objects...");
    if (!createCommandObjects()) {
        D3D_LOG("FAILED: Command objects creation failed");
        LogToFile(">>> FAILED at Step 2: createCommandObjects()");
        return false;
    }
    LogToFile(">>> SUCCESS: Command objects created");

    // Create descriptor heaps
    LogToFile("Step 3/8: Creating descriptor heaps...");
    if (!createDescriptorHeaps()) {
        D3D_LOG("FAILED: Descriptor heaps creation failed");
        LogToFile(">>> FAILED at Step 3: createDescriptorHeaps()");
        return false;
    }
    LogToFile(">>> SUCCESS: Descriptor heaps created");

    // Load and compile shader
    LogToFile("Step 4/8: Loading and compiling shader...");
    if (!loadAndCompileShader()) {
        D3D_LOG("FAILED: Shader load/compile failed");
        LogToFile(">>> FAILED at Step 4: loadAndCompileShader()");
        return false;
    }
    LogToFile(">>> SUCCESS: Shader compiled");

    // Create root signature
    LogToFile("Step 5/8: Creating root signature...");
    if (!createRootSignature()) {
        D3D_LOG("FAILED: Root signature creation failed");
        LogToFile(">>> FAILED at Step 5: createRootSignature()");
        return false;
    }
    LogToFile(">>> SUCCESS: Root signature created");

    // Create pipeline state
    LogToFile("Step 6/8: Creating pipeline state...");
    if (!createPipelineState()) {
        D3D_LOG("FAILED: Pipeline state creation failed");
        LogToFile(">>> FAILED at Step 6: createPipelineState()");
        return false;
    }
    LogToFile(">>> SUCCESS: Pipeline state created");

    // Create fence for synchronization
    LogToFile("Step 7/8: Creating synchronization fence...");
    HRESULT hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    D3D_CHECK(hr, "Failed to create fence");
    fenceValue_ = 1;
    LogToFile(">>> SUCCESS: Fence created");

    // Create fence event
    LogToFile("Step 8/8: Creating fence event...");
    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (fenceEvent_ == nullptr) {
        D3D_LOG("FAILED: Fence event creation failed");
        LogToFile(">>> FAILED at Step 8: CreateEvent()");
        return false;
    }
    LogToFile(">>> SUCCESS: Fence event created");

    // Note: Buffers will be created when resize() is called
    // (need to know output dimensions first)

    initialized_ = true;
    D3D_LOG("=== D3D12 initialization COMPLETE AND SUCCESSFUL! ===");
    LogToFile("===========================================");
    LogToFile("D3D12 INITIALIZATION SUCCESSFUL!");
    LogToFile("===========================================");
    return true;
}

bool D3D12Renderer::createDevice() {
    LogToFile("  createDevice: Starting...");

    // Enable debug layer in debug builds
#ifdef _DEBUG
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
            LogToFile("  Debug layer enabled");
        }
    }
#endif

    // Create DXGI factory
    LogToFile("  Creating DXGI factory...");
    ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    D3D_CHECK(hr, "Failed to create DXGI factory");
    LogToFile("  DXGI factory created successfully");

    // Find hardware adapter
    LogToFile("  Enumerating adapters...");
    ComPtr<IDXGIAdapter1> adapter;
    int adapterCount = 0;
    for (UINT adapterIndex = 0; 
         SUCCEEDED(factory->EnumAdapters1(adapterIndex, &adapter)); 
         ++adapterIndex) {
        
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        // Log adapter info
        char adapterInfo[512];
        char adapterName[256];
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, adapterName, sizeof(adapterName), nullptr, nullptr);
        sprintf_s(adapterInfo, "  Adapter %d: %s (Vendor:0x%04X, Device:0x%04X)", 
                  adapterIndex, adapterName, desc.VendorId, desc.DeviceId);
        LogToFile(adapterInfo);

        // Skip software adapter
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            LogToFile("    Skipping (software adapter)");
            continue;
        }

        // Try to create device with this adapter
        sprintf_s(adapterInfo, "    Attempting D3D12CreateDevice (Feature Level 11.0)...");
        LogToFile(adapterInfo);
        
        hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
        if (SUCCEEDED(hr)) {
            sprintf_s(adapterInfo, "    SUCCESS! Device created with adapter %d: %s", adapterIndex, adapterName);
            LogToFile(adapterInfo);
            adapterCount++;
            break;
        } else {
            sprintf_s(adapterInfo, "    D3D12CreateDevice FAILED (HRESULT: 0x%08X)", (unsigned int)hr);
            LogToFile(adapterInfo);
        }
    }

    if (!device_) {
        LogToFile("  ERROR: No suitable D3D12 adapter found");
        LogToFile("  Possible causes:");
        LogToFile("    - Graphics drivers are too old (need Windows 10+ with D3D12 support)");
        LogToFile("    - GPU doesn't support Feature Level 11.0");
        LogToFile("    - D3D12 runtime not installed");
        return false;
    }

    char successMsg[128];
    sprintf_s(successMsg, "  createDevice: SUCCESS (tested %d adapters)", adapterCount);
    LogToFile(successMsg);
    return true;
}

bool D3D12Renderer::createCommandObjects() {
    LogToFile("  createCommandObjects: Starting...");

    // Create command queue
    LogToFile("  Creating command queue...");
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    
    HRESULT hr = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue_));
    D3D_CHECK(hr, "Failed to create command queue");
    LogToFile("  Command queue created");

    // Create command allocator
    LogToFile("  Creating command allocator...");
    hr = device_->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&commandAllocator_)
    );
    D3D_CHECK(hr, "Failed to create command allocator");
    LogToFile("  Command allocator created");

    // Create command list
    LogToFile("  Creating command list...");
    hr = device_->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        commandAllocator_.Get(),
        nullptr,
        IID_PPV_ARGS(&commandList_)
    );
    D3D_CHECK(hr, "Failed to create command list");
    LogToFile("  Command list created");

    // Close command list (it starts in recording state)
    commandList_->Close();
    LogToFile("  Command list closed");

    LogToFile("  createCommandObjects: SUCCESS");
    return true;
}

bool D3D12Renderer::createDescriptorHeaps() {
    LogToFile("  createDescriptorHeaps: Starting...");

    // Create SRV/UAV descriptor heap
    // We need:
    //  - 1 UAV for output texture
    //  - 1 UAV for accumulation texture
    //  - 1 SRV for scene data
    //  - 1 CBV for parameters
    LogToFile("  Creating CBV_SRV_UAV descriptor heap (4 descriptors)...");
    D3D12_DESCRIPTOR_HEAP_DESC srvUavHeapDesc = {};
    srvUavHeapDesc.NumDescriptors = 4;
    srvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    HRESULT hr = device_->CreateDescriptorHeap(&srvUavHeapDesc, IID_PPV_ARGS(&srvUavHeap_));
    D3D_CHECK(hr, "Failed to create SRV/UAV descriptor heap");
    LogToFile("  Descriptor heap created");

    srvUavDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    
    char sizeMsg[128];
    sprintf_s(sizeMsg, "  Descriptor size: %u bytes", srvUavDescriptorSize_);
    LogToFile(sizeMsg);

    LogToFile("  createDescriptorHeaps: SUCCESS");
    return true;
}

bool D3D12Renderer::loadAndCompileShader() {
    LogToFile("  loadAndCompileShader: Starting...");

    // Try multiple paths to find the shader file
    const wchar_t* shaderPaths[] = {
        L"src/win/shaders/PathTrace.hlsl",           // When running from repo root
        L"../../src/win/shaders/PathTrace.hlsl",     // When running from dist/release/
        L"../../../src/win/shaders/PathTrace.hlsl",  // Just in case
    };
    
    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3DBlob> errorBlob;
    
    LogToFile("  Attempting to compile PathTrace.hlsl from multiple paths...");
    bool shaderLoaded = false;
    for (size_t i = 0; i < ARRAYSIZE(shaderPaths); i++) {
        const wchar_t* shaderPath = shaderPaths[i];
        
        char pathMsg[512];
        char pathBuf[256];
        WideCharToMultiByte(CP_UTF8, 0, shaderPath, -1, pathBuf, sizeof(pathBuf), nullptr, nullptr);
        sprintf_s(pathMsg, "  Attempt %zu: %s", i + 1, pathBuf);
        LogToFile(pathMsg);
        
        // Try to compile from file
        HRESULT hr = D3DCompileFromFile(
            shaderPath,
            nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            "CSMain",
            "cs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,
            &shaderBlob,
            &errorBlob
        );

        if (SUCCEEDED(hr)) {
            sprintf_s(pathMsg, "  SUCCESS! Shader compiled from: %s", pathBuf);
            LogToFile(pathMsg);
            sprintf_s(pathMsg, "  Shader bytecode size: %zu bytes", shaderBlob->GetBufferSize());
            LogToFile(pathMsg);
            shaderLoaded = true;
            break;
        } else {
            sprintf_s(pathMsg, "  FAILED: HRESULT 0x%08X", (unsigned int)hr);
            LogToFile(pathMsg);
            
            // Check if it's a "file not found" type error (allow trying next path)
            // ERROR_FILE_NOT_FOUND = 0x80070002
            // ERROR_PATH_NOT_FOUND = 0x80070003
            bool isFileNotFoundError = (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || 
                                       hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND));
            
            if (!isFileNotFoundError) {
                // Real compilation error (not just missing file)
                LogToFile("  ERROR: Real shader compilation error (not missing file)");
                if (errorBlob) {
                    const char* errorMsg = static_cast<const char*>(errorBlob->GetBufferPointer());
                    LogToFile("  Compilation error details:");
                    LogToFile(errorMsg);
                }
                return false;
            }
            // Otherwise, it's just file/path not found - try next path
        }
    }

    if (!shaderLoaded) {
        // If file not found in any path, use embedded shader source
        LogToFile("  WARNING: Shader file not found in any path");
        LogToFile("  Using embedded fallback shader (gradient test pattern)");
        
        // Minimal embedded shader for fallback
        const char* embeddedShader = R"(
            RWTexture2D<float4> OutputTexture : register(u0);
            RWTexture2D<float4> AccumTexture : register(u1);
            
            cbuffer RenderParams : register(b0) {
                uint g_width;
                uint g_height;
                uint g_resetHistory;
                uint g_frameIndex;
            };
            
            [numthreads(8, 8, 1)]
            void CSMain(uint3 DTid : SV_DispatchThreadID)
            {
                uint x = DTid.x;
                uint y = DTid.y;
                if (x >= g_width || y >= g_height) return;
                
                // Simple test pattern: gradient
                float u = float(x) / float(g_width);
                float v = float(y) / float(g_height);
                float3 color = float3(u, v, 0.5);
                
                OutputTexture[uint2(x, y)] = float4(color, 1.0);
                AccumTexture[uint2(x, y)] = float4(color, 1.0);
            }
        )";
        
        LogToFile("  Compiling embedded fallback shader...");
        HRESULT hr = D3DCompile(
            embeddedShader,
            strlen(embeddedShader),
            nullptr,
            nullptr,
            nullptr,
            "CSMain",
            "cs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,
            &shaderBlob,
            &errorBlob
        );

        if (FAILED(hr)) {
            LogToFile("  ERROR: Embedded shader compilation failed!");
            if (errorBlob) {
                const char* errorMsg = static_cast<const char*>(errorBlob->GetBufferPointer());
                LogToFile("  Embedded shader error:");
                LogToFile(errorMsg);
            }
            return false;
        }
        LogToFile("  Embedded shader compiled successfully");
    }

    // Store shader bytecode
    shaderBytecode_.resize(shaderBlob->GetBufferSize());
    memcpy(shaderBytecode_.data(), shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

    char sizeMsg[128];
    sprintf_s(sizeMsg, "  Stored bytecode: %zu bytes", shaderBytecode_.size());
    LogToFile(sizeMsg);
    LogToFile("  loadAndCompileShader: SUCCESS");
    return true;
}

bool D3D12Renderer::createRootSignature() {
    LogToFile("  createRootSignature: Starting...");

    // Define root parameters
    // 0: UAV for output texture
    // 1: UAV for accumulation texture
    // 2: SRV for scene data
    // 3: CBV for parameters
    LogToFile("  Defining descriptor ranges (u0, u1, t0, b0)...");
    CD3DX12_DESCRIPTOR_RANGE1 ranges[4];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // u0
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1); // u1
    ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0
    ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0); // b0

    LogToFile("  Initializing root parameters (4 descriptor tables)...");
    CD3DX12_ROOT_PARAMETER1 rootParams[4];
    rootParams[0].InitAsDescriptorTable(1, &ranges[0]);
    rootParams[1].InitAsDescriptorTable(1, &ranges[1]);
    rootParams[2].InitAsDescriptorTable(1, &ranges[2]);
    rootParams[3].InitAsDescriptorTable(1, &ranges[3]);

    LogToFile("  Creating root signature descriptor...");
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(
        _countof(rootParams),
        rootParams,
        0,
        nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_NONE
    );

    // Serialize root signature
    LogToFile("  Serializing root signature...");
    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    
    HRESULT hr = D3DX12SerializeVersionedRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1_1,
        &signatureBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        char errMsg[256];
        sprintf_s(errMsg, "  ERROR: Root signature serialization failed (HRESULT: 0x%08X)", (unsigned int)hr);
        LogToFile(errMsg);
        if (errorBlob) {
            const char* errorText = static_cast<const char*>(errorBlob->GetBufferPointer());
            LogToFile("  Serialization error details:");
            LogToFile(errorText);
        }
        return false;
    }
    LogToFile("  Root signature serialized");

    // Create root signature
    LogToFile("  Creating root signature object...");
    hr = device_->CreateRootSignature(
        0,
        signatureBlob->GetBufferPointer(),
        signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_)
    );
    D3D_CHECK(hr, "Failed to create root signature");
    LogToFile("  Root signature object created");

    LogToFile("  createRootSignature: SUCCESS");
    return true;
}

bool D3D12Renderer::createPipelineState() {
    LogToFile("  createPipelineState: Starting...");

    if (!rootSignature_) {
        LogToFile("  ERROR: Root signature is null!");
        return false;
    }
    if (shaderBytecode_.empty()) {
        LogToFile("  ERROR: Shader bytecode is empty!");
        return false;
    }

    char sizeMsg[128];
    sprintf_s(sizeMsg, "  Using shader bytecode: %zu bytes", shaderBytecode_.size());
    LogToFile(sizeMsg);

    LogToFile("  Creating compute pipeline state descriptor...");
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.CS = { shaderBytecode_.data(), shaderBytecode_.size() };

    LogToFile("  Calling CreateComputePipelineState...");
    HRESULT hr = device_->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState_));
    D3D_CHECK(hr, "Failed to create pipeline state");
    LogToFile("  Compute pipeline state created");

    LogToFile("  createPipelineState: SUCCESS");
    return true;
}

bool D3D12Renderer::createBuffers() {
    D3D_LOG("Creating GPU buffers...");

    if (rtW_ <= 0 || rtH_ <= 0) {
        D3D_LOG("Invalid resolution");
        return false;
    }

    HRESULT hr;

    // Create output texture (UAV) - GPU writes path tracing results here
    {
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = rtW_;
        texDesc.Height = rtH_;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; // HDR float4
        texDesc.SampleDesc.Count = 1;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        hr = device_->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&outputTexture_)
        );
        D3D_CHECK(hr, "Failed to create output texture");

        // Create UAV for output texture
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        
        D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = srvUavHeap_->GetCPUDescriptorHandleForHeapStart();
        device_->CreateUnorderedAccessView(outputTexture_.Get(), nullptr, &uavDesc, uavHandle);
    }

    // Create accumulation texture (UAV) - GPU-side temporal accumulation
    {
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = rtW_;
        texDesc.Height = rtH_;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        texDesc.SampleDesc.Count = 1;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        hr = device_->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&accumTexture_)
        );
        D3D_CHECK(hr, "Failed to create accumulation texture");

        // Create UAV for accum texture
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        
        D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = srvUavHeap_->GetCPUDescriptorHandleForHeapStart();
        uavHandle.ptr += srvUavDescriptorSize_; // Offset to descriptor 1
        device_->CreateUnorderedAccessView(accumTexture_.Get(), nullptr, &uavDesc, uavHandle);
    }

    // Create readback buffer for CPU access
    {
        UINT64 readbackSize = rtW_ * rtH_ * 4 * sizeof(float); // RGBA float
        
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = readbackSize;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        hr = device_->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&readbackBuffer_)
        );
        D3D_CHECK(hr, "Failed to create readback buffer");
    }

    // Create scene data buffer (structured buffer for spheres/boxes)
    {
        // Max 64 objects (balls + paddles + walls)
        UINT64 sceneDataSize = 64 * sizeof(float) * 16; // 16 floats per object (pos, radius, material, etc.)
        
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = sceneDataSize;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        hr = device_->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&sceneDataBuffer_)
        );
        D3D_CHECK(hr, "Failed to create scene data buffer");

        // Create SRV for scene data
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.NumElements = 64;
        srvDesc.Buffer.StructureByteStride = sizeof(float) * 16;
        
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = srvUavHeap_->GetCPUDescriptorHandleForHeapStart();
        srvHandle.ptr += srvUavDescriptorSize_ * 2; // Offset to descriptor 2
        device_->CreateShaderResourceView(sceneDataBuffer_.Get(), &srvDesc, srvHandle);
    }

    // Create parameters buffer (constant buffer)
    {
        // Align to 256 bytes (D3D12 requirement)
        UINT64 paramsSize = (sizeof(float) * 64 + 255) & ~255;
        
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = paramsSize;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        hr = device_->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&paramsBuffer_)
        );
        D3D_CHECK(hr, "Failed to create parameters buffer");

        // Create CBV for parameters
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = paramsBuffer_->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = static_cast<UINT>(paramsSize);
        
        D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle = srvUavHeap_->GetCPUDescriptorHandleForHeapStart();
        cbvHandle.ptr += srvUavDescriptorSize_ * 3; // Offset to descriptor 3
        device_->CreateConstantBufferView(&cbvDesc, cbvHandle);
    }

    D3D_LOG("GPU buffers created successfully");
    return true;
}

void D3D12Renderer::waitForGPU() {
    // Signal and wait for fence
    const UINT64 fenceValueForSignal = fenceValue_;
    HRESULT hr = commandQueue_->Signal(fence_.Get(), fenceValueForSignal);
    if (FAILED(hr)) {
        D3D_LOG("Failed to signal fence");
        return;
    }
    fenceValue_++;

    // Wait for fence completion
    if (fence_->GetCompletedValue() < fenceValueForSignal) {
        hr = fence_->SetEventOnCompletion(fenceValueForSignal, fenceEvent_);
        if (FAILED(hr)) {
            D3D_LOG("Failed to set event on completion");
            return;
        }
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void D3D12Renderer::executeCommandList() {
    // Close command list
    HRESULT hr = commandList_->Close();
    if (FAILED(hr)) {
        char buf[256];
        sprintf_s(buf, "Failed to close command list: HRESULT 0x%08X", hr);
        D3D_LOG(buf);
        return;
    }

    // Execute command list
    ID3D12CommandList* commandLists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(_countof(commandLists), commandLists);

    // Signal fence after submitting work
    lastCompletedFenceValue_ = fenceValue_;
    hr = commandQueue_->Signal(fence_.Get(), fenceValue_);
    if (FAILED(hr)) {
        D3D_LOG("Failed to signal fence");
        return;
    }
    fenceValue_++;
    
    // Note: Command allocator will be reset at the START of next frame's dispatchCompute()
    // after waiting for this fence value
}

void D3D12Renderer::configure(const SRConfig &cfg) {
    bool resolutionChanged = (config_.internalScalePct != cfg.internalScalePct);
    
    config_ = cfg;
    
    int oldW = rtW_;
    int oldH = rtH_;
    updateInternalResolution();
    
    // Recreate buffers if internal resolution changed
    if (initialized_ && (oldW != rtW_ || oldH != rtH_)) {
        D3D_LOG("Internal resolution changed, recreating buffers...");
        waitForGPU(); // Wait for GPU to finish with old buffers
        
        // Destroy old buffers - command list will be fine
        outputTexture_.Reset();
        accumTexture_.Reset();
        readbackBuffer_.Reset();
        sceneDataBuffer_.Reset();  // CRITICAL: Must reset ALL buffers!
        paramsBuffer_.Reset();      // Otherwise descriptors point to freed memory
        
        if (!createBuffers()) {
            D3D_LOG("Failed to recreate buffers on configure");
            initialized_ = false;
        }
        
        haveHistory_ = false;
    }
}

void D3D12Renderer::resize(int w, int h) {
    outW_ = w;
    outH_ = h;
    updateInternalResolution();

    // Update BITMAPINFO for GDI
    memset(&bmpInfo_, 0, sizeof(bmpInfo_));
    bmpInfo_.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmpInfo_.bmiHeader.biWidth = outW_;
    bmpInfo_.bmiHeader.biHeight = -outH_; // Top-down
    bmpInfo_.bmiHeader.biPlanes = 1;
    bmpInfo_.bmiHeader.biBitCount = 32;
    bmpInfo_.bmiHeader.biCompression = BI_RGB;

    outputPixels_.resize(static_cast<size_t>(outW_) * outH_);

    // Recreate GPU buffers if initialized
    if (initialized_) {
        if (outputTexture_) {
            // CRITICAL: Must wait for GPU before destroying resources
            waitForGPU();
            
            // Destroy old buffers - command list will be fine, it's in closed state
            outputTexture_.Reset();
            accumTexture_.Reset();
            readbackBuffer_.Reset();
            sceneDataBuffer_.Reset();
            paramsBuffer_.Reset();
        }
        
        // Create or recreate with new size
        if (!createBuffers()) {
            D3D_LOG("Failed to create/recreate buffers on resize");
            initialized_ = false; // Mark as failed so we don't try to render
        }
        
        // Reset history on resize
        haveHistory_ = false;
    }
}

void D3D12Renderer::resetHistory() {
    haveHistory_ = false;
    frameCounter_ = 0;
}

void D3D12Renderer::updateInternalResolution() {
    int scale = std::clamp(config_.internalScalePct, 25, 100);
    rtW_ = std::max(1, (outW_ * scale) / 100);
    rtH_ = std::max(1, (outH_ * scale) / 100);

    stats_.internalW = rtW_;
    stats_.internalH = rtH_;
}

void D3D12Renderer::render(const GameState &gs) {
    if (!initialized_) {
        D3D_LOG("Not initialized, skipping render");
        return;
    }

    auto frameStart = std::chrono::high_resolution_clock::now();

    // Update scene data (spheres and boxes from game state)
    auto uploadStart = std::chrono::high_resolution_clock::now();
    updateSceneData(gs);
    auto uploadEnd = std::chrono::high_resolution_clock::now();

    // Dispatch GPU compute shader
    auto gpuStart = std::chrono::high_resolution_clock::now();
    dispatchCompute();
    auto gpuEnd = std::chrono::high_resolution_clock::now();

    // Read back results from GPU
    auto readbackStart = std::chrono::high_resolution_clock::now();
    readbackResults();
    auto readbackEnd = std::chrono::high_resolution_clock::now();

    // Post-process on CPU (tone mapping + pack to BGRA)
    auto postStart = std::chrono::high_resolution_clock::now();
    postProcess();
    auto postEnd = std::chrono::high_resolution_clock::now();

    auto frameEnd = std::chrono::high_resolution_clock::now();

    // Update stats
    stats_.msTotal = std::chrono::duration<float, std::milli>(frameEnd - frameStart).count();
    stats_.msTrace = std::chrono::duration<float, std::milli>(gpuEnd - gpuStart).count();
    stats_.msUpscale = std::chrono::duration<float, std::milli>(postEnd - postStart).count();
    stats_.spp = config_.raysPerFrame;
    stats_.totalRays = config_.raysPerFrame * rtW_ * rtH_;
    stats_.frame++;
    frameCounter_++;
    
    haveHistory_ = true;
}

void D3D12Renderer::updateSceneData(const GameState &gs) {
    // Map scene data buffer for CPU write
    void* mappedData = nullptr;
    D3D12_RANGE readRange = { 0, 0 }; // We won't read
    HRESULT hr = sceneDataBuffer_->Map(0, &readRange, &mappedData);
    if (FAILED(hr)) {
        D3D_LOG("Failed to map scene data buffer");
        return;
    }

    float* sceneData = static_cast<float*>(mappedData);
    int objectIndex = 0;

    // Helper to convert game coords to world space
    auto toWorld = [&](float gx, float gy) {
        float gw = static_cast<float>(gs.gw);
        float gh = static_cast<float>(gs.gh);
        float wx = (gx / gw - 0.5f) * 8.0f;
        float wy = (0.5f - gy / gh) * 6.0f;
        return std::make_pair(wx, wy);
    };

    // Add balls (spheres)
    int numSpheres = 0;
    for (size_t i = 0; i < gs.balls.size() && i < 32; i++) {
        const auto& ball = gs.balls[i];
        auto [wx, wy] = toWorld(static_cast<float>(ball.x), static_cast<float>(ball.y));
        
        // Ball radius is approximately 0.5 game units
        float radius = (0.5f / static_cast<float>(gs.gw)) * 8.0f * 0.5f;
        int material = (i == 0) ? 1 : 0; // First ball is emissive, others diffuse
        
        int idx = objectIndex * 16;
        sceneData[idx + 0] = 0.0f; // type: sphere
        sceneData[idx + 1] = wx;   // center.x
        sceneData[idx + 2] = wy;   // center.y
        sceneData[idx + 3] = 0.0f; // center.z
        
        sceneData[idx + 4] = radius;           // radius
        sceneData[idx + 5] = static_cast<float>(material); // material
        sceneData[idx + 6] = 0.0f;
        sceneData[idx + 7] = 0.0f;
        
        objectIndex++;
        numSpheres++;
    }

    // Add paddles (boxes)
    int numBoxes = 0;
    
    float gw = static_cast<float>(gs.gw);
    float gh = static_cast<float>(gs.gh);
    
    float paddleHalfX = (2.0f / gw) * 4.0f * 0.5f;
    float paddleHalfY = (static_cast<float>(gs.paddle_h) / gh) * 3.0f * 0.5f;
    float paddleThickness = 0.05f;
    
    // Left paddle
    {
        auto [wx, wy] = toWorld(2.0f, static_cast<float>(gs.left_y) + static_cast<float>(gs.paddle_h) * 0.5f);
        
        int idx = objectIndex * 16;
        sceneData[idx + 0] = 1.0f; // type: box
        sceneData[idx + 5] = 2.0f; // material: metal
        
        // Box min
        sceneData[idx + 8] = wx - paddleHalfX;
        sceneData[idx + 9] = wy - paddleHalfY;
        sceneData[idx + 10] = -paddleThickness;
        
        // Box max
        sceneData[idx + 12] = wx + paddleHalfX;
        sceneData[idx + 13] = wy + paddleHalfY;
        sceneData[idx + 14] = paddleThickness;
        
        objectIndex++;
        numBoxes++;
    }
    
    // Right paddle
    {
        auto [wx, wy] = toWorld(gw - 2.0f, static_cast<float>(gs.right_y) + static_cast<float>(gs.paddle_h) * 0.5f);
        
        int idx = objectIndex * 16;
        sceneData[idx + 0] = 1.0f; // type: box
        sceneData[idx + 5] = 2.0f; // material: metal
        
        sceneData[idx + 8] = wx - paddleHalfX;
        sceneData[idx + 9] = wy - paddleHalfY;
        sceneData[idx + 10] = -paddleThickness;
        
        sceneData[idx + 12] = wx + paddleHalfX;
        sceneData[idx + 13] = wy + paddleHalfY;
        sceneData[idx + 14] = paddleThickness;
        
        objectIndex++;
        numBoxes++;
    }

    sceneDataBuffer_->Unmap(0, nullptr);

    // Update parameters buffer
    void* paramsData = nullptr;
    hr = paramsBuffer_->Map(0, &readRange, &paramsData);
    if (FAILED(hr)) {
        D3D_LOG("Failed to map parameters buffer");
        return;
    }

    float* params = static_cast<float*>(paramsData);
    uint32_t* uintParams = reinterpret_cast<uint32_t*>(params);
    
    uintParams[0] = rtW_;
    uintParams[1] = rtH_;
    uintParams[2] = config_.raysPerFrame;
    uintParams[3] = config_.maxBounces;
    
    params[4] = config_.accumAlpha;
    params[5] = config_.emissiveIntensity;
    params[6] = config_.metallicRoughness;
    uintParams[7] = frameCounter_;
    
    uintParams[8] = numSpheres;
    uintParams[9] = numBoxes;
    uintParams[10] = haveHistory_ ? 0 : 1; // resetHistory flag
    uintParams[11] = 0; // padding

    paramsBuffer_->Unmap(0, nullptr);
}

void D3D12Renderer::dispatchCompute() {
    // Wait for previous frame's GPU work to complete before reusing command allocator
    if (fence_->GetCompletedValue() < lastCompletedFenceValue_) {
        HRESULT hr = fence_->SetEventOnCompletion(lastCompletedFenceValue_, fenceEvent_);
        if (SUCCEEDED(hr)) {
            WaitForSingleObject(fenceEvent_, INFINITE);
        }
    }
    
    // Reset command allocator and list
    HRESULT hr = commandAllocator_->Reset();
    if (FAILED(hr)) {
        char buf[256];
        sprintf_s(buf, "Failed to reset command allocator: HRESULT 0x%08X", hr);
        D3D_LOG(buf);
        return;
    }

    // E_INVALIDARG (0x80070057) usually means null PSO or allocator
    // For compute, we MUST pass the PSO, but it can be null for graphics command lists
    if (!pipelineState_) {
        D3D_LOG("ERROR: Pipeline state is null when trying to reset command list!");
        return;
    }
    
    hr = commandList_->Reset(commandAllocator_.Get(), pipelineState_.Get());
    if (FAILED(hr)) {
        char buf[256];
        sprintf_s(buf, "Failed to reset command list: HRESULT 0x%08X (PSO=%p, Alloc=%p)", 
                  hr, pipelineState_.Get(), commandAllocator_.Get());
        D3D_LOG(buf);
        return;
    }

    // Set descriptor heap
    ID3D12DescriptorHeap* heaps[] = { srvUavHeap_.Get() };
    commandList_->SetDescriptorHeaps(_countof(heaps), heaps);

    // Set root signature and bind descriptors
    commandList_->SetComputeRootSignature(rootSignature_.Get());
    
    // CRITICAL: Validate all resources exist before using them
    if (!outputTexture_ || !accumTexture_ || !sceneDataBuffer_ || !paramsBuffer_ || !readbackBuffer_) {
        D3D_LOG("ERROR: One or more GPU resources are null!");
        char buf[256];
        sprintf_s(buf, "Resources: output=%p accum=%p scene=%p params=%p readback=%p",
                  outputTexture_.Get(), accumTexture_.Get(), sceneDataBuffer_.Get(), 
                  paramsBuffer_.Get(), readbackBuffer_.Get());
        D3D_LOG(buf);
        return;
    }
    
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = srvUavHeap_->GetGPUDescriptorHandleForHeapStart();
    
    // Bind UAV 0 (output texture)
    commandList_->SetComputeRootDescriptorTable(0, gpuHandle);
    
    // Bind UAV 1 (accum texture)
    gpuHandle.ptr += srvUavDescriptorSize_;
    commandList_->SetComputeRootDescriptorTable(1, gpuHandle);
    
    // Bind SRV (scene data)
    gpuHandle.ptr += srvUavDescriptorSize_;
    commandList_->SetComputeRootDescriptorTable(2, gpuHandle);
    
    // Bind CBV (parameters)
    gpuHandle.ptr += srvUavDescriptorSize_;
    commandList_->SetComputeRootDescriptorTable(3, gpuHandle);

    // Dispatch compute shader (8x8 thread groups)
    UINT groupsX = (rtW_ + 7) / 8;
    UINT groupsY = (rtH_ + 7) / 8;
    
    // CRITICAL: Validate dispatch dimensions
    if (groupsX == 0 || groupsY == 0 || groupsX > 65535 || groupsY > 65535) {
        char buf[256];
        sprintf_s(buf, "ERROR: Invalid dispatch dimensions! groupsX=%u, groupsY=%u, rtW=%d, rtH=%d", 
                  groupsX, groupsY, rtW_, rtH_);
        D3D_LOG(buf);
        return;
    }
    
    commandList_->Dispatch(groupsX, groupsY, 1);

    // Transition output texture for copy
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = outputTexture_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    
    commandList_->ResourceBarrier(1, &barrier);

    // Copy texture to readback buffer
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    footprint.Footprint.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    footprint.Footprint.Width = rtW_;
    footprint.Footprint.Height = rtH_;
    footprint.Footprint.Depth = 1;
    footprint.Footprint.RowPitch = (rtW_ * 4 * sizeof(float) + 255) & ~255; // Align to 256 bytes

    D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
    srcLocation.pResource = outputTexture_.Get();
    srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLocation.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
    dstLocation.pResource = readbackBuffer_.Get();
    dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLocation.PlacedFootprint = footprint;

    commandList_->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

    // Transition back to UAV
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    commandList_->ResourceBarrier(1, &barrier);

    // Log state before executing
    char buf[512];
    sprintf_s(buf, "[D3D12] About to execute: groupsX=%u, groupsY=%u, rtW=%d, rtH=%d", groupsX, groupsY, rtW_, rtH_);
    D3D_LOG(buf);
    
    // Execute command list
    executeCommandList();
}

void D3D12Renderer::readbackResults() {
    // Map readback buffer
    void* mappedData = nullptr;
    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(rtW_ * rtH_ * 4 * sizeof(float)) };
    
    HRESULT hr = readbackBuffer_->Map(0, &readRange, &mappedData);
    if (FAILED(hr)) {
        D3D_LOG("Failed to map readback buffer");
        return;
    }

    // The data is stored in the readback buffer, will be processed in postProcess()
    // For now, just unmap (we'll read it in postProcess)
    D3D12_RANGE writeRange = { 0, 0 }; // We didn't write
    readbackBuffer_->Unmap(0, &writeRange);
}

void D3D12Renderer::postProcess() {
    // Map readback buffer to read GPU results
    void* mappedData = nullptr;
    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(rtW_ * rtH_ * 4 * sizeof(float)) };
    
    HRESULT hr = readbackBuffer_->Map(0, &readRange, &mappedData);
    if (FAILED(hr)) {
        D3D_LOG("Failed to map readback buffer for post-processing");
        return;
    }

    const float* hdrData = static_cast<const float*>(mappedData);
    UINT rowPitch = (rtW_ * 4 * sizeof(float) + 255) & ~255; // Match dispatch alignment
    
    // ACES tone mapping + gamma correction + pack to BGRA
    auto toneMapACES = [](float x) {
        const float a = 2.51f;
        const float b = 0.03f;
        const float c = 2.43f;
        const float d = 0.59f;
        const float e = 0.14f;
        float num = x * (a * x + b);
        float den = x * (c * x + d) + e;
        return std::max(0.0f, std::min(1.0f, num / den));
    };
    
    auto gammaCorrect = [](float x) {
        return std::pow(std::max(0.0f, x), 1.0f / 2.2f);
    };

    // If output size != internal size, we need to upscale
    if (outW_ == rtW_ && outH_ == rtH_) {
        // No upscale needed, 1:1 copy with tone mapping
        // NOTE: Flip Y axis because GPU textures are stored bottom-up
        for (int y = 0; y < rtH_; y++) {
            int flippedY = (rtH_ - 1) - y; // Flip Y coordinate
            const float* rowPtr = reinterpret_cast<const float*>(
                reinterpret_cast<const uint8_t*>(hdrData) + flippedY * rowPitch
            );
            
            for (int x = 0; x < rtW_; x++) {
                float r = rowPtr[x * 4 + 0];
                float g = rowPtr[x * 4 + 1];
                float b = rowPtr[x * 4 + 2];
                
                // Tone map
                r = toneMapACES(r);
                g = toneMapACES(g);
                b = toneMapACES(b);
                
                // Gamma correct
                r = gammaCorrect(r);
                g = gammaCorrect(g);
                b = gammaCorrect(b);
                
                // Pack to BGRA
                uint32_t br = static_cast<uint32_t>(r * 255.0f + 0.5f);
                uint32_t bg = static_cast<uint32_t>(g * 255.0f + 0.5f);
                uint32_t bb = static_cast<uint32_t>(b * 255.0f + 0.5f);
                
                outputPixels_[y * outW_ + x] = (bb) | (bg << 8) | (br << 16) | (0xFF << 24);
            }
        }
    } else {
        // Upscale needed (bilinear)
        float scaleX = static_cast<float>(rtW_) / outW_;
        float scaleY = static_cast<float>(rtH_) / outH_;
        
        for (int oy = 0; oy < outH_; oy++) {
            for (int ox = 0; ox < outW_; ox++) {
                float sx = ox * scaleX;
                // Flip Y for correct orientation
                float sy = (outH_ - 1 - oy) * scaleY;
                
                int x0 = static_cast<int>(sx);
                int y0 = static_cast<int>(sy);
                int x1 = std::min(x0 + 1, rtW_ - 1);
                int y1 = std::min(y0 + 1, rtH_ - 1);
                
                float fx = sx - x0;
                float fy = sy - y0;
                
                // Bilinear interpolation
                auto sample = [&](int x, int y) -> std::array<float, 3> {
                    const float* rowPtr = reinterpret_cast<const float*>(
                        reinterpret_cast<const uint8_t*>(hdrData) + y * rowPitch
                    );
                    return { rowPtr[x * 4], rowPtr[x * 4 + 1], rowPtr[x * 4 + 2] };
                };
                
                auto c00 = sample(x0, y0);
                auto c10 = sample(x1, y0);
                auto c01 = sample(x0, y1);
                auto c11 = sample(x1, y1);
                
                float r = (c00[0] * (1-fx) + c10[0] * fx) * (1-fy) + (c01[0] * (1-fx) + c11[0] * fx) * fy;
                float g = (c00[1] * (1-fx) + c10[1] * fx) * (1-fy) + (c01[1] * (1-fx) + c11[1] * fx) * fy;
                float b = (c00[2] * (1-fx) + c10[2] * fx) * (1-fy) + (c01[2] * (1-fx) + c11[2] * fx) * fy;
                
                // Tone map + gamma
                r = gammaCorrect(toneMapACES(r));
                g = gammaCorrect(toneMapACES(g));
                b = gammaCorrect(toneMapACES(b));
                
                // Pack to BGRA
                uint32_t br = static_cast<uint32_t>(r * 255.0f + 0.5f);
                uint32_t bg = static_cast<uint32_t>(g * 255.0f + 0.5f);
                uint32_t bb = static_cast<uint32_t>(b * 255.0f + 0.5f);
                
                outputPixels_[oy * outW_ + ox] = (bb) | (bg << 8) | (br << 16) | (0xFF << 24);
            }
        }
    }

    D3D12_RANGE writeRange = { 0, 0 };
    readbackBuffer_->Unmap(0, &writeRange);
}

#endif // _WIN32
