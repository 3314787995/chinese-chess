#include "net/NetworkSession.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace xiangqi
{

namespace
{

constexpr uintptr_t kInvalidSocketValue = static_cast<uintptr_t>(INVALID_SOCKET);

SOCKET toSocket(const uintptr_t value)
{
    return static_cast<SOCKET>(value);
}

uintptr_t fromSocket(const SOCKET value)
{
    return static_cast<uintptr_t>(value);
}

std::string readUntilNewline(const SOCKET socket)
{
    std::string result;
    char ch = '\0';
    while (true)
    {
        const int received = recv(socket, &ch, 1, 0);
        if (received <= 0)
        {
            throw NetworkError("Connection closed while reading.");
        }
        if (ch == '\n')
        {
            break;
        }
        result.push_back(ch);
    }
    return result;
}

void sendLineToSocket(const SOCKET socket, const std::string& line)
{
    std::string payload = line + "\n";
    size_t sent_total = 0;
    while (sent_total < payload.size())
    {
        const size_t remaining = payload.size() - sent_total;
        const int chunk_size = static_cast<int>(std::min(
            remaining,
            static_cast<size_t>(std::numeric_limits<int>::max())));
        const int sent = send(socket, payload.data() + sent_total, chunk_size, 0);
        if (sent == SOCKET_ERROR || sent == 0)
        {
            throw NetworkError("Failed to send network message.");
        }
        sent_total += static_cast<size_t>(sent);
    }
}

void startupWinsockForUtility()
{
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        throw NetworkError("Failed to initialize WinSock.");
    }
}

} // namespace

std::string escapeProtocolField(const std::string& value)
{
    // 联机协议以换行作为消息边界，所以每个字段都必须先转义再拼接。
    // 这里处理的是“字段级”转义，保证玩家名、房间名里出现竖线或换行时不会破坏协议结构。
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value)
    {
        switch (ch)
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '|':
            escaped += "\\p";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

std::string unescapeProtocolField(const std::string& value)
{
    std::string unescaped;
    unescaped.reserve(value.size());
    for (size_t index = 0; index < value.size(); ++index)
    {
        const char ch = value[index];
        if (ch != '\\' || index + 1 >= value.size())
        {
            unescaped.push_back(ch);
            continue;
        }

        const char escaped = value[++index];
        switch (escaped)
        {
        case '\\':
            unescaped.push_back('\\');
            break;
        case 'p':
            unescaped.push_back('|');
            break;
        case 'n':
            unescaped.push_back('\n');
            break;
        case 'r':
            unescaped.push_back('\r');
            break;
        default:
            unescaped.push_back('\\');
            unescaped.push_back(escaped);
            break;
        }
    }
    return unescaped;
}

NetworkSession::NetworkSession() = default;

NetworkSession::~NetworkSession()
{
    close();
    if (winsock_ready_)
    {
        WSACleanup();
    }
}

NetworkSession::NetworkSession(NetworkSession&& other) noexcept
    : listener_(other.listener_),
      socket_(other.socket_),
      listening_port_(other.listening_port_),
      winsock_ready_(other.winsock_ready_)
{
    other.listener_ = kInvalidSocketValue;
    other.socket_ = kInvalidSocketValue;
    other.listening_port_ = 0;
    other.winsock_ready_ = false;
}

NetworkSession& NetworkSession::operator=(NetworkSession&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    close();
    if (winsock_ready_)
    {
        WSACleanup();
    }

    listener_ = other.listener_;
    socket_ = other.socket_;
    listening_port_ = other.listening_port_;
    winsock_ready_ = other.winsock_ready_;
    other.listener_ = kInvalidSocketValue;
    other.socket_ = kInvalidSocketValue;
    other.listening_port_ = 0;
    other.winsock_ready_ = false;
    return *this;
}

void NetworkSession::listen(const unsigned short port)
{
    ensureWinsock();
    close();

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET)
    {
        throw NetworkError("Failed to create host socket.");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
    {
        closesocket(listener);
        throw NetworkError("Failed to bind listening socket.");
    }

    if (::listen(listener, SOMAXCONN) == SOCKET_ERROR)
    {
        closesocket(listener);
        throw NetworkError("Failed to listen on port.");
    }

    sockaddr_in bound_address{};
    int bound_length = sizeof(bound_address);
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&bound_address), &bound_length) == 0)
    {
        listening_port_ = ntohs(bound_address.sin_port);
    }
    else
    {
        listening_port_ = port;
    }
    listener_ = fromSocket(listener);
}

