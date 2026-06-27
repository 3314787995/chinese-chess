#include "ui_console/ConsoleApp.h"

#include "net/NetworkSession.h"
#include "storage/Storage.h"
#include "tests/EngineSelfTest.h"
#include "ui_console/ConsoleFormatting.h"
#include "ui_console/ConsoleMenu.h"
#include "ui_easyx/EasyXApp.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
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

std::vector<ConsoleMenuEntry> mainMenuEntries()
{
    std::vector<ConsoleMenuEntry> entries;
    for (const auto& group : mainConsoleMenuGroups())
    {
        entries.insert(entries.end(), group.entries.begin(), group.entries.end());
    }
    return entries;
}

std::string trimInput(const std::string& text)
{
    const auto begin = std::find_if_not(
        text.begin(),
        text.end(),
        [](const unsigned char ch)
        {
            return std::isspace(ch) != 0;
        });
    const auto end = std::find_if_not(
        text.rbegin(),
        text.rend(),
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

std::string moveLine(const std::string& tag, const Move& move, const BoardConfig& config)
{
    return tag + "|" + coordText(move.from, config) + "|" + coordText(move.to, config);
}

Side parseWireSide(const std::string& text)
{
    return text == "Black" ? Side::Black : Side::Red;
}

std::string escapeDarkStateField(const std::string& value)
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

DarkGameSession makeDarkReplayInitialSession(
    GameSettings settings,
    const PlayerInfo& players,
    const std::string& initial_private_grid)
{
    settings.game_kind = GameKind::DarkChess;
    settings.ai_enabled = false;
    settings.use_easyx = false;

    std::ostringstream fixture;
    fixture << "VERSION=1\n"
            << "GAME=DarkChess\n"
            << "CURRENT_SEAT=Player1\n"
            << "RESULT=Ongoing\n"
            << "PLAYER1_NAME=" << escapeDarkStateField(players.red_name) << "\n"
            << "PLAYER2_NAME=" << escapeDarkStateField(players.black_name) << "\n"
            << "PLAYER1_COLOR=Unknown\n"
            << "PLAYER2_COLOR=Unknown\n"
            << "MOVE_TIME=" << settings.move_time_limit_seconds << "\n"
            << "ALLOW_UNDO=" << (settings.allow_undo ? 1 : 0) << "\n"
            << "SHOW_LEGAL=" << (settings.show_legal_moves ? 1 : 0) << "\n"
            << "AI_ENABLED=0\n"
            << "AI_SEAT=" << toString(settings.dark_ai_seat) << "\n"
            << "USE_EASYX=0\n"
            << "PLAYER1_REMAINING_MS=" << settings.move_time_limit_seconds * 1000 << "\n"
            << "PLAYER2_REMAINING_MS=" << settings.move_time_limit_seconds * 1000 << "\n"
            << "QUIET_PLIES=0\n"
            << "INITIAL_BOARD_BEGIN\n" << initial_private_grid << "\nINITIAL_BOARD_END\n"
            << "BOARD_BEGIN\n" << initial_private_grid << "\nBOARD_END\n"
            << "HISTORY_BEGIN\n"
            << "HISTORY_END\n";
    return DarkGameSession::deserialize(fixture.str());
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

    auto ensureEasyX = [&]() -> bool
    {
        if (easyx_app.isAvailable())
        {
            return true;
        }
        std::cout << "当前构建环境不可用 EasyX 图形界面。\n";
        return false;
    };

    while (true)
    {
        LauncherAction action = promptMainMenu();
        if (action == LauncherAction::OpenPlayMenu)
        {
            action = promptSubMenu("开始对局", playMenuEntries());
        }
        else if (action == LauncherAction::OpenNetworkMenu)
        {
            action = promptSubMenu("联机大厅", networkMenuEntries());
        }
        else if (action == LauncherAction::OpenWatchReplayMenu)
        {
            action = promptSubMenu("观战回放", watchReplayMenuEntries());
        }
        else if (action == LauncherAction::OpenToolsMenu)
        {
            action = promptSubMenu("排行测试", toolsMenuEntries());
        }

        if (action == LauncherAction::Back)
        {
            continue;
        }

        switch (action)
        {
        case LauncherAction::LocalConsoleGame:
        {
            GameSettings settings = promptSettings(false);
            PlayerInfo players = promptPlayers(false);
            settings.use_easyx = false;
            settings.ai_enabled = false;
            runConsoleGame(settings, std::move(players));
            break;
        }
        case LauncherAction::LocalEasyXGame:
        {
            if (!ensureEasyX())
            {
                break;
            }
            GameSettings settings = promptSettings(false);
            PlayerInfo players = promptPlayers(false);
            settings.use_easyx = true;
            runGuiWithHiddenConsole(
                [&]()
                {
                    easyx_app.run(settings, std::move(players));
                });
            break;
        }
        case LauncherAction::HumanVsAiEasyXGame:
        {
            if (!ensureEasyX())
            {
                break;
            }
            GameSettings settings = promptSettings(true);
            PlayerInfo players = promptPlayers(true);
            settings.use_easyx = true;
            runGuiWithHiddenConsole(
                [&]()
                {
                    easyx_app.run(settings, std::move(players));
                });
            break;
        }
        case LauncherAction::HostLanGame:
        {
            if (!ensureEasyX())
            {
                break;
            }
            GameSettings settings = promptSettings(false);
            PlayerInfo players = promptPlayers(false);
            settings.use_easyx = true;
            settings.ai_enabled = false;
            const unsigned short port = promptPort();
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
                            room.accepts_spectators = true;
                            room.spectator_count = 0;
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
                SpectatorConnections preconnected_spectators = waitForLanPlayer(*network, port, settings, players, false);
                stop_advertising = true;
                if (advertiser.joinable())
                {
                    advertiser.join();
                }
                std::cout << "客户端已连接，正在启动 EasyX 主机视图...\n";
                runGuiWithHiddenConsole(
                    [&]()
                    {
                        easyx_app.runNetworkGame(
                            settings,
                            std::move(players),
                            std::move(network),
                            Side::Red,
                            std::move(preconnected_spectators));
                    });
            }
            catch (const std::exception& ex)
            {
                stop_advertising = true;
                if (advertiser.joinable())
                {
                    advertiser.join();
                }
                std::cout << "网络错误：" << ex.what() << "\n";
            }
            break;
        }
        case LauncherAction::JoinLanGame:
        {
            if (!ensureEasyX())
            {
                break;
            }
            const auto room = chooseLanRoom(true, false);
            const std::string address = room.has_value() ? room->address : promptAddress();
            const unsigned short port = room.has_value() ? room->port : promptPort();
            auto network = std::make_unique<NetworkSession>();
            try
            {
                network->join(address, port);
                network->sendLine(serializeConnectionRequest(ConnectionRole::Player));
                GameSettings settings;
                PlayerInfo players;
                Side first_turn = Side::Red;
                parseHandshake(network->receiveLine(), settings, players, first_turn);
                settings.ai_enabled = false;
                settings.use_easyx = true;
                std::cout << "已连接，正在启动 EasyX 客户端视图...\n";
                runGuiWithHiddenConsole(
                    [&]()
                    {
                        easyx_app.runNetworkGame(settings, std::move(players), std::move(network), Side::Black);
                    });
            }
            catch (const std::exception& ex)
            {
                std::cout << "网络错误：" << ex.what() << "\n";
            }
            break;
        }
        case LauncherAction::WatchLanGame:
            watchNetworkGame();
            break;
        case LauncherAction::ReconnectLanClient:
        {
            if (!ensureEasyX())
            {
                break;
            }
            const auto room = chooseLanRoom(true, false);
            const std::string address = room.has_value() ? room->address : promptAddress();
            const unsigned short port = room.has_value() ? room->port : promptPort();
            auto network = std::make_unique<NetworkSession>();
            try
            {
                network->join(address, port);
                network->sendLine(serializeConnectionRequest(ConnectionRole::Reconnect, Side::Black));
                GameSettings settings;
                PlayerInfo players;
                Side first_turn = Side::Red;
                parseHandshake(network->receiveLine(), settings, players, first_turn);
                settings.ai_enabled = false;
                settings.use_easyx = true;
                std::cout << "已重连，正在启动 EasyX 客户端视图...\n";
                runGuiWithHiddenConsole(
                    [&]()
                    {
                        easyx_app.runNetworkGame(settings, std::move(players), std::move(network), Side::Black);
                    });
            }
            catch (const std::exception& ex)
            {
                std::cout << "重连失败：" << ex.what() << "\n";
            }
            break;
        }
        case LauncherAction::OpenPgnReplay:
        {
            if (!ensureEasyX())
            {
                break;
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
                std::cout << "回放失败：" << ex.what() << "\n";
            }
            break;
        }
        case LauncherAction::ShowLeaderboard:
            showLeaderboard();
            break;
        case LauncherAction::RunSelfTests:
        {
            try
            {
                std::cout << tests::runAll() << "\n";
            }
            catch (const std::exception& ex)
            {
                std::cout << "自测失败：" << ex.what() << "\n";
            }
            break;
        }
        case LauncherAction::LocalDarkConsoleGame:
        {
            GameSettings settings = promptDarkSettings(false);
            PlayerInfo players = promptPlayers(false);
            settings.use_easyx = false;
            settings.ai_enabled = false;
            runDarkConsoleGame(settings, std::move(players));
            break;
        }
        case LauncherAction::LocalDarkEasyXGame:
        {
            if (!ensureEasyX())
            {
                break;
            }
            GameSettings settings = promptDarkSettings(false);
            PlayerInfo players = promptPlayers(false);
            settings.use_easyx = true;
            runGuiWithHiddenConsole(
                [&]()
                {
                    easyx_app.run(settings, std::move(players));
                });
            break;
        }
        case LauncherAction::HumanVsAiDarkEasyXGame:
        {
            if (!ensureEasyX())
            {
                break;
            }
            GameSettings settings = promptDarkSettings(true);
            PlayerInfo players = promptPlayers(true);
            settings.use_easyx = true;
            runGuiWithHiddenConsole(
                [&]()
                {
                    easyx_app.run(settings, std::move(players));
                });
            break;
        }
        case LauncherAction::HostDarkLanGame:
        {
            if (!ensureEasyX())
            {
                break;
            }
            GameSettings settings = promptDarkSettings(false);
            PlayerInfo players = promptPlayers(false);
            settings.use_easyx = true;
            settings.ai_enabled = false;
            settings.game_kind = GameKind::DarkChess;
            const unsigned short port = promptPort();
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
                            room.name = players.red_name + "'s Dark Chess room";
                            room.port = port;
                            room.accepts_player = true;
                            room.accepts_spectators = true;
                            room.spectator_count = 0;
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
                SpectatorConnections preconnected_spectators = waitForLanPlayer(*network, port, settings, players, true);
                stop_advertising = true;
                if (advertiser.joinable())
                {
                    advertiser.join();
                }
                std::cout << "客户端已连接，正在启动 EasyX 揭棋主机视图...\n";
                runGuiWithHiddenConsole(
                    [&]()
                    {
                        easyx_app.runNetworkGame(
                            settings,
                            std::move(players),
                            std::move(network),
                            Side::Red,
                            std::move(preconnected_spectators));
                    });
            }
            catch (const std::exception& ex)
            {
                stop_advertising = true;
                if (advertiser.joinable())
                {
                    advertiser.join();
                }
                std::cout << "网络错误：" << ex.what() << "\n";
            }
            break;
        }
        case LauncherAction::JoinDarkLanGame:
        {
            if (!ensureEasyX())
            {
                break;
            }
            const auto room = chooseLanRoom(true, false);
            const std::string address = room.has_value() ? room->address : promptAddress();
            const unsigned short port = room.has_value() ? room->port : promptPort();
            auto network = std::make_unique<NetworkSession>();
            try
            {
                network->join(address, port);
                network->sendLine(serializeConnectionRequest(ConnectionRole::Player));
                GameSettings settings;
                PlayerInfo players;
                Side first_turn = Side::Red;
                parseHandshake(network->receiveLine(), settings, players, first_turn);
                settings.game_kind = GameKind::DarkChess;
                settings.ai_enabled = false;
                settings.use_easyx = true;
                std::cout << "已连接，正在启动 EasyX 揭棋客户端视图...\n";
                runGuiWithHiddenConsole(
                    [&]()
                    {
                        easyx_app.runNetworkGame(settings, std::move(players), std::move(network), Side::Black);
                    });
            }
            catch (const std::exception& ex)
            {
                std::cout << "网络错误：" << ex.what() << "\n";
            }
            break;
        }
        case LauncherAction::Exit:
            return 0;
        default:
            break;
        }
    }
}

