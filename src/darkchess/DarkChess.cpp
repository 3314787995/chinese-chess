#include "darkchess/DarkChess.h"

#include <algorithm>
#include <cctype>
#include <random>
#include <sstream>
#include <unordered_map>

namespace xiangqi
{

namespace
{

constexpr int kQuietPlyDrawLimit = 50;

std::string trimCopy(const std::string& input)
{
    const auto begin = std::find_if_not(input.begin(), input.end(), [](const unsigned char ch) { return std::isspace(ch) != 0; });
    const auto end = std::find_if_not(input.rbegin(), input.rend(), [](const unsigned char ch) { return std::isspace(ch) != 0; }).base();
    if (begin >= end)
    {
        return {};
    }
    return std::string(begin, end);
}

std::string toLowerAscii(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char ch)
        {
            return static_cast<char>(std::tolower(ch));
        });
    return value;
}

Side parseSideToken(const std::string& value)
{
    return value == "Black" || value == "black" || value == "b" ? Side::Black : Side::Red;
}

DarkSeat parseSeatToken(const std::string& value)
{
    return value == "Player2" || value == "player2" || value == "2" ? DarkSeat::Player2 : DarkSeat::Player1;
}

std::optional<Side> parseOptionalSide(const std::string& value)
{
    if (value == "Unknown" || value == "None" || value.empty())
    {
        return std::nullopt;
    }
    return parseSideToken(value);
}

GameResult parseGameResultToken(const std::string& value)
{
    if (value == "RedWin")
    {
        return GameResult::RedWin;
    }
    if (value == "BlackWin")
    {
        return GameResult::BlackWin;
    }
    if (value == "Draw")
    {
        return GameResult::Draw;
    }
    if (value == "Timeout")
    {
        return GameResult::Timeout;
    }
    if (value == "Resign")
    {
        return GameResult::Resign;
    }
    return GameResult::Ongoing;
}

int pieceLevel(const PieceType type)
{
    switch (type)
    {
    case PieceType::King:
        return 7;
    case PieceType::Advisor:
        return 6;
    case PieceType::Elephant:
        return 5;
    case PieceType::Rook:
        return 4;
    case PieceType::Knight:
        return 3;
    case PieceType::Cannon:
        return 2;
    case PieceType::Pawn:
        return 1;
    default:
        return 0;
    }
}

PieceType pieceTypeFromLetter(const char letter)
{
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(letter))))
    {
    case 'K':
        return PieceType::King;
    case 'A':
        return PieceType::Advisor;
    case 'E':
        return PieceType::Elephant;
    case 'R':
        return PieceType::Rook;
    case 'H':
    case 'N':
        return PieceType::Knight;
    case 'C':
        return PieceType::Cannon;
    case 'P':
        return PieceType::Pawn;
    default:
        throw StorageError("Invalid dark chess piece token.");
    }
}

char pieceLetterForDark(const PieceType type)
{
    return pieceLetter(type);
}

std::optional<DarkPiece> darkPieceFromToken(const std::string& token)
{
    if (token.size() != 2 || (token[0] != 'r' && token[0] != 'b'))
    {
        return std::nullopt;
    }

    DarkPiece piece;
    piece.side = token[0] == 'r' ? Side::Red : Side::Black;
    piece.type = pieceTypeFromLetter(token[1]);
    piece.is_open = true;
    return piece;
}

