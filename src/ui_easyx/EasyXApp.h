#pragma once

#include "app/GameSession.h"
#include "net/NetworkSession.h"

#include <filesystem>
#include <memory>

namespace xiangqi
{

class EasyXApp
{
public:
    bool isAvailable() const noexcept;
    int run(GameSettings settings, PlayerInfo players = {});
    int runNetworkGame(GameSettings settings, PlayerInfo players, std::unique_ptr<NetworkSession> network, Side local_side);
    int runReplayFile(const std::filesystem::path& path);
    int runReplayBrowser();
};

} // namespace xiangqi