void NetworkSession::host(const unsigned short port)
{
    listen(port);
    auto accepted = acceptConnection(-1);
    if (!accepted.has_value())
    {
        throw NetworkError("Failed to accept client connection.");
    }
    replaceConnection(*accepted);
}

void NetworkSession::join(const std::string& address_text, const unsigned short port)
{
    ensureWinsock();
    close();

    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET)
    {
        throw NetworkError("Failed to create client socket.");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (InetPtonA(AF_INET, address_text.c_str(), &address.sin_addr) != 1)
    {
        closesocket(client);
        throw NetworkError("Invalid IPv4 address.");
    }

    if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
    {
        closesocket(client);
        throw NetworkError("Failed to connect to host.");
    }

    socket_ = fromSocket(client);
}

bool NetworkSession::isConnected() const noexcept
{
    return socket_ != kInvalidSocketValue;
}

bool NetworkSession::canAcceptConnections() const noexcept
{
    return listener_ != kInvalidSocketValue;
}

void NetworkSession::close()
{
    if (socket_ != kInvalidSocketValue)
    {
        closesocket(toSocket(socket_));
        socket_ = kInvalidSocketValue;
    }
    if (listener_ != kInvalidSocketValue)
    {
        closesocket(toSocket(listener_));
        listener_ = kInvalidSocketValue;
        listening_port_ = 0;
    }
}

void NetworkSession::sendLine(const std::string& line)
{
    ensureConnected();
    sendLineToSocket(toSocket(socket_), line);
}

std::string NetworkSession::receiveLine()
{
    ensureConnected();
    return readUntilNewline(toSocket(socket_));
}

std::optional<NetworkSession::AcceptedConnection> NetworkSession::acceptConnection(const int timeout_ms)
{
    ensureWinsock();
    if (!canAcceptConnections())
    {
        return std::nullopt;
    }

    fd_set read_set;
    FD_ZERO(&read_set);
    const SOCKET listener = toSocket(listener_);
    FD_SET(listener, &read_set);

    timeval timeout{};
    timeval* timeout_ptr = nullptr;
    if (timeout_ms >= 0)
    {
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        timeout_ptr = &timeout;
    }
    const int ready = select(0, &read_set, nullptr, nullptr, timeout_ptr);
    if (ready == SOCKET_ERROR)
    {
        throw NetworkError("Failed while waiting for a LAN connection.");
    }
    if (ready == 0 || !FD_ISSET(listener, &read_set))
    {
        return std::nullopt;
    }

    sockaddr_in remote{};
    int remote_length = sizeof(remote);
    const SOCKET accepted = accept(listener, reinterpret_cast<sockaddr*>(&remote), &remote_length);
    if (accepted == INVALID_SOCKET)
    {
        throw NetworkError("Failed to accept LAN connection.");
    }

    char address_text[INET_ADDRSTRLEN]{};
    InetNtopA(AF_INET, &remote.sin_addr, address_text, static_cast<DWORD>(sizeof(address_text)));
    return AcceptedConnection{ fromSocket(accepted), address_text };
}

void NetworkSession::replaceConnection(AcceptedConnection& connection)
{
    if (!isValid(connection))
    {
        throw NetworkError("Cannot replace the LAN connection with an invalid socket.");
    }

    if (socket_ != kInvalidSocketValue)
    {
        closesocket(toSocket(socket_));
    }
    socket_ = connection.socket;
    connection.socket = kInvalidSocketValue;
}

bool NetworkSession::isValid(const AcceptedConnection& connection) noexcept
{
    return connection.socket != kInvalidSocketValue;
}

void NetworkSession::closeConnection(AcceptedConnection& connection) noexcept
{
    if (isValid(connection))
    {
        closesocket(toSocket(connection.socket));
        connection.socket = kInvalidSocketValue;
    }
}

void NetworkSession::sendLine(AcceptedConnection& connection, const std::string& line)
{
    if (!isValid(connection))
    {
        throw NetworkError("LAN connection is not connected.");
    }
    sendLineToSocket(toSocket(connection.socket), line);
}

std::string NetworkSession::receiveLine(const AcceptedConnection& connection)
{
    if (!isValid(connection))
    {
        throw NetworkError("LAN connection is not connected.");
    }
    return readUntilNewline(toSocket(connection.socket));
}

