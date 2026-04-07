#pragma once

#include "engine/texr/TexrReader.h"

namespace engine::texr {

/// Runtime handle for a `.texr` package (v1, unencrypted outer). Thin alias of `lcdlln::texr::TexrReader`.
using Package = lcdlln::texr::TexrReader;

}  // namespace engine::texr