std::string escapeTextField(const std::string& value)
{
    std::string escaped;
    for (const char ch : value)
    {
        switch (ch)
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '=':
            escaped += "\\e";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

std::string unescapeTextField(const std::string& value)
{
    std::string result;
    bool escaped = false;
    for (const char ch : value)
    {
        if (!escaped)
        {
            if (ch == '\\')
            {
                escaped = true;
            }
            else
            {
                result.push_back(ch);
            }
            continue;
        }

        if (ch == 'n')
        {
            result.push_back('\n');
        }
        else if (ch == 'e')
        {
            result.push_back('=');
        }
        else
        {
            result.push_back(ch);
        }
        escaped = false;
    }
    if (escaped)
    {
        result.push_back('\\');
    }
    return result;
}

std::vector<DarkPiece> makeDarkPieceSet()
{
    std::vector<DarkPiece> pieces;
    auto add = [&](const Side side, const PieceType type, const int count)
    {
        for (int i = 0; i < count; ++i)
        {
            DarkPiece piece;
            piece.side = side;
            piece.type = type;
            piece.is_open = false;
            pieces.push_back(piece);
        }
    };

    for (const Side side : { Side::Red, Side::Black })
    {
        add(side, PieceType::King, 1);
        add(side, PieceType::Advisor, 2);
        add(side, PieceType::Elephant, 2);
        add(side, PieceType::Rook, 2);
        add(side, PieceType::Knight, 2);
        add(side, PieceType::Cannon, 2);
        add(side, PieceType::Pawn, 5);
    }
    return pieces;
}

bool isAdjacentOrthogonal(const Position<int> from, const Position<int> to)
{
    return std::abs(from.row - to.row) + std::abs(from.col - to.col) == 1;
}

bool sameLine(const Position<int> from, const Position<int> to)
{
    return from.row == to.row || from.col == to.col;
}

} // namespace

DarkAction DarkAction::flip(const Position<int> position, const DarkSeat seat)
{
    DarkAction action;
    action.type = DarkActionType::Flip;
    action.from = position;
    action.to = position;
    action.seat = seat;
    return action;
}

DarkAction DarkAction::move(const Position<int> from, const Position<int> to, const DarkSeat seat)
{
    DarkAction action;
    action.type = DarkActionType::Move;
    action.from = from;
    action.to = to;
    action.seat = seat;
    return action;
}

DarkBoard::DarkBoard()
{
    setup(0);
}

DarkBoard::DarkBoard(const uint32_t seed)
{
    setup(seed);
}

bool DarkBoard::isInside(const Position<int> position) const noexcept
{
    return position.row >= 0 && position.row < kRows && position.col >= 0 && position.col < kCols;
}

bool DarkBoard::isOccupied(const Position<int> position) const
{
    return isInside(position) && cellAt(position).occupied;
}

bool DarkBoard::isOpen(const Position<int> position) const
{
    return isInside(position) && cellAt(position).occupied && cellAt(position).piece.is_open;
}

std::optional<DarkPiece> DarkBoard::pieceAt(const Position<int> position) const
{
    if (!isInside(position))
    {
        return std::nullopt;
    }
    const Cell& cell = cellAt(position);
    if (!cell.occupied || !cell.identity_known)
    {
        return std::nullopt;
    }
    return cell.piece;
}

int DarkBoard::occupiedCount() const noexcept
{
    return static_cast<int>(std::count_if(cells_.begin(), cells_.end(), [](const Cell& cell) { return cell.occupied; }));
}

int DarkBoard::openCount() const noexcept
{
    return static_cast<int>(std::count_if(
        cells_.begin(),
        cells_.end(),
        [](const Cell& cell)
        {
            return cell.occupied && cell.piece.is_open;
        }));
}

int DarkBoard::hiddenCount() const noexcept
{
    return occupiedCount() - openCount();
}

int DarkBoard::countPieces(const Side side) const noexcept
{
    return static_cast<int>(std::count_if(
        cells_.begin(),
        cells_.end(),
        [side](const Cell& cell)
        {
            return cell.occupied && cell.identity_known && cell.piece.side == side;
        }));
}

int DarkBoard::countObstacles(const Position<int> from, const Position<int> to) const
{
    if (!sameLine(from, to))
    {
        return 0;
    }

    const int row_step = to.row == from.row ? 0 : (to.row > from.row ? 1 : -1);
    const int col_step = to.col == from.col ? 0 : (to.col > from.col ? 1 : -1);
    int count = 0;
    Position<int> current{ from.row + row_step, from.col + col_step };
    while (current != to)
    {
        if (isOccupied(current))
        {
            ++count;
        }
        current.row += row_step;
        current.col += col_step;
    }
    return count;
}

void DarkBoard::setup(const uint32_t seed)
{
    cells_.fill(Cell{});
    auto pieces = makeDarkPieceSet();
    std::mt19937 rng(seed == 0 ? std::random_device{}() : seed);
    std::shuffle(pieces.begin(), pieces.end(), rng);

    for (size_t index = 0; index < pieces.size(); ++index)
    {
        cells_[index].occupied = true;
        cells_[index].identity_known = true;
        cells_[index].piece = pieces[index];
    }
}

DarkPiece DarkBoard::reveal(const Position<int> position)
{
    if (!isInside(position))
    {
        throw IllegalMoveError("Dark chess flip is outside the board.");
    }
    Cell& cell = cellAt(position);
    if (!cell.occupied)
    {
        throw IllegalMoveError("Cannot flip an empty square.");
    }
    if (cell.piece.is_open)
    {
        throw IllegalMoveError("The selected dark chess piece is already open.");
    }
    if (!cell.identity_known)
    {
        throw IllegalMoveError("Cannot reveal a public-only hidden piece.");
    }
    cell.piece.is_open = true;
    return cell.piece;
}

void DarkBoard::movePiece(const Position<int> from, const Position<int> to)
{
    if (!isInside(from) || !isInside(to))
    {
        throw IllegalMoveError("Dark chess move is outside the board.");
    }
    Cell& source = cellAt(from);
    Cell& target = cellAt(to);
    if (!source.occupied || !source.identity_known)
    {
        throw IllegalMoveError("No movable piece on source square.");
    }
    if (target.occupied)
    {
        target = Cell{};
    }
    target = source;
    source = Cell{};
}

DarkPiece DarkBoard::removePiece(const Position<int> position)
{
    if (!isInside(position))
    {
        throw IllegalMoveError("Dark chess removal is outside the board.");
    }
    Cell& cell = cellAt(position);
    if (!cell.occupied || !cell.identity_known)
    {
        throw IllegalMoveError("No known piece on the selected square.");
    }
    const DarkPiece removed = cell.piece;
    cell = Cell{};
    return removed;
}

std::string DarkBoard::publicGridString() const
{
    std::ostringstream output;
    for (int row = 0; row < kRows; ++row)
    {
        for (int col = 0; col < kCols; ++col)
        {
            const Cell& cell = cellAt({ row, col });
            if (!cell.occupied)
            {
                output << "__";
            }
            else if (!cell.piece.is_open)
            {
                output << "xx";
            }
            else
            {
                output << darkPieceToken(cell.piece) << '+';
            }

            if (col + 1 < kCols)
            {
                output << ' ';
            }
        }
        if (row + 1 < kRows)
        {
            output << '\n';
        }
    }
    return output.str();
}

std::string DarkBoard::privateGridString() const
{
    std::ostringstream output;
    for (int row = 0; row < kRows; ++row)
    {
        for (int col = 0; col < kCols; ++col)
        {
            const Cell& cell = cellAt({ row, col });
            if (!cell.occupied)
            {
                output << "__";
            }
            else if (!cell.identity_known)
            {
                output << "xx";
            }
            else
            {
                output << darkPieceToken(cell.piece) << (cell.piece.is_open ? '+' : '-');
            }

            if (col + 1 < kCols)
            {
                output << ' ';
            }
        }
        if (row + 1 < kRows)
        {
            output << '\n';
        }
    }
    return output.str();
}

std::string DarkBoard::hashString(const bool include_hidden_identities) const
{
    return include_hidden_identities ? privateGridString() : publicGridString();
}

DarkBoard DarkBoard::deserialize(const std::vector<std::string>& lines)
{
    if (static_cast<int>(lines.size()) != kRows)
    {
        throw StorageError("Invalid dark chess row count in saved file.");
    }

    DarkBoard board;
    board.cells_.fill(Cell{});
    for (int row = 0; row < kRows; ++row)
    {
        std::istringstream input(lines[static_cast<size_t>(row)]);
        for (int col = 0; col < kCols; ++col)
        {
            std::string token;
            if (!(input >> token))
            {
                throw StorageError("Invalid dark chess cell data in saved file.");
            }
            if (token == "__")
            {
                continue;
            }
            Cell cell;
            cell.occupied = true;
            if (token == "xx")
            {
                cell.identity_known = false;
                cell.piece.is_open = false;
                board.cells_[static_cast<size_t>(board.index({ row, col }))] = cell;
                continue;
            }
            if (token.size() != 3)
            {
                throw StorageError("Invalid dark chess piece token in saved file.");
            }
            cell.identity_known = true;
            if (token[0] == 'r')
            {
                cell.piece.side = Side::Red;
            }
            else if (token[0] == 'b')
            {
                cell.piece.side = Side::Black;
            }
            else
            {
                throw StorageError("Invalid dark chess side token in saved file.");
            }
            cell.piece.type = pieceTypeFromLetter(token[1]);
            cell.piece.is_open = token[2] == '+';
            board.cells_[static_cast<size_t>(board.index({ row, col }))] = cell;
        }
        std::string extra;
        if (input >> extra)
        {
            throw StorageError("Dark chess row has too many cells.");
        }
    }
    return board;
}

int DarkBoard::index(const Position<int> position) const noexcept
{
    return position.row * kCols + position.col;
}

DarkBoard::Cell& DarkBoard::cellAt(const Position<int> position)
{
    return cells_[static_cast<size_t>(index(position))];
}

const DarkBoard::Cell& DarkBoard::cellAt(const Position<int> position) const
{
    return cells_[static_cast<size_t>(index(position))];
}

DarkGameSession::DarkGameSession() : DarkGameSession(GameSettings{}, PlayerInfo{}, 0) {}

DarkGameSession::DarkGameSession(const uint32_t seed) : DarkGameSession(GameSettings{}, PlayerInfo{}, seed) {}

DarkGameSession::DarkGameSession(GameSettings settings, PlayerInfo players, const uint32_t seed)
    : settings_(std::move(settings)),
      players_(std::move(players))
{
    settings_.game_kind = GameKind::DarkChess;
    startNewGame(seed);
}

void DarkGameSession::startNewGame(const uint32_t seed)
{
    board_.setup(seed);
    initial_private_grid_ = board_.privateGridString();
    current_seat_ = DarkSeat::Player1;
    result_ = GameResult::Ongoing;
    seat_colors_ = {};
    history_.clear();
    undo_snapshots_.clear();
    position_history_.clear();
    quiet_plies_ = 0;
    remaining_time_ms_[0] = settings_.move_time_limit_seconds * 1000;
    remaining_time_ms_[1] = settings_.move_time_limit_seconds * 1000;
    position_history_.push_back(boardHashWithTurn());
    resetClockForTurn();
}

int DarkGameSession::remainingSeconds(const DarkSeat seat) const noexcept
{
    return std::max(0, remaining_time_ms_[seatIndex(seat)] / 1000);
}

int DarkGameSession::remainingMilliseconds(const DarkSeat seat) const noexcept
{
    return std::max(0, remaining_time_ms_[seatIndex(seat)]);
}

std::optional<Side> DarkGameSession::colorForSeat(const DarkSeat seat) const noexcept
{
    return seat_colors_[static_cast<size_t>(seatIndex(seat))];
}

std::optional<DarkSeat> DarkGameSession::seatForColor(const Side side) const noexcept
{
    if (seat_colors_[0].has_value() && *seat_colors_[0] == side)
    {
        return DarkSeat::Player1;
    }
    if (seat_colors_[1].has_value() && *seat_colors_[1] == side)
    {
        return DarkSeat::Player2;
    }
    return std::nullopt;
}

std::string DarkGameSession::currentPlayerName() const
{
    return current_seat_ == DarkSeat::Player1 ? players_.red_name : players_.black_name;
}

std::string DarkGameSession::resultText() const
{
    if (result_ == GameResult::Ongoing)
    {
        return "Dark chess game is ongoing.";
    }
    if (result_ == GameResult::Draw)
    {
        return "Dark chess game ended in a draw.";
    }
    if (result_ == GameResult::Timeout)
    {
        return toString(current_seat_) + " loses on time.";
    }
    if (result_ == GameResult::Resign)
    {
        return toString(current_seat_) + " resigned.";
    }
    return result_ == GameResult::RedWin ? "Red wins." : "Black wins.";
}

std::vector<DarkAction> DarkGameSession::legalActions(const DarkSeat seat) const
{
    if (gameOver())
    {
        return {};
    }

    std::vector<DarkAction> actions;
    for (int row = 0; row < DarkBoard::kRows; ++row)
    {
        for (int col = 0; col < DarkBoard::kCols; ++col)
        {
            const Position<int> position{ row, col };
            if (board_.isOccupied(position) && !board_.isOpen(position))
            {
                actions.push_back(DarkAction::flip(position, seat));
            }
        }
    }

    const auto own_color = colorForSeat(seat);
    if (!own_color.has_value())
    {
        return actions;
    }

    for (int row = 0; row < DarkBoard::kRows; ++row)
    {
        for (int col = 0; col < DarkBoard::kCols; ++col)
        {
            const Position<int> from{ row, col };
            const auto piece = board_.pieceAt(from);
            if (!piece.has_value() || !piece->is_open || piece->side != *own_color)
            {
                continue;
            }

            for (int to_row = 0; to_row < DarkBoard::kRows; ++to_row)
            {
                for (int to_col = 0; to_col < DarkBoard::kCols; ++to_col)
                {
                    const Position<int> to{ to_row, to_col };
                    if (from == to)
                    {
                        continue;
                    }
                    DarkAction action = DarkAction::move(from, to, seat);
                    if (isLegalAction(action))
                    {
                        actions.push_back(action);
                    }
                }
            }
        }
    }
    return actions;
}

std::vector<DarkAction> DarkGameSession::legalActionsForCurrentSeat() const
{
    return legalActions(current_seat_);
}

std::vector<DarkAction> DarkGameSession::legalActionsFrom(const Position<int> position) const
{
    std::vector<DarkAction> result;
    for (const auto& action : legalActionsForCurrentSeat())
    {
        if (action.from == position)
        {
            result.push_back(action);
        }
    }
    return result;
}

bool DarkGameSession::hasAnyLegalAction(const DarkSeat seat) const
{
    return !legalActions(seat).empty();
}

DarkAction DarkGameSession::submitAction(const DarkAction& candidate_action)
{
    if (gameOver())
    {
        throw IllegalMoveError("The dark chess game has already finished.");
    }
    if (candidate_action.seat != current_seat_)
    {
        throw IllegalMoveError("It is not this dark chess seat's turn.");
    }

    consumeCurrentTurnTime();
    if (gameOver())
    {
        throw IllegalMoveError("Time has already expired.");
    }

    if (!isLegalAction(candidate_action))
    {
        throw IllegalMoveError("Illegal dark chess action.");
    }

    undo_snapshots_.push_back(serialize());
    DarkAction action = candidate_action;
    if (action.type == DarkActionType::Flip)
    {
        action.revealed_piece = board_.reveal(action.from);
        if (!seat_colors_[0].has_value() && !seat_colors_[1].has_value())
        {
            seat_colors_[static_cast<size_t>(seatIndex(action.seat))] = action.revealed_piece->side;
            seat_colors_[static_cast<size_t>(seatIndex(opposite(action.seat)))] = opposite(action.revealed_piece->side);
        }
        quiet_plies_ = 0;
    }
    else
    {
        const auto target = board_.pieceAt(action.to);
        if (target.has_value())
        {
            action.captured_piece = *target;
            (void)board_.removePiece(action.to);
            quiet_plies_ = 0;
        }
        else
        {
            ++quiet_plies_;
        }
        board_.movePiece(action.from, action.to);
    }

    if (action.notation.empty())
    {
        action.notation = darkActionToText(action);
    }
    history_.push_back(action);
    switchTurnAndEvaluate(history_.back());
    resetClockForTurn();
    return history_.back();
}

bool DarkGameSession::undoLastPly()
{
    return undoLastPlies(1) > 0;
}

int DarkGameSession::undoLastPlies(const int count)
{
    if (!settings_.allow_undo || undo_snapshots_.empty() || count <= 0)
    {
        return 0;
    }

    if (!gameOver())
    {
        consumeCurrentTurnTime();
    }

    int undone = 0;
    while (undone < count && !undo_snapshots_.empty())
    {
        std::vector<std::string> previous_snapshots = undo_snapshots_;
        const std::string snapshot = previous_snapshots.back();
        previous_snapshots.pop_back();
        *this = deserialize(snapshot);
        undo_snapshots_ = std::move(previous_snapshots);
        ++undone;
    }

    resetClockForTurn();
    return undone;
}

void DarkGameSession::resign(const DarkSeat seat)
{
    current_seat_ = seat;
    result_ = GameResult::Resign;
}

void DarkGameSession::tickClock()
{
    consumeCurrentTurnTime();
}

std::string DarkGameSession::serialize() const
{
    std::ostringstream output;
    output << "VERSION=1\n";
    output << "GAME=DarkChess\n";
    output << "CURRENT_SEAT=" << toString(current_seat_) << '\n';
    output << "RESULT=" << toString(result_) << '\n';
    output << "PLAYER1_NAME=" << escapeTextField(players_.red_name) << '\n';
    output << "PLAYER2_NAME=" << escapeTextField(players_.black_name) << '\n';
    output << "PLAYER1_COLOR=" << (seat_colors_[0].has_value() ? toString(*seat_colors_[0]) : "Unknown") << '\n';
    output << "PLAYER2_COLOR=" << (seat_colors_[1].has_value() ? toString(*seat_colors_[1]) : "Unknown") << '\n';
    output << "MOVE_TIME=" << settings_.move_time_limit_seconds << '\n';
    output << "ALLOW_UNDO=" << (settings_.allow_undo ? 1 : 0) << '\n';
    output << "SHOW_LEGAL=" << (settings_.show_legal_moves ? 1 : 0) << '\n';
    output << "AI_ENABLED=" << (settings_.ai_enabled ? 1 : 0) << '\n';
    output << "AI_SEAT=" << toString(settings_.dark_ai_seat) << '\n';
    output << "USE_EASYX=" << (settings_.use_easyx ? 1 : 0) << '\n';
    output << "PLAYER1_REMAINING_MS=" << remaining_time_ms_[0] << '\n';
    output << "PLAYER2_REMAINING_MS=" << remaining_time_ms_[1] << '\n';
    output << "QUIET_PLIES=" << quiet_plies_ << '\n';
    output << "INITIAL_BOARD_BEGIN\n" << initial_private_grid_ << "\nINITIAL_BOARD_END\n";
    output << "BOARD_BEGIN\n" << board_.privateGridString() << "\nBOARD_END\n";
    output << "HISTORY_BEGIN\n";
    for (const auto& action : history_)
    {
        output << serializeAction(action) << '\n';
    }
    output << "HISTORY_END\n";
    return output.str();
}

std::string DarkGameSession::serializePublic() const
{
    std::ostringstream output;
    output << "VERSION=1\n";
    output << "GAME=DarkChess\n";
    output << "CURRENT_SEAT=" << toString(current_seat_) << '\n';
    output << "RESULT=" << toString(result_) << '\n';
    output << "PLAYER1_NAME=" << escapeTextField(players_.red_name) << '\n';
    output << "PLAYER2_NAME=" << escapeTextField(players_.black_name) << '\n';
    output << "PLAYER1_COLOR=" << (seat_colors_[0].has_value() ? toString(*seat_colors_[0]) : "Unknown") << '\n';
    output << "PLAYER2_COLOR=" << (seat_colors_[1].has_value() ? toString(*seat_colors_[1]) : "Unknown") << '\n';
    output << "MOVE_TIME=" << settings_.move_time_limit_seconds << '\n';
    output << "ALLOW_UNDO=" << (settings_.allow_undo ? 1 : 0) << '\n';
    output << "SHOW_LEGAL=" << (settings_.show_legal_moves ? 1 : 0) << '\n';
    output << "AI_ENABLED=" << (settings_.ai_enabled ? 1 : 0) << '\n';
    output << "AI_SEAT=" << toString(settings_.dark_ai_seat) << '\n';
    output << "USE_EASYX=" << (settings_.use_easyx ? 1 : 0) << '\n';
    output << "PLAYER1_REMAINING_MS=" << remaining_time_ms_[0] << '\n';
    output << "PLAYER2_REMAINING_MS=" << remaining_time_ms_[1] << '\n';
    output << "QUIET_PLIES=" << quiet_plies_ << '\n';
    output << "BOARD_BEGIN\n" << board_.publicGridString() << "\nBOARD_END\n";
    output << "HISTORY_BEGIN\n";
    for (const auto& action : history_)
    {
        output << serializeAction(action) << '\n';
    }
    output << "HISTORY_END\n";
    return output.str();
}

DarkGameSession DarkGameSession::deserialize(const std::string& text)
{
    std::istringstream input(text);
    std::string line;
    std::unordered_map<std::string, std::string> kv;
    std::vector<std::string> board_lines;
    std::vector<std::string> initial_board_lines;
    std::vector<std::string> history_lines;
    bool reading_board = false;
    bool reading_initial_board = false;
    bool reading_history = false;

    while (std::getline(input, line))
    {
        if (line == "BOARD_BEGIN")
        {
            reading_board = true;
            continue;
        }
        if (line == "BOARD_END")
        {
            reading_board = false;
            continue;
        }
        if (line == "INITIAL_BOARD_BEGIN")
        {
            reading_initial_board = true;
            continue;
        }
        if (line == "INITIAL_BOARD_END")
        {
            reading_initial_board = false;
            continue;
        }
        if (line == "HISTORY_BEGIN")
        {
            reading_history = true;
            continue;
        }
        if (line == "HISTORY_END")
        {
            reading_history = false;
            continue;
        }
        if (reading_board)
        {
            board_lines.push_back(line);
            continue;
        }
        if (reading_initial_board)
        {
            initial_board_lines.push_back(line);
            continue;
        }
        if (reading_history)
        {
            if (!line.empty())
            {
                history_lines.push_back(line);
            }
            continue;
        }

        const auto delimiter = line.find('=');
        if (delimiter != std::string::npos)
        {
            kv.emplace(line.substr(0, delimiter), line.substr(delimiter + 1));
        }
    }

    GameSettings settings;
    settings.game_kind = GameKind::DarkChess;
    if (kv.count("MOVE_TIME") != 0)
    {
        settings.move_time_limit_seconds = std::stoi(kv.at("MOVE_TIME"));
    }
    if (kv.count("ALLOW_UNDO") != 0)
    {
        settings.allow_undo = parseBool(kv.at("ALLOW_UNDO"));
    }
    if (kv.count("SHOW_LEGAL") != 0)
    {
        settings.show_legal_moves = parseBool(kv.at("SHOW_LEGAL"));
    }
    if (kv.count("AI_ENABLED") != 0)
    {
        settings.ai_enabled = parseBool(kv.at("AI_ENABLED"));
    }
    if (kv.count("AI_SEAT") != 0)
    {
        settings.dark_ai_seat = parseSeatToken(kv.at("AI_SEAT"));
    }
    if (kv.count("USE_EASYX") != 0)
    {
        settings.use_easyx = parseBool(kv.at("USE_EASYX"));
    }

    PlayerInfo players;
    if (kv.count("PLAYER1_NAME") != 0)
    {
        players.red_name = unescapeTextField(kv.at("PLAYER1_NAME"));
    }
    if (kv.count("PLAYER2_NAME") != 0)
    {
        players.black_name = unescapeTextField(kv.at("PLAYER2_NAME"));
    }

    DarkGameSession session(settings, players, 1);
    session.board_ = DarkBoard::deserialize(board_lines);
    const bool has_private_initial_board = !initial_board_lines.empty();
    if (has_private_initial_board)
    {
        std::ostringstream initial;
        for (size_t index = 0; index < initial_board_lines.size(); ++index)
        {
            if (index != 0)
            {
                initial << '\n';
            }
            initial << initial_board_lines[index];
        }
        session.initial_private_grid_ = initial.str();
    }
    else
    {
        session.initial_private_grid_ = session.board_.privateGridString();
    }
    if (kv.count("CURRENT_SEAT") != 0)
    {
        session.current_seat_ = parseSeatToken(kv.at("CURRENT_SEAT"));
    }
    if (kv.count("RESULT") != 0)
    {
        session.result_ = parseGameResultToken(kv.at("RESULT"));
    }
    if (kv.count("PLAYER1_COLOR") != 0)
    {
        session.seat_colors_[0] = parseOptionalSide(kv.at("PLAYER1_COLOR"));
    }
    if (kv.count("PLAYER2_COLOR") != 0)
    {
        session.seat_colors_[1] = parseOptionalSide(kv.at("PLAYER2_COLOR"));
    }
    if (kv.count("PLAYER1_REMAINING_MS") != 0)
    {
        session.remaining_time_ms_[0] = std::stoi(kv.at("PLAYER1_REMAINING_MS"));
    }
    if (kv.count("PLAYER2_REMAINING_MS") != 0)
    {
        session.remaining_time_ms_[1] = std::stoi(kv.at("PLAYER2_REMAINING_MS"));
    }
    if (kv.count("QUIET_PLIES") != 0)
    {
        session.quiet_plies_ = std::stoi(kv.at("QUIET_PLIES"));
    }
    session.history_.clear();
    session.undo_snapshots_.clear();
    for (const auto& history_line : history_lines)
    {
        session.history_.push_back(deserializeAction(history_line));
    }
    if (has_private_initial_board && session.initial_private_grid_.find("xx") == std::string::npos)
    {
        session.rebuildPositionHistoryFromHistory();
    }
    else
    {
        session.position_history_.clear();
        session.position_history_.push_back(session.boardHashWithTurn());
    }
    session.resetClockForTurn();
    return session;
}

std::string DarkGameSession::boardHashWithTurn() const
{
    std::ostringstream output;
    output << board_.hashString(true) << '|' << toString(current_seat_) << '|';
    output << (seat_colors_[0].has_value() ? toString(*seat_colors_[0]) : "Unknown") << '|';
    output << (seat_colors_[1].has_value() ? toString(*seat_colors_[1]) : "Unknown");
    return output.str();
}

bool DarkGameSession::belongsToSeat(const DarkPiece& piece, const DarkSeat seat) const
{
    const auto color = colorForSeat(seat);
    return color.has_value() && piece.side == *color;
}

bool DarkGameSession::isLegalAction(const DarkAction& action) const
{
    if (!board_.isInside(action.from))
    {
        return false;
    }
    if (action.type == DarkActionType::Flip)
    {
        return action.from == action.to && board_.isOccupied(action.from) && !board_.isOpen(action.from);
    }

    if (!board_.isInside(action.to) || action.from == action.to)
    {
        return false;
    }
    const auto attacker = board_.pieceAt(action.from);
    if (!attacker.has_value() || !attacker->is_open || !belongsToSeat(*attacker, action.seat))
    {
        return false;
    }

    const bool target_occupied = board_.isOccupied(action.to);
    const auto target = board_.pieceAt(action.to);
    if (!target_occupied)
    {
        return isAdjacentOrthogonal(action.from, action.to);
    }
    if (!target.has_value() || !target->is_open || target->side == attacker->side)
    {
        return false;
    }

    if (attacker->type == PieceType::Cannon)
    {
        return sameLine(action.from, action.to) && board_.countObstacles(action.from, action.to) == 1;
    }
    return isAdjacentOrthogonal(action.from, action.to) && canEat(*attacker, *target);
}

bool DarkGameSession::canEat(const DarkPiece& attacker, const DarkPiece& target) const
{
    if (attacker.side == target.side)
    {
        return false;
    }
    if (attacker.type == target.type)
    {
        return true;
    }
    if (attacker.type == PieceType::Cannon)
    {
        return true;
    }
    if (attacker.type == PieceType::Pawn)
    {
        return target.type == PieceType::King;
    }
    if (attacker.type == PieceType::King)
    {
        return target.type != PieceType::Pawn;
    }
    if (attacker.type == PieceType::Knight)
    {
        return target.type == PieceType::Cannon || target.type == PieceType::Pawn;
    }
    if (attacker.type == PieceType::Rook)
    {
        return target.type != PieceType::King && target.type != PieceType::Advisor && target.type != PieceType::Elephant;
    }
    return pieceLevel(attacker.type) > pieceLevel(target.type);
}

void DarkGameSession::switchTurnAndEvaluate(const DarkAction&)
{
    evaluateGameResultAfterAction();
    if (!gameOver())
    {
        current_seat_ = opposite(current_seat_);
        position_history_.push_back(boardHashWithTurn());
        const int repeats = static_cast<int>(std::count(position_history_.begin(), position_history_.end(), boardHashWithTurn()));
        if (repeats >= 3)
        {
            result_ = GameResult::Draw;
        }
    }
}

void DarkGameSession::evaluateGameResultAfterAction()
{
    if (quiet_plies_ >= kQuietPlyDrawLimit)
    {
        result_ = GameResult::Draw;
        return;
    }

    if (seat_colors_[0].has_value() && seat_colors_[1].has_value())
    {
        const int red_count = board_.countPieces(Side::Red);
        const int black_count = board_.countPieces(Side::Black);
        if (red_count == 0 && black_count == 0)
        {
            result_ = GameResult::Draw;
            return;
        }
        if (red_count == 0)
        {
            result_ = GameResult::BlackWin;
            return;
        }
        if (black_count == 0)
        {
            result_ = GameResult::RedWin;
            return;
        }
    }

    const DarkSeat next = opposite(current_seat_);
    if (!hasAnyLegalAction(next))
    {
        const auto next_color = colorForSeat(next);
        if (next_color.has_value())
        {
            result_ = *next_color == Side::Red ? GameResult::BlackWin : GameResult::RedWin;
        }
        else
        {
            result_ = GameResult::Draw;
        }
    }
}

void DarkGameSession::resetClockForTurn()
{
    turn_started_at_ = std::chrono::steady_clock::now();
}

void DarkGameSession::consumeCurrentTurnTime()
{
    if (settings_.move_time_limit_seconds <= 0 || gameOver())
    {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const int elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - turn_started_at_).count());
    remaining_time_ms_[seatIndex(current_seat_)] -= elapsed_ms;
    turn_started_at_ = now;
    if (remaining_time_ms_[seatIndex(current_seat_)] <= 0)
    {
        remaining_time_ms_[seatIndex(current_seat_)] = 0;
        result_ = GameResult::Timeout;
    }
}