void NetworkSession::ensureWinsock()
{
    if (winsock_ready_)
    {
        return;
    }

    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        throw NetworkError("Failed to initialize WinSock.");
    }
    winsock_ready_ = true;
}

void NetworkSession::ensureConnected() const
{
    if (!isConnected())
    {
        throw NetworkError("Network session is not connected.");
    }
}

std::string serializeHandshake(const GameSettings& settings, const PlayerInfo& players, const Side first_turn)
{
    std::ostringstream output;
    output << "HELLO"
           << "|game=" << toString(settings.game_kind)
           << "|mode=" << toString(settings.board_mode)
           << "|time=" << settings.move_time_limit_seconds
           << "|undo=" << (settings.allow_undo ? 1 : 0)
           << "|show=" << (settings.show_legal_moves ? 1 : 0)
           << "|red=" << escapeProtocolField(players.red_name)
           << "|black=" << escapeProtocolField(players.black_name)
           << "|first=" << toString(first_turn);
    return output.str();
}

void parseHandshake(const std::string& line, GameSettings& settings, PlayerInfo& players, Side& first_turn)
{
    if (line.rfind("HELLO|", 0) != 0)
    {
        throw NetworkError("Invalid handshake message.");
    }

    std::istringstream input(line);
    std::string token;
    while (std::getline(input, token, '|'))
    {
        const auto equal = token.find('=');
        if (equal == std::string::npos)
        {
            continue;
        }
        const std::string key = token.substr(0, equal);
        const std::string value = unescapeProtocolField(token.substr(equal + 1));
        if (key == "mode")
        {
            settings.board_mode = value == "Expanded11x10" ? BoardMode::Expanded11x10 : BoardMode::Standard9x10;
        }
        else if (key == "game")
        {
            settings.game_kind = value == "DarkChess" ? GameKind::DarkChess : GameKind::Xiangqi;
        }
        else if (key == "time")
        {
            settings.move_time_limit_seconds = std::stoi(value);
        }
        else if (key == "undo")
        {
            settings.allow_undo = parseBool(value);
        }
        else if (key == "show")
        {
            settings.show_legal_moves = parseBool(value);
        }
        else if (key == "red")
        {
            players.red_name = value;
        }
        else if (key == "black")
        {
            players.black_name = value;
        }
        else if (key == "first")
        {
            first_turn = value == "Black" ? Side::Black : Side::Red;
        }
    }
}

std::string serializeConnectionRequest(const ConnectionRole role, const std::optional<Side> side)
{
    switch (role)
    {
    case ConnectionRole::Player:
        return "JOIN_REQ";
    case ConnectionRole::Spectator:
        return "WATCH_REQ";
    case ConnectionRole::Reconnect:
        return std::string("REJOIN_REQ|side=") + toString(side.value_or(Side::Black));
    }
    return "JOIN_REQ";
}

std::optional<ConnectionRequest> parseConnectionRequest(const std::string& line)
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

    if (fields.empty())
    {
        return std::nullopt;
    }
    if (fields[0] == "JOIN_REQ")
    {
        return ConnectionRequest{ ConnectionRole::Player, std::nullopt };
    }
    if (fields[0] == "WATCH_REQ")
    {
        return ConnectionRequest{ ConnectionRole::Spectator, std::nullopt };
    }
    if (fields[0] == "REJOIN_REQ")
    {
        ConnectionRequest request{ ConnectionRole::Reconnect, Side::Black };
        for (size_t index = 1; index < fields.size(); ++index)
        {
            const auto equal = fields[index].find('=');
            if (equal == std::string::npos)
            {
                continue;
            }
            const std::string key = fields[index].substr(0, equal);
            const std::string value = fields[index].substr(equal + 1);
            if (key == "side")
            {
                request.side = value == "Red" ? Side::Red : Side::Black;
            }
        }
        return request;
    }
    return std::nullopt;
}

std::string serializeRoomAnnouncement(const LanRoom& room)
{
    // 房间广播只负责告诉局域网内“哪里能连、当前能不能加入/观战”。
    // 真正的身份分流发生在 TCP 连接建立后的 JOIN_REQ / WATCH_REQ / REJOIN_REQ。
    std::ostringstream output;
    output << "ROOM"
           << "|name=" << escapeProtocolField(room.name)
           << "|port=" << room.port
           << "|spectators=" << room.spectator_count
           << "|player=" << (room.accepts_player ? 1 : 0)
           << "|watch=" << (room.accepts_spectators ? 1 : 0);
    return output.str();
}

