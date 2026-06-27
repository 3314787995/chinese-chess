#include "storage/Storage.h"

#include "app/MoveParser.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace xiangqi::storage
{

namespace
{

std::string makeTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
    localtime_s(&local_time, &now_time);
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &local_time);
    return buffer;
}

std::string makeReplayTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
    localtime_s(&local_time, &now_time);

    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) %
        1000;

    char buffer[40]{};
    std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &local_time);

    std::ostringstream output;
    output << buffer << '_' << std::setw(3) << std::setfill('0') << milliseconds.count();
    return output.str();
}

std::string readAllText(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw StorageError("Failed to open file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void writeAllText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary);
    if (!output)
    {
        throw StorageError("Failed to write file: " + path.string());
    }
    output << text;
}

std::string trimCopy(const std::string& input)
{
    const auto begin = std::find_if_not(
        input.begin(),
        input.end(),
        [](const unsigned char ch)
        {
            return std::isspace(ch) != 0;
        });
    const auto end = std::find_if_not(
        input.rbegin(),
        input.rend(),
        [](const unsigned char ch)
        {
            return std::isspace(ch) != 0;
        }).base();
    if (begin >= end)
    {
        return {};
    }
    return std::string(begin, end);
}

std::string lowerAscii(std::string value)
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

std::string upperAscii(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char ch)
        {
            return static_cast<char>(std::toupper(ch));
        });
    return value;
}

bool isSaveFileExtension(const std::filesystem::path& path)
{
    const std::string extension = lowerAscii(path.extension().string());
    return extension == ".dat" || extension == ".xqsave";
}

std::string nonEmptySanitized(std::string value, const std::string& fallback)
{
    value = sanitizeName(trimCopy(value));
    if (value.empty())
    {
        return fallback;
    }
    return value;
}

std::string playerSaveStem(const PlayerInfo& players, const bool dark_game)
{
    const std::string red = nonEmptySanitized(players.red_name, dark_game ? "Player1" : "Red");
    const std::string black = nonEmptySanitized(players.black_name, dark_game ? "Player2" : "Black");
    return (dark_game ? "dark_" : "") + red + "_vs_" + black;
}

std::filesystem::path makeSavePath(
    const std::string& name_hint,
    const PlayerInfo& players,
    const bool dark_game)
{
    const std::string trimmed = trimCopy(name_hint);
    if (trimmed.empty())
    {
        return saveDirectory() / (playerSaveStem(players, dark_game) + ".dat");
    }

    const std::filesystem::path raw_file = std::filesystem::path(trimmed).filename();
    const std::string stem = nonEmptySanitized(raw_file.stem().string(), dark_game ? "dark_save" : "save");
    std::string extension = raw_file.has_extension() ? lowerAscii(raw_file.extension().string()) : ".dat";
    if (extension != ".dat" && extension != ".xqsave")
    {
        extension = ".dat";
    }
    return saveDirectory() / (stem + extension);
}

std::filesystem::path resolveSaveLoadPath(const std::string& name_or_path)
{
    const std::filesystem::path input_path(name_or_path);
    if (input_path.is_absolute())
    {
        return input_path;
    }

    const std::filesystem::path raw_file = input_path.filename();
    const auto directory = saveDirectory();
    std::vector<std::filesystem::path> candidates;
    if (raw_file.has_extension())
    {
        candidates.push_back(directory / raw_file);
    }
    else
    {
        const std::string clean_name = nonEmptySanitized(raw_file.string(), "manual_save");
        candidates.push_back(directory / (clean_name + ".dat"));
        candidates.push_back(directory / (clean_name + ".xqsave"));
        candidates.push_back(directory / (raw_file.string() + ".dat"));
        candidates.push_back(directory / (raw_file.string() + ".xqsave"));
    }

    for (const auto& candidate : candidates)
    {
        if (std::filesystem::exists(candidate))
        {
            return candidate;
        }
    }
    return candidates.empty() ? directory / raw_file : candidates.front();
}

std::string makePgnDate()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
    localtime_s(&local_time, &now_time);
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y.%m.%d", &local_time);
    return buffer;
}

std::string pgnResult(const GameSession& session)
{
    switch (session.result())
    {
    case GameResult::RedWin:
        return "1-0";
    case GameResult::BlackWin:
        return "0-1";
    case GameResult::Draw:
        return "1/2-1/2";
    case GameResult::Timeout:
    case GameResult::Resign:
        return session.currentSide() == Side::Red ? "0-1" : "1-0";
    case GameResult::Ongoing:
    default:
        return "*";
    }
}

std::string pgnTermination(const GameSession& session)
{
    switch (session.result())
    {
    case GameResult::RedWin:
    case GameResult::BlackWin:
        if (!session.history().empty())
        {
            return session.history().back().is_mate ? "checkmate" : "stalemate";
        }
        return "normal";
    case GameResult::Draw:
        return "draw by repetition";
    case GameResult::Timeout:
        return "time forfeit";
    case GameResult::Resign:
        return "resignation";
    case GameResult::Ongoing:
    default:
        return "unterminated";
    }
}

