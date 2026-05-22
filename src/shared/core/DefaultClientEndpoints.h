#pragma once

#include <string_view>

namespace engine::core::defaults
{
	/// URL de la sonde `/status` — à garder alignée avec `external/external_links.json` (`client.status_api_url`).
	inline constexpr std::string_view kStatusApiUrl = "http://82.66.77.254:3842/status";
}
