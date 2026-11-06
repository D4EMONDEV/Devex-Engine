#pragma once

// Reading a page of the internet, in the program of the editor alone: the engine library and the games it exports do not
// carry libcurl for what only the editor does.
#include <devex/core/Error.hpp>

#include <stop_token>
#include <string>

namespace devex::editor {

[[nodiscard]] core::Result<std::string> fetchText(const std::string& address, std::stop_token stop);

} // namespace devex::editor