void ConsoleApp::configureConsole() const
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::cout << std::unitbuf;

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

void ConsoleApp::printMainMenu() const
{
    std::cout << "\n=== 中国象棋启动器 ===\n";
    std::cout << "请输入中文命令进入大类，也可输入括号内别名。\n";
    const auto& groups = mainConsoleMenuGroups();
    for (size_t index = 0; index < groups.size(); ++index)
    {
        const auto& group = groups[index];
        const auto& entry = group.entries.front();
        std::cout << "  " << (index + 1) << ". " << entry.label;
        if (!entry.aliases.empty())
        {
            std::cout << "  [" << entry.aliases.front() << "]";
        }
        std::cout << "\n";
    }
    std::cout << "命令> ";
}

void ConsoleApp::printSubMenu(const std::string& title, const std::vector<ConsoleMenuEntry>& entries) const
{
    std::cout << "\n=== " << title << " ===\n";
    for (size_t index = 0; index < entries.size(); ++index)
    {
        const auto& entry = entries[index];
        std::cout << "  " << (index + 1) << ". " << entry.label;
        if (!entry.aliases.empty())
        {
            std::cout << "  [" << entry.aliases.front() << "]";
        }
        std::cout << "\n";
    }
    std::cout << "命令> ";
}

LauncherAction ConsoleApp::promptMainMenu() const
{
    while (true)
    {
        printMainMenu();
        std::string input;
        std::getline(std::cin, input);
        const LauncherAction action = parseMenuSelection(input, mainMenuEntries());
        if (action != LauncherAction::Invalid)
        {
            return action;
        }
        std::cout << "未识别的命令，请输入菜单里的中文命令或别名。\n";
    }
}

