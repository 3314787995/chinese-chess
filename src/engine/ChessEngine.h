#pragma once

#include "common/Types.h"

#include <iosfwd>
#include <memory>
#include <utility>

namespace xiangqi
{

class Board;

class ChessPiece
{
public:
    explicit ChessPiece(Side side) : side_(side) {}
    virtual ~ChessPiece() = default;

    Side side() const noexcept { return side_; }
    virtual PieceType type() const noexcept = 0;
    virtual char symbol() const noexcept = 0;
    virtual int value() const noexcept = 0;
    virtual std::unique_ptr<ChessPiece> clone() const = 0;
    virtual bool isPseudoLegal(const Board& board, Position<int> from, Position<int> to) const = 0;
    virtual std::vector<Position<int>> generatePseudoLegalMoves(const Board& board, Position<int> from) const = 0;

    bool operator<(const ChessPiece& other) const noexcept
    {
        return value() < other.value();
    }

    bool operator>(const ChessPiece& other) const noexcept
    {
        return value() > other.value();
    }

private:
    Side side_;
};

class KingPiece final : public ChessPiece
{
public:
    using ChessPiece::ChessPiece;
    PieceType type() const noexcept override { return PieceType::King; }
    char symbol() const noexcept override { return 'K'; }
    int value() const noexcept override { return 10000; }
    std::unique_ptr<ChessPiece> clone() const override;
    bool isPseudoLegal(const Board& board, Position<int> from, Position<int> to) const override;
    std::vector<Position<int>> generatePseudoLegalMoves(const Board& board, Position<int> from) const override;
};

class AdvisorPiece final : public ChessPiece
{
public:
    using ChessPiece::ChessPiece;
    PieceType type() const noexcept override { return PieceType::Advisor; }
    char symbol() const noexcept override { return 'A'; }
    int value() const noexcept override { return 120; }
    std::unique_ptr<ChessPiece> clone() const override;
    bool isPseudoLegal(const Board& board, Position<int> from, Position<int> to) const override;
    std::vector<Position<int>> generatePseudoLegalMoves(const Board& board, Position<int> from) const override;
};

class ElephantPiece final : public ChessPiece
{
public:
    using ChessPiece::ChessPiece;
    PieceType type() const noexcept override { return PieceType::Elephant; }
    char symbol() const noexcept override { return 'E'; }
    int value() const noexcept override { return 120; }
    std::unique_ptr<ChessPiece> clone() const override;
    bool isPseudoLegal(const Board& board, Position<int> from, Position<int> to) const override;
    std::vector<Position<int>> generatePseudoLegalMoves(const Board& board, Position<int> from) const override;
};

class KnightPiece final : public ChessPiece
{
public:
    using ChessPiece::ChessPiece;
    PieceType type() const noexcept override { return PieceType::Knight; }
    char symbol() const noexcept override { return 'H'; }
    int value() const noexcept override { return 300; }
    std::unique_ptr<ChessPiece> clone() const override;
    bool isPseudoLegal(const Board& board, Position<int> from, Position<int> to) const override;
    std::vector<Position<int>> generatePseudoLegalMoves(const Board& board, Position<int> from) const override;
};

class RookPiece final : public ChessPiece
{
public:
    using ChessPiece::ChessPiece;
    PieceType type() const noexcept override { return PieceType::Rook; }
    char symbol() const noexcept override { return 'R'; }
    int value() const noexcept override { return 500; }
    std::unique_ptr<ChessPiece> clone() const override;
    bool isPseudoLegal(const Board& board, Position<int> from, Position<int> to) const override;
    std::vector<Position<int>> generatePseudoLegalMoves(const Board& board, Position<int> from) const override;
};

class CannonPiece final : public ChessPiece
{
public:
    using ChessPiece::ChessPiece;
    PieceType type() const noexcept override { return PieceType::Cannon; }
    char symbol() const noexcept override { return 'C'; }
    int value() const noexcept override { return 350; }
    std::unique_ptr<ChessPiece> clone() const override;
    bool isPseudoLegal(const Board& board, Position<int> from, Position<int> to) const override;
    std::vector<Position<int>> generatePseudoLegalMoves(const Board& board, Position<int> from) const override;
};

class PawnPiece final : public ChessPiece
{
public:
    using ChessPiece::ChessPiece;
    PieceType type() const noexcept override { return PieceType::Pawn; }
    char symbol() const noexcept override { return 'P'; }
    int value() const noexcept override { return 70; }
    std::unique_ptr<ChessPiece> clone() const override;
    bool isPseudoLegal(const Board& board, Position<int> from, Position<int> to) const override;
    std::vector<Position<int>> generatePseudoLegalMoves(const Board& board, Position<int> from) const override;
};

std::unique_ptr<ChessPiece> createPiece(PieceType piece_type, Side side);
BoardConfig makeBoardConfig(BoardMode mode);

class Board
{
public:
    Board();
    explicit Board(BoardMode mode);
    explicit Board(BoardConfig config);
    Board(const Board& other);
    Board& operator=(const Board& other);
    Board(Board&&) noexcept = default;
    Board& operator=(Board&&) noexcept = default;
    ~Board() = default;

    const BoardConfig& config() const noexcept { return config_; }
    void setup(const BoardConfig& config);
    void setup(BoardMode mode);

    bool isInside(Position<int> position) const noexcept;
    bool isPalaceCell(Side side, Position<int> position) const noexcept;
    int countObstacles(Position<int> from, Position<int> to) const;
    bool hasFlyingKings() const;
    bool isSquareAttacked(Position<int> position, Side by_side) const;
    bool isInCheck(Side side) const;

    ChessPiece* pieceAt(Position<int> position);
    const ChessPiece* pieceAt(Position<int> position) const;

    std::vector<std::pair<Position<int>, const ChessPiece*>> pieces(Side side) const;
    std::vector<Move> generatePseudoLegalMoves(Side side) const;
    std::vector<Move> generateLegalMoves(Side side) const;

    void applyMove(Move& move);
    void revertMove(const Move& move);

    Position<int> findKing(Side side) const;
    std::string serializeGrid() const;
    std::string hashString() const;
    static Board deserialize(BoardMode mode, const std::vector<std::string>& lines);

    Board operator+(const Move& move) const;
    Board operator-(const Move& move) const;
    bool operator==(const Board& other) const;

    friend std::ostream& operator<<(std::ostream& output, const Board& board);

private:
    int index(Position<int> position) const noexcept;
    bool isEnemy(Position<int> position, Side side) const;
    void setPiece(Position<int> position, std::unique_ptr<ChessPiece> piece);

    BoardConfig config_;
    std::vector<std::unique_ptr<ChessPiece>> cells_;
};

} // namespace xiangqi
