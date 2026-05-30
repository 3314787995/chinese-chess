#pragma once

#include "common/Types.h"
#include "engine/ChessEngine.h"

#include <array>
#include <unordered_map>

namespace xiangqi
{

class GameSession
{
public:
    GameSession();
    explicit GameSession(GameSettings settings, PlayerInfo players = {});

    void startNewGame();

    const Board& board() const noexcept { return board_; }
    Board& board() noexcept { return board_; }
    const GameSettings& settings() const noexcept { return settings_; }
    GameSettings& settings() noexcept { return settings_; }
    const PlayerInfo& players() const noexcept { return players_; }
    Side currentSide() const noexcept { return current_side_; }
    BoardMode boardMode() const noexcept { return settings_.board_mode; }
    GameResult result() const noexcept { return result_; }
    bool gameOver() const noexcept { return result_ != GameResult::Ongoing; }
    int remainingSeconds(Side side) const noexcept;
    int remainingMilliseconds(Side side) const noexcept;

    const std::vector<Move>& history() const noexcept { return history_; }
    std::vector<Move> legalMovesForCurrentSide() const;
    std::vector<Move> legalMovesFrom(Position<int> position) const;
    bool hasAnyLegalMove(Side side) const;

    Move submitMove(const Move& candidate_move);
    void applyRemoteMove(const Move& move);
    void applyImportedResult(GameResult result, Side current_side);
    bool undoLastPly();
    int undoLastPlies(int count);
    void resign(Side side);
    void tickClock();

    std::string currentPlayerName() const;
    std::string resultText() const;
    std::string boardHashWithTurn() const;

    std::string serialize() const;
    static GameSession deserialize(const std::string& text);

private:
    void resetClockForTurn();
    void consumeCurrentTurnTime();
    void rebuildRepetitionFromHistory();
    void evaluateGameResultAfterMove(Move& move);
    Move normalizeMove(const Move& candidate_move) const;
    bool wouldViolatePerpetualCheck(const Move& legal_move) const;
    bool moveGivesChase(const Board& board_after_move, const Move& move) const;
    static std::string serializeMove(const Move& move, const BoardConfig& config);
    static Move deserializeMove(const std::string& line, const BoardConfig& config);

    GameSettings settings_{};
    PlayerInfo players_{};
    Board board_{};
    Side current_side_{ Side::Red };
    GameResult result_{ GameResult::Ongoing };
    std::vector<Move> history_;
    std::unordered_map<std::string, int> repetition_counts_;
    std::array<int, 2> remaining_time_ms_{};
    std::chrono::steady_clock::time_point turn_started_at_{};
};

} // namespace xiangqi
