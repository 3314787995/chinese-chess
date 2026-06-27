#include "ai/SearchEngine.h"

#include "common/Repetition.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <tuple>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace xiangqi
{

namespace
{

using SearchClock = std::chrono::steady_clock;

constexpr int kMateScore = 20000000;
constexpr int kMateThreshold = 19000000;
constexpr int kSearchInf = 30000000;
constexpr int kMaxBoardSquares = 121;
constexpr int kMaxMoveCount = 256;
constexpr int kMaxSearchDepth = 24;
constexpr int kMaxQuiescenceDepth = 8;
constexpr int kMaxPathStates = 4096;
constexpr int kHistoryTableSize = 2 * kMaxBoardSquares * kMaxBoardSquares;
constexpr int kPhaseMax = 82;
constexpr size_t kTTSize = 1u << 20;
constexpr size_t kTTMask = kTTSize - 1;

enum BoundType : uint8_t
{
    BoundNone = 0,
    BoundExact = 1,
    BoundLower = 2,
    BoundUpper = 3,
};

enum SearchPiece : int8_t
{
    EmptyPiece = 0,
    RedKing = 1,
    RedAdvisor = 2,
    RedElephant = 3,
    RedKnight = 4,
    RedRook = 5,
    RedCannon = 6,
    RedPawn = 7,
    BlackKing = -1,
    BlackAdvisor = -2,
    BlackElephant = -3,
    BlackKnight = -4,
    BlackRook = -5,
    BlackCannon = -6,
    BlackPawn = -7,
};

constexpr uint8_t kFlagCheck = 0x01;

class SearchTimeout final : public std::exception
{
public:
    const char* what() const noexcept override
    {
        return "Search timed out.";
    }
};

struct SearchMove
{
    int from{ -1 };
    int to{ -1 };
    int8_t moving_piece{ EmptyPiece };
    int8_t captured_piece{ EmptyPiece };
    uint8_t flags{ 0 };

    bool isValid() const noexcept
    {
        return from >= 0 && to >= 0 && moving_piece != EmptyPiece;
    }

    bool isCapture() const noexcept
    {
        return captured_piece != EmptyPiece;
    }

    bool givesCheck() const noexcept
    {
        return (flags & kFlagCheck) != 0;
    }
};

struct UndoState
{
    int8_t captured_piece{ EmptyPiece };
    std::array<int, 2> previous_kings{ { -1, -1 } };
    int previous_phase{ 0 };
    uint64_t previous_hash{ 0 };
};

struct TTEntry
{
    uint64_t key{ 0 };
    SearchMove best_move{};
    int score{ 0 };
    int depth{ -1 };
    uint8_t bound{ BoundNone };
    uint8_t age{ 0 };
};

struct SearchStats
{
    size_t nodes{ 0 };
    size_t qnodes{ 0 };
    size_t tt_probes{ 0 };
    size_t tt_hits{ 0 };
    size_t nmp_cutoffs{ 0 };
    size_t lmr_reductions{ 0 };
};

struct SearchStack
{
    std::array<SearchMove, 2> killers{};
};

template <typename T, size_t N>
struct MoveBuffer
{
    std::array<T, N> items{};
    size_t count{ 0 };

    void clear() noexcept
    {
        count = 0;
    }

    bool empty() const noexcept
    {
        return count == 0;
    }

    size_t size() const noexcept
    {
        return count;
    }

    T& operator[](const size_t index) noexcept
    {
        return items[index];
    }

    const T& operator[](const size_t index) const noexcept
    {
        return items[index];
    }

    void push(const T& value) noexcept
    {
        if (count < N)
        {
            items[count++] = value;
        }
    }

    auto begin() noexcept
    {
        return items.begin();
    }

    auto end() noexcept
    {
        return items.begin() + static_cast<std::ptrdiff_t>(count);
    }

    auto begin() const noexcept
    {
        return items.begin();
    }

    auto end() const noexcept
    {
        return items.begin() + static_cast<std::ptrdiff_t>(count);
    }
};

struct ScoredMove
{
    SearchMove move{};
    int order_score{ std::numeric_limits<int>::min() };
};

struct SearchContext
{
    SearchClock::time_point deadline{};
    std::array<int, kHistoryTableSize> history{};
    std::array<SearchStack, kMaxSearchDepth + 4> stack{};
    std::array<RepetitionState, kMaxPathStates> path_states{};
    std::array<RepetitionPlyInfo, kMaxPathStates> path_plies{};
    int root_path_index{ 0 };
    uint8_t age{ 0 };
    SearchStats stats{};
};

struct EvalTally
{
    int opening{ 0 };
    int endgame{ 0 };
};

struct SearchBoard;

const std::array<uint64_t, 2>& zobristSideKeys();
int8_t encodePiece(Side side, PieceType piece_type) noexcept;
int pieceAbs(int8_t piece) noexcept;
Side pieceSide(int8_t piece) noexcept;
PieceType pieceTypeFromCode(int8_t piece) noexcept;
int phaseWeight(int piece_abs) noexcept;
uint64_t zobristPiece(int8_t piece, int square) noexcept;
int evaluateSearchBoard(const SearchBoard& board, Side perspective);

struct SearchBoard
{
    BoardMode mode{ BoardMode::Standard9x10 };
    int rows{ 10 };
    int cols{ 9 };
    int palace_min_col{ 3 };
    int palace_max_col{ 5 };
    int river_split_top_row{ 4 };
    int square_count{ 90 };
    std::array<int8_t, kMaxBoardSquares> cells{};
    std::array<int, 2> king_index{ { -1, -1 } };
    int phase{ 0 };
    uint64_t hash{ 0 };

    SearchBoard()
    {
        setup(makeBoardConfig(BoardMode::Standard9x10));
    }

    explicit SearchBoard(const BoardMode board_mode)
    {
        setup(makeBoardConfig(board_mode));
    }

    explicit SearchBoard(const Board& board)
    {
        fromBoard(board);
    }

    void setup(const BoardConfig& config)
    {
        mode = config.mode;
        rows = config.rows;
        cols = config.cols;
        palace_min_col = config.palace_min_col;
        palace_max_col = config.palace_max_col;
        river_split_top_row = config.river_split_top_row;
        square_count = rows * cols;
        cells.fill(EmptyPiece);
        king_index = { { -1, -1 } };
        phase = 0;
        hash = 0;

        for (const auto& initial_piece : config.initial_pieces)
        {
            placePiece(index(initial_piece.position.row, initial_piece.position.col), encodePiece(initial_piece.side, initial_piece.type));
        }
    }

    void fromBoard(const Board& board)
    {
        mode = board.config().mode;
        rows = board.config().rows;
        cols = board.config().cols;
        palace_min_col = board.config().palace_min_col;
        palace_max_col = board.config().palace_max_col;
        river_split_top_row = board.config().river_split_top_row;
        square_count = rows * cols;
        cells.fill(EmptyPiece);
        king_index = { { -1, -1 } };
        phase = 0;
        hash = 0;

        for (int row = 0; row < rows; ++row)
        {
            for (int col = 0; col < cols; ++col)
            {
                const auto* piece = board.pieceAt({ row, col });
                if (piece != nullptr)
                {
                    placePiece(index(row, col), encodePiece(piece->side(), piece->type()));
                }
            }
        }
    }

    int index(const int row, const int col) const noexcept
    {
        return row * cols + col;
    }

    int rowOf(const int square) const noexcept
    {
        return square / cols;
    }

    int colOf(const int square) const noexcept
    {
        return square % cols;
    }

    bool inside(const int row, const int col) const noexcept
    {
        return row >= 0 && row < rows && col >= 0 && col < cols;
    }

    bool isPalaceCell(const Side side, const int row, const int col) const noexcept
    {
        if (!inside(row, col) || col < palace_min_col || col > palace_max_col)
        {
            return false;
        }

        if (side == Side::Black)
        {
            return row >= 0 && row <= 2;
        }
        return row >= rows - 3 && row < rows;
    }

    bool crossedRiver(const Side side, const int row) const noexcept
    {
        return side == Side::Red ? row <= river_split_top_row : row > river_split_top_row;
    }

    int8_t pieceAt(const int square) const noexcept
    {
        return cells[static_cast<size_t>(square)];
    }

    bool hasKing(const Side side) const noexcept
    {
        return king_index[sideIndex(side)] >= 0;
    }

    uint64_t positionKey(const Side side_to_move) const noexcept
    {
        return hash ^ zobristSideKeys()[static_cast<size_t>(sideIndex(side_to_move))];
    }

    bool kingsFacing() const noexcept
    {
        if (!hasKing(Side::Red) || !hasKing(Side::Black))
        {
            return false;
        }

        const int red_square = king_index[0];
        const int black_square = king_index[1];
        if (colOf(red_square) != colOf(black_square))
        {
            return false;
        }

        const int file = colOf(red_square);
        const int min_row = std::min(rowOf(red_square), rowOf(black_square)) + 1;
        const int max_row = std::max(rowOf(red_square), rowOf(black_square));
        for (int row = min_row; row < max_row; ++row)
        {
            if (pieceAt(index(row, file)) != EmptyPiece)
            {
                return false;
            }
        }
        return true;
    }

    bool isSquareAttacked(const int target_square, const Side by_side) const noexcept
    {
        const int row = rowOf(target_square);
        const int col = colOf(target_square);

        const int pawn_forward_source_row = by_side == Side::Red ? row + 1 : row - 1;
        if (inside(pawn_forward_source_row, col))
        {
            const int8_t piece = pieceAt(index(pawn_forward_source_row, col));
            if (piece == encodePiece(by_side, PieceType::Pawn))
            {
                return true;
            }
        }

        if (inside(row, col - 1))
        {
            const int8_t piece = pieceAt(index(row, col - 1));
            if (piece == encodePiece(by_side, PieceType::Pawn) && crossedRiver(by_side, row))
            {
                return true;
            }
        }
        if (inside(row, col + 1))
        {
            const int8_t piece = pieceAt(index(row, col + 1));
            if (piece == encodePiece(by_side, PieceType::Pawn) && crossedRiver(by_side, row))
            {
                return true;
            }
        }

        static const std::array<std::array<int, 4>, 8> knight_sources{
            std::array<int, 4>{ -2, -1, -1, 0 },
            std::array<int, 4>{ -2, 1, -1, 0 },
            std::array<int, 4>{ -1, -2, 0, -1 },
            std::array<int, 4>{ -1, 2, 0, 1 },
            std::array<int, 4>{ 1, -2, 0, -1 },
            std::array<int, 4>{ 1, 2, 0, 1 },
            std::array<int, 4>{ 2, -1, 1, 0 },
            std::array<int, 4>{ 2, 1, 1, 0 },
        };
        for (const auto& source : knight_sources)
        {
            const int source_row = row + source[0];
            const int source_col = col + source[1];
            const int leg_row = row + source[2];
            const int leg_col = col + source[3];
            if (!inside(source_row, source_col) || !inside(leg_row, leg_col))
            {
                continue;
            }
            if (pieceAt(index(leg_row, leg_col)) != EmptyPiece)
            {
                continue;
            }
            if (pieceAt(index(source_row, source_col)) == encodePiece(by_side, PieceType::Knight))
            {
                return true;
            }
        }

        static const std::array<std::array<int, 2>, 4> advisor_sources{
            std::array<int, 2>{ -1, -1 },
            std::array<int, 2>{ -1, 1 },
            std::array<int, 2>{ 1, -1 },
            std::array<int, 2>{ 1, 1 },
        };
        for (const auto& source : advisor_sources)
        {
            const int source_row = row + source[0];
            const int source_col = col + source[1];
            if (!inside(source_row, source_col))
            {
                continue;
            }
            if (pieceAt(index(source_row, source_col)) == encodePiece(by_side, PieceType::Advisor) &&
                isPalaceCell(by_side, source_row, source_col))
            {
                return true;
            }
        }

        static const std::array<std::array<int, 4>, 4> elephant_sources{
            std::array<int, 4>{ -2, -2, -1, -1 },
            std::array<int, 4>{ -2, 2, -1, 1 },
            std::array<int, 4>{ 2, -2, 1, -1 },
            std::array<int, 4>{ 2, 2, 1, 1 },
        };
        for (const auto& source : elephant_sources)
        {
            const int source_row = row + source[0];
            const int source_col = col + source[1];
            const int eye_row = row + source[2];
            const int eye_col = col + source[3];
            if (!inside(source_row, source_col) || !inside(eye_row, eye_col))
            {
                continue;
            }
            if (pieceAt(index(eye_row, eye_col)) != EmptyPiece)
            {
                continue;
            }
            if (pieceAt(index(source_row, source_col)) == encodePiece(by_side, PieceType::Elephant) &&
                !crossedRiver(by_side, source_row))
            {
                return true;
            }
        }

        static const std::array<std::array<int, 2>, 4> king_sources{
            std::array<int, 2>{ -1, 0 },
            std::array<int, 2>{ 1, 0 },
            std::array<int, 2>{ 0, -1 },
            std::array<int, 2>{ 0, 1 },
        };
        for (const auto& source : king_sources)
        {
            const int source_row = row + source[0];
            const int source_col = col + source[1];
            if (!inside(source_row, source_col))
            {
                continue;
            }
            if (pieceAt(index(source_row, source_col)) == encodePiece(by_side, PieceType::King) &&
                isPalaceCell(by_side, source_row, source_col))
            {
                return true;
            }
        }

        static const std::array<std::array<int, 2>, 4> directions{
            std::array<int, 2>{ -1, 0 },
            std::array<int, 2>{ 1, 0 },
            std::array<int, 2>{ 0, -1 },
            std::array<int, 2>{ 0, 1 },
        };
        for (const auto& direction : directions)
        {
            int current_row = row + direction[0];
            int current_col = col + direction[1];
            bool found_screen = false;
            while (inside(current_row, current_col))
            {
                const int current_square = index(current_row, current_col);
                const int8_t piece = pieceAt(current_square);
                if (piece == EmptyPiece)
                {
                    current_row += direction[0];
                    current_col += direction[1];
                    continue;
                }

                if (!found_screen)
                {
                    if (piece == encodePiece(by_side, PieceType::Rook))
                    {
                        return true;
                    }
                    found_screen = true;
                }
                else
                {
                    if (piece == encodePiece(by_side, PieceType::Cannon))
                    {
                        return true;
                    }
                    break;
                }

                current_row += direction[0];
                current_col += direction[1];
            }
        }

        return false;
    }

    bool isInCheck(const Side side) const noexcept
    {
        return !hasKing(side) || kingsFacing() || isSquareAttacked(king_index[sideIndex(side)], opposite(side));
    }

    bool hasNonPawnMaterial(const Side side) const noexcept
    {
        for (int square = 0; square < square_count; ++square)
        {
            const int8_t piece = pieceAt(square);
            if (piece == EmptyPiece || pieceSide(piece) != side)
            {
                continue;
            }

            const int piece_type = pieceAbs(piece);
            if (piece_type != pieceAbs(encodePiece(side, PieceType::King)) &&
                piece_type != pieceAbs(encodePiece(side, PieceType::Pawn)))
            {
                return true;
            }
        }
        return false;
    }

    int countObstacles(const int from_square, const int to_square) const noexcept
    {
        const int from_row = rowOf(from_square);
        const int from_col = colOf(from_square);
        const int to_row = rowOf(to_square);
        const int to_col = colOf(to_square);

        if (from_row != to_row && from_col != to_col)
        {
            return 0;
        }

        const int row_step = to_row == from_row ? 0 : (to_row > from_row ? 1 : -1);
        const int col_step = to_col == from_col ? 0 : (to_col > from_col ? 1 : -1);

        int obstacles = 0;
        int current_row = from_row + row_step;
        int current_col = from_col + col_step;
        while (current_row != to_row || current_col != to_col)
        {
            if (pieceAt(index(current_row, current_col)) != EmptyPiece)
            {
                ++obstacles;
            }
            current_row += row_step;
            current_col += col_step;
        }
        return obstacles;
    }

    void makeMove(SearchMove& move, UndoState& undo) noexcept
    {
        undo.previous_hash = hash;
        undo.previous_phase = phase;
        undo.previous_kings = king_index;

        if (move.moving_piece == EmptyPiece)
        {
            move.moving_piece = pieceAt(move.from);
        }
        undo.captured_piece = pieceAt(move.to);
        move.captured_piece = undo.captured_piece;

        cells[static_cast<size_t>(move.from)] = EmptyPiece;
        cells[static_cast<size_t>(move.to)] = move.moving_piece;

        if (move.moving_piece != EmptyPiece)
        {
            hash ^= zobristPiece(move.moving_piece, move.from);
            hash ^= zobristPiece(move.moving_piece, move.to);
        }
        if (undo.captured_piece != EmptyPiece)
        {
            hash ^= zobristPiece(undo.captured_piece, move.to);
            phase -= phaseWeight(pieceAbs(undo.captured_piece));
            if (pieceTypeFromCode(undo.captured_piece) == PieceType::King)
            {
                king_index[sideIndex(pieceSide(undo.captured_piece))] = -1;
            }
        }
        if (pieceTypeFromCode(move.moving_piece) == PieceType::King)
        {
            king_index[sideIndex(pieceSide(move.moving_piece))] = move.to;
        }
    }

    void unmakeMove(const SearchMove& move, const UndoState& undo) noexcept
    {
        cells[static_cast<size_t>(move.from)] = move.moving_piece;
        cells[static_cast<size_t>(move.to)] = undo.captured_piece;
        king_index = undo.previous_kings;
        phase = undo.previous_phase;
        hash = undo.previous_hash;
    }

private:
    void placePiece(const int square, const int8_t piece) noexcept
    {
        cells[static_cast<size_t>(square)] = piece;
        if (piece == EmptyPiece)
        {
            return;
        }

        hash ^= zobristPiece(piece, square);
        phase += phaseWeight(pieceAbs(piece));
        if (pieceTypeFromCode(piece) == PieceType::King)
        {
            king_index[sideIndex(pieceSide(piece))] = square;
        }
    }
};

struct RootSearchResult
{
    SearchMove best_move{};
    int best_score{ -kSearchInf };
    int completed_depth{ 0 };
};

int timeBudgetMsForTier(int tier)
{
    tier = std::clamp(tier, 1, 5);
    switch (tier)
    {
    case 1:
        return 600;
    case 2:
        return 1200;
    case 3:
        return 2000;
    case 4:
        return 2600;
    case 5:
        return 3400;
    default:
        return 2600;
    }
}

uint64_t splitmix64(uint64_t& seed) noexcept
{
    uint64_t value = (seed += 0x9e3779b97f4a7c15ULL);
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

const std::array<std::array<uint64_t, kMaxBoardSquares>, 14>& zobristPieces()
{
    static const auto table = []()
    {
        std::array<std::array<uint64_t, kMaxBoardSquares>, 14> result{};
        uint64_t seed = 0xC0DEC0DEC0DEC0DEULL;
        for (auto& piece_row : result)
        {
            for (auto& square_key : piece_row)
            {
                square_key = splitmix64(seed);
            }
        }
        return result;
    }();

    return table;
}

const std::array<uint64_t, 2>& zobristSideKeys()
{
    static const auto keys = []()
    {
        std::array<uint64_t, 2> result{};
        uint64_t seed = 0x1234FEDCBA987654ULL;
        result[0] = splitmix64(seed);
        result[1] = splitmix64(seed);
        return result;
    }();

    return keys;
}

auto& transpositionTable()
{
    static std::vector<TTEntry> table(kTTSize);
    return table;
}

uint8_t nextSearchAge() noexcept
{
    static uint8_t age = 0;
    ++age;
    if (age == 0)
    {
        ++age;
    }
    return age;
}

int8_t encodePiece(const Side side, const PieceType piece_type) noexcept
{
    const int8_t sign = side == Side::Red ? 1 : -1;
    switch (piece_type)
    {
    case PieceType::King:
        return static_cast<int8_t>(sign * 1);
    case PieceType::Advisor:
        return static_cast<int8_t>(sign * 2);
    case PieceType::Elephant:
        return static_cast<int8_t>(sign * 3);
    case PieceType::Knight:
        return static_cast<int8_t>(sign * 4);
    case PieceType::Rook:
        return static_cast<int8_t>(sign * 5);
    case PieceType::Cannon:
        return static_cast<int8_t>(sign * 6);
    case PieceType::Pawn:
        return static_cast<int8_t>(sign * 7);
    default:
        return EmptyPiece;
    }
}

int pieceAbs(const int8_t piece) noexcept
{
    return piece >= 0 ? piece : -piece;
}

Side pieceSide(const int8_t piece) noexcept
{
    return piece >= 0 ? Side::Red : Side::Black;
}

PieceType pieceTypeFromCode(const int8_t piece) noexcept
{
    switch (pieceAbs(piece))
    {
    case 1:
        return PieceType::King;
    case 2:
        return PieceType::Advisor;
    case 3:
        return PieceType::Elephant;
    case 4:
        return PieceType::Knight;
    case 5:
        return PieceType::Rook;
    case 6:
        return PieceType::Cannon;
    case 7:
        return PieceType::Pawn;
    default:
        return PieceType::Pawn;
    }
}

int pieceAverageValue(const int8_t piece) noexcept
{
    if (piece == EmptyPiece)
    {
        return 0;
    }

    switch (pieceTypeFromCode(piece))
    {
    case PieceType::King:
        return 100000;
    case PieceType::Rook:
        return 955;
    case PieceType::Cannon:
        return 490;
    case PieceType::Knight:
        return 455;
    case PieceType::Advisor:
        return 145;
    case PieceType::Elephant:
        return 135;
    case PieceType::Pawn:
        return 155;
    default:
        return 0;
    }
}

int pieceZobristIndex(const int8_t piece) noexcept
{
    return piece > 0 ? piece - 1 : 7 + (-piece - 1);
}

uint64_t zobristPiece(const int8_t piece, const int square) noexcept
{
    return zobristPieces()[static_cast<size_t>(pieceZobristIndex(piece))][static_cast<size_t>(square)];
}

int phaseWeight(const int piece_abs) noexcept
{
    switch (piece_abs)
    {
    case 1:
        return 0;
    case 2:
    case 3:
    case 7:
        return 1;
    case 4:
    case 6:
        return 4;
    case 5:
        return 8;
    default:
        return 0;
    }
}

bool sameMove(const SearchMove& left, const SearchMove& right) noexcept
{
    return left.from == right.from && left.to == right.to && left.moving_piece == right.moving_piece;
}

bool sameMove(const Move& left, const SearchMove& right, const SearchBoard& board) noexcept
{
    return board.index(left.from.row, left.from.col) == right.from &&
           board.index(left.to.row, left.to.col) == right.to &&
           encodePiece(left.side, left.piece_type) == right.moving_piece;
}

Move toPublicMove(const SearchBoard& board, const SearchMove& move, const int score_hint)
{
    Move public_move;
    public_move.from = { board.rowOf(move.from), board.colOf(move.from) };
    public_move.to = { board.rowOf(move.to), board.colOf(move.to) };
    public_move.side = pieceSide(move.moving_piece);
    public_move.piece_type = pieceTypeFromCode(move.moving_piece);
    if (move.captured_piece != EmptyPiece)
    {
        public_move.captured_type = pieceTypeFromCode(move.captured_piece);
        public_move.captured_side = pieceSide(move.captured_piece);
    }
    public_move.is_check = move.givesCheck();
    public_move.score_hint = score_hint;
    return public_move;
}

SearchMove toSearchMove(const SearchBoard& board, const Move& move)
{
    SearchMove search_move;
    search_move.from = board.index(move.from.row, move.from.col);
    search_move.to = board.index(move.to.row, move.to.col);
    search_move.moving_piece = encodePiece(move.side, move.piece_type);
    if (move.captured_type.has_value() && move.captured_side.has_value())
    {
        search_move.captured_piece = encodePiece(*move.captured_side, *move.captured_type);
    }
    if (move.is_check)
    {
        search_move.flags |= kFlagCheck;
    }
    return search_move;
}

int fallbackMoveScore(SearchBoard& board, SearchMove move, const Side root_side)
{
    UndoState undo;
    board.makeMove(move, undo);

    int score = 0;
    if (!board.hasKing(opposite(root_side)))
    {
        score = kMateScore / 2;
    }
    else
    {
        score = evaluateSearchBoard(board, root_side);
        if (move.givesCheck() || board.isInCheck(opposite(root_side)))
        {
            score += 220;
        }
        if (move.isCapture())
        {
            score += pieceAverageValue(move.captured_piece);
        }
    }

    board.unmakeMove(move, undo);
    return score;
}

uint64_t boardHashKey(const Board& board, const Side side_to_move)
{
    return static_cast<uint64_t>(std::hash<std::string>{}(board.hashString() + "|" + toString(side_to_move)));
}

void touchNode(SearchContext& context, const bool quiescence_node)
{
    if (quiescence_node)
    {
        ++context.stats.qnodes;
    }
    else
    {
        ++context.stats.nodes;
    }

    const size_t total = context.stats.nodes + context.stats.qnodes;
    if ((total & 2047U) == 0U && SearchClock::now() >= context.deadline)
    {
        throw SearchTimeout();
    }
}

int historyIndex(const Side side, const SearchMove& move) noexcept
{
    return sideIndex(side) * kMaxBoardSquares * kMaxBoardSquares +
           move.from * kMaxBoardSquares +
           move.to;
}

int historyScore(const SearchContext& context, const Side side, const SearchMove& move) noexcept
{
    return context.history[static_cast<size_t>(historyIndex(side, move))];
}

void ageHistory(SearchContext& context) noexcept
{
    for (auto& slot : context.history)
    {
        slot /= 2;
    }
}

void recordHistory(SearchContext& context, const Side side, const SearchMove& move, const int depth) noexcept
{
    auto& slot = context.history[static_cast<size_t>(historyIndex(side, move))];
    slot = std::min(2'000'000, slot + depth * depth * 24);
}

void recordKiller(SearchContext& context, const int ply, const SearchMove& move) noexcept
{
    if (ply < 0 || ply >= static_cast<int>(context.stack.size()))
    {
        return;
    }

    auto& killers = context.stack[static_cast<size_t>(ply)].killers;
    if (sameMove(move, killers[0]))
    {
        return;
    }

    killers[1] = killers[0];
    killers[0] = move;
}

int mateToTt(const int score, const int ply) noexcept
{
    if (score >= kMateThreshold)
    {
        return score + ply;
    }
    if (score <= -kMateThreshold)
    {
        return score - ply;
    }
    return score;
}

int mateFromTt(const int score, const int ply) noexcept
{
    if (score >= kMateThreshold)
    {
        return score - ply;
    }
    if (score <= -kMateThreshold)
    {
        return score + ply;
    }
    return score;
}

const TTEntry* probeTt(SearchContext& context, const uint64_t key)
{
    ++context.stats.tt_probes;
    const auto& table = transpositionTable();
    const TTEntry& entry = table[key & kTTMask];
    if (entry.key != key)
    {
        return nullptr;
    }

    ++context.stats.tt_hits;
    return &entry;
}

void storeTt(
    SearchContext& context,
    const uint64_t key,
    const int depth,
    const int ply,
    const int score,
    const uint8_t bound,
    const SearchMove& best_move)
{
    auto& table = transpositionTable();
    TTEntry& entry = table[key & kTTMask];
    if (entry.key != 0 && entry.key != key && entry.depth > depth && entry.age == context.age)
    {
        return;
    }

    entry.key = key;
    entry.best_move = best_move;
    entry.depth = depth;
    entry.score = mateToTt(score, ply);
    entry.bound = bound;
    entry.age = context.age;
}

int progressTowardsEnemy(const SearchBoard& board, const Side side, const int square) noexcept
{
    const int row = board.rowOf(square);
    return side == Side::Red ? (board.rows - 1 - row) : row;
}

int centralityBonus(const SearchBoard& board, const int square, const PieceType piece_type) noexcept
{
    const int col = board.colOf(square);
    const int center_twice = board.cols - 1;
    const int file_distance_twice = std::abs(col * 2 - center_twice);
    const int raw = std::max(0, board.cols + 1 - file_distance_twice);

    switch (piece_type)
    {
    case PieceType::Knight:
        return raw * 6;
    case PieceType::Cannon:
        return raw * 5;
    case PieceType::Rook:
        return raw * 4;
    case PieceType::Pawn:
        return raw * 3;
    case PieceType::King:
        return raw * 2;
    default:
        return raw;
    }
}

int pawnAdvanceBonus(const SearchBoard& board, const Side side, const int square) noexcept
{
    const int row = board.rowOf(square);
    const int progress = progressTowardsEnemy(board, side, square);
    int bonus = progress * 7;
    if (board.crossedRiver(side, row))
    {
        bonus += 55 + progress * 8;
    }

    const int enemy_home_row = side == Side::Red ? 0 : board.rows - 1;
    if (row == enemy_home_row)
    {
        bonus += 45;
    }
    return bonus;
}

int mobilityCount(const SearchBoard& board, const int square, const int8_t piece)
{
    const Side side = pieceSide(piece);
    const PieceType piece_type = pieceTypeFromCode(piece);
    const int row = board.rowOf(square);
    const int col = board.colOf(square);
    int count = 0;

    auto count_if_target = [&](const int target_row, const int target_col)
    {
        if (!board.inside(target_row, target_col))
        {
            return;
        }
        const int8_t target_piece = board.pieceAt(board.index(target_row, target_col));
        if (target_piece == EmptyPiece || pieceSide(target_piece) != side)
        {
            ++count;
        }
    };

    switch (piece_type)
    {
    case PieceType::King:
    {
        static const std::array<std::array<int, 2>, 4> directions{
            std::array<int, 2>{ -1, 0 },
            std::array<int, 2>{ 1, 0 },
            std::array<int, 2>{ 0, -1 },
            std::array<int, 2>{ 0, 1 },
        };
        for (const auto& direction : directions)
        {
            const int target_row = row + direction[0];
            const int target_col = col + direction[1];
            if (board.isPalaceCell(side, target_row, target_col))
            {
                count_if_target(target_row, target_col);
            }
        }
        break;
    }
    case PieceType::Advisor:
    {
        static const std::array<std::array<int, 2>, 4> directions{
            std::array<int, 2>{ -1, -1 },
            std::array<int, 2>{ -1, 1 },
            std::array<int, 2>{ 1, -1 },
            std::array<int, 2>{ 1, 1 },
        };
        for (const auto& direction : directions)
        {
            const int target_row = row + direction[0];
            const int target_col = col + direction[1];
            if (board.isPalaceCell(side, target_row, target_col))
            {
                count_if_target(target_row, target_col);
            }
        }
        break;
    }
    case PieceType::Elephant:
    {
        static const std::array<std::array<int, 4>, 4> directions{
            std::array<int, 4>{ -2, -2, -1, -1 },
            std::array<int, 4>{ -2, 2, -1, 1 },
            std::array<int, 4>{ 2, -2, 1, -1 },
            std::array<int, 4>{ 2, 2, 1, 1 },
        };
        for (const auto& direction : directions)
        {
            const int target_row = row + direction[0];
            const int target_col = col + direction[1];
            const int eye_row = row + direction[2];
            const int eye_col = col + direction[3];
            if (!board.inside(target_row, target_col) || !board.inside(eye_row, eye_col))
            {
                continue;
            }
            if (board.pieceAt(board.index(eye_row, eye_col)) != EmptyPiece)
            {
                continue;
            }
            if (board.crossedRiver(side, target_row))
            {
                continue;
            }
            count_if_target(target_row, target_col);
        }
        break;
    }
    case PieceType::Knight:
    {
        static const std::array<std::array<int, 4>, 8> jumps{
            std::array<int, 4>{ -2, -1, -1, 0 },
            std::array<int, 4>{ -2, 1, -1, 0 },
            std::array<int, 4>{ -1, -2, 0, -1 },
            std::array<int, 4>{ -1, 2, 0, 1 },
            std::array<int, 4>{ 1, -2, 0, -1 },
            std::array<int, 4>{ 1, 2, 0, 1 },
            std::array<int, 4>{ 2, -1, 1, 0 },
            std::array<int, 4>{ 2, 1, 1, 0 },
        };
        for (const auto& jump : jumps)
        {
            const int target_row = row + jump[0];
            const int target_col = col + jump[1];
            const int leg_row = row + jump[2];
            const int leg_col = col + jump[3];
            if (!board.inside(target_row, target_col) || !board.inside(leg_row, leg_col))
            {
                continue;
            }
            if (board.pieceAt(board.index(leg_row, leg_col)) != EmptyPiece)
            {
                continue;
            }
            count_if_target(target_row, target_col);
        }
        break;
    }
    case PieceType::Rook:
    case PieceType::Cannon:
    {
        static const std::array<std::array<int, 2>, 4> directions{
            std::array<int, 2>{ -1, 0 },
            std::array<int, 2>{ 1, 0 },
            std::array<int, 2>{ 0, -1 },
            std::array<int, 2>{ 0, 1 },
        };
        for (const auto& direction : directions)
        {
            int current_row = row + direction[0];
            int current_col = col + direction[1];
            bool found_screen = false;
            while (board.inside(current_row, current_col))
            {
                const int8_t target_piece = board.pieceAt(board.index(current_row, current_col));
                if (piece_type == PieceType::Rook)
                {
                    if (target_piece == EmptyPiece)
                    {
                        ++count;
                    }
                    else
                    {
                        if (pieceSide(target_piece) != side)
                        {
                            ++count;
                        }
                        break;
                    }
                }
                else if (!found_screen)
                {
                    if (target_piece == EmptyPiece)
                    {
                        ++count;
                    }
                    else
                    {
                        found_screen = true;
                    }
                }
                else if (target_piece != EmptyPiece)
                {
                    if (pieceSide(target_piece) != side)
                    {
                        ++count;
                    }
                    break;
                }

                current_row += direction[0];
                current_col += direction[1];
            }
        }
        break;
    }
    case PieceType::Pawn:
    {
        const int forward = side == Side::Red ? -1 : 1;
        count_if_target(row + forward, col);
        if (board.crossedRiver(side, row))
        {
            count_if_target(row, col - 1);
            count_if_target(row, col + 1);
        }
        break;
    }
    default:
        break;
    }

    return count;
}

int lineQualityBonus(const SearchBoard& board, const int square, const int8_t piece)
{
    const PieceType piece_type = pieceTypeFromCode(piece);
    if (piece_type != PieceType::Rook && piece_type != PieceType::Cannon)
    {
        return 0;
    }

    const Side side = pieceSide(piece);
    const int row = board.rowOf(square);
    const int col = board.colOf(square);
    static const std::array<std::array<int, 2>, 4> directions{
        std::array<int, 2>{ -1, 0 },
        std::array<int, 2>{ 1, 0 },
        std::array<int, 2>{ 0, -1 },
        std::array<int, 2>{ 0, 1 },
    };

    int bonus = 0;
    for (const auto& direction : directions)
    {
        int current_row = row + direction[0];
        int current_col = col + direction[1];
        bool found_screen = false;
        while (board.inside(current_row, current_col))
        {
            const int target_square = board.index(current_row, current_col);
            const int8_t target_piece = board.pieceAt(target_square);
            if (piece_type == PieceType::Rook)
            {
                if (target_piece == EmptyPiece)
                {
                    bonus += 4;
                }
                else
                {
                    if (pieceSide(target_piece) != side)
                    {
                        bonus += pieceTypeFromCode(target_piece) == PieceType::King ? 90 : std::max(12, pieceAverageValue(target_piece) / 15);
                    }
                    break;
                }
            }
            else if (!found_screen)
            {
                if (target_piece == EmptyPiece)
                {
                    bonus += 3;
                }
                else
                {
                    found_screen = true;
                }
            }
            else if (target_piece != EmptyPiece)
            {
                if (pieceSide(target_piece) != side)
                {
                    bonus += pieceTypeFromCode(target_piece) == PieceType::King ? 120 : std::max(14, pieceAverageValue(target_piece) / 12);
                }
                break;
            }

            current_row += direction[0];
            current_col += direction[1];
        }
    }

    return bonus;
}

int hangingPiecePenalty(const SearchBoard& board, const int square, const Side side, const int8_t piece)
{
    if (!board.isSquareAttacked(square, opposite(side)))
    {
        return 0;
    }

    const bool defended = board.isSquareAttacked(square, side);
    const int base = pieceAverageValue(piece);
    const PieceType piece_type = pieceTypeFromCode(piece);
    if (!defended)
    {
        switch (piece_type)
        {
        case PieceType::Rook:
            return base * 3 / 4;
        case PieceType::Cannon:
        case PieceType::Knight:
            return base * 2 / 3;
        case PieceType::Pawn:
            return base / 2;
        default:
            return base / 3;
        }
    }
    return std::max(12, base / 8);
}

EvalTally kingSafetyScore(const SearchBoard& board, const Side side)
{
    EvalTally tally;
    if (!board.hasKing(side))
    {
        tally.opening = -kMateScore / 2;
        tally.endgame = -kMateScore / 2;
        return tally;
    }

    const Side opponent = opposite(side);
    const int king_square = board.king_index[sideIndex(side)];
    const int king_row = board.rowOf(king_square);
    const int king_col = board.colOf(king_square);

    if (!board.isSquareAttacked(king_square, opponent))
    {
        tally.opening += 40;
        tally.endgame += 15;
    }
    if (board.isInCheck(side))
    {
        tally.opening -= 700;
        tally.endgame -= 260;
    }

    for (int square = 0; square < board.square_count; ++square)
    {
        const int8_t piece = board.pieceAt(square);
        if (piece == EmptyPiece || pieceSide(piece) != side)
        {
            continue;
        }

        const PieceType piece_type = pieceTypeFromCode(piece);
        if (piece_type == PieceType::Advisor)
        {
            tally.opening += board.isPalaceCell(side, board.rowOf(square), board.colOf(square)) ? 34 : 18;
        }
        else if (piece_type == PieceType::Elephant)
        {
            tally.opening += 20;
            tally.endgame += 8;
        }
    }

    for (int dr = -1; dr <= 1; ++dr)
    {
        for (int dc = -1; dc <= 1; ++dc)
        {
            if (dr == 0 && dc == 0)
            {
                continue;
            }

            const int target_row = king_row + dr;
            const int target_col = king_col + dc;
            if (!board.isPalaceCell(side, target_row, target_col))
            {
                continue;
            }

            const int target_square = board.index(target_row, target_col);
            if (board.isSquareAttacked(target_square, opponent))
            {
                tally.opening -= 48;
                tally.endgame -= 18;
            }
            else
            {
                tally.opening += 8;
            }
        }
    }

    static const std::array<std::array<int, 4>, 8> knight_sources{
        std::array<int, 4>{ -2, -1, -1, 0 },
        std::array<int, 4>{ -2, 1, -1, 0 },
        std::array<int, 4>{ -1, -2, 0, -1 },
        std::array<int, 4>{ -1, 2, 0, 1 },
        std::array<int, 4>{ 1, -2, 0, -1 },
        std::array<int, 4>{ 1, 2, 0, 1 },
        std::array<int, 4>{ 2, -1, 1, 0 },
        std::array<int, 4>{ 2, 1, 1, 0 },
    };
    for (const auto& source : knight_sources)
    {
        const int source_row = king_row + source[0];
        const int source_col = king_col + source[1];
        const int leg_row = king_row + source[2];
        const int leg_col = king_col + source[3];
        if (!board.inside(source_row, source_col) || !board.inside(leg_row, leg_col))
        {
            continue;
        }
        if (board.pieceAt(board.index(leg_row, leg_col)) != EmptyPiece)
        {
            continue;
        }
        if (board.pieceAt(board.index(source_row, source_col)) == encodePiece(opponent, PieceType::Knight))
        {
            tally.opening -= 90;
            tally.endgame -= 55;
        }
    }

    static const std::array<std::array<int, 2>, 4> directions{
        std::array<int, 2>{ -1, 0 },
        std::array<int, 2>{ 1, 0 },
        std::array<int, 2>{ 0, -1 },
        std::array<int, 2>{ 0, 1 },
    };
    for (const auto& direction : directions)
    {
        int current_row = king_row + direction[0];
        int current_col = king_col + direction[1];
        bool found_screen = false;
        while (board.inside(current_row, current_col))
        {
            const int8_t piece = board.pieceAt(board.index(current_row, current_col));
            if (piece == EmptyPiece)
            {
                current_row += direction[0];
                current_col += direction[1];
                continue;
            }

            if (!found_screen)
            {
                if (piece == encodePiece(opponent, PieceType::Rook))
                {
                    tally.opening -= 150;
                    tally.endgame -= 80;
                }
                found_screen = true;
            }
            else
            {
                if (piece == encodePiece(opponent, PieceType::Cannon))
                {
                    tally.opening -= 130;
                    tally.endgame -= 65;
                }
                break;
            }

            current_row += direction[0];
            current_col += direction[1];
        }
    }

    if (board.kingsFacing())
    {
        tally.opening -= 180;
        tally.endgame -= 80;
    }

    return tally;
}

EvalTally evaluateSide(const SearchBoard& board, const Side side)
{
    static const std::array<int, 8> opening_value{ 0, 0, 140, 130, 430, 980, 520, 100 };
    static const std::array<int, 8> endgame_value{ 0, 0, 160, 150, 500, 930, 460, 210 };
    static const std::array<int, 8> opening_mobility{ 0, 0, 2, 2, 9, 11, 10, 4 };
    static const std::array<int, 8> endgame_mobility{ 0, 0, 1, 1, 10, 9, 8, 5 };

    EvalTally tally = kingSafetyScore(board, side);
    if (!board.hasKing(side))
    {
        return tally;
    }

    for (int square = 0; square < board.square_count; ++square)
    {
        const int8_t piece = board.pieceAt(square);
        if (piece == EmptyPiece || pieceSide(piece) != side)
        {
            continue;
        }

        const int piece_abs = pieceAbs(piece);
        const PieceType piece_type = pieceTypeFromCode(piece);
        const int row = board.rowOf(square);
        const int mobility = mobilityCount(board, square, piece);
        const int center_bonus = centralityBonus(board, square, piece_type);
        const int hanging_penalty = hangingPiecePenalty(board, square, side, piece);
        const int line_bonus = lineQualityBonus(board, square, piece);

        tally.opening += opening_value[static_cast<size_t>(piece_abs)];
        tally.endgame += endgame_value[static_cast<size_t>(piece_abs)];
        tally.opening += mobility * opening_mobility[static_cast<size_t>(piece_abs)];
        tally.endgame += mobility * endgame_mobility[static_cast<size_t>(piece_abs)];
        tally.opening += center_bonus;
        tally.endgame += center_bonus / 2;
        tally.opening += line_bonus;
        tally.endgame += line_bonus / 2;
        tally.opening -= hanging_penalty;
        tally.endgame -= hanging_penalty / 2;

        if (piece_type == PieceType::Pawn)
        {
            const int pawn_bonus = pawnAdvanceBonus(board, side, square);
            tally.opening += pawn_bonus;
            tally.endgame += pawn_bonus + progressTowardsEnemy(board, side, square) * 6;
        }
        else
        {
            const int progress_bonus = progressTowardsEnemy(board, side, square) * (piece_type == PieceType::Rook ? 2 : 1);
            tally.opening += progress_bonus;
            tally.endgame += progress_bonus;
        }

        if (piece_type == PieceType::Cannon)
        {
            tally.opening += board.crossedRiver(side, row) ? 10 : 0;
        }
        else if (piece_type == PieceType::Knight)
        {
            tally.endgame += board.crossedRiver(side, row) ? 12 : 0;
        }
    }

    return tally;
}

int taperedScore(const EvalTally& left, const EvalTally& right, const int phase)
{
    const int opening = left.opening - right.opening;
    const int endgame = left.endgame - right.endgame;
    return (opening * phase + endgame * (kPhaseMax - phase)) / kPhaseMax;
}

int evaluateSearchBoard(const SearchBoard& board, const Side perspective)
{
    // 评估函数把一个静态局面转换成分数：正分表示当前视角更好，负分表示对手更好。
    // 搜索层反复调用它来比较候选走法，而不是在这里决定具体走哪一步。
    if (!board.hasKing(perspective))
    {
        return -kMateScore / 2;
    }
    if (!board.hasKing(opposite(perspective)))
    {
        return kMateScore / 2;
    }

    const int score = taperedScore(evaluateSide(board, perspective), evaluateSide(board, opposite(perspective)), board.phase);
    int adjusted = score;
    if (board.isInCheck(opposite(perspective)))
    {
        adjusted += 180;
    }
    if (board.isInCheck(perspective))
    {
        adjusted -= 220;
    }
    if (board.kingsFacing())
    {
        adjusted += perspective == Side::Red ? 0 : 0;
    }
    return adjusted;
}

template <size_t N>
void addCandidateMove(MoveBuffer<SearchMove, N>& buffer, const SearchMove& move) noexcept
{
    buffer.push(move);
}

template <size_t N>
void generatePseudoMoves(
    const SearchBoard& board,
    const Side side,
    const bool capture_only,
    MoveBuffer<SearchMove, N>& buffer)
{
    buffer.clear();

    for (int square = 0; square < board.square_count; ++square)
    {
        const int8_t piece = board.pieceAt(square);
        if (piece == EmptyPiece || pieceSide(piece) != side)
        {
            continue;
        }

        const PieceType piece_type = pieceTypeFromCode(piece);
        const int row = board.rowOf(square);
        const int col = board.colOf(square);

        auto try_push = [&](const int target_row, const int target_col)
        {
            if (!board.inside(target_row, target_col))
            {
                return;
            }

            const int target_square = board.index(target_row, target_col);
            const int8_t target_piece = board.pieceAt(target_square);
            if (target_piece != EmptyPiece && pieceSide(target_piece) == side)
            {
                return;
            }
            if (capture_only && target_piece == EmptyPiece)
            {
                return;
            }

            SearchMove move;
            move.from = square;
            move.to = target_square;
            move.moving_piece = piece;
            move.captured_piece = target_piece;
            addCandidateMove(buffer, move);
        };

        switch (piece_type)
        {
        case PieceType::King:
        {
            static const std::array<std::array<int, 2>, 4> directions{
                std::array<int, 2>{ -1, 0 },
                std::array<int, 2>{ 1, 0 },
                std::array<int, 2>{ 0, -1 },
                std::array<int, 2>{ 0, 1 },
            };
            for (const auto& direction : directions)
            {
                const int target_row = row + direction[0];
                const int target_col = col + direction[1];
                if (board.isPalaceCell(side, target_row, target_col))
                {
                    try_push(target_row, target_col);
                }
            }
            break;
        }
        case PieceType::Advisor:
        {
            static const std::array<std::array<int, 2>, 4> directions{
                std::array<int, 2>{ -1, -1 },
                std::array<int, 2>{ -1, 1 },
                std::array<int, 2>{ 1, -1 },
                std::array<int, 2>{ 1, 1 },
            };
            for (const auto& direction : directions)
            {
                const int target_row = row + direction[0];
                const int target_col = col + direction[1];
                if (board.isPalaceCell(side, target_row, target_col))
                {
                    try_push(target_row, target_col);
                }
            }
            break;
        }
        case PieceType::Elephant:
        {
            static const std::array<std::array<int, 4>, 4> directions{
                std::array<int, 4>{ -2, -2, -1, -1 },
                std::array<int, 4>{ -2, 2, -1, 1 },
                std::array<int, 4>{ 2, -2, 1, -1 },
                std::array<int, 4>{ 2, 2, 1, 1 },
            };
            for (const auto& direction : directions)
            {
                const int target_row = row + direction[0];
                const int target_col = col + direction[1];
                const int eye_row = row + direction[2];
                const int eye_col = col + direction[3];
                if (!board.inside(target_row, target_col) || !board.inside(eye_row, eye_col))
                {
                    continue;
                }
                if (board.pieceAt(board.index(eye_row, eye_col)) != EmptyPiece)
                {
                    continue;
                }
                if (board.crossedRiver(side, target_row))
                {
                    continue;
                }
                try_push(target_row, target_col);
            }
            break;
        }
        case PieceType::Knight:
        {
            static const std::array<std::array<int, 4>, 8> jumps{
                std::array<int, 4>{ -2, -1, -1, 0 },
                std::array<int, 4>{ -2, 1, -1, 0 },
                std::array<int, 4>{ -1, -2, 0, -1 },
                std::array<int, 4>{ -1, 2, 0, 1 },
                std::array<int, 4>{ 1, -2, 0, -1 },
                std::array<int, 4>{ 1, 2, 0, 1 },
                std::array<int, 4>{ 2, -1, 1, 0 },
                std::array<int, 4>{ 2, 1, 1, 0 },
            };
            for (const auto& jump : jumps)
            {
                const int target_row = row + jump[0];
                const int target_col = col + jump[1];
                const int leg_row = row + jump[2];
                const int leg_col = col + jump[3];
                if (!board.inside(target_row, target_col) || !board.inside(leg_row, leg_col))
                {
                    continue;
                }
                if (board.pieceAt(board.index(leg_row, leg_col)) != EmptyPiece)
                {
                    continue;
                }
                try_push(target_row, target_col);
            }
            break;
        }
        case PieceType::Rook:
        case PieceType::Cannon:
        {
            static const std::array<std::array<int, 2>, 4> directions{
                std::array<int, 2>{ -1, 0 },
                std::array<int, 2>{ 1, 0 },
                std::array<int, 2>{ 0, -1 },
                std::array<int, 2>{ 0, 1 },
            };
            for (const auto& direction : directions)
            {
                int current_row = row + direction[0];
                int current_col = col + direction[1];
                bool found_screen = false;
                while (board.inside(current_row, current_col))
                {
                    const int target_square = board.index(current_row, current_col);
                    const int8_t target_piece = board.pieceAt(target_square);
                    if (piece_type == PieceType::Rook)
                    {
                        if (target_piece == EmptyPiece)
                        {
                            if (!capture_only)
                            {
                                SearchMove move;
                                move.from = square;
                                move.to = target_square;
                                move.moving_piece = piece;
                                addCandidateMove(buffer, move);
                            }
                        }
                        else
                        {
                            if (pieceSide(target_piece) != side)
                            {
                                SearchMove move;
                                move.from = square;
                                move.to = target_square;
                                move.moving_piece = piece;
                                move.captured_piece = target_piece;
                                addCandidateMove(buffer, move);
                            }
                            break;
                        }
                    }
                    else if (!found_screen)
                    {
                        if (target_piece == EmptyPiece)
                        {
                            if (!capture_only)
                            {
                                SearchMove move;
                                move.from = square;
                                move.to = target_square;
                                move.moving_piece = piece;
                                addCandidateMove(buffer, move);
                            }
                        }
                        else
                        {
                            found_screen = true;
                        }
                    }
                    else if (target_piece != EmptyPiece)
                    {
                        if (pieceSide(target_piece) != side)
                        {
                            SearchMove move;
                            move.from = square;
                            move.to = target_square;
                            move.moving_piece = piece;
                            move.captured_piece = target_piece;
                            addCandidateMove(buffer, move);
                        }
                        break;
                    }

                    current_row += direction[0];
                    current_col += direction[1];
                }
            }
            break;
        }
        case PieceType::Pawn:
        {
            const int forward = side == Side::Red ? -1 : 1;
            try_push(row + forward, col);
            if (board.crossedRiver(side, row))
            {
                try_push(row, col - 1);
                try_push(row, col + 1);
            }
            break;
        }
        default:
            break;
        }
    }
}

int moveOrderScore(
    const SearchContext& context,
    const SearchBoard& board,
    const Side side,
    const SearchMove& move,
    const SearchMove& tt_move,
    const bool gives_check,
    const bool in_check,
    const int ply) noexcept
{
    int score = historyScore(context, side, move);

    if (tt_move.isValid() && sameMove(move, tt_move))
    {
        score += 10'000'000;
    }

    if (move.isCapture())
    {
        score += 300'000 + pieceAverageValue(move.captured_piece) * 24 - pieceAverageValue(move.moving_piece);
    }

    if (gives_check)
    {
        score += 120'000;
    }

    if (in_check)
    {
        score += 60'000;
    }

    if (ply >= 0 && ply < static_cast<int>(context.stack.size()))
    {
        const auto& killers = context.stack[static_cast<size_t>(ply)].killers;
        if (sameMove(move, killers[0]))
        {
            score += 90'000;
        }
        else if (sameMove(move, killers[1]))
        {
            score += 80'000;
        }
    }

    const PieceType piece_type = pieceTypeFromCode(move.moving_piece);
    if (piece_type == PieceType::Rook || piece_type == PieceType::Cannon)
    {
        score += 300;
    }
    else if (piece_type == PieceType::Knight)
    {
        score += 180;
    }
    else if (piece_type == PieceType::Pawn)
    {
        const int target_row = board.rowOf(move.to);
        score += board.crossedRiver(side, target_row) ? 120 : 40;
    }

    return score;
}

bool scoredMoveBetter(const ScoredMove& left, const ScoredMove& right) noexcept
{
    return std::make_tuple(
               left.order_score,
               left.move.givesCheck(),
               left.move.isCapture(),
               pieceAverageValue(left.move.captured_piece),
               left.move.from,
               left.move.to) >
           std::make_tuple(
               right.order_score,
               right.move.givesCheck(),
               right.move.isCapture(),
               pieceAverageValue(right.move.captured_piece),
               right.move.from,
               right.move.to);
}

template <size_t N>
void generateOrderedLegalMoves(
    SearchContext& context,
    SearchBoard& board,
    const Side side,
    const bool capture_only,
    const int ply,
    const int path_index,
    const SearchMove& tt_move,
    MoveBuffer<ScoredMove, N>& legal_moves)
{
    MoveBuffer<SearchMove, kMaxMoveCount> pseudo_moves;
    generatePseudoMoves(board, side, capture_only, pseudo_moves);
    legal_moves.clear();

    const bool in_check = board.isInCheck(side);
    for (size_t move_index = 0; move_index < pseudo_moves.size(); ++move_index)
    {
        SearchMove move = pseudo_moves[move_index];
        UndoState undo;
        board.makeMove(move, undo);

        const bool illegal = board.isInCheck(side);
        const bool gives_check = !board.hasKing(opposite(side)) || board.isInCheck(opposite(side));
        if (!illegal)
        {
            if (gives_check)
            {
                move.flags |= kFlagCheck;
            }

            if (!capture_only || in_check || move.isCapture() || gives_check)
            {
                bool forbidden_long_check = false;
                if (gives_check && path_index + 1 < kMaxPathStates)
                {
                    context.path_plies[static_cast<size_t>(path_index)] = { side, true };
                    context.path_states[static_cast<size_t>(path_index + 1)] = { board.positionKey(opposite(side)), opposite(side) };
                    forbidden_long_check = isPerpetualCheck(
                        context.path_states,
                        static_cast<size_t>(path_index + 2),
                        context.path_plies,
                        static_cast<size_t>(path_index + 1),
                        side);
                }

                if (!forbidden_long_check)
                {
                    legal_moves.push(
                        {
                            move,
                            moveOrderScore(context, board, side, move, tt_move, gives_check, in_check, ply),
                        });
                }
            }
        }

        board.unmakeMove(move, undo);
    }

    std::sort(legal_moves.begin(), legal_moves.end(), scoredMoveBetter);
}

int quiescence(
    SearchContext& context,
    SearchBoard& board,
    Side side_to_move,
    int alpha,
    int beta,
    int ply,
    int path_index);

int negamax(
    SearchContext& context,
    SearchBoard& board,
    const Side side_to_move,
    const int depth,
    int alpha,
    const int beta,
    const int ply,
    const int path_index,
    const bool allow_null_move,
    const bool is_pv)
{
    touchNode(context, false);

    if (!board.hasKing(side_to_move))
    {
        return -kMateScore + ply;
    }
    if (!board.hasKing(opposite(side_to_move)))
    {
        return kMateScore - ply;
    }

    if (depth <= 0)
    {
        return quiescence(context, board, side_to_move, alpha, beta, ply, path_index);
    }

    const uint64_t key = board.positionKey(side_to_move);
    const int original_alpha = alpha;
    SearchMove tt_move{};
    if (const TTEntry* tt_entry = probeTt(context, key))
    {
        tt_move = tt_entry->best_move;
        if (tt_entry->depth >= depth)
        {
            const int tt_score = mateFromTt(tt_entry->score, ply);
            if (tt_entry->bound == BoundExact)
            {
                return tt_score;
            }
            if (tt_entry->bound == BoundLower && tt_score >= beta)
            {
                return tt_score;
            }
            if (tt_entry->bound == BoundUpper && tt_score <= alpha)
            {
                return tt_score;
            }
        }
    }

    const bool in_check = board.isInCheck(side_to_move);
    if (!is_pv && !in_check && allow_null_move && depth >= 3 && board.hasNonPawnMaterial(side_to_move))
    {
        const int reduction = 2 + depth / 4;
        const int null_score = -negamax(
            context,
            board,
            opposite(side_to_move),
            depth - 1 - reduction,
            -beta,
            -beta + 1,
            ply + 1,
            path_index,
            false,
            false);
        if (null_score >= beta)
        {
            ++context.stats.nmp_cutoffs;
            return null_score;
        }
    }

    MoveBuffer<ScoredMove, kMaxMoveCount> legal_moves;
    generateOrderedLegalMoves(context, board, side_to_move, false, ply, path_index, tt_move, legal_moves);
    if (legal_moves.empty())
    {
        return -kMateScore + ply;
    }

    SearchMove best_move{};
    int best_score = -kSearchInf;
    int legal_index = 0;
    for (size_t move_index = 0; move_index < legal_moves.size(); ++move_index)
    {
        SearchMove move = legal_moves[move_index].move;
        UndoState undo;
        board.makeMove(move, undo);
        context.path_plies[static_cast<size_t>(path_index)] = { side_to_move, move.givesCheck() };
        context.path_states[static_cast<size_t>(path_index + 1)] = { board.positionKey(opposite(side_to_move)), opposite(side_to_move) };

        int score = 0;
        const bool quiet_move = !move.isCapture() && !move.givesCheck();
        if (legal_index == 0)
        {
            score = -negamax(
                context,
                board,
                opposite(side_to_move),
                depth - 1,
                -beta,
                -alpha,
                ply + 1,
                path_index + 1,
                true,
                is_pv);
        }
        else
        {
            int reduction = 0;
            if (!is_pv && !in_check && quiet_move && depth >= 3 && legal_index >= 3)
            {
                reduction = depth >= 5 ? 2 : 1;
            }

            if (reduction > 0)
            {
                ++context.stats.lmr_reductions;
                score = -negamax(
                    context,
                    board,
                    opposite(side_to_move),
                    depth - 1 - reduction,
                    -alpha - 1,
                    -alpha,
                    ply + 1,
                    path_index + 1,
                    true,
                    false);
                if (score > alpha)
                {
                    score = -negamax(
                        context,
                        board,
                        opposite(side_to_move),
                        depth - 1,
                        -alpha - 1,
                        -alpha,
                        ply + 1,
                        path_index + 1,
                        true,
                        false);
                }
            }
            else
            {
                score = -negamax(
                    context,
                    board,
                    opposite(side_to_move),
                    depth - 1,
                    -alpha - 1,
                    -alpha,
                    ply + 1,
                    path_index + 1,
                    true,
                    false);
            }

            if (score > alpha && score < beta)
            {
                score = -negamax(
                    context,
                    board,
                    opposite(side_to_move),
                    depth - 1,
                    -beta,
                    -alpha,
                    ply + 1,
                    path_index + 1,
                    true,
                    true);
            }
        }

        board.unmakeMove(move, undo);

        if (score > best_score)
        {
            best_score = score;
            best_move = move;
        }

        alpha = std::max(alpha, score);
        if (alpha >= beta)
        {
            if (quiet_move)
            {
                recordHistory(context, side_to_move, move, depth);
                recordKiller(context, ply, move);
            }
            storeTt(context, key, depth, ply, alpha, BoundLower, move);
            return alpha;
        }

        ++legal_index;
    }

    uint8_t bound = BoundExact;
    if (best_score <= original_alpha)
    {
        bound = BoundUpper;
    }
    else if (best_score >= beta)
    {
        bound = BoundLower;
    }
    storeTt(context, key, depth, ply, best_score, bound, best_move);
    return best_score;
}

int quiescence(
    SearchContext& context,
    SearchBoard& board,
    const Side side_to_move,
    int alpha,
    const int beta,
    const int ply,
    const int path_index)
{
    touchNode(context, true);

    if (!board.hasKing(side_to_move))
    {
        return -kMateScore + ply;
    }
    if (!board.hasKing(opposite(side_to_move)))
    {
        return kMateScore - ply;
    }

    const bool in_check = board.isInCheck(side_to_move);
    const int stand_pat = evaluateSearchBoard(board, side_to_move);
    if (ply >= kMaxQuiescenceDepth)
    {
        return stand_pat;
    }

    if (!in_check)
    {
        if (stand_pat >= beta)
        {
            return stand_pat;
        }
        alpha = std::max(alpha, stand_pat);
    }

    MoveBuffer<ScoredMove, kMaxMoveCount> tactical_moves;
    generateOrderedLegalMoves(context, board, side_to_move, !in_check, ply, path_index, SearchMove{}, tactical_moves);
    if (tactical_moves.empty())
    {
        return in_check ? (-kMateScore + ply) : stand_pat;
    }

    int best_score = in_check ? -kSearchInf : stand_pat;
    for (size_t move_index = 0; move_index < tactical_moves.size(); ++move_index)
    {
        SearchMove move = tactical_moves[move_index].move;
        UndoState undo;
        board.makeMove(move, undo);
        context.path_plies[static_cast<size_t>(path_index)] = { side_to_move, move.givesCheck() };
        context.path_states[static_cast<size_t>(path_index + 1)] = { board.positionKey(opposite(side_to_move)), opposite(side_to_move) };

        const int score = -quiescence(
            context,
            board,
            opposite(side_to_move),
            -beta,
            -alpha,
            ply + 1,
            path_index + 1);

        board.unmakeMove(move, undo);

        best_score = std::max(best_score, score);
        alpha = std::max(alpha, score);
        if (alpha >= beta)
        {
            break;
        }
    }

    return best_score;
}

RootSearchResult searchRoot(
    SearchContext& context,
    SearchBoard& board,
    const Side side_to_move,
    const int depth,
    const int path_index)
{
    SearchMove tt_move{};
    if (const TTEntry* tt_entry = probeTt(context, board.positionKey(side_to_move)))
    {
        tt_move = tt_entry->best_move;
    }

    MoveBuffer<ScoredMove, kMaxMoveCount> root_moves;
    generateOrderedLegalMoves(context, board, side_to_move, false, 0, path_index, tt_move, root_moves);
    if (root_moves.empty())
    {
        throw IllegalMoveError("No legal move available for root search.");
    }

    RootSearchResult result{ root_moves[0].move, -kSearchInf, depth };
    int alpha = -kSearchInf;
    constexpr int beta = kSearchInf;

    for (size_t move_index = 0; move_index < root_moves.size(); ++move_index)
    {
        SearchMove move = root_moves[move_index].move;
        UndoState undo;
        board.makeMove(move, undo);
        context.path_plies[static_cast<size_t>(path_index)] = { side_to_move, move.givesCheck() };
        context.path_states[static_cast<size_t>(path_index + 1)] = { board.positionKey(opposite(side_to_move)), opposite(side_to_move) };

        int score = 0;
        if (move_index == 0)
        {
            score = depth <= 1
                ? -quiescence(context, board, opposite(side_to_move), -beta, -alpha, 1, path_index + 1)
                : -negamax(context, board, opposite(side_to_move), depth - 1, -beta, -alpha, 1, path_index + 1, true, true);
        }
        else
        {
            score = depth <= 1
                ? -quiescence(context, board, opposite(side_to_move), -alpha - 1, -alpha, 1, path_index + 1)
                : -negamax(context, board, opposite(side_to_move), depth - 1, -alpha - 1, -alpha, 1, path_index + 1, true, false);
            if (score > alpha && score < beta)
            {
                score = depth <= 1
                    ? -quiescence(context, board, opposite(side_to_move), -beta, -alpha, 1, path_index + 1)
                    : -negamax(context, board, opposite(side_to_move), depth - 1, -beta, -alpha, 1, path_index + 1, true, true);
            }
        }

        board.unmakeMove(move, undo);

        if (score > result.best_score)
        {
            result.best_score = score;
            result.best_move = move;
        }
        alpha = std::max(alpha, score);
    }

    return result;
}

void buildSearchRoot(
    const GameSession& session,
    SearchContext& context,
    SearchBoard& root_board)
{
    SearchBoard replay(session.boardMode());
    context.path_states[0] = { replay.positionKey(Side::Red), Side::Red };
    int path_index = 0;

    for (const auto& public_move : session.history())
    {
        if (path_index + 1 >= kMaxPathStates)
        {
            break;
        }

        SearchMove move = toSearchMove(replay, public_move);
        UndoState undo;
        replay.makeMove(move, undo);
        const bool gives_check = public_move.is_check || replay.isInCheck(opposite(public_move.side));
        context.path_plies[static_cast<size_t>(path_index)] = { public_move.side, gives_check };
        ++path_index;
        context.path_states[static_cast<size_t>(path_index)] = { replay.positionKey(opposite(public_move.side)), opposite(public_move.side) };
    }

    SearchBoard current_board(session.board());
    if (current_board.positionKey(session.currentSide()) != replay.positionKey(session.currentSide()))
    {
        root_board = current_board;
        context.path_states[0] = { root_board.positionKey(session.currentSide()), session.currentSide() };
        context.root_path_index = 0;
        return;
    }

    root_board = replay;
    context.root_path_index = path_index;
}

void emitSearchStats(
    const SearchContext& context,
    const int reached_depth,
    const int best_score,
    const SearchMove& best_move,
    const SearchBoard& board)
{
#ifdef _WIN32
#ifdef _DEBUG
    std::ostringstream output;
    output << "[AI] depth=" << reached_depth
           << " score=" << best_score
           << " nodes=" << context.stats.nodes
           << " qnodes=" << context.stats.qnodes
           << " tt=" << context.stats.tt_hits << "/" << context.stats.tt_probes
           << " nmp=" << context.stats.nmp_cutoffs
           << " lmr=" << context.stats.lmr_reductions
           << " best="
           << static_cast<char>('a' + board.colOf(best_move.from)) << board.rowOf(best_move.from)
           << "->"
           << static_cast<char>('a' + board.colOf(best_move.to)) << board.rowOf(best_move.to)
           << "\n";
    OutputDebugStringA(output.str().c_str());
#endif
#else
    (void)context;
    (void)reached_depth;
    (void)best_score;
    (void)best_move;
    (void)board;
#endif
}

} // namespace

SearchEngine::SearchEngine(const int depth) : depth_(std::clamp(depth, 1, 5)) {}

std::optional<Move> SearchEngine::chooseBestMove(const GameSession& session) const
{
    // 搜索入口只接收当前 GameSession，不直接修改棋局。
    // 引擎在内部复制局面并尝试走法，最终只把推荐走法返回给 UI 或提示命令。
    SearchContext context;
    context.deadline = SearchClock::now() + std::chrono::milliseconds(timeBudgetMsForTier(depth_));
    context.age = nextSearchAge();

    SearchBoard root_board;
    buildSearchRoot(session, context, root_board);
    const Side root_side = session.currentSide();

    MoveBuffer<ScoredMove, kMaxMoveCount> fallback_moves;
    generateOrderedLegalMoves(context, root_board, root_side, false, 0, context.root_path_index, SearchMove{}, fallback_moves);
    if (fallback_moves.empty())
    {
        return std::nullopt;
    }

    SearchMove best_search_move = fallback_moves[0].move;
    int best_score = -kSearchInf;
    for (size_t move_index = 0; move_index < fallback_moves.size(); ++move_index)
    {
        const SearchMove move = fallback_moves[move_index].move;
        const int score = fallbackMoveScore(root_board, move, root_side);
        if (score > best_score)
        {
            best_score = score;
            best_search_move = move;
        }
    }
    int completed_depth = 0;

    for (int depth = 1; depth <= kMaxSearchDepth; ++depth)
    {
        if (SearchClock::now() >= context.deadline)
        {
            break;
        }

        ageHistory(context);

        try
        {
            SearchBoard board = root_board;
            const RootSearchResult result = searchRoot(context, board, root_side, depth, context.root_path_index);
            best_search_move = result.best_move;
            best_score = result.best_score;
            completed_depth = result.completed_depth;
        }
        catch (const SearchTimeout&)
        {
            break;
        }
    }

    emitSearchStats(context, completed_depth, best_score, best_search_move, root_board);
    return toPublicMove(root_board, best_search_move, best_score);
}

int SearchEngine::evaluateBoard(const Board& board, const Side perspective) const
{
    return evaluateSearchBoard(SearchBoard(board), perspective);
}

} // namespace xiangqi
