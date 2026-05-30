#include "engine/ChessEngine.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace xiangqi
{

namespace
{

std::vector<InitialPiece> makeInitialPieces(const BoardMode mode)
{
    const int shift = mode == BoardMode::Expanded11x10 ? 1 : 0;
    std::vector<InitialPiece> pieces;

    const std::array<PieceType, 9> order{
        PieceType::Rook,
        PieceType::Knight,
        PieceType::Elephant,
        PieceType::Advisor,
        PieceType::King,
        PieceType::Advisor,
        PieceType::Elephant,
        PieceType::Knight,
        PieceType::Rook,
    };

    for (int col = 0; col < static_cast<int>(order.size()); ++col)
    {
        pieces.push_back({ Side::Black, order[col], { 0, col + shift } });
        pieces.push_back({ Side::Red, order[col], { 9, col + shift } });
    }

    pieces.push_back({ Side::Black, PieceType::Cannon, { 2, 1 + shift } });
    pieces.push_back({ Side::Black, PieceType::Cannon, { 2, 7 + shift } });
    pieces.push_back({ Side::Red, PieceType::Cannon, { 7, 1 + shift } });
    pieces.push_back({ Side::Red, PieceType::Cannon, { 7, 7 + shift } });

    const std::array<int, 5> pawn_cols{ 0, 2, 4, 6, 8 };
    for (const int base_col : pawn_cols)
    {
        pieces.push_back({ Side::Black, PieceType::Pawn, { 3, base_col + shift } });
        pieces.push_back({ Side::Red, PieceType::Pawn, { 6, base_col + shift } });
    }

    return pieces;
}

bool containsPosition(const std::vector<Position<int>>& positions, const Position<int> target)
{
    return std::find(positions.begin(), positions.end(), target) != positions.end();
}

bool isForwardDelta(const Side side, const int delta_row)
{
    return side == Side::Red ? delta_row < 0 : delta_row > 0;
}

} // namespace

BoardConfig makeBoardConfig(const BoardMode mode)
{
    BoardConfig config;
    config.mode = mode;
    config.rows = 10;
    config.cols = mode == BoardMode::Expanded11x10 ? 11 : 9;
    config.river_split_top_row = 4;
    config.upper_palace_min_row = 0;
    config.upper_palace_max_row = 2;
    config.lower_palace_min_row = 7;
    config.lower_palace_max_row = 9;
    config.palace_min_col = mode == BoardMode::Expanded11x10 ? 4 : 3;
    config.palace_max_col = mode == BoardMode::Expanded11x10 ? 6 : 5;
    config.coordinate_files = boardModeCoordinateFiles(mode);
    config.initial_pieces = makeInitialPieces(mode);
    return config;
}

std::unique_ptr<ChessPiece> createPiece(const PieceType piece_type, const Side side)
{
    switch (piece_type)
    {
    case PieceType::King:
        return std::make_unique<KingPiece>(side);
    case PieceType::Advisor:
        return std::make_unique<AdvisorPiece>(side);
    case PieceType::Elephant:
        return std::make_unique<ElephantPiece>(side);
    case PieceType::Knight:
        return std::make_unique<KnightPiece>(side);
    case PieceType::Rook:
        return std::make_unique<RookPiece>(side);
    case PieceType::Cannon:
        return std::make_unique<CannonPiece>(side);
    case PieceType::Pawn:
        return std::make_unique<PawnPiece>(side);
    default:
        throw std::invalid_argument("Unknown piece type.");
    }
}

std::unique_ptr<ChessPiece> KingPiece::clone() const
{
    return std::make_unique<KingPiece>(*this);
}

bool KingPiece::isPseudoLegal(const Board& board, const Position<int> from, const Position<int> to) const
{
    const int distance = std::abs(from.row - to.row) + std::abs(from.col - to.col);
    return distance == 1 && board.isPalaceCell(side(), to);
}

std::vector<Position<int>> KingPiece::generatePseudoLegalMoves(const Board& board, const Position<int> from) const
{
    static const std::array<Position<int>, 4> directions{
        Position<int>{ -1, 0 },
        Position<int>{ 1, 0 },
        Position<int>{ 0, -1 },
        Position<int>{ 0, 1 },
    };

    std::vector<Position<int>> result;
    for (const auto& direction : directions)
    {
        const Position<int> target{ from.row + direction.row, from.col + direction.col };
        if (!board.isInside(target) || !board.isPalaceCell(side(), target))
        {
            continue;
        }

        const auto* target_piece = board.pieceAt(target);
        if (target_piece == nullptr || target_piece->side() != side())
        {
            result.push_back(target);
        }
    }
    return result;
}

std::unique_ptr<ChessPiece> AdvisorPiece::clone() const
{
    return std::make_unique<AdvisorPiece>(*this);
}

bool AdvisorPiece::isPseudoLegal(const Board& board, const Position<int> from, const Position<int> to) const
{
    return std::abs(from.row - to.row) == 1 && std::abs(from.col - to.col) == 1 && board.isPalaceCell(side(), to);
}

std::vector<Position<int>> AdvisorPiece::generatePseudoLegalMoves(const Board& board, const Position<int> from) const
{
    static const std::array<Position<int>, 4> directions{
        Position<int>{ -1, -1 },
        Position<int>{ -1, 1 },
        Position<int>{ 1, -1 },
        Position<int>{ 1, 1 },
    };

    std::vector<Position<int>> result;
    for (const auto& direction : directions)
    {
        const Position<int> target{ from.row + direction.row, from.col + direction.col };
        if (!board.isInside(target) || !board.isPalaceCell(side(), target))
        {
            continue;
        }

        const auto* target_piece = board.pieceAt(target);
        if (target_piece == nullptr || target_piece->side() != side())
        {
            result.push_back(target);
        }
    }
    return result;
}

std::unique_ptr<ChessPiece> ElephantPiece::clone() const
{
    return std::make_unique<ElephantPiece>(*this);
}

bool ElephantPiece::isPseudoLegal(const Board& board, const Position<int> from, const Position<int> to) const
{
    if (std::abs(from.row - to.row) != 2 || std::abs(from.col - to.col) != 2)
    {
        return false;
    }

    if ((side() == Side::Black && to.row > board.config().river_split_top_row) ||
        (side() == Side::Red && to.row <= board.config().river_split_top_row))
    {
        return false;
    }

    const Position<int> eye{ (from.row + to.row) / 2, (from.col + to.col) / 2 };
    return board.pieceAt(eye) == nullptr;
}

std::vector<Position<int>> ElephantPiece::generatePseudoLegalMoves(const Board& board, const Position<int> from) const
{
    static const std::array<Position<int>, 4> directions{
        Position<int>{ -2, -2 },
        Position<int>{ -2, 2 },
        Position<int>{ 2, -2 },
        Position<int>{ 2, 2 },
    };

    std::vector<Position<int>> result;
    for (const auto& direction : directions)
    {
        const Position<int> target{ from.row + direction.row, from.col + direction.col };
        if (!board.isInside(target) || !isPseudoLegal(board, from, target))
        {
            continue;
        }

        const auto* target_piece = board.pieceAt(target);
        if (target_piece == nullptr || target_piece->side() != side())
        {
            result.push_back(target);
        }
    }
    return result;
}

std::unique_ptr<ChessPiece> KnightPiece::clone() const
{
    return std::make_unique<KnightPiece>(*this);
}

bool KnightPiece::isPseudoLegal(const Board& board, const Position<int> from, const Position<int> to) const
{
    const int dr = to.row - from.row;
    const int dc = to.col - from.col;
    const int abs_dr = std::abs(dr);
    const int abs_dc = std::abs(dc);

    if (!((abs_dr == 2 && abs_dc == 1) || (abs_dr == 1 && abs_dc == 2)))
    {
        return false;
    }

    Position<int> leg = from;
    if (abs_dr == 2)
    {
        leg.row += dr / 2;
    }
    else
    {
        leg.col += dc / 2;
    }

    return board.pieceAt(leg) == nullptr;
}

std::vector<Position<int>> KnightPiece::generatePseudoLegalMoves(const Board& board, const Position<int> from) const
{
    static const std::array<Position<int>, 8> jumps{
        Position<int>{ -2, -1 },
        Position<int>{ -2, 1 },
        Position<int>{ -1, -2 },
        Position<int>{ -1, 2 },
        Position<int>{ 1, -2 },
        Position<int>{ 1, 2 },
        Position<int>{ 2, -1 },
        Position<int>{ 2, 1 },
    };

    std::vector<Position<int>> result;
    for (const auto& jump : jumps)
    {
        const Position<int> target{ from.row + jump.row, from.col + jump.col };
        if (!board.isInside(target) || !isPseudoLegal(board, from, target))
        {
            continue;
        }

        const auto* target_piece = board.pieceAt(target);
        if (target_piece == nullptr || target_piece->side() != side())
        {
            result.push_back(target);
        }
    }
    return result;
}

std::unique_ptr<ChessPiece> RookPiece::clone() const
{
    return std::make_unique<RookPiece>(*this);
}

bool RookPiece::isPseudoLegal(const Board& board, const Position<int> from, const Position<int> to) const
{
    return (from.row == to.row || from.col == to.col) && board.countObstacles(from, to) == 0;
}

std::vector<Position<int>> RookPiece::generatePseudoLegalMoves(const Board& board, const Position<int> from) const
{
    static const std::array<Position<int>, 4> directions{
        Position<int>{ -1, 0 },
        Position<int>{ 1, 0 },
        Position<int>{ 0, -1 },
        Position<int>{ 0, 1 },
    };

    std::vector<Position<int>> result;
    for (const auto& direction : directions)
    {
        Position<int> current{ from.row + direction.row, from.col + direction.col };
        while (board.isInside(current))
        {
            const auto* target_piece = board.pieceAt(current);
            if (target_piece == nullptr)
            {
                result.push_back(current);
            }
            else
            {
                if (target_piece->side() != side())
                {
                    result.push_back(current);
                }
                break;
            }
            current.row += direction.row;
            current.col += direction.col;
        }
    }
    return result;
}

std::unique_ptr<ChessPiece> CannonPiece::clone() const
{
    return std::make_unique<CannonPiece>(*this);
}

bool CannonPiece::isPseudoLegal(const Board& board, const Position<int> from, const Position<int> to) const
{
    if (!(from.row == to.row || from.col == to.col))
    {
        return false;
    }

    const int obstacles = board.countObstacles(from, to);
    const auto* target_piece = board.pieceAt(to);
    if (target_piece == nullptr)
    {
        return obstacles == 0;
    }
    return target_piece->side() != side() && obstacles == 1;
}

std::vector<Position<int>> CannonPiece::generatePseudoLegalMoves(const Board& board, const Position<int> from) const
{
    static const std::array<Position<int>, 4> directions{
        Position<int>{ -1, 0 },
        Position<int>{ 1, 0 },
        Position<int>{ 0, -1 },
        Position<int>{ 0, 1 },
    };

    std::vector<Position<int>> result;
    for (const auto& direction : directions)
    {
        Position<int> current{ from.row + direction.row, from.col + direction.col };
        bool found_screen = false;
        while (board.isInside(current))
        {
            const auto* target_piece = board.pieceAt(current);
            if (!found_screen)
            {
                if (target_piece == nullptr)
                {
                    result.push_back(current);
                }
                else
                {
                    found_screen = true;
                }
            }
            else if (target_piece != nullptr)
            {
                if (target_piece->side() != side())
                {
                    result.push_back(current);
                }
                break;
            }

            current.row += direction.row;
            current.col += direction.col;
        }
    }
    return result;
}

std::unique_ptr<ChessPiece> PawnPiece::clone() const
{
    return std::make_unique<PawnPiece>(*this);
}

bool PawnPiece::isPseudoLegal(const Board& board, const Position<int> from, const Position<int> to) const
{
    const int dr = to.row - from.row;
    const int dc = to.col - from.col;
    const bool crossed_river = side() == Side::Red ? from.row <= board.config().river_split_top_row
                                                   : from.row > board.config().river_split_top_row;

    const int forward = side() == Side::Red ? -1 : 1;
    if (dr == forward && dc == 0)
    {
        return true;
    }

    if (crossed_river && dr == 0 && std::abs(dc) == 1)
    {
        return true;
    }

    return false;
}

std::vector<Position<int>> PawnPiece::generatePseudoLegalMoves(const Board& board, const Position<int> from) const
{
    std::vector<Position<int>> result;
    const bool crossed_river = side() == Side::Red ? from.row <= board.config().river_split_top_row
                                                   : from.row > board.config().river_split_top_row;
    const int forward = side() == Side::Red ? -1 : 1;

    const std::array<Position<int>, 3> deltas{
        Position<int>{ forward, 0 },
        Position<int>{ 0, -1 },
        Position<int>{ 0, 1 },
    };

    for (const auto& delta : deltas)
    {
        if (!crossed_river && delta.row == 0)
        {
            continue;
        }

        const Position<int> target{ from.row + delta.row, from.col + delta.col };
        if (!board.isInside(target) || !isPseudoLegal(board, from, target))
        {
            continue;
        }

        const auto* target_piece = board.pieceAt(target);
        if (target_piece == nullptr || target_piece->side() != side())
        {
            result.push_back(target);
        }
    }
    return result;
}

Board::Board() : Board(BoardMode::Standard9x10) {}

Board::Board(const BoardMode mode) : Board(makeBoardConfig(mode)) {}

Board::Board(BoardConfig config)
{
    setup(config);
}

Board::Board(const Board& other) : config_(other.config_)
{
    cells_.resize(other.cells_.size());
    for (size_t index = 0; index < other.cells_.size(); ++index)
    {
        if (other.cells_[index] != nullptr)
        {
            cells_[index] = other.cells_[index]->clone();
        }
    }
}

Board& Board::operator=(const Board& other)
{
    if (this == &other)
    {
        return *this;
    }

    config_ = other.config_;
    cells_.clear();
    cells_.resize(other.cells_.size());
    for (size_t index = 0; index < other.cells_.size(); ++index)
    {
        if (other.cells_[index] != nullptr)
        {
            cells_[index] = other.cells_[index]->clone();
        }
    }
    return *this;
}

void Board::setup(const BoardConfig& config)
{
    config_ = config;
    cells_.clear();
    cells_.resize(static_cast<size_t>(config.rows * config.cols));
    for (const auto& piece : config.initial_pieces)
    {
        setPiece(piece.position, createPiece(piece.type, piece.side));
    }
}

void Board::setup(const BoardMode mode)
{
    setup(makeBoardConfig(mode));
}

bool Board::isInside(const Position<int> position) const noexcept
{
    return position.row >= 0 && position.row < config_.rows && position.col >= 0 && position.col < config_.cols;
}

bool Board::isPalaceCell(const Side side, const Position<int> position) const noexcept
{
    if (!isInside(position) || position.col < config_.palace_min_col || position.col > config_.palace_max_col)
    {
        return false;
    }

    if (side == Side::Black)
    {
        return position.row >= config_.upper_palace_min_row && position.row <= config_.upper_palace_max_row;
    }

    return position.row >= config_.lower_palace_min_row && position.row <= config_.lower_palace_max_row;
}

int Board::countObstacles(const Position<int> from, const Position<int> to) const
{
    if (from.row != to.row && from.col != to.col)
    {
        return 0;
    }

    const int row_step = to.row == from.row ? 0 : (to.row > from.row ? 1 : -1);
    const int col_step = to.col == from.col ? 0 : (to.col > from.col ? 1 : -1);

    int obstacles = 0;
    Position<int> current{ from.row + row_step, from.col + col_step };
    while (current != to)
    {
        if (pieceAt(current) != nullptr)
        {
            ++obstacles;
        }
        current.row += row_step;
        current.col += col_step;
    }
    return obstacles;
}

bool Board::hasFlyingKings() const
{
    Position<int> red_king{};
    Position<int> black_king{};
    try
    {
        red_king = findKing(Side::Red);
        black_king = findKing(Side::Black);
    }
    catch (const IllegalMoveError&)
    {
        return false;
    }

    if (red_king.col != black_king.col)
    {
        return false;
    }

    return countObstacles(red_king, black_king) == 0;
}

bool Board::isSquareAttacked(const Position<int> position, const Side by_side) const
{
    for (const auto& [from, piece] : pieces(by_side))
    {
        if (containsPosition(piece->generatePseudoLegalMoves(*this, from), position))
        {
            return true;
        }
    }

    try
    {
        const Position<int> red_king = findKing(Side::Red);
        const Position<int> black_king = findKing(Side::Black);
        if (red_king.col == black_king.col && countObstacles(red_king, black_king) == 0)
        {
            if (by_side == Side::Red && position == black_king)
            {
                return true;
            }
            if (by_side == Side::Black && position == red_king)
            {
                return true;
            }
        }
    }
    catch (const IllegalMoveError&)
    {
    }

    return false;
}

bool Board::isInCheck(const Side side) const
{
    try
    {
        return isSquareAttacked(findKing(side), opposite(side)) || hasFlyingKings();
    }
    catch (const IllegalMoveError&)
    {
        return true;
    }
}

ChessPiece* Board::pieceAt(const Position<int> position)
{
    if (!isInside(position))
    {
        return nullptr;
    }
    return cells_[static_cast<size_t>(index(position))].get();
}

const ChessPiece* Board::pieceAt(const Position<int> position) const
{
    if (!isInside(position))
    {
        return nullptr;
    }
    return cells_[static_cast<size_t>(index(position))].get();
}

std::vector<std::pair<Position<int>, const ChessPiece*>> Board::pieces(const Side side) const
{
    std::vector<std::pair<Position<int>, const ChessPiece*>> result;
    for (int row = 0; row < config_.rows; ++row)
    {
        for (int col = 0; col < config_.cols; ++col)
        {
            const Position<int> position{ row, col };
            const auto* piece = pieceAt(position);
            if (piece != nullptr && piece->side() == side)
            {
                result.emplace_back(position, piece);
            }
        }
    }
    return result;
}

std::vector<Move> Board::generatePseudoLegalMoves(const Side side) const
{
    std::vector<Move> moves;
    for (const auto& [from, piece] : pieces(side))
    {
        for (const auto& to : piece->generatePseudoLegalMoves(*this, from))
        {
            Move move;
            move.from = from;
            move.to = to;
            move.side = side;
            move.piece_type = piece->type();
            const auto* target_piece = pieceAt(to);
            if (target_piece != nullptr)
            {
                move.captured_type = target_piece->type();
                move.captured_side = target_piece->side();
            }
            moves.push_back(move);
        }
    }
    return moves;
}

std::vector<Move> Board::generateLegalMoves(const Side side) const
{
    std::vector<Move> legal_moves;
    for (auto move : generatePseudoLegalMoves(side))
    {
        Board clone(*this);
        clone.applyMove(move);
        if (clone.hasFlyingKings() || clone.isInCheck(side))
        {
            continue;
        }
        legal_moves.push_back(move);
    }
    return legal_moves;
}

void Board::applyMove(Move& move)
{
    auto& from_cell = cells_[static_cast<size_t>(index(move.from))];
    auto& to_cell = cells_[static_cast<size_t>(index(move.to))];
    if (from_cell == nullptr)
    {
        throw IllegalMoveError("No piece on the source position.");
    }

    if (to_cell != nullptr)
    {
        move.captured_type = to_cell->type();
        move.captured_side = to_cell->side();
    }
    else
    {
        move.captured_type.reset();
        move.captured_side.reset();
    }

    to_cell = std::move(from_cell);
}

void Board::revertMove(const Move& move)
{
    auto& from_cell = cells_[static_cast<size_t>(index(move.from))];
    auto& to_cell = cells_[static_cast<size_t>(index(move.to))];
    if (to_cell == nullptr)
    {
        throw IllegalMoveError("No piece on the destination position during revert.");
    }

    from_cell = std::move(to_cell);
    if (move.captured_type.has_value() && move.captured_side.has_value())
    {
        to_cell = createPiece(*move.captured_type, *move.captured_side);
    }
}

Position<int> Board::findKing(const Side side) const
{
    for (int row = 0; row < config_.rows; ++row)
    {
        for (int col = 0; col < config_.cols; ++col)
        {
            const Position<int> position{ row, col };
            const auto* piece = pieceAt(position);
            if (piece != nullptr && piece->side() == side && piece->type() == PieceType::King)
            {
                return position;
            }
        }
    }

    throw IllegalMoveError("King is missing from the board.");
}

std::string Board::serializeGrid() const
{
    std::ostringstream output;
    for (int row = 0; row < config_.rows; ++row)
    {
        for (int col = 0; col < config_.cols; ++col)
        {
            const auto* piece = pieceAt({ row, col });
            if (piece == nullptr)
            {
                output << "__";
            }
            else
            {
                output << (piece->side() == Side::Red ? 'r' : 'b') << piece->symbol();
            }

            if (col + 1 < config_.cols)
            {
                output << ' ';
            }
        }
        if (row + 1 < config_.rows)
        {
            output << '\n';
        }
    }
    return output.str();
}

std::string Board::hashString() const
{
    std::ostringstream output;
    output << toString(config_.mode) << '|';
    for (int row = 0; row < config_.rows; ++row)
    {
        for (int col = 0; col < config_.cols; ++col)
        {
            const auto* piece = pieceAt({ row, col });
            if (piece == nullptr)
            {
                output << "__";
            }
            else
            {
                output << (piece->side() == Side::Red ? 'r' : 'b') << piece->symbol();
            }
        }
    }
    return output.str();
}

Board Board::deserialize(const BoardMode mode, const std::vector<std::string>& lines)
{
    Board board(makeBoardConfig(mode));
    board.cells_.clear();
    board.cells_.resize(static_cast<size_t>(board.config_.rows * board.config_.cols));

    if (static_cast<int>(lines.size()) != board.config_.rows)
    {
        throw StorageError("Invalid board row count in saved file.");
    }

    for (int row = 0; row < board.config_.rows; ++row)
    {
        std::istringstream line_stream(lines[static_cast<size_t>(row)]);
        for (int col = 0; col < board.config_.cols; ++col)
        {
            std::string token;
            if (!(line_stream >> token))
            {
                throw StorageError("Invalid board cell data in saved file.");
            }

            if (token == "__")
            {
                continue;
            }

            if (token.size() != 2)
            {
                throw StorageError("Invalid piece token in saved file.");
            }

            Side side = Side::Red;
            if (token[0] == 'r')
            {
                side = Side::Red;
            }
            else if (token[0] == 'b')
            {
                side = Side::Black;
            }
            else
            {
                throw StorageError("Invalid piece side token in saved file.");
            }

            PieceType piece_type = PieceType::Pawn;
            switch (token[1])
            {
            case 'K':
                piece_type = PieceType::King;
                break;
            case 'A':
                piece_type = PieceType::Advisor;
                break;
            case 'E':
                piece_type = PieceType::Elephant;
                break;
            case 'H':
                piece_type = PieceType::Knight;
                break;
            case 'R':
                piece_type = PieceType::Rook;
                break;
            case 'C':
                piece_type = PieceType::Cannon;
                break;
            case 'P':
                piece_type = PieceType::Pawn;
                break;
            default:
                throw StorageError("Unknown piece token in saved file.");
            }

            board.setPiece({ row, col }, createPiece(piece_type, side));
        }

        std::string extra_token;
        if (line_stream >> extra_token)
        {
            throw StorageError("Extra board cell data in saved file.");
        }
    }

    return board;
}

Board Board::operator+(const Move& move) const
{
    Board copy(*this);
    auto applied_move = move;
    copy.applyMove(applied_move);
    return copy;
}

Board Board::operator-(const Move& move) const
{
    Board copy(*this);
    copy.revertMove(move);
    return copy;
}

bool Board::operator==(const Board& other) const
{
    if (config_.mode != other.config_.mode || config_.rows != other.config_.rows || config_.cols != other.config_.cols)
    {
        return false;
    }

    for (int row = 0; row < config_.rows; ++row)
    {
        for (int col = 0; col < config_.cols; ++col)
        {
            const auto* left = pieceAt({ row, col });
            const auto* right = other.pieceAt({ row, col });
            if ((left == nullptr) != (right == nullptr))
            {
                return false;
            }
            if (left != nullptr && (left->side() != right->side() || left->type() != right->type()))
            {
                return false;
            }
        }
    }
    return true;
}

std::ostream& operator<<(std::ostream& output, const Board& board)
{
    output << "    ";
    for (char file : board.config_.coordinate_files)
    {
        output << std::setw(3) << file;
    }
    output << '\n';

    for (int row = 0; row < board.config_.rows; ++row)
    {
        output << std::setw(3) << row << ' ';
        for (int col = 0; col < board.config_.cols; ++col)
        {
            const auto* piece = board.pieceAt({ row, col });
            if (piece == nullptr)
            {
                output << std::setw(3) << "..";
            }
            else
            {
                std::string token;
                token += piece->side() == Side::Red ? 'r' : 'b';
                token += piece->symbol();
                output << std::setw(3) << token;
            }
        }
        output << '\n';
    }
    return output;
}

int Board::index(const Position<int> position) const noexcept
{
    return position.row * config_.cols + position.col;
}

bool Board::isEnemy(const Position<int> position, const Side side) const
{
    const auto* piece = pieceAt(position);
    return piece != nullptr && piece->side() != side;
}

void Board::setPiece(const Position<int> position, std::unique_ptr<ChessPiece> piece)
{
    cells_[static_cast<size_t>(index(position))] = std::move(piece);
}

} // namespace xiangqi
