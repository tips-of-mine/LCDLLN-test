#include "engine/texr/TexrPath.h"

#include <cctype>

namespace lcdlln::texr {

std::string NormalizeRelativePath(const std::filesystem::path& relative_path)
{
	std::string s = relative_path.generic_string();
	for (char& c : s)
	{
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	return s;
}

}  // namespace lcdlln::texr
