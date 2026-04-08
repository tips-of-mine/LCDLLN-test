#include "TexrPack.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

void PrintUsage()
{
	std::fprintf(stderr,
	             "texr_builder — v1 .texr pack / validate / inspect (LZ4, optional AES-256-GCM outer)\n"
	             "Usage:\n"
	             "  texr_builder pack [--meta] [--encrypt] [--key-hex <64_hex>] [--texconv <texconv.exe>] <input_dir> <output.texr>\n"
	             "  texr_builder validate <file.texr>\n"
	             "  texr_builder inspect <file.texr>\n"
	             "  texr_builder help\n"
	             "Decrypt key: env TEXR_AES_KEY_HEX (64 hex chars) when package is encrypted.\n");
}

}  // namespace

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		PrintUsage();
		return 1;
	}
	const std::string_view cmd(argv[1]);
	if (cmd == "help" || cmd == "-h" || cmd == "--help")
	{
		PrintUsage();
		return 0;
	}
	if (cmd == "validate")
	{
		if (argc != 3)
		{
			PrintUsage();
			return 1;
		}
		std::string err;
		const int rc = texr::ValidateFile(argv[2], err);
		if (rc != 0)
			std::fprintf(stderr, "validate failed: %s\n", err.c_str());
		return rc;
	}
	if (cmd == "inspect")
	{
		if (argc != 3)
		{
			PrintUsage();
			return 1;
		}
		std::string err;
		const int rc = texr::InspectFile(argv[2], err);
		if (rc != 0)
			std::fprintf(stderr, "inspect failed: %s\n", err.c_str());
		return rc;
	}
	if (cmd == "pack")
	{
		texr::PackOptions opts{};
		int argi = 2;
		while (argi < argc)
		{
			const std::string_view a(argv[argi]);
			if (a == "--meta")
			{
				opts.write_meta_json = true;
				++argi;
			}
			else if (a == "--encrypt")
			{
				opts.encrypt = true;
				++argi;
			}
			else if (a == "--key-hex")
			{
				if (argi + 1 >= argc)
				{
					PrintUsage();
					return 1;
				}
				opts.aes_key_hex = argv[argi + 1];
				argi += 2;
			}
			else if (a == "--texconv")
			{
				if (argi + 1 >= argc)
				{
					PrintUsage();
					return 1;
				}
				opts.texconv_exe = argv[argi + 1];
				argi += 2;
			}
			else
				break;
		}
		if (argc - argi != 2)
		{
			PrintUsage();
			return 1;
		}
		std::string err;
		const int rc = texr::PackDirectory(argv[argi], argv[argi + 1], opts, err);
		if (rc != 0)
			std::fprintf(stderr, "pack failed: %s\n", err.c_str());
		return rc;
	}
	PrintUsage();
	return 1;
}
