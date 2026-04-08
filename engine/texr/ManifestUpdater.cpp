#include "engine/texr/ManifestUpdater.h"

#include "engine/texr/HttpFetch.h"
#include "engine/texr/KeysVerify.h"
#include "engine/texr/ManifestCrypto.h"
#include "engine/texr/ManifestVerify.h"
#include "engine/texr/TexrReader.h"

#include <filesystem>
#include <fstream>

namespace lcdlln::manifest {
namespace {

std::string JoinCdnPath(std::string_view base, std::string_view rel_path)
{
	std::string b(base);
	while (!b.empty() && (b.back() == '/' || b.back() == '\\'))
	{
		b.pop_back();
	}
	std::string r(rel_path);
	while (!r.empty() && (r.front() == '/' || r.front() == '\\'))
	{
		r.erase(0, 1);
	}
	if (b.empty())
	{
		return r;
	}
	return b + "/" + r;
}

bool WriteBytesToFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& data, std::string& err)
{
	err.clear();
	std::error_code ec;
	const std::filesystem::path parent = path.parent_path();
	if (!parent.empty())
	{
		std::filesystem::create_directories(parent, ec);
		if (ec)
		{
			err = "create_directories: " + ec.message();
			return false;
		}
	}
	const std::filesystem::path tmp = path.string() + ".download_tmp";
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		if (!f || !f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size())))
		{
			err = "write temp file failed";
			std::filesystem::remove(tmp, ec);
			return false;
		}
	}
	std::filesystem::rename(tmp, path, ec);
	if (ec)
	{
		err = "rename to final: " + ec.message();
		std::filesystem::remove(tmp, ec);
		return false;
	}
	return true;
}

bool TryVerifyManifestFromBytes(const std::vector<std::uint8_t>& bytes, const SigningKeys& known,
                                const std::array<std::uint8_t, 32>& k_embedded, ParsedManifest& parsed, std::string& err)
{
	const std::string utf8(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	return VerifyManifestJson(utf8, known, k_embedded, parsed, err);
}

}  // namespace

bool DownloadVerifiedTexr(const ManifestUpdateConfig& cfg, std::filesystem::path& out_texr_path, std::string& err)
{
	out_texr_path.clear();
	err.clear();

	std::vector<std::uint8_t> keys_bytes;
	if (!FetchUrlBytes(cfg.keys_url, keys_bytes, err))
	{
		return false;
	}
	const std::string keys_utf8(reinterpret_cast<const char*>(keys_bytes.data()), keys_bytes.size());
	SigningKeys known;
	if (!VerifyKeysJson(keys_utf8, cfg.k_embedded, known, err))
	{
		return false;
	}

	std::vector<std::string> manifest_urls;
	manifest_urls.reserve(1 + cfg.extra_manifest_urls.size());
	manifest_urls.push_back(cfg.manifest_url);
	for (const std::string& u : cfg.extra_manifest_urls)
	{
		if (!u.empty())
		{
			manifest_urls.push_back(u);
		}
	}

	ParsedManifest parsed;
	std::vector<std::uint8_t> man_bytes;
	bool manifest_ok = false;
	for (const std::string& mu : manifest_urls)
	{
		man_bytes.clear();
		if (!FetchUrlBytes(mu, man_bytes, err))
		{
			continue;
		}
		if (TryVerifyManifestFromBytes(man_bytes, known, cfg.k_embedded, parsed, err))
		{
			manifest_ok = true;
			break;
		}
	}
	if (!manifest_ok)
	{
		if (err.empty())
		{
			err = "all manifest URLs failed fetch or verify";
		}
		return false;
	}

	const auto art_it = parsed.artifacts.find(cfg.artifact_id);
	if (art_it == parsed.artifacts.end())
	{
		err = "artifact_id not in manifest: " + cfg.artifact_id;
		return false;
	}
	const ArtifactDescriptor& art = art_it->second;

	const std::string pkg_url = JoinCdnPath(cfg.cdn_base, art.relative_path);
	std::vector<std::uint8_t> pkg_bytes;
	if (!FetchUrlBytes(pkg_url, pkg_bytes, err))
	{
		return false;
	}
	if (pkg_bytes.size() != art.cipher_size)
	{
		err = "downloaded size mismatch (expected cipher_size=" + std::to_string(art.cipher_size) + " got "
		       + std::to_string(pkg_bytes.size()) + ")";
		return false;
	}

	std::string hex_cipher;
	if (!Sha256BufferHexLower(pkg_bytes, hex_cipher, err))
	{
		return false;
	}
	if (hex_cipher != art.hash_cipher)
	{
		err = "hash_cipher mismatch after download";
		return false;
	}

	const std::filesystem::path out_path = cfg.cache_dir / std::filesystem::path(art.relative_path).filename();
	if (!WriteBytesToFile(out_path, pkg_bytes, err))
	{
		return false;
	}

	std::string hex_plain;
	if (!lcdlln::texr::TexrReader::ComputeInnerSha256Hex(out_path, hex_plain, err))
	{
		std::error_code rm;
		std::filesystem::remove(out_path, rm);
		return false;
	}
	if (hex_plain != art.hash_plain)
	{
		err = "hash_plain mismatch (inner SHA-256)";
		std::error_code rm;
		std::filesystem::remove(out_path, rm);
		return false;
	}

	out_texr_path = out_path;
	return true;
}

}  // namespace lcdlln::manifest
