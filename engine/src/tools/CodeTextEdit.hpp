#pragma once

#include <devex/core/Export.hpp>
#include <string>

namespace devex::tools::detail
{

struct DEVEX_API CodeTextEdit
{
    int begin = 0;
    int end = 0;
    std::string text;
    bool operator==(const CodeTextEdit&) const = default;
};

} // namespace devex::tools::detail