void DarkGameSession::rebuildPositionHistoryFromHistory()
{
    std::vector<std::string> initial_lines;
    std::istringstream input(initial_private_grid_);
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty())
        {
            initial_lines.push_back(line);
        }
    }

    DarkGameSession replay(settings_, players_, 1);
    replay.settings_.move_time_limit_seconds = 0;
    replay.board_ = DarkBoard::deserialize(initial_lines);
    replay.initial_private_grid_ = initial_private_grid_;
    replay.current_seat_ = DarkSeat::Player1;
    replay.result_ = GameResult::Ongoing;
    replay.seat_colors_ = !history_.empty() && history_.front().type == DarkActionType::Flip
        ? std::array<std::optional<Side>, 2>{}
        : seat_colors_;
    replay.history_.clear();
    replay.undo_snapshots_.clear();
    replay.quiet_plies_ = 0;
    replay.position_history_.clear();
    replay.position_history_.push_back(replay.boardHashWithTurn());
    replay.resetClockForTurn();

    for (const auto& action : history_)
    {
        if (replay.gameOver())
        {
            break;
        }
        replay.submitAction(action);
    }

    position_history_ = replay.position_history_;
}

std::string DarkGameSession::serializeAction(const DarkAction& action)
{
    std::ostringstream output;
    output << (action.type == DarkActionType::Flip ? 'F' : 'M') << '|'
           << toString(action.seat) << '|'
           << darkCoordText(action.from) << '|'
           << darkCoordText(action.to) << '|';
    if (action.revealed_piece.has_value())
    {
        output << darkPieceToken(*action.revealed_piece);
    }
    output << '|';
    if (action.captured_piece.has_value())
    {
        output << darkPieceToken(*action.captured_piece);
    }
    output << '|' << escapeTextField(action.notation);
    return output.str();
}

