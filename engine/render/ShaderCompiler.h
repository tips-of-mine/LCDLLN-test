#pragma once

/**
 * @file ShaderCompiler.h
 * @brief GLSL to SPIR-V compilation utility.
 *
 * Ticket: M01.5 — Shader management + hot reload.
 *
 * Compiles vertex and fragment shaders (GLSL) to SPIR-V using an external
 * compiler (e.g. glslangValidator from Vulkan SDK). All paths are relative
 * to paths.content (Config). Produces readable error messages on failure.
 */

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::render {

/**
 * @brief Result of a shader compilation attempt.
 */
struct ShaderCompileResult {
    /// Compiled SPIR-V bytes (empty on failure).
    std::vector<uint8_t> spirv;
    /// Human-readable error output from the compiler (empty on success).
    std::string errorMessage;
    /// True if compilation succeeded.
    bool success = false;
};

/**
 * @brief Compiles a single GLSL file to SPIR-V.
 *
 * Uses the content root from Config ("paths.content"). Resolves
 * relativePath to (contentRoot + "/" + relativePath). Invokes the
 * configured compiler (e.g. glslangValidator) and returns SPIR-V
 * bytes plus any error output.
 *
 * @param relativePath  Path relative to content root (e.g. "shaders/main.vert").
 * @param defines       Optional preprocessor defines, e.g. {"KEY=value"}.
 * @param compilerPath  Optional path to glslangValidator; if empty, uses
 *                      config "shaders.compiler_path" or VULKAN_SDK/bin.
 * @return              ShaderCompileResult with spirv and errorMessage.
 */
ShaderCompileResult CompileGlslToSpirv(
    std::string_view relativePath,
    const std::vector<std::string>& defines = {},
    std::string_view compilerPath = "");

} // namespace engine::render