LauncherAction ConsoleApp::promptSubMenu(const std::string& title, const std::vector<ConsoleMenuEntry>& entries) const
{
    while (true)
    {
        printSubMenu(title, entries);
        std::string input;
        std::getline(std::cin, input);
        const LauncherAction action = parseMenuSelection(input, entries);
        if (action != LauncherAction::Invalid)
        {
            return action;
        }
        std::cout << "这个子菜单没有该命令，请重新输入。\n";
    }
}

void ConsoleApp::showHelp() const
{
    std::cout << "局内命令：\n";
    std::cout << "  a0 a1       按坐标走子\n";
    std::cout << "  flip a0     揭棋模式翻开暗子\n";
    std::cout << "  a0          查看该位置可走位置\n";
    std::cout << "  中文棋谱    标准棋盘支持中文行棋输入\n";
    std::cout << "  undo        悔一步棋\n";
    std::cout << "  save 名称   保存到 data/saves；名称为空时使用双方玩家名\n";
    std::cout << "  load 名称   读取存档；不带名称时列出存档供选择\n";
    std::cout << "  hint        请求搜索引擎给出建议\n";
    std::cout << "  replay      回放当前棋局历史\n";
    std::cout << "  resign      认输\n";
    std::cout << "  tests       运行引擎自测\n";
    std::cout << "  exit        退出当前棋局\n";
}

