#include "ui_console/ConsoleApp.h"

#include "net/NetworkSession.h"
#include "storage/Storage.h"
#include "tests/EngineSelfTest.h"
#include "ui_easyx/EasyXApp.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace xiangqi
{

namespace
{

std::string coordText(const Position<int> position, const BoardConfig& config)
{
    std::string text;
    text += config.coordinate_files[static_cast<size_t>(position.col)];
    text += static_cast<char>('0' + position.row);
    return text;
}

std::string colorize(const std::string& text, const Side side)
{
    const char* code = side == Side::Red ? "\x1b[31m" : "\x1b[36m";
    return std::string(code) + text + "\x1b[0m";
}

std::vector<std::string> splitProtocol(const std::string& line)
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
    return fields;
}

std::string moveLine(const std::string& tag, const Move& move, const BoardConfig& config)
{
    return tag + "|" + coordText(move.from, config) + "|" + coordText(move.to, config);
}

Side parseWireSide(const std::string& text)
{
    return text == "Black" ? Side::Black : Side::Red;
}

} // namespace

int ConsoleApp::run()
{
    configureConsole();
    storage::ensureDirectories();
    EasyXApp easyx_app;

    auto runGuiWithHiddenConsole = [&](auto&& callback)
    {
        HWND console_window = GetConsoleWindow();
        if (console_window != nullptr)
        {
            ShowWindow(console_window, SW_HIDE);
        }

        try
        {
            callback();
        }
        catch (...)
        {
            if (console_window != nullptr)
            {
                ShowWindow(console_window, SW_SHOW);
                SetForegroundWindow(console_window);
            }
            throw;
        }

        if (console_window != nullptr)
        {
            ShowWindow(console_window, SW_SHOW);
            SetForegroundWindow(console_window);
        }
    };

    while (true)
    {
        printMenu();
        std::string choice;
        std::getline(std::cin, choice);
        if (choice == "1")
        {
            GameSettings settings = promptSettings(false);
            PlayerInfo players = promptPlayers(false);
            settings.use_easyx = false;
            settings.ai_enabled = false;
            runConsoleGame(settings, std::move(players));
        }
        else if (choice == "2")
        {
            if (!easyx_app.isAvailable())
            {
                std::cout << "EasyX is not available in the current build environment.\n";
                continue;
            }
            GameSettings settings = promptSettings(false);
            PlayerInfo players = promptPlayers(false);
            settings.use_easyx = true;
            runGuiWithHiddenConsole(
                [&]()
                {
                    easyx_app.run(settings, std::move(players));
                });
        }
        else if (choice == "3")
        {
            if (!easyx_app.isAvailable())
            {
                std::cout << "EasyX is not available in the current build environment.\n";
                continue;
            }
            GameSettings settings = promptSettings(true);
            PlayerInfo players = promptPlayers(true);
            settings.use_easyx = true;
            runGuiWithHiddenConsole(
                [&]()
                {
                    easyx_app.run(settings, std::move(players));
                });
        }
        else if (choice == "4")
        {
            if (!easyx_app.isAvailable())
            {
                std::cout << "EasyX is not available in the current build environment.\n";
                continue;
            }
            GameSettings settings = promptSettings(false);
            PlayerInfo players = promptPlayers(false);
            settings.use_easyx = true;
            settings.ai_enabled = false;
            const unsigned short port = promptPort();
            std::cout << "Waiting for one client on port " << port << "...\n";
            auto network = std::make_unique<NetworkSession>();
            std::atomic_bool stop_advertising{ false };
            std::thread advertiser(
                [&]()
                {
                    while (!stop_advertising)
                    {
                        try
                        {
                            LanRoom room;
                            room.name = players.red_name + "'s Xiangqi room";
                            room.port = port;
                            room.accepts_player = true;
                            room.accepts_spectators = false;
                            broadcastLanRoom(room);
                        }
                        catch (...)
                        {
                        }
                        for (int i = 0; i < 20 && !stop_advertising; ++i)
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }
                    }
                });
            try
            {
                network->host(port);
                stop_advertising = true;
                if (advertiser.joinable())
                {
                    advertiser.join();
                }
                network->sendLine(serializeHandshake(settings, players, Side::Red));
                std::cout << "Client connected. Launching EasyX host view...\n";
                runGuiWithHiddenConsole(
                    [&]()
                    {
                        easyx_app.runNetworkGame(settings, std::move(players), std::move(network), Side::Red);
                    });
            }
            catch (const std::exception& ex)
            {
                stop_advertising = true;
                if (advertiser.joinable())
                {
                    advertiser.join();
                }
                std::cout << "Network error: " << ex.what() << "\n";
            }
        }
        else if (choice == "5")
        {
            if (!easyx_app.isAvailable())
            {
                std::cout << "EasyX is not available in the current build environment.\n";
                continue;
            }
            const auto room = chooseLanRoom(true, false);
            const std::string address = room.has_value() ? room->address : promptAddress();
            const unsigned short port = room.has_value() ? room->port : promptPort();
            auto network = std::make_unique<NetworkSession>();
            try
            {
                network->join(address, port);
                GameSettings settings;
                PlayerInfo players;
                Side first_turn = Side::Red;
                parseHandshake(network->receiveLine(), settings, players, first_turn);
                settings.ai_enabled = false;
                settings.use_easyx = true;
                std::cout << "Connected. Launching EasyX client view...\n";
                runGuiWithHiddenConsole(
                    [&]()
                    {
                        easyx_app.runNetworkGame(settings, std::move(players), std::move(network), Side::Black);
                    });
            }
            catch (const std::exception& ex)
            {
                std::cout << "Network error: " << ex.what() << "\n";
            }
        }
        else if (choice == "6")
        {
            watchNetworkGame();
        }
        else if (choice == "7")
        {
            if (!easyx_app.isAvailable())
            {
                std::cout << "EasyX is not available in the current build environment.\n";
                continue;
            }
            const auto room = chooseLanRoom(true, false);
            const std::string address = room.has_value() ? room->address : promptAddress();
            const unsigned short port = room.has_value() ? room->port : promptPort();
            auto network = std::make_unique<NetworkSession>();
            try
            {
                network->join(address, port);
                network->sendLine("REJOIN_REQ|Black");
                GameSettings settings;
                PlayerInfo players;
                Side first_turn = Side::Red;
                parseHandshake(network->receiveLine(), settings, players, first_turn);
                settings.ai_enabled = false;
                settings.use_easyx = true;
                std::cout << "Reconnected. Launching EasyX client view...\n";
                runGuiWithHiddenConsole(
                    [&]()
                    {
                        easyx_app.runNetworkGame(settings, std::move(players), std::move(network), Side::Black);
                    });
            }
            catch (const std::exception& ex)
            {
                std::cout << "Reconnect error: " << ex.what() << "\n";
            }
        }
        else if (choice == "8")
        {
            if (!easyx_app.isAvailable())
            {
                std::cout << "EasyX is not available in the current build environment.\n";
                continue;
            }
            try
            {
                runGuiWithHiddenConsole(
                    [&]()
                    {
                        easyx_app.runReplayBrowser();
                    });
            }
            catch (const std::exception& ex)
            {
                std::cout << "Replay error: " << ex.what() << "\n";
            }
        }
        else if (choice == "9")
        {
            showLeaderboard();
        }
        else if (choice == "10")
        {
            try
            {
                std::cout << tests::runAll() << "\n";
            }
            catch (const std::exception& ex)
            {
                std::cout << "Self-test failed: " << ex.what() << "\n";
            }
        }
        else if (choice == "11" || choice == "exit")
        {
            return 0;
        }
    }
}

