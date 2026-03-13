/// Zone builder (M09.4 / M10.5 / M11.2): HLOD builder — clustering + mesh merge.
/// Writes hlod.pak (merged meshes per cluster with material and bounds).
///
/// Usage (manifest mode):
///   hlod_builder --input <manifest.txt> [--output hlod.pak] [--content <path>]
///
/// Usage (scan mode  — no manifest needed):
///   hlod_builder --scan [--output hlod.pak] [--content <path>]
///
/// Scan mode discovers all *.mesh files under <content>/meshes/ automatically
/// and places them on a uniform grid in chunk (0,0).

#include "Clustering.h"
#include "ManifestParser.h"
#include "MeshMerge.h"
#include "PakWriter.h"
#include "ScanManifest.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
	std::string ReadAllText(const std::string& path)
	{
		std::ifstream f(path);
		if (!f)
			return {};
		return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	}

	void PrintUsage()
	{
		std::cerr <<
			"Usage:\n"
			"  hlod_builder --input <manifest.txt> [--output hlod.pak] [--content <path>]\n"
			"  hlod_builder --scan                 [--output hlod.pak] [--content <path>]\n"
			"\n"
			"Options:\n"
			"  --input <file>   Path to manifest.txt (chunk/mesh declarations)\n"
			"  --scan           Auto-discover *.mesh files under <content>/meshes/\n"
			"  --output <file>  Output .pak path (default: hlod.pak)\n"
			"  --content <dir>  Content root directory (default: game/data)\n";
	}
}

int main(int argc, char** argv)
{
	std::string inputPath;
	std::string outputPath  = "hlod.pak";
	std::string contentDir  = "game/data";
	bool        scanMode    = false;

	for (int i = 1; i < argc; ++i)
	{
		std::string arg = argv[i];
		if (arg == "--input" && i + 1 < argc)
			inputPath = argv[++i];
		else if (arg == "--output" && i + 1 < argc)
			outputPath = argv[++i];
		else if (arg == "--content" && i + 1 < argc)
			contentDir = argv[++i];
		else if (arg == "--scan")
			scanMode = true;
		else if (arg == "--help" || arg == "-h")
		{
			PrintUsage();
			return 0;
		}
	}

	// --- Resolve chunks -------------------------------------------------
	std::vector<tools::hlod_builder::ChunkInput> chunks;
	std::string resolvedContent = contentDir;

	if (scanMode)
	{
		std::cout << "hlod_builder: scan mode -- scanning " << contentDir << "/meshes/\n";
		chunks = tools::hlod_builder::ScanContentDir(contentDir);
		if (chunks.empty())
		{
			std::cerr << "hlod_builder: no *.mesh files found under " << contentDir << "/meshes/\n";
			return 1;
		}
		size_t total = 0;
		for (const auto& c : chunks) total += c.instances.size();
		std::cout << "hlod_builder: found " << total << " mesh(es) across "
		          << chunks.size() << " chunk(s)\n";
	}
	else
	{
		if (inputPath.empty())
		{
			std::cerr << "hlod_builder: --input <manifest.txt> or --scan required\n\n";
			PrintUsage();
			return 1;
		}
		std::string manifestText = ReadAllText(inputPath);
		if (manifestText.empty())
		{
			std::cerr << "hlod_builder: could not read manifest: " << inputPath << "\n";
			return 1;
		}
		chunks = tools::hlod_builder::ParseManifest(contentDir, manifestText, resolvedContent);
		if (chunks.empty())
		{
			std::cerr << "hlod_builder: no chunks in manifest (or parse error)\n";
			return 1;
		}
	}

	// --- Ensure output directory exists ---------------------------------
	{
		std::error_code ec;
		std::filesystem::create_directories(
			std::filesystem::path(outputPath).parent_path(), ec);
		// Non-fatal if parent path is empty (file in cwd).
	}

	// --- Cluster + merge ------------------------------------------------
	std::vector<tools::hlod_builder::ChunkOutput> outputs;
	outputs.reserve(chunks.size());

	for (const tools::hlod_builder::ChunkInput& chunk : chunks)
	{
		std::cout << "hlod_builder: processing chunk ("
		          << chunk.chunkX << ", " << chunk.chunkZ << ") -- "
		          << chunk.instances.size() << " instance(s)\n";

		std::vector<tools::hlod_builder::Cluster> clusters =
			tools::hlod_builder::ClusterInstances(chunk, 20, 80);

		tools::hlod_builder::ChunkOutput out;
		out.chunkX = chunk.chunkX;
		out.chunkZ = chunk.chunkZ;

		for (const tools::hlod_builder::Cluster& cl : clusters)
		{
			tools::hlod_builder::MergedMesh merged =
				tools::hlod_builder::MergeClusterMeshes(resolvedContent, chunk, cl);
			if (!merged.data.vertices.empty())
				out.clusters.push_back(std::move(merged));
		}
		outputs.push_back(std::move(out));
	}

	// --- Write pak ------------------------------------------------------
	if (!tools::hlod_builder::WriteHlodPak(outputPath, outputs, {}))
	{
		std::cerr << "hlod_builder: failed to write " << outputPath << "\n";
		return 1;
	}

	std::cout << "hlod_builder: wrote " << outputPath
	          << " (" << outputs.size() << " chunk(s))\n";
	return 0;
}