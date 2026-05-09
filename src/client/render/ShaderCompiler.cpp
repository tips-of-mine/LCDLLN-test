#include "engine/render/ShaderCompiler.h"
#include "engine/core/Log.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <filesystem>

namespace engine::render
{
	namespace
	{
		std::string GetQuotedPath(const std::filesystem::path& p)
		{
			std::string s = p.string();
			if (s.find(' ') != std::string::npos)
			{
				return "\"" + s + "\"";
			}
			return s;
		}
	}

	bool ShaderCompiler::LocateCompiler()
	{
		if (!m_compilerPath.empty())
		{
			return true;
		}

		const char* vulkanSdk = std::getenv("VULKAN_SDK");
		if (vulkanSdk)
		{
			#if defined(_WIN32)
				std::filesystem::path exe = std::filesystem::path(vulkanSdk) / "bin" / "glslangValidator.exe";
			#else
				std::filesystem::path exe = std::filesystem::path(vulkanSdk) / "bin" / "glslangValidator";
			#endif
			std::error_code ec;
			if (std::filesystem::exists(exe, ec))
			{
				m_compilerPath = exe.string();
				return true;
			}
		}

		// Fallback: assume in PATH
		m_compilerPath = "glslangValidator";
		return true;
	}

	std::optional<std::vector<uint32_t>> ShaderCompiler::CompileGlslToSpirv(const std::filesystem::path& glslPath, ShaderStage stage)
	{
		m_lastErrors.clear();

		std::error_code ec;
		if (!std::filesystem::exists(glslPath, ec))
		{
			m_lastErrors = "File not found: " + glslPath.string();
			LOG_ERROR(Render, "ShaderCompiler: {}", m_lastErrors);
			return std::nullopt;
		}

		std::filesystem::path tempDir = std::filesystem::temp_directory_path(ec);
		if (ec || tempDir.empty())
		{
			m_lastErrors = "Could not get temp directory";
			return std::nullopt;
		}

		static int s_counter = 0;
		auto now = std::chrono::steady_clock::now().time_since_epoch().count();
		std::string baseName = "lcdlln_shader_" + std::to_string(s_counter++) + "_" + std::to_string(now);
		std::filesystem::path outSpv = tempDir / (baseName + ".spv");
		std::filesystem::path errTxt = tempDir / (baseName + "_err.txt");

		const char* stageStr = "frag";
		switch (stage)
		{
		case ShaderStage::Vertex:  stageStr = "vert"; break;
		case ShaderStage::Fragment: stageStr = "frag"; break;
		case ShaderStage::Compute: stageStr = "comp"; break;
		default: stageStr = "frag"; break;
		}
		std::string compilerExe = m_compilerPath.empty() ? "glslangValidator" : GetQuotedPath(m_compilerPath);
		std::string cmd = compilerExe + " -V -S " + stageStr + " -o " + GetQuotedPath(outSpv) + " " + GetQuotedPath(glslPath) + " 2> " + GetQuotedPath(errTxt);

		int ret = std::system(cmd.c_str());

		if (ret != 0)
		{
			std::ifstream errFile(errTxt);
			if (errFile.is_open())
			{
				m_lastErrors = std::string(std::istreambuf_iterator<char>(errFile), std::istreambuf_iterator<char>());
				errFile.close();
			}
			else
			{
				m_lastErrors = "glslangValidator exited with code " + std::to_string(ret);
			}
			std::filesystem::remove(errTxt, ec);
			LOG_WARN(Render, "ShaderCompiler: {} failed: {}", glslPath.string(), m_lastErrors);
			return std::nullopt;
		}

		std::ifstream spvFile(outSpv, std::ios::binary);
		if (!spvFile.is_open())
		{
			m_lastErrors = "Could not read output SPIR-V file";
			std::filesystem::remove(outSpv, ec);
			return std::nullopt;
		}
		spvFile.seekg(0, std::ios::end);
		const std::streamsize size = spvFile.tellg();
		spvFile.seekg(0, std::ios::beg);
		if (size <= 0 || (size % 4) != 0)
		{
			m_lastErrors = "Invalid SPIR-V output size";
			spvFile.close();
			std::filesystem::remove(outSpv, ec);
			return std::nullopt;
		}
		std::vector<uint32_t> spirv(static_cast<size_t>(size) / 4);
		spvFile.read(reinterpret_cast<char*>(spirv.data()), size);
		spvFile.close();
		std::filesystem::remove(outSpv, ec);
		std::filesystem::remove(errTxt, ec);

		return spirv;
	}
}
