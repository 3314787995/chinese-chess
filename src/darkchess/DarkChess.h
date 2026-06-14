#pragma once

#include "common/Types.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xiangqi
{

struct DarkPiece
{
    Side side{ Side::Red };
    PieceType type{ PieceType::Pawn };
    bool is_open{ false };
};

struct DarkAction
{
    DarkActionType type{ DarkActionType::Move };
    Position<int> from{};
    Position<int> to{};
    DarkSeat seat{ DarkSeat::Player1 };
    std::optional<DarkPiece> revealed_piece;
    std::optional<DarkPiece> captured_piece;
    std::string notation;
    int score_hint{ 0 };

    static DarkAction flip(Position<int> position, DarkSeat seat);
    static DarkAction move(Position<int> from, Position<int> to, DarkSeat seat);
};

class DarkBoard
{
public:
    static constexpr int kRows = 4;
    static constexpr int kCols = 8;

    DarkBoard();
    explicit DarkBoard(uint32_t seed);

    int rows() const noexcept { return kRows; }
    int cols() const noexcept { return kCols; }
    bool isInside(Position<int> position) const noexcept;
    bool isOccupied(Position<int> position) const;
    bool isOpen(Position<int> position) const;
    std::optional<DarkPiece> pieceAt(Position<int> position) const;
    int occupiedCount() const noexcept;
    int openCount() const noexcept;
    int hiddenCount() const noexcept;
    int countPieces(Side side) const noexcept;
    int countObstacles(Position<int> from, Position<int> to) const;

    void setup(uint32_t seed);
    DarkPiece reveal(Position<int> position);
    void movePiece(Position<int> from, Position<int> to);
    DarkPiece removePiece(Position<int> position);

    std::string publicGridString() const;
    std::string privateGridString() const;
    std::string hashString(bool include_hidden_identities) const;
    static DarkBoard deserialize(const std::vector<std::string>& lines);

private:
    struct Cell
    {
        bool occupied{ false };
        bool identity_known{ false };
        DarkPiece piece{};
    };

    int index(Position<int> position) const noexcept;
    Cell& cellAt(Position<int> position);
    const Cell& cellAt(Position<int> position) const;

    std::array<Cell, kRows * kCols> cells_{};
};

class DarkGameSession
{
public:
    DarkGameSession();
    explicit DarkGameSession(uint32_t seed);
    explicit DarkGameSession(GameSettings settings, PlayerInfo players = {}, uint32_t seed = 0);

    void startNewGame(uint32_t seed = 0);

    const DarkBoard& board() const noexcept { return board_; }
    DarkBoard& board() noexcept { return board_; }
    const GameSettings& settings() const noexcept { return settings_; }
    GameSettings& settings() noexcept { return settings_; }
    const PlayerInfo& players() const noexcept { return players_; }
    DarkSeat currentSeat() const noexcept { return current_seat_; }
    GameResult result() const noexcept { return result_; }
    bool gameOver() const noexcept { return result_ != GameResult::Ongoing; }
    int remainingSeconds(DarkSeat seat) const noexcept;
    int remainingMilliseconds(DarkSeat seat) const noexcept;
    int quietPlies() const noexcept { return quiet_plies_; }

    std::optional<Side> colorForSeat(DarkSeat seat) const noexcept;
    std::optional<DarkSeat> seatForColor(Side side) const noexcept;
    std::string currentPlayerName() const;
    std::string resultText() const;

    const std::vector<DarkAction>& history() const noexcept { return history_; }
    std::vector<DarkAction> legalActions(DarkSeat seat) const;
    std::vector<DarkAction> legalActionsForCurrentSeat() const;
    std::vector<DarkAction> legalActionsFrom(Position<int> position) const;
    bool hasAnyLegalAction(DarkSeat seat) const;

    DarkAction submitAction(const DarkAction& candidate_action);
    bool undoLastPly();
    int undoLastPlies(int count);
    void resign(DarkSeat seat);
    void tickClock();

    std::string serialize() const;
    std::string serializePublic() const;
    const std::string& initialPrivateGridString() const noexcept { return initial_private_grid_; }
    static DarkGameSession deserialize(const std::string& text);
    std::string boardHashWithTurn() const;

private:
    bool belongsToSeat(const DarkPiece& piece, DarkSeat seat) const;
    bool isLegalAction(const DarkAction& action) const;
    bool canEat(const DarkPiece& attacker, const DarkPiece& target) const;
    void switchTurnAndEvaluate(const DarkAction& action);
    void evaluateGameResultAfterAction();
    void resetClockForTurn();
    void consumeCurrentTurnTime();
    void rebuildPositionHistoryFromHistory();
    static std::string serializeAction(const DarkAction& action);
    static DarkAction deserializeAction(const std::string& line);

    GameSettings settings_{};
    PlayerInfo players_{};
    DarkBoard board_{};
    DarkSeat current_seat_{ DarkSeat::Player1 };
    GameResult result_{ GameResult::Ongoing };
    std::array<std::optional<Side>, 2> seat_colors_{};
    std::string initial_private_grid_;
    std::vector<DarkAction> history_;
    std::vector<std::string> undo_snapshots_;
    std::array<int, 2> remaining_time_ms_{};
    std::chrono::steady_clock::time_point turn_started_at_{};
    std::vector<std::string> position_history_;
    int quiet_plies_{ 0 };
};

class DarkSearchEngine
{
public:
    std::optional<DarkAction> chooseAction(const DarkGameSession& session) const;
};

std::string darkCoordText(Position<int> position);
std::optional<Position<int>> parseDarkCoord(const std::string& text);
std::string darkPieceToken(const DarkPiece& piece);
std::string darkActionToText(const DarkAction& action);
DarkAction parseDarkActionText(const std::string& text, const DarkGameSession& session);

} // namespace xiangqi