void ConsoleApp::configureConsole() const
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return;
    }

    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode))
    {
        return;
    }
    SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

void ConsoleApp::printMenu() const
{
    std::cout << "\n=== Xiangqi Launcher ===\n";
    std::cout << "1. Local console game\n";
    std::cout << "2. Local two-player EasyX game\n";
    std::cout << "3. Human vs AI EasyX game\n";
    std::cout << "4. LAN host EasyX game\n";
    std::cout << "5. Join LAN EasyX game\n";
    std::cout << "6. Watch LAN game\n";
    std::cout << "7. Reconnect LAN client\n";
    std::cout << "8. Open PGN replay in EasyX\n";
    std::cout << "9. Show leaderboard\n";
    std::cout << "10. Run engine self-tests\n";
    std::cout << "11. Exit\n";
    std::cout << "Choice: ";
}

void ConsoleApp::showHelp() const
{
    std::cout << "Commands:\n";
    std::cout << "  a0 a1     move by coordinates\n";
    std::cout << "  a0        list legal moves from one square\n";
    std::cout << "  Chinese notation is available on the standard board\n";
    std::cout << "  undo      revert one ply\n";
    std::cout << "  save name save to data/saves/name.xqsave\n";
    std::cout << "  load name load a saved game\n";
    std::cout << "  hint      ask the search engine for a move\n";
    std::cout << "  replay    replay the current move history\n";
    std::cout << "  resign    resign the game\n";
    std::cout << "  tests     run engine smoke tests\n";
    std::cout << "  exit      leave the current game\n";
}

