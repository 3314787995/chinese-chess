#pragma once

#include "app/GameSession.h"
#include "darkchess/DarkChess.h"

#include <filesystem>
#include <vector>

namespace xiangqi::storage
{

struct ReplayRecord
{
    GameSettings settings{};
    PlayerInfo players{};
    std::vector<Move> moves;
    GameResult result{ GameResult::Ongoing };
    Side result_side{ Side::Red };
    std::filesystem::path source_path;
};

struct DarkReplayRecord
{
    GameSettings settings{};
    PlayerInfo players{};
    std::string initial_private_grid;
    std::vector<DarkAction> actions;
    GameResult result{ GameResult::Ongoing };
    std::filesystem::path source_path;
};

struct LeaderboardStanding
{
    std::string player_name;
    int games{ 0 };
    int wins{ 0 };
    int losses{ 0 };
    int draws{ 0 };
    int total_moves{ 0 };
    int total_duration_seconds{ 0 };
    double win_rate{ 0.0 };
    double average_moves{ 0.0 };
};

std::filesystem::path rootDirectory();
std::filesystem::path saveDirectory();
std::filesystem::path replayDirectory();
std::filesystem::path leaderboardPath();

void ensureDirectories();
std::filesystem::path saveGame(const GameSession& session, const std::string& name_hint);
GameSession loadGame(const std::string& name_or_path);
std::vector<std::filesystem::path> listSaveFiles();

std::filesystem::path saveDarkGame(const DarkGameSession& session, const std::string& name_hint);
DarkGameSession loadDarkGame(const std::string& name_or_path);

std::filesystem::path saveReplay(const GameSession& session, const std::string& name_hint);
ReplayRecord loadReplay(const std::string& name_or_path);
std::filesystem::path saveDarkReplay(const DarkGameSession& session, const std::string& name_hint);
DarkReplayRecord loadDarkReplay(const std::string& name_or_path);
std::vector<std::filesystem::path> listReplayFiles();
std::vector<std::string> loadReplayLines(const std::filesystem::path& path);

void appendLeaderboard(const GameSession& session);
void appendDarkLeaderboard(const DarkGameSession& session);
std::vector<std::string> readLeaderboardLines();
std::vector<LeaderboardStanding> readLeaderboardStandings();
std::vector<std::string> formatLeaderboardTable(const std::vector<LeaderboardStanding>& standings, size_t max_rows);
std::string sanitizeName(std::string value);

} // namespace xiangqi::storage
