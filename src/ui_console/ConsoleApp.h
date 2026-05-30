#pragma once

#include "ai/SearchEngine.h"
#include "app/MoveParser.h"
#include "net/NetworkSession.h"

#include <optional>
#include <string>

namespace xiangqi
{

class ConsoleApp
{
public:
    int run();

private:
    void configureConsole() const;
    void printMenu() const;
    void showHelp() const;
    void printBoard(const GameSession& session, const std::vector<Move>* highlight_moves = nullptr) const;
    BoardMode promptBoardMode() const;
    GameSettings promptSettings(bool ai_enabled) const;
    PlayerInfo promptPlayers(bool ai_enabled) const;
    unsigned short promptPort(unsigned short default_port = 9527) const;
    std::string promptAddress() const;
    std::optional<LanRoom> chooseLanRoom(bool require_player_slot, bool require_spectator_slot) const;
    void runConsoleGame(GameSettings settings, PlayerInfo players);
    void runHostedNetworkGame(GameSettings settings, PlayerInfo players);
    void runJoinedNetworkGame();
    void watchNetworkGame();
    void replayCurrentGame(const GameSession& session) const;
    void showLeaderboard() const;

    MoveParser parser_{};
};

} // namespace xiangqi
