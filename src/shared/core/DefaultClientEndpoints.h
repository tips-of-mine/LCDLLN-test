#pragma once

#include <string_view>

namespace engine::core::defaults
{
	/// URL de la sonde `/status` — à garder alignée avec `external/external_links.json` (`client.status_api_url`).
	inline constexpr std::string_view kStatusApiUrl = "http://10.0.4.133:3842/status";
}