void ConsoleApp::printBoard(const GameSession& session, const std::vector<Move>* highlight_moves) const
{
    const auto& board = session.board();
    std::cout << "    ";
    for (char file : board.config().coordinate_files)
    {
        std::cout << rightAlignConsoleCell(std::string(1, file), 3);
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
                std::cout << rightAlignConsoleCell(is_highlight ? "..*" : "..", 3);
                continue;
            }

            std::string token;
            token += piece->side() == Side::Red ? 'r' : 'b';
            token += piece->symbol();
            if (is_highlight)
            {
                token += '*';
            }

            std::cout << rightAlignConsoleCell(colorize(token, piece->side()), 3);
        }
        std::cout << '\n';
    }

    std::cout << "模式: " << (session.boardMode() == BoardMode::Expanded11x10 ? "扩展11x10" : "标准9x10")
              << " | 当前: " << session.currentPlayerName()
              << " (" << toString(session.currentSide()) << ")"
              << " | 红方 " << session.remainingSeconds(Side::Red) << "s"
              << " | 黑方 " << session.remainingSeconds(Side::Black) << "s\n";
}

void ConsoleApp::printDarkBoard(const DarkGameSession& session, const std::vector<DarkAction>* highlight_actions) const
{
    std::cout << "    ";
    for (int col = 0; col < DarkBoard::kCols; ++col)
    {
        std::cout << rightAlignConsoleCell(std::string(1, static_cast<char>('a' + col)), 4);
    }
    std::cout << "\n";

    for (int row = 0; row < DarkBoard::kRows; ++row)
    {
        std::cout << std::setw(3) << row << ' ';
        for (int col = 0; col < DarkBoard::kCols; ++col)
        {
            const Position<int> position{ row, col };
            bool is_highlight = false;
            if (highlight_actions != nullptr)
            {
                for (const auto& action : *highlight_actions)
                {
                    if (action.to == position)
                    {
                        is_highlight = true;
                        break;
                    }
                }
            }

            std::string token = "..";
            if (session.board().isOccupied(position))
            {
                const auto piece = session.board().pieceAt(position);
                if (!piece.has_value() || !piece->is_open)
                {
                    token = "??";
                }
                else
                {
                    token = darkPieceToken(*piece);
                }
            }
            if (is_highlight)
            {
                token += '*';
            }

            const auto piece = session.board().pieceAt(position);
            if (piece.has_value() && piece->is_open)
            {
                std::cout << rightAlignConsoleCell(colorize(token, piece->side), 4);
            }
            else
            {
                std::cout << rightAlignConsoleCell(token, 4);
            }
        }
        std::cout << '\n';
    }

    const auto p1_color = session.colorForSeat(DarkSeat::Player1);
    const auto p2_color = session.colorForSeat(DarkSeat::Player2);
    std::cout << "模式: 揭棋"
              << " | 当前: " << session.currentPlayerName()
              << " (" << toString(session.currentSeat()) << ")"
              << " | 玩家1颜色: " << (p1_color.has_value() ? toString(*p1_color) : "未知")
              << " | 玩家2颜色: " << (p2_color.has_value() ? toString(*p2_color) : "未知")
              << " | P1 " << session.remainingSeconds(DarkSeat::Player1) << "s"
              << " | P2 " << session.remainingSeconds(DarkSeat::Player2) << "s\n";
}

