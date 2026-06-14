#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace xiangqi
{

template <typename T>
struct Position
{
    T row{};
    T col{};

    bool operator==(const Position& other) const noexcept
    {
        return row == other.row && col == other.col;
    }

    bool operator!=(const Position& other) const noexcept
    {
        return !(*this == other);
    }
};

enum class BoardMode
{
    Standard9x10,
    Expanded11x10,
};

enum class GameKind
{
    Xiangqi,
    DarkChess,
};

enum class Side
{
    Red,
    Black,
};

enum class DarkSeat
{
    Player1,
    Player2,
};

enum class DarkActionType
{
    Flip,
    Move,
};

enum class PieceType
{
    King,
    Advisor,
    Elephant,
    Knight,
    Rook,
    Cannon,
    Pawn,
};

enum class GameResult
{
    Ongoing,
    RedWin,
    BlackWin,
    Draw,
    Timeout,
    Resign,
};

enum class CommandType
{
    Invalid,
    Move,
    ShowMoves,
    Undo,
    Save,
    Load,
    Resign,
    Hint,
    Replay,
    Help,
    Exit,
    Tests,
};

struct InitialPiece
{
    Side side{};
    PieceType type{};
    Position<int> position{};
};

struct BoardConfig
{
    BoardMode mode{ BoardMode::Standard9x10 };
    int rows{ 10 };
    int cols{ 9 };
    int river_split_top_row{ 4 };
    int upper_palace_min_row{ 0 };
    int upper_palace_max_row{ 2 };
    int lower_palace_min_row{ 7 };
    int lower_palace_max_row{ 9 };
    int palace_min_col{ 3 };
    int palace_max_col{ 5 };
    std::string coordinate_files{ "abcdefghi" };
    std::vector<InitialPiece> initial_pieces;
};

struct Move
{
    Position<int> from{};
    Position<int> to{};
    Side side{ Side::Red };
    PieceType piece_type{ PieceType::Pawn };
    std::optional<PieceType> captured_type;
    std::optional<Side> captured_side;
    bool is_check{ false };
    bool is_mate{ false };
    std::string notation;
    int score_hint{ 0 };
};

struct ParsedCommand
{
    CommandType type{ CommandType::Invalid };
    std::string raw_text;
    std::optional<Move> move;
    std::optional<Position<int>> position;
    std::string argument;
};

struct PlayerInfo
{
    std::string red_name{ "Red" };
    std::string black_name{ "Black" };
};

struct GameSettings
{
    GameKind game_kind{ GameKind::Xiangqi };
    BoardMode board_mode{ BoardMode::Standard9x10 };
    int move_time_limit_seconds{ 60 };
    bool allow_undo{ true };
    bool show_legal_moves{ true };
    bool ai_enabled{ false };
    Side ai_side{ Side::Black };
    DarkSeat dark_ai_seat{ DarkSeat::Player2 };
    int ai_depth{ 4 };
    bool use_easyx{ false };
};

class InputError : public std::runtime_error
{
public:
    explicit InputError(const std::string& message) : std::runtime_error(message) {}
};

class IllegalMoveError : public std::runtime_error
{
public:
    explicit IllegalMoveError(const std::string& message) : std::runtime_error(message) {}
};

class StorageError : public std::runtime_error
{
public:
    explicit StorageError(const std::string& message) : std::runtime_error(message) {}
};

class NetworkError : public std::runtime_error
{
public:
    explicit NetworkError(const std::string& message) : std::runtime_error(message) {}
};

class ResourceError : public std::runtime_error
{
public:
    explicit ResourceError(const std::string& message) : std::runtime_error(message) {}
};

inline Side opposite(const Side side) noexcept
{
    return side == Side::Red ? Side::Black : Side::Red;
}

inline DarkSeat opposite(const DarkSeat seat) noexcept
{
    return seat == DarkSeat::Player1 ? DarkSeat::Player2 : DarkSeat::Player1;
}

inline int sideIndex(const Side side) noexcept
{
    return side == Side::Red ? 0 : 1;
}

inline int seatIndex(const DarkSeat seat) noexcept
{
    return seat == DarkSeat::Player1 ? 0 : 1;
}

inline std::string toString(const Side side)
{
    return side == Side::Red ? "Red" : "Black";
}

inline std::string toString(const BoardMode mode)
{
    return mode == BoardMode::Standard9x10 ? "Standard9x10" : "Expanded11x10";
}

inline std::string toString(const GameKind kind)
{
    return kind == GameKind::DarkChess ? "DarkChess" : "Xiangqi";
}

inline std::string toString(const DarkSeat seat)
{
    return seat == DarkSeat::Player1 ? "Player1" : "Player2";
}

inline std::string toString(const GameResult result)
{
    switch (result)
    {
    case GameResult::Ongoing:
        return "Ongoing";
    case GameResult::RedWin:
        return "RedWin";
    case GameResult::BlackWin:
        return "BlackWin";
    case GameResult::Draw:
        return "Draw";
    case GameResult::Timeout:
        return "Timeout";
    case GameResult::Resign:
        return "Resign";
    default:
        return "Unknown";
    }
}

inline std::string toString(const PieceType piece_type)
{
    switch (piece_type)
    {
    case PieceType::King:
        return "King";
    case PieceType::Advisor:
        return "Advisor";
    case PieceType::Elephant:
        return "Elephant";
    case PieceType::Knight:
        return "Knight";
    case PieceType::Rook:
        return "Rook";
    case PieceType::Cannon:
        return "Cannon";
    case PieceType::Pawn:
        return "Pawn";
    default:
        return "Unknown";
    }
}

inline char pieceLetter(const PieceType piece_type)
{
    switch (piece_type)
    {
    case PieceType::King:
        return 'K';
    case PieceType::Advisor:
        return 'A';
    case PieceType::Elephant:
        return 'E';
    case PieceType::Knight:
        return 'H';
    case PieceType::Rook:
        return 'R';
    case PieceType::Cannon:
        return 'C';
    case PieceType::Pawn:
        return 'P';
    default:
        return '?';
    }
}

inline std::string boardModeCoordinateFiles(const BoardMode mode)
{
    return mode == BoardMode::Standard9x10 ? "abcdefghi" : "abcdefghijk";
}

inline bool parseBool(const std::string& value)
{
    return value == "1" || value == "true" || value == "True" || value == "TRUE";
}

} // namespace xiangqi