std::string pgnEvent(const GameSession& session)
{
    if (session.settings().ai_enabled)
    {
        return "Human vs AI";
    }
    return session.settings().use_easyx ? "EasyX Match" : "Console Match";
}

std::string pgnVariant(const GameSession& session)
{
    return session.boardMode() == BoardMode::Standard9x10 ? "Xiangqi" : "Xiangqi Expanded 11x10";
}

std::string pgnFormat(const GameSession& session)
{
    return session.boardMode() == BoardMode::Standard9x10 ? "WXF" : "ICCS";
}

std::string darkPgnResult(const DarkGameSession& session)
{
    switch (session.result())
    {
    case GameResult::RedWin:
        return "1-0";
    case GameResult::BlackWin:
        return "0-1";
    case GameResult::Draw:
        return "1/2-1/2";
    case GameResult::Timeout:
    case GameResult::Resign:
    {
        const auto loser_color = session.colorForSeat(session.currentSeat());
        if (loser_color.has_value())
        {
            return *loser_color == Side::Red ? "0-1" : "1-0";
        }
        return session.currentSeat() == DarkSeat::Player1 ? "0-1" : "1-0";
    }
    case GameResult::Ongoing:
    default:
        return "*";
    }
}

std::string escapePgn(std::string value)
{
    std::string result;
    result.reserve(value.size());
    for (const char ch : value)
    {
        if (ch == '\\' || ch == '"')
        {
            result.push_back('\\');
        }
        result.push_back(ch);
    }
    return result;
}

std::string gridToPgnTag(std::string value)
{
    std::replace(value.begin(), value.end(), '\n', '/');
    return escapePgn(std::move(value));
}

std::string gridFromPgnTag(std::string value)
{
    std::replace(value.begin(), value.end(), '/', '\n');
    return value;
}

std::string csvField(const std::string& value)
{
    std::string result;
    bool needs_quotes = false;
    result.reserve(value.size());
    for (const char ch : value)
    {
        if (ch == '"')
        {
            result += "\"\"";
            needs_quotes = true;
        }
        else if (ch == ',')
        {
            result.push_back(ch);
            needs_quotes = true;
        }
        else if (ch == '\n' || ch == '\r')
        {
            result.push_back(' ');
            needs_quotes = true;
        }
        else
        {
            result.push_back(ch);
        }
    }

    if (!needs_quotes)
    {
        return result;
    }
    return '"' + result + '"';
}

