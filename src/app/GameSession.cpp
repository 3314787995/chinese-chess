#include "app/GameSession.h"

#include "common/Repetition.h"

#include <algorithm>
#include <sstream>

namespace xiangqi
{

namespace
{

std::string movePieceToken(const Move& move)
{
    return std::string(1, pieceLetter(move.piece_type));
}

std::string positionToText(const Position<int> position, const BoardConfig& config)
{
    if (position.col < 0 || position.col >= static_cast<int>(config.coordinate_files.size()))
    {
        throw StorageError("Invalid column in saved move.");
    }

    std::string text;
    text += config.coordinate_files[static_cast<size_t>(position.col)];
    text += static_cast<char>('0' + position.row);
    return text;
}

Position<int> textToPosition(const std::string& text, const BoardConfig& config)
{
    if (text.size() != 2)
    {
        throw StorageError("Invalid coordinate text in saved move.");
    }

    const auto found = config.coordinate_files.find(static_cast<char>(std::tolower(static_cast<unsigned char>(text[0]))));
    if (found == std::string::npos || !std::isdigit(static_cast<unsigned char>(text[1])))
    {
        throw StorageError("Invalid coordinate text in saved move.");
    }

    return { text[1] - '0', static_cast<int>(found) };
}

std::string valueOrNone(const std::optional<PieceType>& value)
{
    return value.has_value() ? toString(*value) : "None";
}

std::string valueOrNone(const std::optional<Side>& value)
{
    return value.has_value() ? toString(*value) : "None";
}

std::string escapeTextField(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
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
        case '\r':
            escaped += "\\r";
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
    std::string unescaped;
    unescaped.reserve(value.size());
    for (size_t index = 0; index < value.size(); ++index)
    {
        const char ch = value[index];
        if (ch != '\\' || index + 1 >= value.size())
        {
            unescaped.push_back(ch);
            continue;
        }

        const char escaped = value[++index];
        switch (escaped)
        {
        case '\\':
            unescaped.push_back('\\');
            break;
        case 'n':
            unescaped.push_back('\n');
            break;
        case 'r':
            unescaped.push_back('\r');
            break;
        default:
            unescaped.push_back('\\');
            unescaped.push_back(escaped);
            break;
        }
    }
    return unescaped;
}

std::optional<PieceType> parsePieceTypeOptional(const std::string& value)
{
    if (value == "None")
    {
        return std::nullopt;
    }

    if (value == "King")
    {
        return PieceType::King;
    }
    if (value == "Advisor")
    {
        return PieceType::Advisor;
    }
    if (value == "Elephant")
    {
        return PieceType::Elephant;
    }
    if (value == "Knight")
    {
        return PieceType::Knight;
    }
    if (value == "Rook")
    {
        return PieceType::Rook;
    }
    if (value == "Cannon")
    {
        return PieceType::Cannon;
    }
    if (value == "Pawn")
    {
        return PieceType::Pawn;
    }

    throw StorageError("Unknown saved piece type.");
}

std::optional<Side> parseSideOptional(const std::string& value)
{
    if (value == "None")
    {
        return std::nullopt;
    }
    if (value == "Red")
    {
        return Side::Red;
    }
    if (value == "Black")
    {
        return Side::Black;
    }
    throw StorageError("Unknown saved side.");
}

Side parseSide(const std::string& value)
{
    if (value == "Red")
    {
        return Side::Red;
    }
    if (value == "Black")
    {
        return Side::Black;
    }
    throw StorageError("Unknown saved side.");
}

BoardMode parseBoardMode(const std::string& value)
{
    if (value == "Standard9x10")
    {
        return BoardMode::Standard9x10;
    }
    if (value == "Expanded11x10")
    {
        return BoardMode::Expanded11x10;
    }
    throw StorageError("Unknown saved board mode.");
}

GameResult parseGameResult(const std::string& value)
{
    if (value == "Ongoing")
    {
        return GameResult::Ongoing;
    }
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
    throw StorageError("Unknown saved game result.");
}

uint64_t repetitionBoardKey(const Board& board, const Side side_to_move)
{
    return static_cast<uint64_t>(std::hash<std::string>{}(board.hashString() + "|" + toString(side_to_move)));
}

} // namespace

GameSession::GameSession() : GameSession(GameSettings{}) {}

GameSession::GameSession(GameSettings settings, PlayerInfo players)
    : settings_(std::move(settings)),
      players_(std::move(players)),
      board_(settings_.board_mode)
{
    startNewGame();
}

void GameSession::startNewGame()
{
    board_.setup(settings_.board_mode);
    current_side_ = Side::Red;
    result_ = GameResult::Ongoing;
    history_.clear();
    repetition_counts_.clear();
    remaining_time_ms_[0] = settings_.move_time_limit_seconds * 1000;
    remaining_time_ms_[1] = settings_.move_time_limit_seconds * 1000;
    repetition_counts_[boardHashWithTurn()] = 1;
    resetClockForTurn();
}

int GameSession::remainingSeconds(const Side side) const noexcept
{
    return std::max(0, remaining_time_ms_[sideIndex(side)] / 1000);
}

int GameSession::remainingMilliseconds(const Side side) const noexcept
{
    return std::max(0, remaining_time_ms_[sideIndex(side)]);
}

std::vector<Move> GameSession::legalMovesForCurrentSide() const
{
    return board_.generateLegalMoves(current_side_);
}

std::vector<Move> GameSession::legalMovesFrom(const Position<int> position) const
{
    std::vector<Move> result;
    for (const auto& move : legalMovesForCurrentSide())
    {
        if (move.from == position)
        {
            result.push_back(move);
        }
    }
    return result;
}

bool GameSession::hasAnyLegalMove(const Side side) const
{
    return !board_.generateLegalMoves(side).empty();
}

Move GameSession::submitMove(const Move& candidate_move)
{
    if (gameOver())
    {
        throw IllegalMoveError("The game has already finished.");
    }

    consumeCurrentTurnTime();
    if (gameOver())
    {
        throw IllegalMoveError("Time has already expired.");
    }

    Move legal_move = normalizeMove(candidate_move);
    if (!candidate_move.notation.empty())
    {
        legal_move.notation = candidate_move.notation;
    }
    if (wouldViolatePerpetualCheck(legal_move))
    {
        throw IllegalMoveError("Perpetual check or chase is not allowed.");
    }
    board_.applyMove(legal_move);
    history_.push_back(legal_move);
    evaluateGameResultAfterMove(history_.back());
    if (!gameOver())
    {
        current_side_ = opposite(current_side_);
        repetition_counts_[boardHashWithTurn()]++;
        if (repetition_counts_[boardHashWithTurn()] >= 3)
        {
            result_ = GameResult::Draw;
        }
    }
    resetClockForTurn();
    return history_.back();
}

void GameSession::applyRemoteMove(const Move& move)
{
    (void)submitMove(move);
}

void GameSession::applyImportedResult(const GameResult result, const Side current_side)
{
    result_ = result;
    current_side_ = current_side;
}

bool GameSession::undoLastPly()
{
    return undoLastPlies(1) > 0;
}

int GameSession::undoLastPlies(const int count)
{
    if (!settings_.allow_undo || history_.empty() || count <= 0)
    {
        return 0;
    }

    if (!gameOver())
    {
        consumeCurrentTurnTime();
    }

    int undone = 0;
    while (undone < count && !history_.empty())
    {
        const Move move = history_.back();
        board_.revertMove(move);
        history_.pop_back();
        current_side_ = move.side;
        result_ = GameResult::Ongoing;
        ++undone;
    }

    rebuildRepetitionFromHistory();
    resetClockForTurn();
    return undone;
}

void GameSession::resign(const Side side)
{
    if (gameOver())
    {
        return;
    }

    consumeCurrentTurnTime();
    result_ = GameResult::Resign;
    current_side_ = side;
}

void GameSession::tickClock()
{
    if (gameOver())
    {
        return;
    }
    consumeCurrentTurnTime();
    if (!gameOver())
    {
        resetClockForTurn();
    }
}

std::string GameSession::currentPlayerName() const
{
    return current_side_ == Side::Red ? players_.red_name : players_.black_name;
}

std::string GameSession::resultText() const
{
    switch (result_)
    {
    case GameResult::Ongoing:
        return "Game in progress";
    case GameResult::RedWin:
        return "Red wins";
    case GameResult::BlackWin:
        return "Black wins";
    case GameResult::Draw:
        return "Draw by repetition";
    case GameResult::Timeout:
        return current_side_ == Side::Red ? "Red loses on time" : "Black loses on time";
    case GameResult::Resign:
        return current_side_ == Side::Red ? "Red resigns" : "Black resigns";
    default:
        return "Unknown result";
    }
}

std::string GameSession::boardHashWithTurn() const
{
    return board_.hashString() + "|" + toString(current_side_);
}

std::string GameSession::serialize() const
{
    std::ostringstream output;
    output << "VERSION=1\n";
    output << "MODE=" << toString(settings_.board_mode) << '\n';
    output << "CURRENT=" << toString(current_side_) << '\n';
    output << "RESULT=" << toString(result_) << '\n';
    output << "RED_NAME=" << escapeTextField(players_.red_name) << '\n';
    output << "BLACK_NAME=" << escapeTextField(players_.black_name) << '\n';
    output << "MOVE_TIME=" << settings_.move_time_limit_seconds << '\n';
    output << "ALLOW_UNDO=" << (settings_.allow_undo ? 1 : 0) << '\n';
    output << "SHOW_LEGAL=" << (settings_.show_legal_moves ? 1 : 0) << '\n';
    output << "AI_ENABLED=" << (settings_.ai_enabled ? 1 : 0) << '\n';
    output << "AI_SIDE=" << toString(settings_.ai_side) << '\n';
    output << "AI_DEPTH=" << settings_.ai_depth << '\n';
    output << "USE_EASYX=" << (settings_.use_easyx ? 1 : 0) << '\n';
    output << "RED_REMAINING_MS=" << remaining_time_ms_[0] << '\n';
    output << "BLACK_REMAINING_MS=" << remaining_time_ms_[1] << '\n';
    output << "BOARD_BEGIN\n" << board_.serializeGrid() << "\nBOARD_END\n";
    output << "HISTORY_BEGIN\n";
    for (const auto& move : history_)
    {
        output << serializeMove(move, board_.config()) << '\n';
    }
    output << "HISTORY_END\n";
    return output.str();
}

GameSession GameSession::deserialize(const std::string& text)
{
    std::istringstream input(text);
    std::string line;
    std::unordered_map<std::string, std::string> kv;
    std::vector<std::string> board_lines;
    std::vector<std::string> move_lines;
    bool reading_board = false;
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
        if (reading_history)
        {
            if (!line.empty())
            {
                move_lines.push_back(line);
            }
            continue;
        }

        const auto delimiter = line.find('=');
        if (delimiter == std::string::npos)
        {
            continue;
        }
        kv.emplace(line.substr(0, delimiter), line.substr(delimiter + 1));
    }

