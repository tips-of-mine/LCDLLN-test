#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace engine::render
{
	/// Shader stage for GLSL compilation.
	enum class ShaderStage
	{
		Vertex,
		Fragment
	};

	/// Compiles GLSL to SPIR-V via glslangValidator (Vulkan SDK). Erreurs lisibles via GetLastErrors().
	class ShaderCompiler final
	{
	public:
		ShaderCompiler() = default;

		/// Tries to locate glslangValidator (VULKAN_SDK/bin or PATH). Call once before Compile.
		/// \return true if the executable was found.
		bool LocateCompiler();

		/// Compiles a GLSL file to SPIR-V. Uses glslangValidator if LocateCompiler() succeeded.
		/// \param glslPath Full path to the .vert or .frag file.
		/// \param stage Vertex or Fragment.
		/// \return SPIR-V binary (uint32_t words), or nullopt on failure (see GetLastErrors()).
		std::optional<std::vector<uint32_t>> CompileGlslToSpirv(const std::filesystem::path& glslPath, ShaderStage stage);

		/// Returns the last compilation error output (stderr of glslangValidator). Empty if last compile succeeded.
		std::string_view GetLastErrors() const { return m_lastErrors; }

	private:
		std::string m_compilerPath;
		mutable std::string m_lastErrors;
	};
}
