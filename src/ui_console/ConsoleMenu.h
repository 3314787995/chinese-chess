#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace xiangqi
{

enum class LauncherAction
{
    Invalid,
    Back,
    Exit,
    OpenPlayMenu,
    OpenNetworkMenu,
    OpenWatchReplayMenu,
    OpenToolsMenu,
    LocalConsoleGame,
    LocalEasyXGame,
    HumanVsAiEasyXGame,
    LocalDarkConsoleGame,
    LocalDarkEasyXGame,
    HumanVsAiDarkEasyXGame,
    HostLanGame,
    JoinLanGame,
    ReconnectLanClient,
    HostDarkLanGame,
    JoinDarkLanGame,
    WatchLanGame,
    OpenPgnReplay,
    ShowLeaderboard,
    RunSelfTests,
};

struct ConsoleMenuEntry
{
    LauncherAction action{ LauncherAction::Invalid };
    std::string label;
    std::vector<std::string> aliases;
};

struct ConsoleMenuGroup
{
    std::string title;
    std::vector<ConsoleMenuEntry> entries;
};

std::string normalizeMenuInput(std::string_view input);
LauncherAction parseLauncherAction(std::string_view input);
LauncherAction parseMenuSelection(std::string_view input, const std::vector<ConsoleMenuEntry>& entries);
const std::vector<ConsoleMenuGroup>& mainConsoleMenuGroups();
const std::vector<ConsoleMenuEntry>& playMenuEntries();
const std::vector<ConsoleMenuEntry>& networkMenuEntries();
const std::vector<ConsoleMenuEntry>& watchReplayMenuEntries();
const std::vector<ConsoleMenuEntry>& toolsMenuEntries();

} // namespace xiangqi