std::vector<std::string> parseCsvLine(const std::string& line)
{
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;
    for (size_t i = 0; i < line.size(); ++i)
    {
        const char ch = line[i];
        if (in_quotes)
        {
            if (ch == '"' && i + 1 < line.size() && line[i + 1] == '"')
            {
                current.push_back('"');
                ++i;
            }
            else if (ch == '"')
            {
                in_quotes = false;
            }
            else
            {
                current.push_back(ch);
            }
        }
        else if (ch == '"')
        {
            in_quotes = true;
        }
        else if (ch == ',')
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
    return fields;
}

int parseCsvInt(const std::vector<std::string>& fields, const size_t index)
{
    if (index >= fields.size())
    {
        return 0;
    }

    try
    {
        return std::stoi(fields[index]);
    }
    catch (...)
    {
        return 0;
    }
}

std::string standardWinnerName(const GameSession& session)
{
    switch (session.result())
    {
    case GameResult::RedWin:
        return session.players().red_name;
    case GameResult::BlackWin:
        return session.players().black_name;
    case GameResult::Timeout:
    case GameResult::Resign:
        return session.currentSide() == Side::Red ? session.players().black_name : session.players().red_name;
    case GameResult::Draw:
    case GameResult::Ongoing:
    default:
        return {};
    }
}

std::string darkWinnerName(const DarkGameSession& session)
{
    switch (session.result())
    {
    case GameResult::RedWin:
    case GameResult::BlackWin:
    {
        const Side winner_side = session.result() == GameResult::RedWin ? Side::Red : Side::Black;
        const auto winner_seat = session.seatForColor(winner_side);
        if (winner_seat.has_value())
        {
            return *winner_seat == DarkSeat::Player1 ? session.players().red_name : session.players().black_name;
        }
        return session.result() == GameResult::RedWin ? session.players().red_name : session.players().black_name;
    }
    case GameResult::Timeout:
    case GameResult::Resign:
        return session.currentSeat() == DarkSeat::Player1 ? session.players().black_name : session.players().red_name;
    case GameResult::Draw:
    case GameResult::Ongoing:
    default:
        return {};
    }
}

int standardDurationSeconds(const GameSession& session)
{
    const int budget = std::max(0, session.settings().move_time_limit_seconds * 2);
    const int remaining = session.remainingSeconds(Side::Red) + session.remainingSeconds(Side::Black);
    return std::max(0, budget - remaining);
}

int darkDurationSeconds(const DarkGameSession& session)
{
    const int budget = std::max(0, session.settings().move_time_limit_seconds * 2);
    const int remaining = session.remainingSeconds(DarkSeat::Player1) + session.remainingSeconds(DarkSeat::Player2);
    return std::max(0, budget - remaining);
}

void ensureStanding(
    std::unordered_map<std::string, LeaderboardStanding>& standings,
    const std::string& player_name)
{
    LeaderboardStanding& standing = standings[player_name];
    if (standing.player_name.empty())
    {
        standing.player_name = player_name;
    }
}

void recordLeaderboardGame(
    std::unordered_map<std::string, LeaderboardStanding>& standings,
    const std::string& red_name,
    const std::string& black_name,
    const std::string& result,
    const std::string& winner_name,
    const int moves,
    const int duration_seconds)
{
    if (red_name.empty() || black_name.empty())
    {
        return;
    }

    ensureStanding(standings, red_name);
    ensureStanding(standings, black_name);
    LeaderboardStanding& red = standings[red_name];
    LeaderboardStanding& black = standings[black_name];

    ++red.games;
    ++black.games;
    red.total_moves += moves;
    black.total_moves += moves;
    red.total_duration_seconds += duration_seconds;
    black.total_duration_seconds += duration_seconds;

    if (result == "Draw")
    {
        ++red.draws;
        ++black.draws;
        return;
    }

    std::string winner = winner_name;
    if (winner.empty())
    {
        if (result == "RedWin")
        {
            winner = red_name;
        }
        else if (result == "BlackWin")
        {
            winner = black_name;
        }
    }

    if (winner == red_name)
    {
        ++red.wins;
        ++black.losses;
    }
    else if (winner == black_name)
    {
        ++black.wins;
        ++red.losses;
    }
}

std::filesystem::path resolveReplayPath(const std::string& name_or_path)
{
    std::filesystem::path path(name_or_path);
    if (!path.is_absolute())
    {
        path = replayDirectory() / name_or_path;
        if (!path.has_extension())
        {
            path.replace_extension(".pgn");
        }
    }
    return path;
}

std::unordered_map<std::string, std::string> parsePgnTags(const std::string& text)
{
    std::unordered_map<std::string, std::string> tags;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line))
    {
        line = trimCopy(line);
        if (line.size() < 5 || line.front() != '[' || line.back() != ']')
        {
            continue;
        }

        const size_t space = line.find(' ');
        const size_t first_quote = line.find('"', space);
        const size_t last_quote = line.rfind('"');
        if (space == std::string::npos || first_quote == std::string::npos || last_quote == std::string::npos || last_quote <= first_quote)
        {
            continue;
        }

        const std::string key = line.substr(1, space - 1);
        const std::string value = line.substr(first_quote + 1, last_quote - first_quote - 1);
        tags[key] = value;
    }
    return tags;
}

std::string parsePgnMovetext(const std::string& text)
{
    std::istringstream input(text);
    std::string line;
    std::ostringstream output;
    while (std::getline(input, line))
    {
        const std::string trimmed = trimCopy(line);
        if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']')
        {
            continue;
        }
        output << line << '\n';
    }
    return output.str();
}

std::string stripPgnDecorations(const std::string& text)
{
    std::string result;
    result.reserve(text.size());
    int brace_depth = 0;
    int variation_depth = 0;
    bool line_comment = false;

    for (const char ch : text)
    {
        if (line_comment)
        {
            if (ch == '\n')
            {
                line_comment = false;
                result.push_back(' ');
            }
            continue;
        }
        if (brace_depth > 0)
        {
            if (ch == '{')
            {
                ++brace_depth;
            }
            else if (ch == '}')
            {
                --brace_depth;
            }
            continue;
        }
        if (variation_depth > 0)
        {
            if (ch == '(')
            {
                ++variation_depth;
            }
            else if (ch == ')')
            {
                --variation_depth;
            }
            continue;
        }

        if (ch == ';')
        {
            line_comment = true;
            continue;
        }
        if (ch == '{')
        {
            brace_depth = 1;
            continue;
        }
        if (ch == '(')
        {
            variation_depth = 1;
            continue;
        }

        result.push_back(ch == '\n' || ch == '\r' || ch == '\t' ? ' ' : ch);
    }

    return result;
}

bool isResultToken(const std::string& token)
{
    return token == "1-0" || token == "0-1" || token == "1/2-1/2" || token == "*";
}

std::string normalizeMoveToken(std::string token)
{
    token = trimCopy(token);
    if (token.empty())
    {
        return {};
    }

    size_t prefix = 0;
    while (prefix < token.size() && std::isdigit(static_cast<unsigned char>(token[prefix])) != 0)
    {
        ++prefix;
    }
    if (prefix < token.size() && token[prefix] == '.')
    {
        while (prefix < token.size() && token[prefix] == '.')
        {
            ++prefix;
        }
        token.erase(0, prefix);
    }

    while (!token.empty())
    {
        const char tail = token.back();
        if (tail == '+' || tail == '#' || tail == '!' || tail == '?')
        {
            token.pop_back();
            continue;
        }
        break;
    }

    return upperAscii(token);
}