void ConsoleApp::printBoard(const GameSession& session, const std::vector<Move>* highlight_moves) const
{
    const auto& board = session.board();
    std::cout << "    ";
    for (char file : board.config().coordinate_files)
    {
        std::cout << std::setw(3) << file;
    }
    std::cout << "\n";

    for (int row = 0; row < board.config().rows; ++row)
    {
        std::cout << std::setw(3) << row << ' ';
        for (int col = 0; col < board.config().cols; ++col)
        {
            const Position<int> position{ row, col };
            const auto* piece = board.pieceAt(position);
            bool is_highlight = false;
            if (highlight_moves != nullptr)
            {
                for (const auto& move : *highlight_moves)
                {
                    if (move.to == position)
                    {
                        is_highlight = true;
                        break;
                    }
                }
            }

            if (piece == nullptr)
            {
                std::cout << std::setw(3) << (is_highlight ? "..*" : "..");
                continue;
            }

            std::string token;
            token += piece->side() == Side::Red ? 'r' : 'b';
            token += piece->symbol();
            if (is_highlight)
            {
                token += '*';
            }

            std::cout << std::setw(3) << colorize(token, piece->side());
        }
        std::cout << '\n';
    }

    std::cout << "Mode: " << toString(session.boardMode())
              << " | Current: " << session.currentPlayerName()
              << " (" << toString(session.currentSide()) << ")"
              << " | Red " << session.remainingSeconds(Side::Red) << "s"
              << " | Black " << session.remainingSeconds(Side::Black) << "s\n";
}

BoardMode ConsoleApp::promptBoardMode() const
{
    std::cout << "Board mode: 1) Standard 9x10  2) Expanded 11x10 : ";
    std::string choice;
    std::getline(std::cin, choice);
    return choice == "2" ? BoardMode::Expanded11x10 : BoardMode::Standard9x10;
}

GameSettings ConsoleApp::promptSettings(const bool ai_enabled) const
{
    GameSettings settings;
    settings.board_mode = promptBoardMode();
    settings.ai_enabled = ai_enabled;
    settings.ai_side = Side::Black;

    std::string line;
    std::cout << "Move time limit in seconds (default 60): ";
    std::getline(std::cin, line);
    if (!line.empty())
    {
        settings.move_time_limit_seconds = std::max(5, std::stoi(line));
    }

    std::cout << "Allow undo? (y/n, default y): ";
    std::getline(std::cin, line);
    if (!line.empty())
    {
        settings.allow_undo = line != "n" && line != "N";
    }

    std::cout << "Show legal move hints? (y/n, default y): ";
    std::getline(std::cin, line);
    if (!line.empty())
    {
        settings.show_legal_moves = line != "n" && line != "N";
    }

    if (ai_enabled)
    {
        settings.ai_depth = 4;
    }

    return settings;
}