bool parseRoomAnnouncement(const std::string& line, LanRoom& room)
{
    if (line.rfind("ROOM|", 0) != 0)
    {
        return false;
    }

    std::istringstream input(line);
    std::string token;
    while (std::getline(input, token, '|'))
    {
        const auto equal = token.find('=');
        if (equal == std::string::npos)
        {
            continue;
        }

        const std::string key = token.substr(0, equal);
        const std::string value = unescapeProtocolField(token.substr(equal + 1));
        if (key == "name")
        {
            room.name = value;
        }
        else if (key == "port")
        {
            room.port = static_cast<unsigned short>(std::stoi(value));
        }
        else if (key == "spectators")
        {
            room.spectator_count = std::stoi(value);
        }
        else if (key == "player")
        {
            room.accepts_player = parseBool(value);
        }
        else if (key == "watch")
        {
            room.accepts_spectators = parseBool(value);
        }
    }

    return room.port != 0;
}

void broadcastLanRoom(const LanRoom& room, const unsigned short discovery_port)
{
    startupWinsockForUtility();
    SOCKET udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp == INVALID_SOCKET)
    {
        WSACleanup();
        throw NetworkError("Failed to create LAN discovery socket.");
    }

    BOOL enabled = TRUE;
    setsockopt(udp, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&enabled), sizeof(enabled));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_BROADCAST;
    address.sin_port = htons(discovery_port);

    const std::string message = serializeRoomAnnouncement(room);
    const int sent = sendto(
        udp,
        message.c_str(),
        static_cast<int>(message.size()),
        0,
        reinterpret_cast<sockaddr*>(&address),
        sizeof(address));

    closesocket(udp);
    WSACleanup();
    if (sent == SOCKET_ERROR)
    {
        throw NetworkError("Failed to broadcast LAN room.");
    }
}

std::vector<LanRoom> discoverLanRooms(const int timeout_ms, const unsigned short discovery_port)
{
    startupWinsockForUtility();
    SOCKET udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp == INVALID_SOCKET)
    {
        WSACleanup();
        throw NetworkError("Failed to create LAN discovery socket.");
    }

    BOOL reuse = TRUE;
    setsockopt(udp, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(discovery_port);
    if (bind(udp, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR)
    {
        closesocket(udp);
        WSACleanup();
        throw NetworkError("Failed to listen for LAN rooms.");
    }

    std::unordered_map<std::string, LanRoom> rooms_by_key;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(udp, &read_set);

        timeval timeout{};
        timeout.tv_sec = static_cast<long>(remaining.count() / 1000);
        timeout.tv_usec = static_cast<long>((remaining.count() % 1000) * 1000);

        const int ready = select(0, &read_set, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR)
        {
            closesocket(udp);
            WSACleanup();
            throw NetworkError("Failed while discovering LAN rooms.");
        }
        if (ready == 0)
        {
            break;
        }

        char buffer[1024]{};
        sockaddr_in remote{};
        int remote_length = sizeof(remote);
        const int received = recvfrom(
            udp,
            buffer,
            static_cast<int>(sizeof(buffer) - 1),
            0,
            reinterpret_cast<sockaddr*>(&remote),
            &remote_length);
        if (received <= 0)
        {
            continue;
        }

        buffer[received] = '\0';
        LanRoom room;
        if (!parseRoomAnnouncement(buffer, room))
        {
            continue;
        }

        char address_text[INET_ADDRSTRLEN]{};
        InetNtopA(AF_INET, &remote.sin_addr, address_text, static_cast<DWORD>(sizeof(address_text)));
        room.address = address_text;
        const std::string key = room.address + ":" + std::to_string(room.port);
        rooms_by_key[key] = room;
    }

    closesocket(udp);
    WSACleanup();

    std::vector<LanRoom> rooms;
    rooms.reserve(rooms_by_key.size());
    for (auto& entry : rooms_by_key)
    {
        rooms.push_back(std::move(entry.second));
    }
    std::sort(
        rooms.begin(),
        rooms.end(),
        [](const LanRoom& left, const LanRoom& right)
        {
            return left.name < right.name;
        });
    return rooms;
}

} // namespace xiangqi