bool parseBoardModeTag(const std::string& value, BoardMode& mode)
{
    if (value == "Standard9x10")
    {
        mode = BoardMode::Standard9x10;
        return true;
    }
    if (value == "Expanded11x10")
    {
        mode = BoardMode::Expanded11x10;
        return true;
    }
    return false;
}

std::pair<GameResult, Side> parseReplayOutcome(const std::unordered_map<std::string, std::string>& tags)
{
    const auto result_it = tags.find("Result");
    const auto termination_it = tags.find("Termination");
    const std::string result = result_it != tags.end() ? result_it->second : "*";
    const std::string termination = termination_it != tags.end() ? upperAscii(termination_it->second) : "";

    if (result == "1/2-1/2")
    {
        return { GameResult::Draw, Side::Red };
    }
    if (termination.find("TIME") != std::string::npos)
    {
        return result == "1-0" ? std::pair{ GameResult::Timeout, Side::Black } : std::pair{ GameResult::Timeout, Side::Red };
    }
    if (termination.find("RESIGN") != std::string::npos)
    {
        return result == "1-0" ? std::pair{ GameResult::Resign, Side::Black } : std::pair{ GameResult::Resign, Side::Red };
    }
    if (result == "1-0")
    {
        return { GameResult::RedWin, Side::Black };
    }
    if (result == "0-1")
    {
        return { GameResult::BlackWin, Side::Red };
    }
    return { GameResult::Ongoing, Side::Red };
}

std::string buildPgn(const GameSession& session)
{
    MoveParser parser;
    GameSettings replay_settings = session.settings();
    replay_settings.ai_enabled = false;
    GameSession replay(replay_settings, session.players());

    std::vector<std::string> move_tokens;
    move_tokens.reserve(session.history().size());
    for (const auto& historic_move : session.history())
    {
        if (replay.boardMode() == BoardMode::Standard9x10)
        {
            move_tokens.push_back(parser.moveToWxfText(historic_move, replay));
        }
        else
        {
            move_tokens.push_back(parser.moveToIccsText(historic_move, replay.board().config()));
        }

        auto applied_move = historic_move;
        replay.submitMove(applied_move);
    }

    std::ostringstream output;
    output << "[Game \"Chinese Chess\"]\n";
    output << "[Event \"" << escapePgn(pgnEvent(session)) << "\"]\n";
    output << "[Site \"Local\"]\n";
    output << "[Date \"" << makePgnDate() << "\"]\n";
    output << "[Round \"-\"]\n";
    output << "[Red \"" << escapePgn(session.players().red_name) << "\"]\n";
    output << "[Black \"" << escapePgn(session.players().black_name) << "\"]\n";
    output << "[Result \"" << pgnResult(session) << "\"]\n";
    output << "[Format \"" << pgnFormat(session) << "\"]\n";
    output << "[Variant \"" << pgnVariant(session) << "\"]\n";
    output << "[BoardMode \"" << toString(session.boardMode()) << "\"]\n";
    output << "[TimeControl \"" << session.settings().move_time_limit_seconds << "\"]\n";
    output << "[PlyCount \"" << session.history().size() << "\"]\n";
    output << "[Termination \"" << pgnTermination(session) << "\"]\n\n";

    std::vector<std::string> words;
    words.reserve(move_tokens.size() + move_tokens.size() / 2 + 1);
    for (size_t index = 0; index < move_tokens.size(); ++index)
    {
        if (index % 2 == 0)
        {
            words.push_back(std::to_string(index / 2 + 1) + ".");
        }
        words.push_back(move_tokens[index]);
    }
    words.push_back(pgnResult(session));

    std::string line;
    for (const auto& word : words)
    {
        if (line.empty())
        {
            line = word;
            continue;
        }

        if (line.size() + 1 + word.size() > 80)
        {
            output << line << '\n';
            line = word;
        }
        else
        {
            line += ' ';
            line += word;
        }
    }
    if (!line.empty())
    {
        output << line << '\n';
    }

    return output.str();
}

std::string darkCdcToken(const DarkAction& action)
{
    if (action.type == DarkActionType::Flip)
    {
        std::string token = darkCoordText(action.from);
        if (action.revealed_piece.has_value())
        {
            token += '(';
            token += darkPieceToken(*action.revealed_piece);
            token += ')';
        }
        return token;
    }

    return darkCoordText(action.from) + "-" + darkCoordText(action.to);
}

std::string buildDarkCdc(const DarkGameSession& session)
{
    std::ostringstream output;
    output << "[Game \"Chinese Dark Chess\"]\n";
    output << "[GameKind \"DarkChess\"]\n";
    output << "[Variant \"Banqi 4x8\"]\n";
    output << "[Date \"" << makePgnDate() << "\"]\n";
    output << "[Player1 \"" << escapePgn(session.players().red_name) << "\"]\n";
    output << "[Player2 \"" << escapePgn(session.players().black_name) << "\"]\n";
    output << "[Format \"CDC\"]\n";
    output << "[InitialPrivateGrid \"" << gridToPgnTag(session.initialPrivateGridString()) << "\"]\n";
    output << "[Result \"" << darkPgnResult(session) << "\"]\n";
    output << "\n";

    int ply = 0;
    for (const auto& action : session.history())
    {
        if (ply % 2 == 0)
        {
            output << (ply / 2 + 1) << ". ";
        }
        output << darkCdcToken(action) << ' ';
        ++ply;
    }
    output << darkPgnResult(session) << '\n';
    return output.str();
}