DarkAction DarkGameSession::deserializeAction(const std::string& line)
{
    std::vector<std::string> fields;
    std::string current;
    for (const char ch : line)
    {
        if (ch == '|')
        {
            fields.push_back(current);
            current.clear();
        }
        else
        {
            current.push_back(ch);
        }
    }
    fields.push_back(current);
    if (fields.size() < 4)
    {
        throw StorageError("Invalid dark chess history action.");
    }
    const DarkSeat seat = parseSeatToken(fields[1]);
    const auto from = parseDarkCoord(fields[2]);
    const auto to = parseDarkCoord(fields[3]);
    if (!from.has_value() || !to.has_value())
    {
        throw StorageError("Invalid dark chess history coordinate.");
    }
    if (fields[0] != "F" && fields[0] != "M")
    {
        throw StorageError("Invalid dark chess history action type.");
    }
    DarkAction action = fields[0] == "F" ? DarkAction::flip(*from, seat) : DarkAction::move(*from, *to, seat);
    if (fields.size() > 4 && !fields[4].empty())
    {
        action.revealed_piece = darkPieceFromToken(fields[4]);
        if (!action.revealed_piece.has_value())
        {
            throw StorageError("Invalid dark chess revealed piece in history.");
        }
    }
    if (fields.size() > 5 && !fields[5].empty())
    {
        action.captured_piece = darkPieceFromToken(fields[5]);
        if (!action.captured_piece.has_value())
        {
            throw StorageError("Invalid dark chess captured piece in history.");
        }
    }
    if (fields.size() > 6)
    {
        action.notation = unescapeTextField(fields[6]);
    }
    return action;
}

