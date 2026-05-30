#include "app/MoveParser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>

namespace xiangqi
{

namespace
{

const std::string kKingRed = u8"\u5e05";
const std::string kKingBlack = u8"\u5c06";
const std::string kAdvisorRed = u8"\u4ed5";
const std::string kAdvisorBlack = u8"\u58eb";
const std::string kElephantRed = u8"\u76f8";
const std::string kElephantBlack = u8"\u8c61";
const std::string kKnight = u8"\u9a6c";
const std::string kRook = u8"\u8f66";
const std::string kCannon = u8"\u70ae";
const std::string kCannonAlt = u8"\u7832";
const std::string kPawnRed = u8"\u5175";
const std::string kPawnBlack = u8"\u5352";

const std::string kActionAdvance = u8"\u8fdb";
const std::string kActionRetreat = u8"\u9000";
const std::string kActionFlat = u8"\u5e73";

const std::array<std::string, 10> kChineseDigits{
    "",
    u8"\u4e00",
    u8"\u4e8c",
    u8"\u4e09",
    u8"\u56db",
    u8"\u4e94",
    u8"\u516d",
    u8"\u4e03",
    u8"\u516b",
    u8"\u4e5d",
};

std::string trim(const std::string& input)
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

bool parseCoordinate(const std::string& text, const BoardConfig& config, Position<int>& position)
{
    if (text.size() != 2)
    {
        return false;
    }

    const auto file = config.coordinate_files.find(static_cast<char>(std::tolower(static_cast<unsigned char>(text[0]))));
    if (file == std::string::npos || !std::isdigit(static_cast<unsigned char>(text[1])))
    {
        return false;
    }

    const int row = text[1] - '0';
    if (row < 0 || row >= config.rows)
    {
        return false;
    }

    position = { row, static_cast<int>(file) };
    return true;
}

std::vector<std::string> splitUtf8Characters(const std::string& text)
{
    std::vector<std::string> result;
    for (size_t index = 0; index < text.size();)
    {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        size_t width = 1;
        if ((lead & 0xF8u) == 0xF0u)
        {
            width = 4;
        }
        else if ((lead & 0xF0u) == 0xE0u)
        {
            width = 3;
        }
        else if ((lead & 0xE0u) == 0xC0u)
        {
            width = 2;
        }
        result.push_back(text.substr(index, width));
        index += width;
    }
    return result;
}

std::optional<PieceType> pieceTypeFromUtf8(const std::string& token)
{
    if (token == kKingRed || token == kKingBlack)
    {
        return PieceType::King;
    }
    if (token == kAdvisorRed || token == kAdvisorBlack)
    {
        return PieceType::Advisor;
    }
    if (token == kElephantRed || token == kElephantBlack)
    {
        return PieceType::Elephant;
    }
    if (token == kKnight || token == u8"\u99ac")
    {
        return PieceType::Knight;
    }
    if (token == kRook || token == u8"\u8eca")
    {
        return PieceType::Rook;
    }
    if (token == kCannon || token == kCannonAlt)
    {
        return PieceType::Cannon;
    }
    if (token == kPawnRed || token == kPawnBlack)
    {
        return PieceType::Pawn;
    }
    return std::nullopt;
}

std::optional<int> numeralFromUtf8(const std::string& token)
{
    if (token.size() == 1 && std::isdigit(static_cast<unsigned char>(token[0])) != 0)
    {
        const int value = token[0] - '0';
        if (value >= 1 && value <= 9)
        {
            return value;
        }
    }

    for (int index = 1; index <= 9; ++index)
    {
        if (token == kChineseDigits[static_cast<size_t>(index)])
        {
            return index;
        }
    }
    return std::nullopt;
}

int fileNumberForSide(const Side side, const int col, const int cols)
{
    return side == Side::Red ? cols - col : col + 1;
}

bool isForwardMove(const Side side, const Move& move)
{
    return side == Side::Red ? move.to.row < move.from.row : move.to.row > move.from.row;
}

bool moveMatchesChinese(
    const Move& move,
    const Side side,
    const PieceType piece_type,
    const int source_file_number,
    const std::string& action,
    const int target_number,
    const BoardConfig& config)
{
    if (move.side != side || move.piece_type != piece_type)
    {
        return false;
    }

    if (fileNumberForSide(side, move.from.col, config.cols) != source_file_number)
    {
        return false;
    }

    if (action == kActionFlat)
    {
        return move.from.row == move.to.row && fileNumberForSide(side, move.to.col, config.cols) == target_number;
    }

    const bool forward = isForwardMove(side, move);
    if ((action == kActionAdvance && !forward) || (action == kActionRetreat && forward))
    {
        return false;
    }

    switch (piece_type)
    {
    case PieceType::Rook:
    case PieceType::Cannon:
    case PieceType::King:
    case PieceType::Pawn:
        return move.from.col == move.to.col && std::abs(move.to.row - move.from.row) == target_number;
    case PieceType::Knight:
    case PieceType::Advisor:
    case PieceType::Elephant:
        return fileNumberForSide(side, move.to.col, config.cols) == target_number;
    default:
        return false;
    }
}

std::string pieceName(const PieceType type, const Side side)
{
    switch (type)
    {
    case PieceType::King:
        return side == Side::Red ? kKingRed : kKingBlack;
    case PieceType::Advisor:
        return side == Side::Red ? kAdvisorRed : kAdvisorBlack;
    case PieceType::Elephant:
        return side == Side::Red ? kElephantRed : kElephantBlack;
    case PieceType::Knight:
        return kKnight;
    case PieceType::Rook:
        return kRook;
    case PieceType::Cannon:
        return kCannon;
    case PieceType::Pawn:
        return side == Side::Red ? kPawnRed : kPawnBlack;
    default:
        return "?";
    }
}

std::string chineseDigit(const int value)
{
    return kChineseDigits[static_cast<size_t>(value)];
}

char wxfPieceLetter(const PieceType piece_type, const Side side)
{
    const char letter = pieceLetter(piece_type);
    return side == Side::Red ? letter : static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
}

bool frontBefore(const Side side, const Position<int> left, const Position<int> right)
{
    if (left.row != right.row)
    {
        return side == Side::Red ? left.row < right.row : left.row > right.row;
    }
    return left.col < right.col;
}

std::vector<Position<int>> sameTypeSameFilePositions(
    const GameSession& session,
    const Side side,
    const PieceType piece_type,
    const int col)
{
    std::vector<Position<int>> positions;
    for (const auto& [position, piece] : session.board().pieces(side))
    {
        if (piece->type() == piece_type && position.col == col)
        {
            positions.push_back(position);
        }
    }

    std::sort(
        positions.begin(),
        positions.end(),
        [side](const Position<int> left, const Position<int> right)
        {
            return frontBefore(side, left, right);
        });
    return positions;
}

int frontIndexInFile(
    const GameSession& session,
    const Side side,
    const PieceType piece_type,
    const Position<int> from)
{
    const auto positions = sameTypeSameFilePositions(session, side, piece_type, from.col);
    const auto found = std::find(positions.begin(), positions.end(), from);
    if (found == positions.end())
    {
        return 0;
    }
    return static_cast<int>(std::distance(positions.begin(), found));
}

} // namespace

ParsedCommand MoveParser::parse(const std::string& input, const GameSession& session) const
{
    ParsedCommand command;
    command.raw_text = trim(input);
    if (command.raw_text.empty())
    {
        return command;
    }

    const std::string lower = toLowerAscii(command.raw_text);
    if (lower == "undo" || lower == "u")
    {
        command.type = CommandType::Undo;
        return command;
    }
    if (lower == "resign")
    {
        command.type = CommandType::Resign;
        return command;
    }
    if (lower == "hint")
    {
        command.type = CommandType::Hint;
        return command;
    }
    if (lower == "replay")
    {
        command.type = CommandType::Replay;
        return command;
    }
    if (lower == "help")
    {
        command.type = CommandType::Help;
        return command;
    }
    if (lower == "exit" || lower == "quit")
    {
        command.type = CommandType::Exit;
        return command;
    }
    if (lower == "tests" || lower == "selftest")
    {
        command.type = CommandType::Tests;
        return command;
    }

    auto split_once = [](const std::string& text) -> std::pair<std::string, std::string>
    {
        const auto pos = text.find(' ');
        if (pos == std::string::npos)
        {
            return { text, "" };
        }
        return { text.substr(0, pos), trim(text.substr(pos + 1)) };
    };

    {
        const auto [head, tail] = split_once(lower);
        if (head == "save")
        {
            command.type = CommandType::Save;
            command.argument = tail;
            return command;
        }
        if (head == "load")
        {
            command.type = CommandType::Load;
            command.argument = tail;
            return command;
        }
    }

    const BoardConfig& config = session.board().config();
    if (command.raw_text.size() == 2)
    {
        Position<int> position;
        if (parseCoordinate(command.raw_text, config, position))
        {
            command.type = CommandType::ShowMoves;
            command.position = position;
            return command;
        }
    }

    {
        std::istringstream coords(command.raw_text);
        std::string from_text;
        std::string to_text;
        if (coords >> from_text >> to_text)
        {
            Position<int> from;
            Position<int> to;
            if (parseCoordinate(from_text, config, from) && parseCoordinate(to_text, config, to))
            {
                command.type = CommandType::Move;
                Move move;
                move.from = from;
                move.to = to;
                move.side = session.currentSide();
                const auto* piece = session.board().pieceAt(from);
                if (piece != nullptr)
                {
                    move.piece_type = piece->type();
                }
                command.move = move;
                return command;
            }
        }
    }

    if (session.boardMode() == BoardMode::Standard9x10)
    {
        const auto chars = splitUtf8Characters(command.raw_text);
        if (chars.size() == 4)
        {
            const auto piece_type = pieceTypeFromUtf8(chars[0]);
            const auto source_file = numeralFromUtf8(chars[1]);
            const std::string action = chars[2];
            const auto target = numeralFromUtf8(chars[3]);
            if (piece_type.has_value() && source_file.has_value() && target.has_value() &&
                (action == kActionFlat || action == kActionAdvance || action == kActionRetreat))
            {
                std::vector<Move> matches;
                for (const auto& move : session.legalMovesForCurrentSide())
                {
                    if (moveMatchesChinese(move, session.currentSide(), *piece_type, *source_file, action, *target, config))
                    {
                        matches.push_back(move);
                    }
                }

                if (matches.size() == 1)
                {
                    command.type = CommandType::Move;
                    command.move = matches.front();
                    return command;
                }

                if (matches.size() > 1)
                {
                    throw InputError("The Chinese notation is ambiguous. Please use coordinates.");
                }
            }
        }
    }

    throw InputError("Unknown command or move format.");
}

std::string MoveParser::moveToCoordinateText(const Move& move, const BoardConfig& config) const
{
    std::string text;
    text += config.coordinate_files[static_cast<size_t>(move.from.col)];
    text += static_cast<char>('0' + move.from.row);
    text += ' ';
    text += config.coordinate_files[static_cast<size_t>(move.to.col)];
    text += static_cast<char>('0' + move.to.row);
    return text;
}

std::string MoveParser::moveToChineseText(const Move& move, const GameSession& session) const
{
    const BoardConfig& config = session.board().config();
    const int from_file = fileNumberForSide(move.side, move.from.col, config.cols);
    const int to_file = fileNumberForSide(move.side, move.to.col, config.cols);
    const bool forward = isForwardMove(move.side, move);

    std::string notation = pieceName(move.piece_type, move.side) + chineseDigit(from_file);
    if (move.from.row == move.to.row)
    {
        notation += kActionFlat;
        notation += chineseDigit(to_file);
        return notation;
    }

    notation += forward ? kActionAdvance : kActionRetreat;
    switch (move.piece_type)
    {
    case PieceType::Knight:
    case PieceType::Advisor:
    case PieceType::Elephant:
        notation += chineseDigit(to_file);
        break;
    default:
        notation += chineseDigit(std::abs(move.to.row - move.from.row));
        break;
    }
    return notation;
}

std::string MoveParser::moveToWxfText(const Move& move, const GameSession& session) const
{
    const BoardConfig& config = session.board().config();
    if (config.mode != BoardMode::Standard9x10)
    {
        return moveToIccsText(move, config);
    }

    const int from_file = fileNumberForSide(move.side, move.from.col, config.cols);
    const int to_file = fileNumberForSide(move.side, move.to.col, config.cols);
    const bool forward = isForwardMove(move.side, move);
    const char action =
        move.from.row == move.to.row ? '.' : (forward ? '+' : '-');

    std::string notation;
    if (move.piece_type == PieceType::Pawn)
    {
        const auto same_file_pawns = sameTypeSameFilePositions(session, move.side, move.piece_type, move.from.col);
        if (same_file_pawns.size() <= 1)
        {
            notation.push_back(wxfPieceLetter(move.piece_type, move.side));
            notation.push_back(static_cast<char>('0' + from_file));
        }
        else
        {
            const int order_from_front = frontIndexInFile(session, move.side, move.piece_type, move.from) + 1;
            notation.push_back(static_cast<char>('0' + order_from_front));
            notation.push_back(static_cast<char>('0' + from_file));
        }
    }
    else
    {
        const auto same_file_pieces = sameTypeSameFilePositions(session, move.side, move.piece_type, move.from.col);
        if (same_file_pieces.size() <= 1)
        {
            notation.push_back(wxfPieceLetter(move.piece_type, move.side));
            notation.push_back(static_cast<char>('0' + from_file));
        }
        else
        {
            const int index_from_front = frontIndexInFile(session, move.side, move.piece_type, move.from);
            char qualifier = '+';
            if (same_file_pieces.size() == 2)
            {
                qualifier = index_from_front == 0 ? '+' : '-';
            }
            else if (same_file_pieces.size() >= 3)
            {
                qualifier = index_from_front == 0 ? '+' : (index_from_front + 1 == static_cast<int>(same_file_pieces.size()) ? '-' : '=');
            }
            notation.push_back(qualifier);
            notation.push_back(wxfPieceLetter(move.piece_type, move.side));
        }
    }

    notation.push_back(action);
    if (move.from.row == move.to.row ||
        move.piece_type == PieceType::Knight ||
        move.piece_type == PieceType::Advisor ||
        move.piece_type == PieceType::Elephant)
    {
        notation.push_back(static_cast<char>('0' + to_file));
    }
    else
    {
        notation.push_back(static_cast<char>('0' + std::abs(move.to.row - move.from.row)));
    }
    return notation;
}

std::string MoveParser::moveToIccsText(const Move& move, const BoardConfig& config) const
{
    auto square = [&](const Position<int> position)
    {
        std::string text;
        text.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(config.coordinate_files[static_cast<size_t>(position.col)]))));
        text.push_back(static_cast<char>('0' + (config.rows - 1 - position.row)));
        return text;
    };

    std::string text;
    text += square(move.from);
    text.push_back('-');
    text += square(move.to);
    return text;
}

std::istream& operator>>(std::istream& input, ParsedCommand& command)
{
    std::getline(input, command.raw_text);
    command.type = CommandType::Invalid;
    command.move.reset();
    command.position.reset();
    command.argument.clear();
    return input;
}

} // namespace xiangqi