std::optional<DarkPiece> parseDarkPieceTokenForReplay(const std::string& token)
{
    if (token.size() != 2)
    {
        return std::nullopt;
    }
    if (token[0] != 'r' && token[0] != 'b')
    {
        return std::nullopt;
    }

    DarkPiece piece;
    piece.side = token[0] == 'b' ? Side::Black : Side::Red;
    switch (token[1])
    {
    case 'K':
        piece.type = PieceType::King;
        break;
    case 'A':
        piece.type = PieceType::Advisor;
        break;
    case 'E':
        piece.type = PieceType::Elephant;
        break;
    case 'R':
        piece.type = PieceType::Rook;
        break;
    case 'H':
        piece.type = PieceType::Knight;
        break;
    case 'C':
        piece.type = PieceType::Cannon;
        break;
    case 'P':
        piece.type = PieceType::Pawn;
        break;
    default:
        return std::nullopt;
    }
    piece.is_open = true;
    return piece;
}

std::optional<DarkAction> parseDarkCdcToken(const std::string& token, const DarkSeat seat)
{
    if (token.rfind("flip_", 0) == 0)
    {
        const auto coord_end = token.find('(', 5);
        const std::string coord = coord_end == std::string::npos ? token.substr(5) : token.substr(5, coord_end - 5);
        const auto position = parseDarkCoord(coord);
        if (!position.has_value())
        {
            return std::nullopt;
        }
        DarkAction action = DarkAction::flip(*position, seat);
        if (coord_end != std::string::npos && token.back() == ')')
        {
            action.revealed_piece = parseDarkPieceTokenForReplay(token.substr(coord_end + 1, token.size() - coord_end - 2));
            if (!action.revealed_piece.has_value())
            {
                return std::nullopt;
            }
        }
        action.notation = token;
        return action;
    }

    const auto reveal_begin = token.find('(');
    if (reveal_begin != std::string::npos && token.back() == ')')
    {
        const auto position = parseDarkCoord(token.substr(0, reveal_begin));
        if (!position.has_value())
        {
            return std::nullopt;
        }
        DarkAction action = DarkAction::flip(*position, seat);
        action.revealed_piece = parseDarkPieceTokenForReplay(token.substr(reveal_begin + 1, token.size() - reveal_begin - 2));
        if (!action.revealed_piece.has_value())
        {
            return std::nullopt;
        }
        action.notation = token;
        return action;
    }

    const auto dash = token.find('-');
    if (dash != std::string::npos)
    {
        const auto from = parseDarkCoord(token.substr(0, dash));
        const auto to = parseDarkCoord(token.substr(dash + 1));
        if (from.has_value() && to.has_value())
        {
            DarkAction action = DarkAction::move(*from, *to, seat);
            action.notation = token;
            return action;
        }
    }
    return std::nullopt;
}

std::string normalizeDarkCdcToken(std::string token)
{
    token = trimCopy(token);
    if (token.empty())
    {
        return {};
    }
    size_t prefix = 0;
    while (prefix < token.size() && std::isdigit(static_cast<unsigned char>(token[prefix])) != 0)
    {
        ++prefix;
    }
    if (prefix < token.size() && token[prefix] == '.')
    {
        token.erase(0, prefix + 1);
    }
    while (!token.empty() && (token.back() == '!' || token.back() == '?'))
    {
        token.pop_back();
    }
    return token;
}

} // namespace

std::filesystem::path rootDirectory()
{
    return std::filesystem::current_path() / "data";
}

std::filesystem::path saveDirectory()
{
    return rootDirectory() / "saves";
}

std::filesystem::path replayDirectory()
{
    return std::filesystem::path{ L"D:\\visualstudio\\\u4E2D\u56FD\u8C61\u68CB\\pgn" };
}

std::filesystem::path leaderboardPath()
{
    return rootDirectory() / "leaderboard.csv";
}

void ensureDirectories()
{
    std::filesystem::create_directories(saveDirectory());
    std::filesystem::create_directories(replayDirectory());
}

std::filesystem::path saveGame(const GameSession& session, const std::string& name_hint)
{
    // 存档保存的是“可继续下棋”的完整内部状态，包含计时、当前行棋方和历史。
    // 棋谱导出保存的是“可回放/交流”的对局记录，两者用途不同，不能混用。
    ensureDirectories();
    const auto path = makeSavePath(name_hint, session.players(), false);
    writeAllText(path, session.serialize());
    return path;
}