std::optional<DarkAction> DarkSearchEngine::chooseAction(const DarkGameSession& session) const
{
    const auto actions = session.legalActionsForCurrentSeat();
    if (actions.empty())
    {
        return std::nullopt;
    }

    auto score = [&](const DarkAction& action)
    {
        int value = action.type == DarkActionType::Flip ? 5 : 1;
        if (action.type == DarkActionType::Move)
        {
            const auto attacker = session.board().pieceAt(action.from);
            const auto target = session.board().pieceAt(action.to);
            if (attacker.has_value() && target.has_value() && target->is_open)
            {
                value += 100 + pieceLevel(target->type) * 10 - pieceLevel(attacker->type);
            }
            else if (attacker.has_value())
            {
                value += pieceLevel(attacker->type);
            }
        }
        value -= action.from.row * 2 + action.from.col;
        return value;
    };

    return *std::max_element(
        actions.begin(),
        actions.end(),
        [&](const DarkAction& left, const DarkAction& right)
        {
            return score(left) < score(right);
        });
}

std::string darkCoordText(const Position<int> position)
{
    std::string text;
    text.push_back(static_cast<char>('a' + position.col));
    text.push_back(static_cast<char>('0' + position.row));
    return text;
}

std::optional<Position<int>> parseDarkCoord(const std::string& text)
{
    if (text.size() != 2)
    {
        return std::nullopt;
    }
    const int col = static_cast<int>(std::tolower(static_cast<unsigned char>(text[0])) - 'a');
    const int row = text[1] - '0';
    if (row < 0 || row >= DarkBoard::kRows || col < 0 || col >= DarkBoard::kCols)
    {
        return std::nullopt;
    }
    return Position<int>{ row, col };
}