BoardMode ConsoleApp::promptBoardMode() const
{
    std::cout << "棋盘模式：输入“标准”或“扩展”（默认标准）：";
    std::string choice;
    std::getline(std::cin, choice);
    choice = normalizeMenuInput(choice);
    return choice == "扩展" || choice == "kuozhan" || choice == "expanded" || choice == "2"
        ? BoardMode::Expanded11x10
        : BoardMode::Standard9x10;
}

GameSettings ConsoleApp::promptSettings(const bool ai_enabled) const
{
    GameSettings settings;
    settings.board_mode = promptBoardMode();
    settings.ai_enabled = ai_enabled;
    settings.ai_side = Side::Black;

    settings.move_time_limit_seconds = promptInteger("每步限时秒数", 60, 5, 24 * 60 * 60);

    std::string line;
    std::cout << "允许悔棋？（y/n，默认 y）：";
    std::getline(std::cin, line);
    if (!line.empty())
    {
        settings.allow_undo = line != "n" && line != "N";
    }

    std::cout << "显示可走提示？（y/n，默认 y）：";
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

GameSettings ConsoleApp::promptDarkSettings(const bool ai_enabled) const
{
    GameSettings settings;
    settings.game_kind = GameKind::DarkChess;
    settings.ai_enabled = ai_enabled;
    settings.dark_ai_seat = DarkSeat::Player2;

    settings.move_time_limit_seconds = promptInteger("揭棋每步限时秒数", 60, 5, 24 * 60 * 60);

    std::string line;

    std::cout << "允许悔棋？（y/n，默认 y）：";
    std::getline(std::cin, line);
    if (!line.empty())
    {
        settings.allow_undo = line != "n" && line != "N";
    }

    std::cout << "显示揭棋可行动作提示？（y/n，默认 y）：";
    std::getline(std::cin, line);
    if (!line.empty())
    {
        settings.show_legal_moves = line != "n" && line != "N";
    }

    return settings;
}

PlayerInfo ConsoleApp::promptPlayers(const bool ai_enabled) const
{
    PlayerInfo players;
    std::cout << "红方名称（默认 Red）：";
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
        std::cout << "黑方名称（默认 Black）：";
        std::getline(std::cin, players.black_name);
        if (players.black_name.empty())
        {
            players.black_name = "Black";
        }
    }
    return players;
}

int ConsoleApp::promptInteger(
    const std::string& prompt,
    const int default_value,
    const int min_value,
    const int max_value) const
{
    std::cout << prompt << "（默认 " << default_value << "）：";
    std::string text;
    std::getline(std::cin, text);
    text = trimInput(text);
    if (text.empty())
    {
        return default_value;
    }

    try
    {
        size_t consumed = 0;
        int value = std::stoi(text, &consumed);
        if (consumed != text.size())
        {
            throw std::invalid_argument("extra characters");
        }
        if (value < min_value)
        {
            std::cout << "数值过小，已使用下限 " << min_value << "。\n";
            return min_value;
        }
        if (value > max_value)
        {
            std::cout << "数值过大，已使用上限 " << max_value << "。\n";
            return max_value;
        }
        return value;
    }
    catch (const std::exception&)
    {
        std::cout << "输入不是有效数字，已使用默认值 " << default_value << "。\n";
        return default_value;
    }
}

unsigned short ConsoleApp::promptPort(const unsigned short default_port) const
{
    return static_cast<unsigned short>(promptInteger("端口", default_port, 1, 65535));
}

std::string ConsoleApp::promptAddress() const
{
    std::cout << "主机 IPv4 地址：";
    std::string address;
    std::getline(std::cin, address);
    return address;
}

std::optional<std::string> ConsoleApp::chooseSaveFile() const
{
    std::vector<std::filesystem::path> saves;
    try
    {
        saves = storage::listSaveFiles();
    }
    catch (const std::exception& ex)
    {
        std::cout << "读取存档列表失败：" << ex.what() << "\n";
        return std::nullopt;
    }

    if (saves.empty())
    {
        std::cout << "未在 " << storage::saveDirectory().string() << " 找到存档。\n";
        return std::nullopt;
    }

    std::cout << "存档列表：\n";
    for (size_t i = 0; i < saves.size(); ++i)
    {
        std::cout << "  " << (i + 1) << ". " << saves[i].filename().string() << "\n";
    }
    std::cout << "选择存档序号（留空取消）：";

    std::string choice;
    std::getline(std::cin, choice);
    choice = trimInput(choice);
    if (choice.empty())
    {
        std::cout << "已取消读取存档。\n";
        return std::nullopt;
    }

    try
    {
        size_t consumed = 0;
        const int selected = std::stoi(choice, &consumed);
        if (consumed == choice.size() && selected >= 1 && selected <= static_cast<int>(saves.size()))
        {
            return saves[static_cast<size_t>(selected - 1)].filename().string();
        }
    }
    catch (const std::exception&)
    {
    }

    std::cout << "存档选择无效。\n";
    return std::nullopt;
}