PlayerInfo ConsoleApp::promptPlayers(const bool ai_enabled) const
{
    PlayerInfo players;
    std::cout << "Red player name (default Red): ";
    std::getline(std::cin, players.red_name);
    if (players.red_name.empty())
    {
        players.red_name = "Red";
    }

    if (ai_enabled)
    {
        players.black_name = "Computer";
    }
    else
    {
        std::cout << "Black player name (default Black): ";
        std::getline(std::cin, players.black_name);
        if (players.black_name.empty())
        {
            players.black_name = "Black";
        }
    }
    return players;
}

unsigned short ConsoleApp::promptPort(const unsigned short default_port) const
{
    std::cout << "Port (default " << default_port << "): ";
    std::string text;
    std::getline(std::cin, text);
    if (text.empty())
    {
        return default_port;
    }
    return static_cast<unsigned short>(std::stoi(text));
}

std::string ConsoleApp::promptAddress() const
{
    std::cout << "Host IPv4 address: ";
    std::string address;
    std::getline(std::cin, address);
    return address;
}

std::optional<LanRoom> ConsoleApp::chooseLanRoom(const bool require_player_slot, const bool require_spectator_slot) const
{
    std::cout << "Searching LAN rooms...\n";
    std::vector<LanRoom> rooms;
    try
    {
        rooms = discoverLanRooms();
    }
    catch (const std::exception& ex)
    {
        std::cout << "Room discovery failed: " << ex.what() << "\n";
    }

    rooms.erase(
        std::remove_if(
            rooms.begin(),
            rooms.end(),
            [&](const LanRoom& room)
            {
                return (require_player_slot && !room.accepts_player) ||
                       (require_spectator_slot && !room.accepts_spectators);
            }),
        rooms.end());

    if (rooms.empty())
    {
        std::cout << "No matching LAN rooms found. Use manual address? (y/n, default y): ";
        std::string answer;
        std::getline(std::cin, answer);
        if (answer == "n" || answer == "N")
        {
            return std::nullopt;
        }
        LanRoom manual;
        manual.address = promptAddress();
        manual.port = promptPort();
        manual.name = manual.address;
        manual.accepts_player = true;
        manual.accepts_spectators = true;
        return manual;
    }

    std::cout << "Discovered rooms:\n";
    for (size_t index = 0; index < rooms.size(); ++index)
    {
        const auto& room = rooms[index];
        std::cout << "  " << (index + 1) << ". " << room.name
                  << " @ " << room.address << ':' << room.port
                  << " | spectators=" << room.spectator_count
                  << " | player=" << (room.accepts_player ? "open" : "closed")
                  << " | watch=" << (room.accepts_spectators ? "open" : "closed")
                  << "\n";
    }
    std::cout << "Choose room number, or 0 for manual address: ";

    std::string choice;
    std::getline(std::cin, choice);
    if (choice == "0")
    {
        LanRoom manual;
        manual.address = promptAddress();
        manual.port = promptPort();
        manual.name = manual.address;
        manual.accepts_player = true;
        manual.accepts_spectators = true;
        return manual;
    }

    try
    {
        const int selected = std::stoi(choice);
        if (selected >= 1 && selected <= static_cast<int>(rooms.size()))
        {
            return rooms[static_cast<size_t>(selected - 1)];
        }
    }
    catch (...)
    {
    }

    std::cout << "Invalid room selection.\n";
    return std::nullopt;
}

