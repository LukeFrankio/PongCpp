/**
 * @file slang_compiler.cpp
 * @brief Implementation of Slang shader compiler integration
 */

#include "slang_compiler.h"
#include <slang.h>
#include <iostream>
#include <fstream>

bool SlangCompiler::initialize() {
    // Create global session
    if (SLANG_FAILED(slang::createGlobalSession(&m_globalSession))) {
        setError("Failed to create Slang global session");
        return false;
    }
    
    // Configure session for SPIR-V output
    slang::SessionDesc sessionDesc = {};
    sessionDesc.targetCount = 1;
    
    slang::TargetDesc targetDesc = {};
    targetDesc.format = SLANG_SPIRV;
    targetDesc.profile = m_globalSession->findProfile("spirv_1_0");
    targetDesc.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;
    
    sessionDesc.targets = &targetDesc;
    
    // Add search paths for shader includes
    const char* searchPaths[] = {
        ".",                   // Current directory (where .exe is located)
        "src/vulkan/shaders",  // Relative to project root
        "./shaders",           // Relative to executable
        "../shaders"           // Alternative relative path
    };
    sessionDesc.searchPathCount = 4;
    sessionDesc.searchPaths = searchPaths;
    
    if (SLANG_FAILED(m_globalSession->createSession(sessionDesc, &m_session))) {
        setError("Failed to create Slang session");
        m_globalSession->release();
        m_globalSession = nullptr;
        return false;
    }
    
    return true;
}

void SlangCompiler::shutdown() {
    // Clear module cache
    for (auto& pair : m_moduleCache) {
        pair.second->release();
    }
    m_moduleCache.clear();
    
    // Release session
    if (m_session) {
        m_session->release();
        m_session = nullptr;
    }
    
    // Release global session
    if (m_globalSession) {
        m_globalSession->release();
        m_globalSession = nullptr;
    }
}

CompiledShader SlangCompiler::compileVertexShader(
    const std::string& slangFile, 
    const std::string& entryPoint) {
    return compileShader(slangFile, entryPoint, "vertex");
}

CompiledShader SlangCompiler::compileFragmentShader(
    const std::string& slangFile, 
    const std::string& entryPoint) {
    return compileShader(slangFile, entryPoint, "fragment");
}

CompiledShader SlangCompiler::compileShader(
    const std::string& slangFile,
    const std::string& entryPoint,
    const std::string& stage) {
    
    CompiledShader result;
    
    if (!m_session) {
        result.errors = "Slang compiler not initialized";
        return result;
    }
    
    // Load or get cached module
    slang::IModule* module = nullptr;
    auto it = m_moduleCache.find(slangFile);
    if (it != m_moduleCache.end()) {
        module = it->second;
    } else {
        // Load module from file
        module = m_session->loadModule(slangFile.c_str());
        if (!module) {
            result.errors = "Failed to load Slang module: " + slangFile;
            return result;
        }
        // Cache the module
        m_moduleCache[slangFile] = module;
    }
    
    // Find entry point
    slang::IEntryPoint* entryPointObj = nullptr;
    SlangResult findResult = module->findEntryPointByName(entryPoint.c_str(), &entryPointObj);
    if (SLANG_FAILED(findResult) || !entryPointObj) {
        result.errors = "Entry point not found: " + entryPoint + " in " + slangFile;
        return result;
    }
    
    // Also load the shared uniforms module so its cbuffers and helpers are
    // included in the final program. Some Slang configurations emit decorations
    // only when the module defining the cbuffers is part of the composite
    // component; explicitly including it ensures DescriptorSet/Binding
    // decorations are present in SPIR-V.
    slang::IModule* uniformsModule = nullptr;
    auto uIt = m_moduleCache.find(std::string("uniforms.slang"));
    if (uIt != m_moduleCache.end()) {
        uniformsModule = uIt->second;
    } else {
        uniformsModule = m_session->loadModule("uniforms.slang");
        if (uniformsModule) m_moduleCache["uniforms.slang"] = uniformsModule;
    }
    
    // Create component type with entry point and, if available, the uniforms module
    std::vector<slang::IComponentType*> componentList;
    if (uniformsModule) {
        componentList.push_back(uniformsModule);
    }
    componentList.push_back(module);
    componentList.push_back(entryPointObj);
    
    slang::IComponentType* program = nullptr;
    SlangResult compileResult = m_session->createCompositeComponentType(
        componentList.data(), static_cast<uint32_t>(componentList.size()), &program
    );
    
    if (SLANG_FAILED(compileResult)) {
        result.errors = "Failed to create composite component type";
        return result;
    }
    
    // Get target code (SPIR-V)
    slang::IBlob* spirvBlob = nullptr;
    slang::IBlob* diagnosticsBlob = nullptr;
    
    compileResult = program->getTargetCode(0, &spirvBlob, &diagnosticsBlob);
    
    // Handle diagnostics (warnings/errors)
    if (diagnosticsBlob) {
        const char* diagnostics = (const char*)diagnosticsBlob->getBufferPointer();
        if (diagnostics && strlen(diagnostics) > 0) {
            std::string diagnosticsStr(diagnostics);
            // Separate warnings and errors (simple heuristic)
            if (diagnosticsStr.find("error") != std::string::npos) {
                result.errors = diagnosticsStr;
            } else {
                result.warnings = diagnosticsStr;
            }
        }
        diagnosticsBlob->release();
    }
    
    if (SLANG_FAILED(compileResult) || !spirvBlob) {
        if (result.errors.empty()) {
            result.errors = "Failed to generate SPIR-V code";
        }
        if (program) program->release();
        return result;
    }
    
    // Copy SPIR-V data
    const uint8_t* spirvData = (const uint8_t*)spirvBlob->getBufferPointer();
    size_t spirvSize = spirvBlob->getBufferSize();
    
    result.spirv.resize(spirvSize);
    std::memcpy(result.spirv.data(), spirvData, spirvSize);
    result.success = true;
    
    // Cleanup
    spirvBlob->release();
    program->release();
    
    return result;
}