GameSession loadGame(const std::string& name_or_path)
{
    // 读取存档时恢复的是内部局面，不是从棋谱重新推演；这样能保留悔棋、计时和重复局面信息。
    ensureDirectories();
    const auto path = resolveSaveLoadPath(name_or_path);
    return GameSession::deserialize(readAllText(path));
}

std::filesystem::path saveDarkGame(const DarkGameSession& session, const std::string& name_hint)
{
    // 揭棋存档需要保留完整暗子身份，否则读取后无法继续原来的隐藏信息局面。
    ensureDirectories();
    const auto path = makeSavePath(name_hint, session.players(), true);
    writeAllText(path, session.serialize());
    return path;
}

DarkGameSession loadDarkGame(const std::string& name_or_path)
{
    // 揭棋读档同样恢复私有局面，观战和回放路径不能直接使用这个私有序列化结果。
    ensureDirectories();
    const auto path = resolveSaveLoadPath(name_or_path);
    return DarkGameSession::deserialize(readAllText(path));
}

std::vector<std::filesystem::path> listSaveFiles()
{
    ensureDirectories();
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(saveDirectory()))
    {
        if (entry.is_regular_file() && isSaveFileExtension(entry.path()))
        {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::filesystem::path saveReplay(const GameSession& session, const std::string& name_hint)
{
    // 棋谱导出面向回放和交流，只记录可公开复盘的信息。
    ensureDirectories();
    std::string base_name = sanitizeName(name_hint.empty() ? "replay" : name_hint);
    if (base_name.empty())
    {
        base_name = "replay";
    }

    const auto directory = replayDirectory();
    const std::string timestamp = makeReplayTimestamp();
    std::filesystem::path path = directory / (base_name + "_" + timestamp + ".pgn");
    for (int suffix = 2; std::filesystem::exists(path); ++suffix)
    {
        path = directory / (base_name + "_" + timestamp + "_" + std::to_string(suffix) + ".pgn");
    }
    writeAllText(path, buildPgn(session));
    return path;
}

ReplayRecord loadReplay(const std::string& name_or_path)
{
    ensureDirectories();
    const auto path = resolveReplayPath(name_or_path);
    const std::string text = readAllText(path);
    const auto tags = parsePgnTags(text);

    ReplayRecord record;
    record.source_path = path;
    record.settings.use_easyx = true;
    record.settings.ai_enabled = false;

    auto board_mode_it = tags.find("BoardMode");
    if (board_mode_it != tags.end())
    {
        parseBoardModeTag(board_mode_it->second, record.settings.board_mode);
    }
    else
    {
        const auto variant_it = tags.find("Variant");
        if (variant_it != tags.end() && upperAscii(variant_it->second).find("11X10") != std::string::npos)
        {
            record.settings.board_mode = BoardMode::Expanded11x10;
        }
    }

    const auto time_it = tags.find("TimeControl");
    if (time_it != tags.end())
    {
        try
        {
            record.settings.move_time_limit_seconds = std::max(0, std::stoi(time_it->second));
        }
        catch (...)
        {
        }
    }

    const auto red_it = tags.find("Red");
    const auto black_it = tags.find("Black");
    const auto white_it = tags.find("White");
    if (red_it != tags.end())
    {
        record.players.red_name = red_it->second;
    }
    else if (white_it != tags.end())
    {
        record.players.red_name = white_it->second;
    }
    if (black_it != tags.end())
    {
        record.players.black_name = black_it->second;
    }

    const auto [parsed_result, parsed_side] = parseReplayOutcome(tags);
    record.result = parsed_result;
    record.result_side = parsed_side;

    std::string format = record.settings.board_mode == BoardMode::Standard9x10 ? "WXF" : "ICCS";
    const auto format_it = tags.find("Format");
    if (format_it != tags.end())
    {
        format = upperAscii(trimCopy(format_it->second));
    }

    const std::string stripped_movetext = stripPgnDecorations(parsePgnMovetext(text));
    std::istringstream input(stripped_movetext);
    std::string token;
    std::vector<std::string> move_tokens;
    while (input >> token)
    {
        token = normalizeMoveToken(token);
        if (token.empty() || isResultToken(token))
        {
            continue;
        }
        move_tokens.push_back(token);
    }

    MoveParser parser;
    GameSession replay(record.settings, record.players);
    for (const auto& token_text : move_tokens)
    {
        const auto legal_moves = replay.legalMovesForCurrentSide();
        std::vector<Move> matches;
        matches.reserve(legal_moves.size());

        for (auto candidate : legal_moves)
        {
            std::vector<std::string> candidate_notations;
            if (format == "ICCS")
            {
                candidate_notations.push_back(parser.moveToIccsText(candidate, replay.board().config()));
            }
            else if (format == "WXF")
            {
                candidate_notations.push_back(parser.moveToWxfText(candidate, replay));
            }
            else
            {
                candidate_notations.push_back(parser.moveToWxfText(candidate, replay));
                candidate_notations.push_back(parser.moveToIccsText(candidate, replay.board().config()));
            }

            const auto matched = std::find_if(
                candidate_notations.begin(),
                candidate_notations.end(),
                [&](const std::string& notation)
                {
                    return normalizeMoveToken(notation) == token_text;
                });
            if (matched != candidate_notations.end())
            {
                candidate.notation = *matched;
                matches.push_back(candidate);
            }
        }

        if (matches.empty())
        {
            throw StorageError("Unsupported or invalid PGN move: " + token_text);
        }
        if (matches.size() > 1)
        {
            throw StorageError("Ambiguous PGN move: " + token_text);
        }

        replay.submitMove(matches.front());
        record.moves.push_back(replay.history().back());
    }

    return record;
}

std::filesystem::path saveDarkReplay(const DarkGameSession& session, const std::string& name_hint)
{
    // 揭棋有私有局面和公开局面两套视角：存档需要保留完整暗子身份，
    // 回放和观战只能使用公开信息，避免提前泄露未翻开的棋子。
    ensureDirectories();
    std::string base_name = sanitizeName(name_hint.empty() ? "dark_replay" : name_hint);
    if (base_name.empty())
    {
        base_name = "dark_replay";
    }

    const auto directory = replayDirectory();
    const std::string timestamp = makeReplayTimestamp();
    std::filesystem::path path = directory / (base_name + "_" + timestamp + ".pgn");
    for (int suffix = 2; std::filesystem::exists(path); ++suffix)
    {
        path = directory / (base_name + "_" + timestamp + "_" + std::to_string(suffix) + ".pgn");
    }
    writeAllText(path, buildDarkCdc(session));
    return path;
}

DarkReplayRecord loadDarkReplay(const std::string& name_or_path)
{
    // CDC 回放通过公开翻子记录重建过程，不读取私有存档格式。
    ensureDirectories();
    const auto path = resolveReplayPath(name_or_path);
    const std::string text = readAllText(path);
    const auto tags = parsePgnTags(text);

    DarkReplayRecord record;
    record.source_path = path;
    record.settings.game_kind = GameKind::DarkChess;
    record.settings.use_easyx = true;
    record.settings.ai_enabled = false;

    if (const auto it = tags.find("Player1"); it != tags.end())
    {
        record.players.red_name = it->second;
    }
    if (const auto it = tags.find("Player2"); it != tags.end())
    {
        record.players.black_name = it->second;
    }
    if (const auto it = tags.find("InitialPrivateGrid"); it != tags.end())
    {
        record.initial_private_grid = gridFromPgnTag(it->second);
    }

    if (const auto it = tags.find("Result"); it != tags.end())
    {
        if (it->second == "1-0")
        {
            record.result = GameResult::RedWin;
        }
        else if (it->second == "0-1")
        {
            record.result = GameResult::BlackWin;
        }
        else if (it->second == "1/2-1/2")
        {
            record.result = GameResult::Draw;
        }
    }

    const std::string movetext = parsePgnMovetext(text);
    std::istringstream input(movetext);
    std::string token;
    int ply = 0;
    while (input >> token)
    {
        token = normalizeDarkCdcToken(token);
        if (token.empty() || isResultToken(token))
        {
            continue;
        }
        if (!token.empty() && token.back() == '.')
        {
            continue;
        }

        const DarkSeat seat = (ply % 2 == 0) ? DarkSeat::Player1 : DarkSeat::Player2;
        const auto action = parseDarkCdcToken(token, seat);
        if (!action.has_value())
        {
            throw StorageError("Unsupported or invalid dark CDC token: " + token);
        }
        record.actions.push_back(*action);
        ++ply;
    }
    return record;
}

std::vector<std::filesystem::path> listReplayFiles()
{
    ensureDirectories();
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(replayDirectory()))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".pgn")
        {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::string> loadReplayLines(const std::filesystem::path& path)
{
    std::vector<std::string> lines;
    std::istringstream input(readAllText(path));
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty())
        {
            lines.push_back(line);
        }
    }
    return lines;
}

