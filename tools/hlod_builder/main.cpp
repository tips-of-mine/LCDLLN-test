/// M09.4 — HLOD builder: clustering + mesh merge. Offline tool under /tools.
/// Builds HLOD per chunk and writes hlod.pak (mesh + materials list).
/// Usage: hlod_builder --input <manifest.txt> --output hlod.pak [--content <path>]

#include "Clustering.h"
#include "ManifestParser.h"
#include "MeshMerge.h"
#include "PakWriter.h"

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
}

int main(int argc, char** argv)
{
	std::string inputPath;
	std::string outputPath = "hlod.pak";
	std::string contentDir = ".";

	for (int i = 1; i < argc; ++i)
	{
		std::string arg = argv[i];
		if (arg == "--input" && i + 1 < argc)
			inputPath = argv[++i];
		else if (arg == "--output" && i + 1 < argc)
			outputPath = argv[++i];
		else if (arg == "--content" && i + 1 < argc)
			contentDir = argv[++i];
	}

	if (inputPath.empty())
	{
		std::cerr << "hlod_builder: --input <manifest.txt> required\n";
		return 1;
	}

	std::string manifestText = ReadAllText(inputPath);
	if (manifestText.empty())
	{
		std::cerr << "hlod_builder: could not read manifest: " << inputPath << "\n";
		return 1;
	}

	std::string resolvedContent;
	std::vector<tools::hlod_builder::ChunkInput> chunks = tools::hlod_builder::ParseManifest(contentDir, manifestText, resolvedContent);
	if (chunks.empty())
	{
		std::cerr << "hlod_builder: no chunks in manifest (or parse error)\n";
		return 1;
	}

	std::vector<tools::hlod_builder::ChunkOutput> outputs;
	outputs.reserve(chunks.size());
	for (const tools::hlod_builder::ChunkInput& chunk : chunks)
	{
		std::vector<tools::hlod_builder::Cluster> clusters = tools::hlod_builder::ClusterInstances(chunk, 20, 80);
		tools::hlod_builder::ChunkOutput out;
		out.chunkX = chunk.chunkX;
		out.chunkZ = chunk.chunkZ;
		for (const tools::hlod_builder::Cluster& cl : clusters)
		{
			tools::hlod_builder::MergedMesh merged = tools::hlod_builder::MergeClusterMeshes(resolvedContent, chunk, cl);
			if (!merged.data.vertices.empty())
				out.clusters.push_back(std::move(merged));
		}
		outputs.push_back(std::move(out));
	}

	if (!tools::hlod_builder::WriteHlodPak(outputPath, outputs, {}))
	{
		std::cerr << "hlod_builder: failed to write " << outputPath << "\n";
		return 1;
	}
	std::cout << "hlod_builder: wrote " << outputPath << " (" << outputs.size() << " chunks)\n";
	return 0;
}