void ConsoleApp::runConsoleGame(GameSettings settings, PlayerInfo players)
{
    GameSession session(settings, std::move(players));
    SearchEngine search(settings.ai_depth);

    while (true)
    {
        if (!session.gameOver())
        {
            session.tickClock();
        }

        printBoard(session);
        if (session.gameOver())
        {
            std::cout << session.resultText() << "\n";
            try
            {
                storage::appendLeaderboard(session);
            }
            catch (...)
            {
            }
            std::cout << "Press Enter to return to the main menu.";
            std::string dummy;
            std::getline(std::cin, dummy);
            return;
        }

        if (session.settings().ai_enabled && session.currentSide() == session.settings().ai_side)
        {
            if (const auto best = search.chooseBestMove(session); best.has_value())
            {
                Move move = *best;
                move.notation = parser_.moveToCoordinateText(move, session.board().config());
                if (session.boardMode() == BoardMode::Standard9x10)
                {
                    move.notation = parser_.moveToChineseText(move, session);
                }
                session.submitMove(move);
                std::cout << "AI plays: " << move.notation << "\n";
                continue;
            }
        }

        std::cout << session.currentPlayerName() << " > ";
        std::string input;
        std::getline(std::cin, input);

        try
        {
            const ParsedCommand command = parser_.parse(input, session);
            switch (command.type)
            {
            case CommandType::Move:
            {
                Move move = *command.move;
                move.notation = parser_.moveToCoordinateText(move, session.board().config());
                if (session.boardMode() == BoardMode::Standard9x10 && input.find(' ') == std::string::npos)
                {
                    move.notation = parser_.moveToChineseText(move, session);
                }
                session.submitMove(move);
                break;
            }
            case CommandType::ShowMoves:
            {
                const auto moves = session.legalMovesFrom(*command.position);
                if (moves.empty())
                {
                    std::cout << "No legal moves from " << coordText(*command.position, session.board().config()) << ".\n";
                }
                else
                {
                    printBoard(session, &moves);
                    std::cout << "Legal moves:";
                    for (const auto& move : moves)
                    {
                        std::cout << ' ' << coordText(move.to, session.board().config());
                    }
                    std::cout << "\n";
                }
                break;
            }
            case CommandType::Undo:
                if (!session.undoLastPly())
                {
                    std::cout << "Undo is not available.\n";
                }
                else if (session.settings().ai_enabled && !session.history().empty() && session.currentSide() == session.settings().ai_side)
                {
                    session.undoLastPly();
                }
                break;
            case CommandType::Save:
            {
                const auto path = storage::saveGame(session, command.argument.empty() ? "manual_save" : command.argument);
                const auto replay_path = storage::saveReplay(session, command.argument.empty() ? "manual_replay" : command.argument);
                std::cout << "Saved to " << path.string() << "\n";
                std::cout << "PGN exported to " << replay_path.string() << "\n";
                break;
            }
            case CommandType::Load:
                session = storage::loadGame(command.argument.empty() ? "manual_save" : command.argument);
                break;
            case CommandType::Resign:
                session.resign(session.currentSide());
                break;
            case CommandType::Hint:
                if (const auto best = search.chooseBestMove(session); best.has_value())
                {
                    std::cout << "Hint: " << parser_.moveToCoordinateText(*best, session.board().config()) << "\n";
                }
                else
                {
                    std::cout << "No legal move available.\n";
                }
                break;
            case CommandType::Replay:
                replayCurrentGame(session);
                break;
            case CommandType::Help:
                showHelp();
                break;
            case CommandType::Tests:
                std::cout << tests::runAll() << "\n";
                break;
            case CommandType::Exit:
                return;
            default:
                std::cout << "Unknown command.\n";
                break;
            }
        }
        catch (const std::exception& ex)
        {
            std::cout << "Error: " << ex.what() << "\n";
        }
    }
}