void appendLeaderboard(const GameSession& session)
{
    if (!session.gameOver())
    {
        return;
    }

    ensureDirectories();
    const bool exists = std::filesystem::exists(leaderboardPath());
    std::ofstream output(leaderboardPath(), std::ios::app);
    if (!output)
    {
        throw StorageError("Failed to update leaderboard.");
    }

    if (!exists)
    {
        output << "timestamp,mode,red,black,result,winner,moves,duration_s,red_time_s,black_time_s\n";
    }

    output << makeTimestamp() << ','
           << toString(session.boardMode()) << ','
           << csvField(session.players().red_name) << ','
           << csvField(session.players().black_name) << ','
           << toString(session.result()) << ','
           << csvField(standardWinnerName(session)) << ','
           << session.history().size() << ','
           << standardDurationSeconds(session) << ','
           << session.remainingSeconds(Side::Red) << ','
           << session.remainingSeconds(Side::Black) << '\n';
}

void appendDarkLeaderboard(const DarkGameSession& session)
{
    if (!session.gameOver())
    {
        return;
    }

    ensureDirectories();
    const bool exists = std::filesystem::exists(leaderboardPath());
    std::ofstream output(leaderboardPath(), std::ios::app);
    if (!output)
    {
        throw StorageError("Failed to update leaderboard.");
    }

    if (!exists)
    {
        output << "timestamp,mode,red,black,result,winner,moves,duration_s,red_time_s,black_time_s\n";
    }

    output << makeTimestamp() << ','
           << "DarkChess" << ','
           << csvField(session.players().red_name) << ','
           << csvField(session.players().black_name) << ','
           << toString(session.result()) << ','
           << csvField(darkWinnerName(session)) << ','
           << session.history().size() << ','
           << darkDurationSeconds(session) << ','
           << session.remainingSeconds(DarkSeat::Player1) << ','
           << session.remainingSeconds(DarkSeat::Player2) << '\n';
}