    GameSettings settings;
    settings.board_mode = parseBoardMode(kv.at("MODE"));
    settings.move_time_limit_seconds = std::stoi(kv.at("MOVE_TIME"));
    settings.allow_undo = parseBool(kv.at("ALLOW_UNDO"));
    settings.show_legal_moves = parseBool(kv.at("SHOW_LEGAL"));
    settings.ai_enabled = parseBool(kv.at("AI_ENABLED"));
    settings.ai_side = parseSide(kv.at("AI_SIDE"));
    settings.ai_depth = std::stoi(kv.at("AI_DEPTH"));
    settings.use_easyx = parseBool(kv.at("USE_EASYX"));

    PlayerInfo players;
    players.red_name = unescapeTextField(kv.at("RED_NAME"));
    players.black_name = unescapeTextField(kv.at("BLACK_NAME"));

    GameSession session(settings, players);
    session.board_ = Board::deserialize(settings.board_mode, board_lines);
    session.current_side_ = parseSide(kv.at("CURRENT"));
    session.result_ = parseGameResult(kv.at("RESULT"));
    session.remaining_time_ms_[0] = std::stoi(kv.at("RED_REMAINING_MS"));
    session.remaining_time_ms_[1] = std::stoi(kv.at("BLACK_REMAINING_MS"));
    session.history_.clear();
    for (const auto& move_line : move_lines)
    {
        session.history_.push_back(deserializeMove(move_line, session.board_.config()));
    }
    session.rebuildRepetitionFromHistory();
    session.resetClockForTurn();
    return session;
}