void ConsoleApp::runHostedNetworkGame(GameSettings settings, PlayerInfo players)
{
    const unsigned short port = promptPort();
    NetworkSession network;
    std::cout << "Waiting for one client on port " << port << "...\n";
    network.host(port);
    std::cout << "Client connected. Host plays Red.\n";

    GameSession session(settings, std::move(players));
    SearchEngine search(settings.ai_depth);
    network.sendLine(serializeHandshake(settings, session.players(), Side::Red));

    while (true)
    {
        if (!session.gameOver())
        {
            session.tickClock();
        }

        printBoard(session);
        if (session.gameOver())
        {
            std::cout << session.resultText() << "\n";
            try
            {
                storage::appendLeaderboard(session);
            }
            catch (...)
            {
            }
            std::cout << "Press Enter to return to the main menu.";
            std::string dummy;
            std::getline(std::cin, dummy);
            return;
        }

        if (session.currentSide() == Side::Red)
        {
            std::cout << session.players().red_name << " [host] > ";
            std::string input;
            std::getline(std::cin, input);

            try
            {
                const ParsedCommand command = parser_.parse(input, session);
                switch (command.type)
                {
                case CommandType::Move:
                {
                    Move move = *command.move;
                    move.notation = parser_.moveToCoordinateText(move, session.board().config());
                    if (session.boardMode() == BoardMode::Standard9x10 && input.find(' ') == std::string::npos)
                    {
                        move.notation = parser_.moveToChineseText(move, session);
                    }
                    const Move applied = session.submitMove(move);
                    network.sendLine(moveLine("MOVE_OK", applied, session.board().config()));
                    break;
                }
                case CommandType::ShowMoves:
                {
                    const auto moves = session.legalMovesFrom(*command.position);
                    printBoard(session, &moves);
                    break;
                }
                case CommandType::Undo:
                    if (session.undoLastPly())
                    {
                        network.sendLine("UNDO_OK");
                    }
                    else
                    {
                        std::cout << "Undo is not available.\n";
                    }
                    break;
                case CommandType::Save:
                {
                    const auto path = storage::saveGame(session, command.argument.empty() ? "network_host" : command.argument);
                    const auto replay_path = storage::saveReplay(session, command.argument.empty() ? "network_host" : command.argument);
                    std::cout << "Saved to " << path.string() << "\n";
                    std::cout << "PGN exported to " << replay_path.string() << "\n";
                    break;
                }
                case CommandType::Load:
                    std::cout << "Load is disabled during network play.\n";
                    break;
                case CommandType::Resign:
                    session.resign(Side::Red);
                    network.sendLine("RESIGN_OK|Red");
                    break;
                case CommandType::Hint:
                    if (const auto best = search.chooseBestMove(session); best.has_value())
                    {
                        std::cout << "Hint: " << parser_.moveToCoordinateText(*best, session.board().config()) << "\n";
                    }
                    break;
                case CommandType::Replay:
                    replayCurrentGame(session);
                    break;
                case CommandType::Help:
                    showHelp();
                    break;
                case CommandType::Tests:
                    std::cout << tests::runAll() << "\n";
                    break;
                case CommandType::Exit:
                    session.resign(Side::Red);
                    network.sendLine("RESIGN_OK|Red");
                    return;
                default:
                    break;
                }
            }
            catch (const std::exception& ex)
            {
                std::cout << "Error: " << ex.what() << "\n";
            }
        }
        else
        {
            std::cout << "Waiting for remote move...\n";
            try
            {
                const auto fields = splitProtocol(network.receiveLine());
                if (fields.empty())
                {
                    continue;
                }

                if (fields[0] == "MOVE_REQ" && fields.size() >= 3)
                {
                    ParsedCommand parsed = parser_.parse(fields[1] + " " + fields[2], session);
                    Move move = *parsed.move;
                    move.notation = parser_.moveToCoordinateText(move, session.board().config());
                    const Move applied = session.submitMove(move);
                    network.sendLine(moveLine("MOVE_OK", applied, session.board().config()));
                }
                else if (fields[0] == "UNDO_REQ")
                {
                    if (session.undoLastPly())
                    {
                        network.sendLine("UNDO_OK");
                    }
                    else
                    {
                        network.sendLine("ERROR|Undo is not available.");
                    }
                }
                else if (fields[0] == "RESIGN_REQ")
                {
                    session.resign(Side::Black);
                    network.sendLine("RESIGN_OK|Black");
                }
            }
            catch (const std::exception& ex)
            {
                network.sendLine(std::string("ERROR|") + ex.what());
                std::cout << "Remote move failed: " << ex.what() << "\n";
            }
        }
    }
}

