#pragma once

#include "app/GameSession.h"

namespace xiangqi
{

class SearchEngine
{
public:
    explicit SearchEngine(int depth = 4);

    std::optional<Move> chooseBestMove(const GameSession& session) const;
    int evaluateBoard(const Board& board, Side perspective) const;

private:
    int depth_{ 4 };
};

} // namespace xiangqi
