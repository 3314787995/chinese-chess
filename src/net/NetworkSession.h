#pragma once

#include "common/Types.h"

#include <optional>
#include <string>
#include <vector>

namespace xiangqi
{

struct LanRoom
{
    std::string name;
    std::string address;
    unsigned short port{ 0 };
    int spectator_count{ 0 };
    bool accepts_player{ true };
    bool accepts_spectators{ true };
};

class NetworkSession
{
public:
    struct AcceptedConnection
    {
        uintptr_t socket{ static_cast<uintptr_t>(~0ULL) };
        std::string address;
    };

    NetworkSession();
    ~NetworkSession();

    NetworkSession(const NetworkSession&) = delete;
    NetworkSession& operator=(const NetworkSession&) = delete;
    NetworkSession(NetworkSession&& other) noexcept;
    NetworkSession& operator=(NetworkSession&& other) noexcept;

    void host(unsigned short port);
    void join(const std::string& address, unsigned short port);
    bool isConnected() const noexcept;
    bool canAcceptConnections() const noexcept;
    unsigned short listeningPort() const noexcept { return listening_port_; }
    void close();

    void sendLine(const std::string& line);
    std::string receiveLine();
    std::optional<AcceptedConnection> acceptConnection(int timeout_ms);
    void replaceConnection(AcceptedConnection& connection);

    static bool isValid(const AcceptedConnection& connection) noexcept;
    static void closeConnection(AcceptedConnection& connection) noexcept;
    static void sendLine(AcceptedConnection& connection, const std::string& line);
    static std::string receiveLine(const AcceptedConnection& connection);

private:
    void ensureWinsock();
    void ensureConnected() const;

    uintptr_t listener_{ static_cast<uintptr_t>(~0ULL) };
    uintptr_t socket_{ static_cast<uintptr_t>(~0ULL) };
    unsigned short listening_port_{ 0 };
    bool winsock_ready_{ false };
};

std::string escapeProtocolField(const std::string& value);
std::string unescapeProtocolField(const std::string& value);

std::string serializeHandshake(const GameSettings& settings, const PlayerInfo& players, Side first_turn);
void parseHandshake(const std::string& line, GameSettings& settings, PlayerInfo& players, Side& first_turn);
std::string serializeRoomAnnouncement(const LanRoom& room);
bool parseRoomAnnouncement(const std::string& line, LanRoom& room);
void broadcastLanRoom(const LanRoom& room, unsigned short discovery_port = 47654);
std::vector<LanRoom> discoverLanRooms(int timeout_ms = 900, unsigned short discovery_port = 47654);

} // namespace xiangqi
