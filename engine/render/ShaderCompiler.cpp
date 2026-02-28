/**
 * @file ShaderCompiler.cpp
 * @brief GLSL to SPIR-V compilation via external compiler (e.g. glslangValidator).
 */

#include "engine/render/ShaderCompiler.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace engine::render {

namespace {

/**
 * @brief Returns the path to the GLSL compiler executable.
 */
std::string ResolveCompilerPath(std::string_view compilerPath) {
    if (!compilerPath.empty()) {
        return std::string(compilerPath);
    }
    std::string fromConfig = engine::core::Config::GetString("shaders.compiler_path", "");
    if (!fromConfig.empty()) {
        return fromConfig;
    }
#if defined(_WIN32)
    const char* sdk = std::getenv("VULKAN_SDK");
    if (sdk && sdk[0] != '\0') {
        fs::path p(sdk);
        p /= "Bin";
        p /= "glslangValidator.exe";
        if (fs::exists(p)) {
            return p.generic_string();
        }
    }
#endif
    return "";
}

/**
 * @brief Reads a file into a byte vector.
 */
std::optional<std::vector<uint8_t>> ReadFileBytes(const fs::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        return std::nullopt;
    }
    const auto size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (size > 0 && !f.read(reinterpret_cast<char*>(buf.data()), size)) {
        return std::nullopt;
    }
    return buf;
}

/**
 * @brief Reads a text file into a string.
 */
std::string ReadFileText(const fs::path& path) {
    std::ifstream f(path);
    if (!f) {
        return "";
    }
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

} // namespace

ShaderCompileResult CompileGlslToSpirv(
    std::string_view relativePath,
    const std::vector<std::string>& defines,
    std::string_view compilerPath) {

    ShaderCompileResult result;

    const std::string contentRoot = engine::core::Config::GetString("paths.content", "game/data");
    const fs::path rootPath(contentRoot);
    const fs::path inputPath = rootPath / std::string(relativePath);

    if (!fs::exists(inputPath)) {
        result.errorMessage = "Shader file not found: " + inputPath.generic_string();
        LOG_ERROR(Render, "ShaderCompiler: {}", result.errorMessage);
        return result;
    }

    const std::string compiler = ResolveCompilerPath(compilerPath);
    if (compiler.empty()) {
        result.errorMessage = "GLSL compiler not found (set shaders.compiler_path or VULKAN_SDK)";
        LOG_ERROR(Render, "ShaderCompiler: {}", result.errorMessage);
        return result;
    }

    std::error_code ec;
    const fs::path tempDir = fs::temp_directory_path(ec);
    if (ec) {
        result.errorMessage = "Cannot get temp directory";
        return result;
    }

    const fs::path outSpv = tempDir / "shader_compiled.spv";
    const fs::path outErr = tempDir / "shader_compile_err.txt";

    std::string cmd;
    cmd.reserve(512);
    cmd += "\"";
    cmd += compiler;
    cmd += "\" -V \"";
    cmd += inputPath.generic_string();
    cmd += "\" -o \"";
    cmd += outSpv.generic_string();
    cmd += "\"";
    for (const auto& d : defines) {
        cmd += " -D";
        cmd += d;
    }
    cmd += " 2> \"";
    cmd += outErr.generic_string();
    cmd += "\"";

    const int ret = std::system(cmd.c_str());

    if (ret != 0) {
        result.errorMessage = ReadFileText(outErr);
        if (result.errorMessage.empty()) {
            result.errorMessage = "Compiler exited with code " + std::to_string(ret);
        }
        LOG_WARN(Render, "Shader compile failed for '{}': {}",
                               std::string(relativePath), result.errorMessage);
        return result;
    }

    auto bytes = ReadFileBytes(outSpv);
    if (!bytes || bytes->empty()) {
        result.errorMessage = "Compiled SPIR-V file missing or empty";
        return result;
    }

    result.spirv = std::move(*bytes);
    result.success = true;
    return result;
}

} // namespace engine::render
