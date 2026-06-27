#pragma once

#include "ai/SearchEngine.h"
#include "app/MoveParser.h"
#include "darkchess/DarkChess.h"
#include "net/NetworkSession.h"
#include "ui_console/ConsoleMenu.h"

#include <optional>
#include <string>
#include <vector>

namespace xiangqi
{

class ConsoleApp
{
public:
    int run();

private:
    void configureConsole() const;
    LauncherAction promptMainMenu() const;
    LauncherAction promptSubMenu(const std::string& title, const std::vector<ConsoleMenuEntry>& entries) const;
    void printMainMenu() const;
    void printSubMenu(const std::string& title, const std::vector<ConsoleMenuEntry>& entries) const;
    void showHelp() const;
    void printBoard(const GameSession& session, const std::vector<Move>* highlight_moves = nullptr) const;
    void printDarkBoard(const DarkGameSession& session, const std::vector<DarkAction>* highlight_actions = nullptr) const;
    BoardMode promptBoardMode() const;
    GameSettings promptSettings(bool ai_enabled) const;
    GameSettings promptDarkSettings(bool ai_enabled) const;
    PlayerInfo promptPlayers(bool ai_enabled) const;
    int promptInteger(const std::string& prompt, int default_value, int min_value, int max_value) const;
    unsigned short promptPort(unsigned short default_port = 9527) const;
    std::string promptAddress() const;
    std::optional<std::string> chooseSaveFile() const;
    std::optional<LanRoom> chooseLanRoom(bool require_player_slot, bool require_spectator_slot) const;
    SpectatorConnections waitForLanPlayer(
        NetworkSession& network,
        unsigned short port,
        const GameSettings& settings,
        const PlayerInfo& players,
        bool dark_chess) const;
    void runConsoleGame(GameSettings settings, PlayerInfo players);
    void runDarkConsoleGame(GameSettings settings, PlayerInfo players);
    void watchNetworkGame();
    void replayCurrentGame(const GameSession& session) const;
    void showLeaderboard() const;

    MoveParser parser_{};
};

} // namespace xiangqi
