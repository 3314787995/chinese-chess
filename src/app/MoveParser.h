#pragma once

#include "app/GameSession.h"

#include <istream>

namespace xiangqi
{

class MoveParser
{
public:
    ParsedCommand parse(const std::string& input, const GameSession& session) const;
    std::string moveToCoordinateText(const Move& move, const BoardConfig& config) const;
    std::string moveToChineseText(const Move& move, const GameSession& session) const;
    std::string moveToWxfText(const Move& move, const GameSession& session) const;
    std::string moveToIccsText(const Move& move, const BoardConfig& config) const;
};

std::istream& operator>>(std::istream& input, ParsedCommand& command);

} // namespace xiangqi