void GameSession::resetClockForTurn()
{
    turn_started_at_ = std::chrono::steady_clock::now();
}

void GameSession::consumeCurrentTurnTime()
{
    if (settings_.move_time_limit_seconds <= 0 || gameOver())
    {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const int elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - turn_started_at_).count());
    remaining_time_ms_[sideIndex(current_side_)] -= elapsed_ms;
    turn_started_at_ = now;
    if (remaining_time_ms_[sideIndex(current_side_)] <= 0)
    {
        remaining_time_ms_[sideIndex(current_side_)] = 0;
        result_ = GameResult::Timeout;
    }
}

void GameSession::rebuildRepetitionFromHistory()
{
    repetition_counts_.clear();

    Board replay(settings_.board_mode);
    Side replay_side = Side::Red;
    repetition_counts_[replay.hashString() + "|" + toString(replay_side)] = 1;

    for (const auto& historic_move : history_)
    {
        auto applied_move = historic_move;
        replay.applyMove(applied_move);
        replay_side = opposite(replay_side);
        repetition_counts_[replay.hashString() + "|" + toString(replay_side)]++;
    }
}

void GameSession::evaluateGameResultAfterMove(Move& move)
{
    if (move.captured_type.has_value() && *move.captured_type == PieceType::King)
    {
        move.is_check = true;
        move.is_mate = true;
        result_ = current_side_ == Side::Red ? GameResult::RedWin : GameResult::BlackWin;
        return;
    }

    const Side target_side = opposite(current_side_);
    move.is_check = board_.isInCheck(target_side);
    const auto opponent_moves = board_.generateLegalMoves(target_side);
    if (!opponent_moves.empty())
    {
        return;
    }

    move.is_mate = move.is_check;
    if (target_side == Side::Red)
    {
        result_ = GameResult::BlackWin;
    }
    else
    {
        result_ = GameResult::RedWin;
    }
}