void ConsoleApp::runJoinedNetworkGame()
{
    const std::string address = promptAddress();
    const unsigned short port = promptPort();
    NetworkSession network;
    network.join(address, port);

    GameSettings settings;
    PlayerInfo players;
    Side first_turn = Side::Red;
    parseHandshake(network.receiveLine(), settings, players, first_turn);
    GameSession session(settings, players);
    SearchEngine search(settings.ai_depth);

    std::cout << "Connected. Client plays Black.\n";
    std::cout << "Mode: " << toString(settings.board_mode)
              << " | Time: " << settings.move_time_limit_seconds
              << " | Undo: " << (settings.allow_undo ? "on" : "off") << "\n";

    while (true)
    {
        if (!session.gameOver())
        {
            session.tickClock();
        }

        printBoard(session);
        if (session.gameOver())
        {
            std::cout << session.resultText() << "\n";
            std::cout << "Press Enter to return to the main menu.";
            std::string dummy;
            std::getline(std::cin, dummy);
            return;
        }

        if (session.currentSide() == Side::Black)
        {
            std::cout << session.players().black_name << " [client] > ";
            std::string input;
            std::getline(std::cin, input);

            try
            {
                const ParsedCommand command = parser_.parse(input, session);
                switch (command.type)
                {
                case CommandType::Move:
                {
                    Move move = *command.move;
                    move.notation = parser_.moveToCoordinateText(move, session.board().config());
                    network.sendLine(moveLine("MOVE_REQ", move, session.board().config()));
                    const auto fields = splitProtocol(network.receiveLine());
                    if (!fields.empty() && fields[0] == "MOVE_OK" && fields.size() >= 3)
                    {
                        ParsedCommand parsed = parser_.parse(fields[1] + " " + fields[2], session);
                        Move approved = *parsed.move;
                        approved.notation = parser_.moveToCoordinateText(approved, session.board().config());
                        session.submitMove(approved);
                    }
                    else if (!fields.empty() && fields[0] == "ERROR" && fields.size() >= 2)
                    {
                        std::cout << "Host rejected move: " << fields[1] << "\n";
                    }
                    break;
                }
                case CommandType::ShowMoves:
                {
                    const auto moves = session.legalMovesFrom(*command.position);
                    printBoard(session, &moves);
                    break;
                }
                case CommandType::Undo:
                {
                    network.sendLine("UNDO_REQ");
                    const auto fields = splitProtocol(network.receiveLine());
                    if (!fields.empty() && fields[0] == "UNDO_OK")
                    {
                        session.undoLastPly();
                    }
                    else if (!fields.empty() && fields[0] == "ERROR" && fields.size() >= 2)
                    {
                        std::cout << "Undo rejected: " << fields[1] << "\n";
                    }
                    break;
                }
                case CommandType::Save:
                {
                    const auto path = storage::saveGame(session, command.argument.empty() ? "network_client" : command.argument);
                    std::cout << "Saved to " << path.string() << "\n";
                    std::cout << "PGN is saved by the LAN host.\n";
                    break;
                }
                case CommandType::Load:
                    std::cout << "Load is disabled during network play.\n";
                    break;
                case CommandType::Resign:
                {
                    network.sendLine("RESIGN_REQ");
                    const auto fields = splitProtocol(network.receiveLine());
                    if (!fields.empty() && fields[0] == "RESIGN_OK" && fields.size() >= 2)
                    {
                        session.resign(parseWireSide(fields[1]));
                    }
                    break;
                }
                case CommandType::Hint:
                    if (const auto best = search.chooseBestMove(session); best.has_value())
                    {
                        std::cout << "Hint: " << parser_.moveToCoordinateText(*best, session.board().config()) << "\n";
                    }
                    break;
                case CommandType::Replay:
                    replayCurrentGame(session);
                    break;
                case CommandType::Help:
                    showHelp();
                    break;
                case CommandType::Tests:
                    std::cout << tests::runAll() << "\n";
                    break;
                case CommandType::Exit:
                    network.sendLine("RESIGN_REQ");
                    return;
                default:
                    break;
                }
            }
            catch (const std::exception& ex)
            {
                std::cout << "Error: " << ex.what() << "\n";
            }
        }
        else
        {
            std::cout << "Waiting for host move...\n";
            try
            {
                const auto fields = splitProtocol(network.receiveLine());
                if (fields.empty())
                {
                    continue;
                }

                if (fields[0] == "MOVE_OK" && fields.size() >= 3)
                {
                    ParsedCommand parsed = parser_.parse(fields[1] + " " + fields[2], session);
                    Move move = *parsed.move;
                    move.notation = parser_.moveToCoordinateText(move, session.board().config());
                    session.submitMove(move);
                }
                else if (fields[0] == "UNDO_OK")
                {
                    session.undoLastPly();
                }
                else if (fields[0] == "RESIGN_OK" && fields.size() >= 2)
                {
                    session.resign(parseWireSide(fields[1]));
                }
                else if (fields[0] == "ERROR" && fields.size() >= 2)
                {
                    std::cout << "Host error: " << fields[1] << "\n";
                }
            }
            catch (const std::exception& ex)
            {
                std::cout << "Network error: " << ex.what() << "\n";
                return;
            }
        }
    }
}

