#include "storage/Storage.h"

#include "app/MoveParser.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <unordered_map>
#include <sstream>

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
    ensureDirectories();
    const std::string file_name = sanitizeName(name_hint.empty() ? ("save_" + makeTimestamp()) : name_hint) + ".xqsave";
    const auto path = saveDirectory() / file_name;
    writeAllText(path, session.serialize());
    return path;
}

GameSession loadGame(const std::string& name_or_path)
{
    ensureDirectories();
    std::filesystem::path path(name_or_path);
    if (!path.is_absolute())
    {
        path = saveDirectory() / name_or_path;
        if (!path.has_extension())
        {
            path.replace_extension(".xqsave");
        }
    }

    return GameSession::deserialize(readAllText(path));
}

std::vector<std::filesystem::path> listSaveFiles()
{
    ensureDirectories();
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(saveDirectory()))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".xqsave")
        {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::filesystem::path saveReplay(const GameSession& session, const std::string& name_hint)
{
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
        output << "timestamp,mode,red,black,result,moves,red_time_s,black_time_s\n";
    }

    output << makeTimestamp() << ','
           << toString(session.boardMode()) << ','
           << csvField(session.players().red_name) << ','
           << csvField(session.players().black_name) << ','
           << toString(session.result()) << ','
           << session.history().size() << ','
           << session.remainingSeconds(Side::Red) << ','
           << session.remainingSeconds(Side::Black) << '\n';
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
