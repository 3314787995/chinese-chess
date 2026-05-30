#pragma once

#include "app/GameSession.h"

#include <filesystem>

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

std::filesystem::path rootDirectory();
std::filesystem::path saveDirectory();
std::filesystem::path replayDirectory();
std::filesystem::path leaderboardPath();

void ensureDirectories();
std::filesystem::path saveGame(const GameSession& session, const std::string& name_hint);
GameSession loadGame(const std::string& name_or_path);
std::vector<std::filesystem::path> listSaveFiles();

std::filesystem::path saveReplay(const GameSession& session, const std::string& name_hint);
ReplayRecord loadReplay(const std::string& name_or_path);
std::vector<std::filesystem::path> listReplayFiles();
std::vector<std::string> loadReplayLines(const std::filesystem::path& path);

void appendLeaderboard(const GameSession& session);
std::vector<std::string> readLeaderboardLines();
std::string sanitizeName(std::string value);

} // namespace xiangqi::storage
