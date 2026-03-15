// M21.2 — Outil offline : calcule le checksum SHA-256 de chaque fichier .sql dans db/migrations.
// Sortie : une ligne par fichier "version checksum_hex path".
// Utilisé pour vérifier les migrations et détecter pending / mismatch (voir db/migrations/README.md).

#include <openssl/sha.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

/// Calcule le checksum SHA-256 (hex 64 caractères) du contenu du fichier \a path.
/// Retourne une chaîne vide en cas d'erreur de lecture.
std::string Sha256HexFromFile(const fs::path& path)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return {};
	SHA256_CTX ctx;
	SHA256_Init(&ctx);
	std::vector<char> buf(8192);
	while (f.read(buf.data(), buf.size()) || f.gcount() > 0)
		SHA256_Update(&ctx, buf.data(), static_cast<size_t>(f.gcount()));
	unsigned char hash[SHA256_DIGEST_LENGTH];
	SHA256_Final(hash, &ctx);
	std::ostringstream os;
	os << std::hex << std::setfill('0');
	for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
		os << std::setw(2) << static_cast<unsigned>(hash[i]);
	return os.str();
}

/// Extrait le numéro de version (4 chiffres) du nom de fichier NNNN_name.sql.
/// Retourne -1 si le format n'est pas reconnu.
int VersionFromFilename(const std::string& name)
{
	if (name.size() < 5 || name[4] != '_')
		return -1;
	int v = 0;
	for (int i = 0; i < 4; ++i)
	{
		if (name[i] < '0' || name[i] > '9')
			return -1;
		v = v * 10 + (name[i] - '0');
	}
	return v;
}

int main(int argc, char** argv)
{
	fs::path migrationsDir = "db/migrations";
	if (argc >= 2)
		migrationsDir = argv[1];

	if (!fs::is_directory(migrationsDir))
	{
		std::cerr << "migration_checksum: not a directory: " << migrationsDir << "\n";
		return 1;
	}

	std::vector<fs::path> files;
	for (const auto& e : fs::directory_iterator(migrationsDir))
	{
		if (e.path().extension() == ".sql")
			files.push_back(e.path());
	}
	std::sort(files.begin(), files.end());

	for (const auto& p : files)
	{
		std::string name = p.filename().string();
		int version = VersionFromFilename(name);
		if (version < 0)
		{
			std::cerr << "migration_checksum: skip (invalid name): " << name << "\n";
			continue;
		}
		std::string checksum = Sha256HexFromFile(p);
		if (checksum.empty())
		{
			std::cerr << "migration_checksum: read failed: " << p << "\n";
			return 1;
		}
		std::cout << version << "\t" << checksum << "\t" << p.generic_string() << "\n";
	}
	return 0;
}
