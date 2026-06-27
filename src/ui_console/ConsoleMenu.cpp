#include "ui_console/ConsoleMenu.h"

#include <algorithm>
#include <cctype>

namespace xiangqi
{

namespace
{

std::string trimAscii(std::string_view input)
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
    for (char& ch : value)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool matchesEntry(const ConsoleMenuEntry& entry, const std::string& normalized)
{
    if (normalizeMenuInput(entry.label) == normalized)
    {
        return true;
    }
    return std::any_of(
        entry.aliases.begin(),
        entry.aliases.end(),
        [&](const std::string& alias)
        {
            return normalizeMenuInput(alias) == normalized;
        });
}

LauncherAction actionForMenuIndex(const std::string& normalized, const std::vector<ConsoleMenuEntry>& entries)
{
    // Numeric choices are intentionally scoped to the menu currently on screen.
    // The global launcher parser keeps bare numbers invalid so different pages can reuse 1, 2, 3 safely.
    if (normalized.empty() ||
        !std::all_of(normalized.begin(), normalized.end(), [](const unsigned char ch)
        {
            return std::isdigit(ch) != 0;
        }))
    {
        return LauncherAction::Invalid;
    }

    size_t index = 0;
    for (const char ch : normalized)
    {
        index = index * 10 + static_cast<size_t>(ch - '0');
        if (index > entries.size())
        {
            return LauncherAction::Invalid;
        }
    }

    if (index == 0 || index > entries.size())
    {
        return LauncherAction::Invalid;
    }
    return entries[index - 1].action;
}

const std::vector<ConsoleMenuEntry> kPlayEntries{
    { LauncherAction::LocalConsoleGame, "本地控制台象棋", { "象棋控制台", "console", "xiangqi-console" } },
    { LauncherAction::LocalEasyXGame, "本地双人象棋", { "象棋双人", "easyx", "local" } },
    { LauncherAction::HumanVsAiEasyXGame, "人机象棋", { "人机", "ai", "renji" } },
    { LauncherAction::LocalDarkConsoleGame, "本地控制台揭棋", { "揭棋控制台", "dark-console" } },
    { LauncherAction::LocalDarkEasyXGame, "本地双人揭棋", { "揭棋双人", "dark-local" } },
    { LauncherAction::HumanVsAiDarkEasyXGame, "人机揭棋", { "揭棋人机", "dark-ai" } },
    { LauncherAction::Back, "返回", { "back", "fanhui" } },
};

const std::vector<ConsoleMenuEntry> kNetworkEntries{
    { LauncherAction::HostLanGame, "象棋联机主机", { "建房", "host", "xiangqi-host" } },
    { LauncherAction::JoinLanGame, "加入象棋房间", { "加入", "join", "xiangqi-join" } },
    { LauncherAction::ReconnectLanClient, "重连象棋房间", { "重连", "reconnect" } },
    { LauncherAction::HostDarkLanGame, "揭棋联机主机", { "揭棋建房", "dark-host" } },
    { LauncherAction::JoinDarkLanGame, "加入揭棋房间", { "揭棋加入", "dark-join" } },
    { LauncherAction::Back, "返回", { "back", "fanhui" } },
};

const std::vector<ConsoleMenuEntry> kWatchReplayEntries{
    { LauncherAction::WatchLanGame, "观战局域网对局", { "观战", "guanzhan", "watch" } },
    { LauncherAction::OpenPgnReplay, "打开棋谱回放", { "回放", "replay", "pgn" } },
    { LauncherAction::Back, "返回", { "back", "fanhui" } },
};

const std::vector<ConsoleMenuEntry> kToolsEntries{
    { LauncherAction::ShowLeaderboard, "查看排行榜", { "排行", "leaderboard", "rank" } },
    { LauncherAction::RunSelfTests, "运行自测", { "测试", "tests", "selftest" } },
    { LauncherAction::Back, "返回", { "back", "fanhui" } },
};

const std::vector<ConsoleMenuGroup> kMainGroups{
    { "开始对局", { { LauncherAction::OpenPlayMenu, "开始对局", { "kaishi", "play" } } } },
    { "联机大厅", { { LauncherAction::OpenNetworkMenu, "联机大厅", { "lianji", "network", "lan" } } } },
    { "观战回放", { { LauncherAction::OpenWatchReplayMenu, "观战回放", { "watch-menu", "replay-menu", "guanzhanhuifang" } } } },
    { "排行测试", { { LauncherAction::OpenToolsMenu, "排行测试", { "gongju", "tools" } } } },
    { "退出", { { LauncherAction::Exit, "退出", { "q", "quit", "exit" } } } },
};

void collectEntries(std::vector<ConsoleMenuEntry>& entries)
{
    entries.insert(entries.end(), kPlayEntries.begin(), kPlayEntries.end());
    entries.insert(entries.end(), kNetworkEntries.begin(), kNetworkEntries.end());
    entries.insert(entries.end(), kWatchReplayEntries.begin(), kWatchReplayEntries.end());
    entries.insert(entries.end(), kToolsEntries.begin(), kToolsEntries.end());
    for (const auto& group : kMainGroups)
    {
        entries.insert(entries.end(), group.entries.begin(), group.entries.end());
    }
}

} // namespace

std::string normalizeMenuInput(std::string_view input)
{
    return lowerAscii(trimAscii(input));
}

LauncherAction parseLauncherAction(std::string_view input)
{
    const std::string normalized = normalizeMenuInput(input);
    if (normalized.empty())
    {
        return LauncherAction::Invalid;
    }

    static const std::vector<ConsoleMenuEntry> all_entries = []()
    {
        std::vector<ConsoleMenuEntry> entries;
        collectEntries(entries);
        return entries;
    }();

    const auto found = std::find_if(
        all_entries.begin(),
        all_entries.end(),
        [&](const ConsoleMenuEntry& entry)
        {
            return matchesEntry(entry, normalized);
        });
    return found == all_entries.end() ? LauncherAction::Invalid : found->action;
}

LauncherAction parseMenuSelection(std::string_view input, const std::vector<ConsoleMenuEntry>& entries)
{
    const std::string normalized = normalizeMenuInput(input);
    if (normalized.empty())
    {
        return LauncherAction::Invalid;
    }

    const LauncherAction numbered_action = actionForMenuIndex(normalized, entries);
    if (numbered_action != LauncherAction::Invalid)
    {
        return numbered_action;
    }

    const auto found = std::find_if(
        entries.begin(),
        entries.end(),
        [&](const ConsoleMenuEntry& entry)
        {
            return matchesEntry(entry, normalized);
        });
    return found == entries.end() ? LauncherAction::Invalid : found->action;
}

const std::vector<ConsoleMenuGroup>& mainConsoleMenuGroups()
{
    return kMainGroups;
}

const std::vector<ConsoleMenuEntry>& playMenuEntries()
{
    return kPlayEntries;
}

const std::vector<ConsoleMenuEntry>& networkMenuEntries()
{
    return kNetworkEntries;
}

const std::vector<ConsoleMenuEntry>& watchReplayMenuEntries()
{
    return kWatchReplayEntries;
}

const std::vector<ConsoleMenuEntry>& toolsMenuEntries()
{
    return kToolsEntries;
}

} // namespace xiangqi
