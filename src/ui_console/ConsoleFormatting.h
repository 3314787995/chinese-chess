#pragma once

#include <string>
#include <string_view>

namespace xiangqi
{

int consoleDisplayWidth(std::string_view text);
std::string rightAlignConsoleCell(std::string_view rendered, int width);

} // namespace xiangqi
