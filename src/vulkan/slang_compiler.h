/**
 * @file slang_compiler.h
 * @brief Slang shader compiler integration for Vulkan renderer
 * 
 * This file provides a clean interface for compiling Slang shaders to SPIR-V
 * bytecode for use with Vulkan. Supports runtime compilation and hot reload.
 */

#pragma once

#include <vector>
#include <string>
#include <map>

// Forward declare Slang types to avoid including slang.h in header
namespace slang {
    struct IGlobalSession;
    struct ISession;
    struct IModule;
    struct IEntryPoint;
}

/**
 * @brief Compiled shader data container
 */
struct CompiledShader {
    std::vector<uint8_t> spirv;     ///< SPIR-V bytecode
    std::string errors;             ///< Compilation error messages  
    std::string warnings;           ///< Compilation warnings
    bool success = false;           ///< Compilation success flag
    
    /// Get SPIR-V data as uint32_t array for Vulkan
    const uint32_t* data() const { 
        return reinterpret_cast<const uint32_t*>(spirv.data()); 
    }
    
    /// Get SPIR-V size in bytes
    size_t size() const { return spirv.size(); }
    
    /// Get SPIR-V size in uint32_t elements
    size_t sizeWords() const { return spirv.size() / sizeof(uint32_t); }
};

/**
 * @brief Slang shader compiler wrapper
 * 
 * Provides high-level interface for compiling Slang shaders to SPIR-V.
 * Manages Slang session and handles error reporting.
 */
class SlangCompiler {
public:
    /**
     * @brief Initialize the Slang compiler
     * @return true if successful, false on error
     */
    bool initialize();
    
    /**
     * @brief Shutdown and cleanup Slang resources
     */
    void shutdown();
    
    /**
     * @brief Compile vertex shader from Slang source
     * @param slangFile Path to .slang source file
     * @param entryPoint Entry point function name
     * @return Compiled shader data
     */
    CompiledShader compileVertexShader(
        const std::string& slangFile, 
        const std::string& entryPoint
    );
    
    /**
     * @brief Compile fragment shader from Slang source
     * @param slangFile Path to .slang source file
     * @param entryPoint Entry point function name
     * @return Compiled shader data
     */
    CompiledShader compileFragmentShader(
        const std::string& slangFile, 
        const std::string& entryPoint
    );
    
    /**
     * @brief Check if compiler is initialized
     * @return true if ready to compile
     */
    bool isInitialized() const { return m_session != nullptr; }
    
    /**
     * @brief Get last error message
     * @return Error string from last operation
     */
    const std::string& getLastError() const { return m_lastError; }
    
private:
    slang::IGlobalSession* m_globalSession = nullptr;
    slang::ISession* m_session = nullptr;
    std::string m_lastError;
    
    /// Cache compiled modules to avoid recompilation
    std::map<std::string, slang::IModule*> m_moduleCache;
    
    /**
     * @brief Internal compilation helper
     * @param slangFile Source file path
     * @param entryPoint Entry point name
     * @param stage Shader stage (vertex/fragment)
     * @return Compiled shader data
     */
    CompiledShader compileShader(
        const std::string& slangFile,
        const std::string& entryPoint,
        const std::string& stage
    );
    
    /**
     * @brief Set last error message
     * @param error Error message to store
     */
    void setError(const std::string& error) { m_lastError = error; }
};