Move GameSession::normalizeMove(const Move& candidate_move) const
{
    const auto legal_moves = legalMovesForCurrentSide();
    const auto found = std::find_if(
        legal_moves.begin(),
        legal_moves.end(),
        [&](const Move& move)
        {
            return move.from == candidate_move.from && move.to == candidate_move.to;
        });

    if (found == legal_moves.end())
    {
        throw IllegalMoveError("Illegal move.");
    }

    return *found;
}

bool GameSession::wouldViolatePerpetualCheck(const Move& legal_move) const
{
    Board replay(settings_.board_mode);
    Side side_to_move = Side::Red;
    std::vector<RepetitionState> states;
    std::vector<RepetitionPlyInfo> plies;
    states.reserve(history_.size() + 2);
    plies.reserve(history_.size() + 1);
    states.push_back({ repetitionBoardKey(replay, side_to_move), side_to_move });

    for (const auto& historic_move : history_)
    {
        auto applied_move = historic_move;
        replay.applyMove(applied_move);
        plies.push_back(
            {
                historic_move.side,
                historic_move.is_check || replay.isInCheck(opposite(historic_move.side)),
                moveGivesChase(replay, historic_move),
            });
        side_to_move = opposite(side_to_move);
        states.push_back({ repetitionBoardKey(replay, side_to_move), side_to_move });
    }

    auto candidate_move = legal_move;
    replay.applyMove(candidate_move);
    const bool gives_check = (candidate_move.captured_type.has_value() && *candidate_move.captured_type == PieceType::King) ||
                             replay.isInCheck(opposite(candidate_move.side));
    const bool gives_chase = moveGivesChase(replay, candidate_move);
    plies.push_back({ candidate_move.side, gives_check, gives_chase });
    side_to_move = opposite(side_to_move);
    states.push_back({ repetitionBoardKey(replay, side_to_move), side_to_move });

    return (gives_check &&
               isPerpetualCheck(states, states.size(), plies, plies.size(), candidate_move.side)) ||
           (gives_chase &&
               isPerpetualChase(states, states.size(), plies, plies.size(), candidate_move.side));
}

bool GameSession::moveGivesChase(const Board& board_after_move, const Move& move) const
{
    const auto* moved_piece = board_after_move.pieceAt(move.to);
    if (moved_piece == nullptr || moved_piece->side() != move.side)
    {
        return false;
    }

    for (const auto& target : moved_piece->generatePseudoLegalMoves(board_after_move, move.to))
    {
        const auto* target_piece = board_after_move.pieceAt(target);
        if (target_piece != nullptr &&
            target_piece->side() == opposite(move.side) &&
            target_piece->type() != PieceType::King)
        {
            return true;
        }
    }

    return false;
}

std::string GameSession::serializeMove(const Move& move, const BoardConfig& config)
{
    std::ostringstream output;
    output << positionToText(move.from, config) << ','
           << positionToText(move.to, config) << ','
           << toString(move.side) << ','
           << toString(move.piece_type) << ','
           << valueOrNone(move.captured_type) << ','
           << valueOrNone(move.captured_side) << ','
           << (move.is_check ? 1 : 0) << ','
           << (move.is_mate ? 1 : 0) << ','
           << escapeTextField(move.notation);
    return output.str();
}

Move GameSession::deserializeMove(const std::string& line, const BoardConfig& config)
{
    std::istringstream input(line);
    std::vector<std::string> fields;
    std::string field;
    while (std::getline(input, field, ','))
    {
        fields.push_back(field);
    }

    if (fields.size() < 9)
    {
        throw StorageError("Invalid saved move format.");
    }

    Move move;
    move.from = textToPosition(fields[0], config);
    move.to = textToPosition(fields[1], config);
    move.side = parseSide(fields[2]);
    move.piece_type = *parsePieceTypeOptional(fields[3]);
    move.captured_type = parsePieceTypeOptional(fields[4]);
    move.captured_side = parseSideOptional(fields[5]);
    move.is_check = parseBool(fields[6]);
    move.is_mate = parseBool(fields[7]);
    std::string notation = fields[8];
    for (size_t index = 9; index < fields.size(); ++index)
    {
        notation += ',';
        notation += fields[index];
    }
    move.notation = unescapeTextField(notation);
    return move;
}

} // namespace xiangqi