std::optional<LanRoom> ConsoleApp::chooseLanRoom(const bool require_player_slot, const bool require_spectator_slot) const
{
    std::cout << "正在搜索局域网房间...\n";
    std::vector<LanRoom> rooms;
    try
    {
        rooms = discoverLanRooms();
    }
    catch (const std::exception& ex)
    {
        std::cout << "房间搜索失败：" << ex.what() << "\n";
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
        std::cout << "没有找到匹配的局域网房间。是否手动填写地址？（y/n，默认 y）：";
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

    std::cout << "发现的房间：\n";
    for (size_t index = 0; index < rooms.size(); ++index)
    {
        const auto& room = rooms[index];
        std::cout << "  " << (index + 1) << ". " << room.name
                  << " @ " << room.address << ':' << room.port
                  << " | 观战人数=" << room.spectator_count
                  << " | 玩家位=" << (room.accepts_player ? "开放" : "关闭")
                  << " | 观战=" << (room.accepts_spectators ? "开放" : "关闭")
                  << "\n";
    }
    std::cout << "输入房间序号，或输入 0 手动填写地址：";

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

    std::cout << "房间选择无效。\n";
    return std::nullopt;
}

SpectatorConnections ConsoleApp::waitForLanPlayer(
    NetworkSession& network,
    const unsigned short port,
    const GameSettings& settings,
    const PlayerInfo& players,
    const bool dark_chess) const
{
    SpectatorConnections spectators;
    network.listen(port);

    std::cout << "房间已创建，正在等待玩家加入；观战者现在也可以连接。\n";
    while (!network.isConnected())
    {
        auto accepted = network.acceptConnection(250);
        if (!accepted.has_value())
        {
            continue;
        }

        try
        {
            const std::string request_line = NetworkSession::receiveLine(*accepted);
            const auto request = parseConnectionRequest(request_line);
            if (!request.has_value())
            {
                NetworkSession::sendLine(*accepted, "ERROR|请先发送 JOIN_REQ、WATCH_REQ 或 REJOIN_REQ。");
                NetworkSession::closeConnection(*accepted);
                continue;
            }

            if (request->role == ConnectionRole::Player)
            {
                network.replaceConnection(*accepted);
                network.sendLine(serializeHandshake(settings, players, Side::Red));
                std::cout << "玩家已加入，准备进入对局。\n";
                break;
            }

            if (request->role == ConnectionRole::Spectator)
            {
                NetworkSession::sendLine(
                    *accepted,
                    std::string("WAITING|") + escapeProtocolField(dark_chess ? "等待揭棋玩家加入" : "等待象棋玩家加入"));
                spectators.push_back(std::move(*accepted));
                std::cout << "观战者已进入等待队列，当前观战人数：" << spectators.size() << "\n";
                continue;
            }

            NetworkSession::sendLine(*accepted, "ERROR|对局尚未开始，暂不接受重连。");
            NetworkSession::closeConnection(*accepted);
        }
        catch (const std::exception& ex)
        {
            std::cout << "连接处理失败：" << ex.what() << "\n";
            NetworkSession::closeConnection(*accepted);
        }
    }

    return spectators;
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
            catch (const std::exception& ex)
            {
                std::cout << "警告：更新排行榜失败：" << ex.what() << "\n";
            }
            std::cout << "按 Enter 返回主菜单。";
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
                std::cout << "AI 落子：" << move.notation << "\n";
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
                    std::cout << coordText(*command.position, session.board().config()) << " 没有合法走法。\n";
                }
                else
                {
                    printBoard(session, &moves);
                    std::cout << "可走位置：";
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
                    std::cout << "当前不能悔棋。\n";
                }
                else if (session.settings().ai_enabled && !session.history().empty() && session.currentSide() == session.settings().ai_side)
                {
                    session.undoLastPly();
                }
                break;
            case CommandType::Save:
            {
                const auto path = storage::saveGame(session, command.argument);
                const auto replay_path = storage::saveReplay(session, command.argument.empty() ? path.stem().string() : command.argument);
                std::cout << "已保存到 " << path.string() << "\n";
                std::cout << "PGN 已导出到 " << replay_path.string() << "\n";
                break;
            }
            case CommandType::Load:
            {
                std::string save_name = command.argument;
                if (save_name.empty())
                {
                    const auto selected = chooseSaveFile();
                    if (!selected.has_value())
                    {
                        break;
                    }
                    save_name = *selected;
                }
                session = storage::loadGame(save_name);
                search = SearchEngine(session.settings().ai_depth);
                std::cout << "已读取 " << save_name << "。\n";
                break;
            }
            case CommandType::Resign:
                session.resign(session.currentSide());
                break;
            case CommandType::Hint:
                if (const auto best = search.chooseBestMove(session); best.has_value())
                {
                    std::cout << "建议：" << parser_.moveToCoordinateText(*best, session.board().config()) << "\n";
                }
                else
                {
                    std::cout << "当前没有可用合法走法。\n";
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
                std::cout << "未识别的局内命令。\n";
                break;
            }
        }
        catch (const std::exception& ex)
        {
            std::cout << "错误：" << ex.what() << "\n";
        }
    }
}