std::vector<std::string> readLeaderboardLines()
{
    ensureDirectories();
    if (!std::filesystem::exists(leaderboardPath()))
    {
        return {};
    }
    return loadReplayLines(leaderboardPath());
}

std::vector<LeaderboardStanding> readLeaderboardStandings()
{
    std::unordered_map<std::string, LeaderboardStanding> standings;
    const auto lines = readLeaderboardLines();
    for (const auto& line : lines)
    {
        if (line.empty() || line.rfind("timestamp,", 0) == 0)
        {
            continue;
        }

        const auto fields = parseCsvLine(line);
        if (fields.size() < 8)
        {
            continue;
        }

        const std::string& red_name = fields[2];
        const std::string& black_name = fields[3];
        const std::string& result = fields[4];
        std::string winner_name;
        int moves = 0;
        int duration_seconds = 0;

        if (fields.size() >= 10)
        {
            winner_name = fields[5];
            moves = parseCsvInt(fields, 6);
            duration_seconds = parseCsvInt(fields, 7);
        }
        else
        {
            moves = parseCsvInt(fields, 5);
        }

        recordLeaderboardGame(standings, red_name, black_name, result, winner_name, moves, duration_seconds);
    }

    std::vector<LeaderboardStanding> result;
    result.reserve(standings.size());
    for (auto& entry : standings)
    {
        LeaderboardStanding standing = entry.second;
        if (standing.games > 0)
        {
            standing.win_rate = static_cast<double>(standing.wins) * 100.0 / static_cast<double>(standing.games);
            standing.average_moves = static_cast<double>(standing.total_moves) / static_cast<double>(standing.games);
        }
        result.push_back(standing);
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const LeaderboardStanding& lhs, const LeaderboardStanding& rhs)
        {
            if (lhs.win_rate != rhs.win_rate)
            {
                return lhs.win_rate > rhs.win_rate;
            }
            if (lhs.games != rhs.games)
            {
                return lhs.games > rhs.games;
            }
            if (lhs.wins != rhs.wins)
            {
                return lhs.wins > rhs.wins;
            }
            return lhs.player_name < rhs.player_name;
        });
    return result;
}

std::vector<std::string> formatLeaderboardTable(
    const std::vector<LeaderboardStanding>& standings,
    const size_t max_rows)
{
    std::vector<std::string> lines;
    lines.push_back("Leaderboard by Win Rate");
    if (standings.empty())
    {
        lines.push_back("Leaderboard is empty. Finish a game to create the first record.");
        return lines;
    }

    std::ostringstream header;
    header << std::left << std::setw(4) << "#"
           << std::setw(18) << "Player"
           << std::right << std::setw(7) << "Games"
           << std::setw(7) << "Wins"
           << std::setw(7) << "Loss"
           << std::setw(7) << "Draw"
           << std::setw(9) << "Win%"
           << std::setw(10) << "AvgMove"
           << std::setw(10) << "AvgTime";
    lines.push_back(header.str());

    const size_t rows = std::min(max_rows, standings.size());
    for (size_t i = 0; i < rows; ++i)
    {
        const auto& standing = standings[i];
        const double average_time = standing.games > 0
            ? static_cast<double>(standing.total_duration_seconds) / static_cast<double>(standing.games)
            : 0.0;
        std::string player = standing.player_name;
        if (player.size() > 17)
        {
            player = player.substr(0, 16) + ".";
        }

        std::ostringstream row;
        row << std::left << std::setw(4) << (i + 1)
            << std::setw(18) << player
            << std::right << std::setw(7) << standing.games
            << std::setw(7) << standing.wins
            << std::setw(7) << standing.losses
            << std::setw(7) << standing.draws
            << std::setw(8) << std::fixed << std::setprecision(1) << standing.win_rate << "%"
            << std::setw(10) << std::fixed << std::setprecision(1) << standing.average_moves
            << std::setw(9) << std::fixed << std::setprecision(1) << average_time << "s";
        lines.push_back(row.str());
    }
    return lines;
}

std::string sanitizeName(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char ch)
        {
            if (std::isalnum(ch) != 0 || ch == '_' || ch == '-')
            {
                return static_cast<char>(ch);
            }
            return '_';
        });
    return value;
}

} // namespace xiangqi::storage