void ConsoleApp::watchNetworkGame()
{
    const auto room = chooseLanRoom(false, true);
    if (!room.has_value())
    {
        return;
    }

    NetworkSession network;
    try
    {
        network.join(room->address, room->port);
        network.sendLine("WATCH_REQ");

        GameSettings settings;
        PlayerInfo players;
        Side first_turn = Side::Red;
        parseHandshake(network.receiveLine(), settings, players, first_turn);

        std::cout << "Watching " << room->name << " at " << room->address << ':' << room->port << ".\n";
        std::cout << "This view is read-only. Close the window or press Ctrl+C to stop watching.\n";

        while (true)
        {
            const auto fields = splitProtocol(network.receiveLine());
            if (fields.empty())
            {
                continue;
            }

            if (fields[0] == "STATE" && fields.size() >= 2)
            {
                const GameSession session = GameSession::deserialize(unescapeProtocolField(fields[1]));
                printBoard(session);
                if (session.gameOver())
                {
                    std::cout << session.resultText() << "\n";
                }
            }
            else if (fields[0] == "ERROR" && fields.size() >= 2)
            {
                std::cout << "Spectator error: " << fields[1] << "\n";
                return;
            }
        }
    }
    catch (const std::exception& ex)
    {
        std::cout << "Watch error: " << ex.what() << "\n";
    }
}

void ConsoleApp::replayCurrentGame(const GameSession& session) const
{
    GameSettings settings = session.settings();
    settings.ai_enabled = false;
    GameSession replay(settings, session.players());

    std::cout << "Replay from start:\n";
    printBoard(replay);
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    for (const auto& move : session.history())
    {
        replay.submitMove(move);
        printBoard(replay);
        std::cout << "Move: " << (!move.notation.empty() ? move.notation : coordText(move.from, replay.board().config()) + " " + coordText(move.to, replay.board().config())) << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
    }
}

void ConsoleApp::showLeaderboard() const
{
    const auto lines = storage::readLeaderboardLines();
    if (lines.empty())
    {
        std::cout << "Leaderboard is empty.\n";
        return;
    }
    for (const auto& line : lines)
    {
        std::cout << line << '\n';
    }
}

} // namespace xiangqi