void ConsoleApp::runDarkConsoleGame(GameSettings settings, PlayerInfo players)
{
    DarkGameSession session(settings, std::move(players));
    DarkSearchEngine search;
    bool leaderboard_recorded = false;

    while (true)
    {
        if (!session.gameOver())
        {
            session.tickClock();
        }

        printDarkBoard(session);
        if (session.gameOver())
        {
            std::cout << session.resultText() << "\n";
            if (!leaderboard_recorded)
            {
                leaderboard_recorded = true;
                try
                {
                    storage::appendDarkLeaderboard(session);
                }
                catch (const std::exception& ex)
                {
                    std::cout << "警告：更新排行榜失败：" << ex.what() << "\n";
                }
            }
            std::cout << "按 Enter 返回主菜单。";
            std::string dummy;
            std::getline(std::cin, dummy);
            return;
        }

        if (session.settings().ai_enabled && session.currentSeat() == session.settings().dark_ai_seat)
        {
            if (const auto best = search.chooseAction(session); best.has_value())
            {
                DarkAction action = *best;
                action.notation = darkActionToText(action);
                session.submitAction(action);
                std::cout << "AI 行动：" << action.notation << "\n";
                continue;
            }
        }

        std::cout << session.currentPlayerName() << " > ";
        std::string input;
        std::getline(std::cin, input);
        const std::string lowered = [&]()
        {
            std::string value = input;
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char ch)
                {
                    return static_cast<char>(std::tolower(ch));
                });
            return value;
        }();

        try
        {
            if (lowered == "exit" || lowered == "quit")
            {
                return;
            }
            if (lowered == "help")
            {
                showHelp();
                continue;
            }
            if (lowered == "undo" || lowered == "u")
            {
                if (!session.undoLastPly())
                {
                    std::cout << "当前揭棋局面不能悔棋。\n";
                }
                continue;
            }
            if (lowered == "resign")
            {
                session.resign(session.currentSeat());
                continue;
            }
            if (lowered == "hint")
            {
                if (const auto best = search.chooseAction(session); best.has_value())
                {
                    std::cout << "建议：" << darkActionToText(*best) << "\n";
                }
                else
                {
                    std::cout << "当前没有可用合法行动。\n";
                }
                continue;
            }
            if (lowered.rfind("save", 0) == 0)
            {
                std::string name;
                if (input.size() > 4)
                {
                    name = input.substr(5);
                }
                const auto path = storage::saveDarkGame(session, name);
                const auto replay_path = storage::saveDarkReplay(session, (name.empty() ? path.stem().string() : name) + "_replay");
                std::cout << "已保存到 " << path.string() << "\n";
                std::cout << "CDC 回放已导出到 " << replay_path.string() << "\n";
                continue;
            }
            if (lowered.rfind("load", 0) == 0)
            {
                std::string name;
                if (input.size() > 4)
                {
                    name = input.substr(5);
                }
                if (name.empty())
                {
                    const auto selected = chooseSaveFile();
                    if (!selected.has_value())
                    {
                        continue;
                    }
                    name = *selected;
                }
                session = storage::loadDarkGame(name);
                std::cout << "已读取 " << name << "。\n";
                continue;
            }
            if (lowered == "replay")
            {
                DarkGameSession replay = makeDarkReplayInitialSession(session.settings(), session.players(), session.initialPrivateGridString());
                std::cout << "揭棋回放开始：\n";
                printDarkBoard(replay);
                for (const auto& action : session.history())
                {
                    std::cout << darkActionToText(action) << "\n";
                    replay.submitAction(action);
                    printDarkBoard(replay);
                }
                continue;
            }
            if (input.size() == 2)
            {
                const auto position = parseDarkCoord(input);
                if (position.has_value())
                {
                    const auto actions = session.legalActionsFrom(*position);
                    if (actions.empty())
                    {
                        std::cout << input << " 没有合法揭棋行动。\n";
                    }
                    else
                    {
                        printDarkBoard(session, &actions);
                        std::cout << "可用行动：";
                        for (const auto& action : actions)
                        {
                            std::cout << ' ' << darkActionToText(action);
                        }
                        std::cout << "\n";
                    }
                    continue;
                }
            }

            DarkAction action = parseDarkActionText(input, session);
            action.notation = darkActionToText(action);
            session.submitAction(action);
        }
        catch (const std::exception& ex)
        {
            std::cout << "错误：" << ex.what() << "\n";
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
        network.sendLine(serializeConnectionRequest(ConnectionRole::Spectator));

        GameSettings settings;
        PlayerInfo players;
        Side first_turn = Side::Red;

        while (true)
        {
            const std::string first_line = network.receiveLine();
            const auto first_fields = splitProtocol(first_line);
            if (!first_fields.empty() && first_fields[0] == "WAITING" && first_fields.size() >= 2)
            {
                std::cout << "观战已连接：" << unescapeProtocolField(first_fields[1]) << "\n";
                continue;
            }

            parseHandshake(first_line, settings, players, first_turn);
            break;
        }

        std::cout << "正在观战 " << room->name << "（" << room->address << ':' << room->port << "）。\n";
        std::cout << "当前视图只读；关闭窗口或按 Ctrl+C 可停止观战。\n";

        EasyXApp easyx_app;
        if (easyx_app.isAvailable())
        {
            std::cout << "Starting EasyX spectator view...\n";
            auto spectator_network = std::make_unique<NetworkSession>(std::move(network));
            easyx_app.runSpectatorGame(settings, std::move(players), std::move(spectator_network), first_turn);
            return;
        }

        // Keep the console renderer as a fallback for builds or machines without EasyX support.
        while (true)
        {
            const auto fields = splitProtocol(network.receiveLine());
            if (fields.empty())
            {
                continue;
            }

            if (settings.game_kind == GameKind::DarkChess && fields[0] == "DARK_STATE" && fields.size() >= 2)
            {
                const DarkGameSession session = DarkGameSession::deserialize(unescapeProtocolField(fields[1]));
                printDarkBoard(session);
                if (session.gameOver())
                {
                    std::cout << session.resultText() << "\n";
                }
            }
            else if (fields[0] == "STATE" && fields.size() >= 2)
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
                std::cout << "观战错误：" << fields[1] << "\n";
                return;
            }
        }
    }
    catch (const std::exception& ex)
    {
        std::cout << "观战失败：" << ex.what() << "\n";
    }
}