std::string darkPieceToken(const DarkPiece& piece)
{
    std::string token;
    token.push_back(piece.side == Side::Red ? 'r' : 'b');
    token.push_back(pieceLetterForDark(piece.type));
    return token;
}

std::string darkActionToText(const DarkAction& action)
{
    if (action.type == DarkActionType::Flip)
    {
        std::string text = "flip ";
        text += darkCoordText(action.from);
        if (action.revealed_piece.has_value())
        {
            text += '(';
            text += darkPieceToken(*action.revealed_piece);
            text += ')';
        }
        return text;
    }
    return darkCoordText(action.from) + " " + darkCoordText(action.to);
}

DarkAction parseDarkActionText(const std::string& text, const DarkGameSession& session)
{
    const std::string trimmed = trimCopy(text);
    const std::string lower = toLowerAscii(trimmed);
    if (lower.rfind("flip ", 0) == 0 || lower.rfind("open ", 0) == 0)
    {
        const auto position = parseDarkCoord(trimCopy(trimmed.substr(trimmed.find(' ') + 1)));
        if (!position.has_value())
        {
            throw InputError("Invalid dark chess flip coordinate.");
        }
        return DarkAction::flip(*position, session.currentSeat());
    }

    std::istringstream input(trimmed);
    std::string from_text;
    std::string to_text;
    if (input >> from_text >> to_text)
    {
        const auto from = parseDarkCoord(from_text);
        const auto to = parseDarkCoord(to_text);
        if (from.has_value() && to.has_value())
        {
            return DarkAction::move(*from, *to, session.currentSeat());
        }
    }
    throw InputError("Unknown dark chess command or move format.");
}

} // namespace xiangqi