void ConsoleApp::replayCurrentGame(const GameSession& session) const
{
    GameSettings settings = session.settings();
    settings.ai_enabled = false;
    GameSession replay(settings, session.players());

    std::cout << "从开局开始回放：\n";
    printBoard(replay);
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    for (const auto& move : session.history())
    {
        replay.submitMove(move);
        printBoard(replay);
        std::cout << "走子：" << (!move.notation.empty() ? move.notation : coordText(move.from, replay.board().config()) + " " + coordText(move.to, replay.board().config())) << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
    }
}

void ConsoleApp::showLeaderboard() const
{
    const auto standings = storage::readLeaderboardStandings();
    if (standings.empty())
    {
        std::cout << "排行榜暂无记录。\n";
        return;
    }

    std::cout << "=== 按胜率排序的排行榜 ===\n";
    std::cout << std::left << std::setw(4) << "#"
              << std::setw(20) << "玩家"
              << std::right << std::setw(7) << "局数"
              << std::setw(7) << "胜"
              << std::setw(7) << "负"
              << std::setw(7) << "和"
              << std::setw(10) << "胜率"
              << std::setw(10) << "均步"
              << std::setw(10) << "均时" << "\n";

    for (size_t i = 0; i < standings.size(); ++i)
    {
        const auto& standing = standings[i];
        const double average_time = standing.games > 0
            ? static_cast<double>(standing.total_duration_seconds) / static_cast<double>(standing.games)
            : 0.0;
        std::cout << std::left << std::setw(4) << (i + 1)
                  << std::setw(20) << standing.player_name.substr(0, 19)
                  << std::right << std::setw(7) << standing.games
                  << std::setw(7) << standing.wins
                  << std::setw(7) << standing.losses
                  << std::setw(7) << standing.draws
                  << std::setw(9) << std::fixed << std::setprecision(1) << standing.win_rate << "%"
                  << std::setw(10) << std::fixed << std::setprecision(1) << standing.average_moves
                  << std::setw(9) << std::fixed << std::setprecision(1) << average_time << "s\n";
    }
}

} // namespace xiangqi
