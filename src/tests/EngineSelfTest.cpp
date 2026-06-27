#include "tests/EngineSelfTest.h"

#include "ai/SearchEngine.h"
#include "app/GameSession.h"
#include "common/Repetition.h"
#include "darkchess/DarkChess.h"
#include "engine/ChessEngine.h"
#include "net/NetworkSession.h"
#include "storage/Storage.h"
#include "ui_console/ConsoleFormatting.h"
#include "ui_console/ConsoleMenu.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace xiangqi::tests
{

namespace
{

void require(const bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template <typename ExpectedException, typename Func>
void requireThrows(Func&& func, const std::string& message)
{
    try
    {
        func();
    }
    catch (const ExpectedException&)
    {
        return;
    }
    catch (const std::exception& ex)
    {
        throw std::runtime_error(message + " Wrong exception: " + ex.what());
    }

    throw std::runtime_error(message);
}

std::string moveText(const Move& move)
{
    std::ostringstream output;
    output << '(' << move.from.row << ',' << move.from.col << ")->(" << move.to.row << ',' << move.to.col << ')';
    return output.str();
}

bool startsWith(const std::string& text, const std::string& prefix)
{
    return text.rfind(prefix, 0) == 0;
}

void runLanSpectatorRoutingSelfTest(const bool dark_chess)
{
    GameSettings settings;
    settings.ai_enabled = false;
    settings.use_easyx = true;
    settings.game_kind = dark_chess ? GameKind::DarkChess : GameKind::Xiangqi;

    PlayerInfo players;
    players.red_name = "Host";
    players.black_name = "Guest";
    std::string state_line;
    if (dark_chess)
    {
        DarkGameSession session(settings, players, 4242u);
        state_line = std::string("DARK_STATE|") + escapeProtocolField(session.serializePublic());
    }
    else
    {
        GameSession session(settings, players);
        state_line = std::string("STATE|") + escapeProtocolField(session.serialize());
    }

    NetworkSession host;
    host.listen(0);
    const unsigned short port = host.listeningPort();
    require(port != 0, "Ephemeral LAN listener should report its assigned port.");

    std::exception_ptr host_error;
    std::thread host_thread(
        [&]()
        {
            try
            {
                SpectatorConnections pregame_spectators;

                while (!host.isConnected())
                {
                    auto accepted = host.acceptConnection(5000);
                    require(accepted.has_value(), "Host should accept pre-game spectator or player connections.");

                    const auto request = parseConnectionRequest(NetworkSession::receiveLine(*accepted));
                    require(request.has_value(), "LAN clients should identify themselves before the handshake.");

                    if (request->role == ConnectionRole::Spectator)
                    {
                        NetworkSession::sendLine(
                            *accepted,
                            std::string("WAITING|") + escapeProtocolField(dark_chess ? "等待揭棋玩家加入" : "等待象棋玩家加入"));
                        pregame_spectators.push_back(std::move(*accepted));
                        continue;
                    }

                    require(request->role == ConnectionRole::Player, "The first player connection should remain the player slot.");
                    host.replaceConnection(*accepted);
                    host.sendLine(serializeHandshake(settings, players, Side::Red));
                }

                require(pregame_spectators.size() == 1, "Pre-game spectator should be queued instead of becoming the black player.");
                for (auto& spectator : pregame_spectators)
                {
                    NetworkSession::sendLine(spectator, serializeHandshake(settings, players, Side::Red));
                    NetworkSession::sendLine(spectator, state_line);
                    NetworkSession::closeConnection(spectator);
                }

                auto live_spectator = host.acceptConnection(5000);
                require(live_spectator.has_value(), "Host should accept a live spectator after the player joins.");
                const auto live_request = parseConnectionRequest(NetworkSession::receiveLine(*live_spectator));
                require(
                    live_request.has_value() && live_request->role == ConnectionRole::Spectator,
                    "Live spectator should identify as WATCH_REQ.");
                NetworkSession::sendLine(*live_spectator, serializeHandshake(settings, players, Side::Red));
                NetworkSession::sendLine(*live_spectator, state_line);
                NetworkSession::closeConnection(*live_spectator);
            }
            catch (...)
            {
                host_error = std::current_exception();
            }
        });

    try
    {
        NetworkSession pregame_spectator;
        pregame_spectator.join("127.0.0.1", port);
        pregame_spectator.sendLine(serializeConnectionRequest(ConnectionRole::Spectator));
        require(startsWith(pregame_spectator.receiveLine(), "WAITING|"), "Pre-game spectator should receive a waiting message.");

        NetworkSession player;
        player.join("127.0.0.1", port);
        player.sendLine(serializeConnectionRequest(ConnectionRole::Player));
        require(startsWith(player.receiveLine(), "HELLO|"), "Player should receive the game handshake.");

        require(startsWith(pregame_spectator.receiveLine(), "HELLO|"), "Queued spectator should receive the game handshake once the player joins.");
        require(
            startsWith(pregame_spectator.receiveLine(), dark_chess ? "DARK_STATE|" : "STATE|"),
            "Queued spectator should receive the first public board state.");

        NetworkSession live_spectator;
        live_spectator.join("127.0.0.1", port);
        live_spectator.sendLine(serializeConnectionRequest(ConnectionRole::Spectator));
        require(startsWith(live_spectator.receiveLine(), "HELLO|"), "Live spectator should receive the game handshake.");
        require(
            startsWith(live_spectator.receiveLine(), dark_chess ? "DARK_STATE|" : "STATE|"),
            "Live spectator should receive the current public board state.");
    }
    catch (...)
    {
        if (host_thread.joinable())
        {
            host_thread.join();
        }
        throw;
    }

    if (host_thread.joinable())
    {
        host_thread.join();
    }
    if (host_error)
    {
        std::rethrow_exception(host_error);
    }
}

} // namespace

std::string runAll()
{
    std::ostringstream output;
    Board standard(BoardMode::Standard9x10);
    require(standard.config().cols == 9, "Standard board should have 9 columns.");
    require(standard.config().initial_pieces.size() == 32, "Standard board should start with 32 pieces.");
    require(standard.generateLegalMoves(Side::Red).size() > 0, "Red should have legal opening moves.");

    Board expanded(BoardMode::Expanded11x10);
    require(expanded.config().cols == 11, "Expanded board should have 11 columns.");
    require(expanded.isPalaceCell(Side::Red, { 8, 5 }), "Expanded red palace should be centered.");
    require(expanded.pieceAt({ 9, 5 }) != nullptr, "Expanded red king should be centered.");
    require(expanded.pieceAt({ 9, 1 }) != nullptr, "Expanded red rook should be shifted.");

    SearchEngine search(1);

    const std::string mate_fixture =
        "VERSION=1\n"
        "MODE=Standard9x10\n"
        "CURRENT=Red\n"
        "RESULT=Ongoing\n"
        "RED_NAME=Red\n"
        "BLACK_NAME=Black\n"
        "MOVE_TIME=60\n"
        "ALLOW_UNDO=1\n"
        "SHOW_LEGAL=1\n"
        "AI_ENABLED=0\n"
        "AI_SIDE=Black\n"
        "AI_DEPTH=1\n"
        "USE_EASYX=0\n"
        "RED_REMAINING_MS=60000\n"
        "BLACK_REMAINING_MS=60000\n"
        "BOARD_BEGIN\n"
        "__ __ __ __ bK __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ rR __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ rK __ __ __ __\n"
        "BOARD_END\n"
        "HISTORY_BEGIN\n"
        "HISTORY_END\n";

    const GameSession mate_session = GameSession::deserialize(mate_fixture);
    const auto mate_move = search.chooseBestMove(mate_session);
    require(mate_move.has_value(), "Search engine should find a move.");
    require(
        mate_move->from == Position<int>{ 5, 4 } && mate_move->to == Position<int>{ 0, 4 },
        "Search should prefer immediate mate, but chose " + moveText(*mate_move) + '.');

    const std::string trap_fixture =
        "VERSION=1\n"
        "MODE=Standard9x10\n"
        "CURRENT=Red\n"
        "RESULT=Ongoing\n"
        "RED_NAME=Red\n"
        "BLACK_NAME=Black\n"
        "MOVE_TIME=60\n"
        "ALLOW_UNDO=1\n"
        "SHOW_LEGAL=1\n"
        "AI_ENABLED=0\n"
        "AI_SIDE=Black\n"
        "AI_DEPTH=1\n"
        "USE_EASYX=0\n"
        "RED_REMAINING_MS=60000\n"
        "BLACK_REMAINING_MS=60000\n"
        "BOARD_BEGIN\n"
        "__ __ __ bK bR __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ bP __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ rR __ __ bP __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ __ rK __ __ __\n"
        "BOARD_END\n"
        "HISTORY_BEGIN\n"
        "HISTORY_END\n";

    const GameSession trap_session = GameSession::deserialize(trap_fixture);
    const auto trap_move = search.chooseBestMove(trap_session);
    require(trap_move.has_value(), "Search engine should find a tactical move.");
    require(
        !(trap_move->from == Position<int>{ 5, 4 } && trap_move->to == Position<int>{ 3, 4 }),
        "Search should avoid the poisoned pawn that loses the rook.");

    const std::string escape_fixture =
        "VERSION=1\n"
        "MODE=Standard9x10\n"
        "CURRENT=Red\n"
        "RESULT=Ongoing\n"
        "RED_NAME=Red\n"
        "BLACK_NAME=Black\n"
        "MOVE_TIME=60\n"
        "ALLOW_UNDO=1\n"
        "SHOW_LEGAL=1\n"
        "AI_ENABLED=0\n"
        "AI_SIDE=Black\n"
        "AI_DEPTH=1\n"
        "USE_EASYX=0\n"
        "RED_REMAINING_MS=60000\n"
        "BLACK_REMAINING_MS=60000\n"
        "BOARD_BEGIN\n"
        "__ __ __ bK __ __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ rR bR __ __ __ __\n"
        "__ __ __ __ __ __ __ __ __\n"
        "__ __ __ __ rK __ __ __ __\n"
        "BOARD_END\n"
        "HISTORY_BEGIN\n"
        "HISTORY_END\n";

    const GameSession escape_session = GameSession::deserialize(escape_fixture);
    const auto escape_move = search.chooseBestMove(escape_session);
    require(escape_move.has_value(), "Search engine should find an escape move.");
    const auto legal_escapes = escape_session.legalMovesForCurrentSide();
    require(
        std::any_of(
            legal_escapes.begin(),
            legal_escapes.end(),
            [&](const Move& move)
            {
                return move.from == escape_move->from && move.to == escape_move->to;
            }),
        "Search must return a legal escape move.");
    {
        Board board_after_escape = escape_session.board();
        Move applied_escape = *escape_move;
        board_after_escape.applyMove(applied_escape);
        require(!board_after_escape.isInCheck(Side::Red), "Escape move should resolve the current check.");
    }

    GameSettings expanded_settings;
    expanded_settings.board_mode = BoardMode::Expanded11x10;
    const GameSession expanded_session(expanded_settings);
    const auto expanded_move = search.chooseBestMove(expanded_session);
    require(expanded_move.has_value(), "Expanded mode should also have a valid AI move.");
    const auto expanded_legal_moves = expanded_session.legalMovesForCurrentSide();
    require(
        std::any_of(
            expanded_legal_moves.begin(),
            expanded_legal_moves.end(),
            [&](const Move& move)
            {
                return move.from == expanded_move->from && move.to == expanded_move->to;
            }),
        "Expanded mode AI move should be legal.");

    const auto hint_move = search.chooseBestMove(trap_session);
    require(hint_move.has_value(), "Hint search should return a move.");
    require(
        hint_move->from == trap_move->from && hint_move->to == trap_move->to,
        "Hint and AI should agree with the same search settings.");

    {
        PlayerInfo special_players;
        special_players.red_name = "Red|Comma,Line\nEquals=Slash\\";
        special_players.black_name = "Black \"Quote\"";

        GameSession special_session(GameSettings{}, special_players);
        const GameSession restored = GameSession::deserialize(special_session.serialize());
        require(restored.players().red_name == special_players.red_name, "Saved red name should round-trip escaped characters.");
        require(restored.players().black_name == special_players.black_name, "Saved black name should round-trip escaped characters.");
    }

    {
        GameSettings network_settings;
        network_settings.board_mode = BoardMode::Expanded11x10;
        network_settings.move_time_limit_seconds = 45;
        network_settings.allow_undo = false;
        network_settings.show_legal_moves = false;

        PlayerInfo network_players;
        network_players.red_name = "Red|Pipe\nLine";
        network_players.black_name = "Black=Equals\\Slash";

        const std::string wire = serializeHandshake(network_settings, network_players, Side::Black);
        require(wire.find('\n') == std::string::npos, "Handshake should remain a single wire line.");

        GameSettings parsed_settings;
        PlayerInfo parsed_players;
        Side parsed_first_turn = Side::Red;
        parseHandshake(wire, parsed_settings, parsed_players, parsed_first_turn);
        require(parsed_settings.board_mode == network_settings.board_mode, "Handshake should preserve board mode.");
        require(parsed_settings.move_time_limit_seconds == network_settings.move_time_limit_seconds, "Handshake should preserve time limit.");
        require(parsed_settings.allow_undo == network_settings.allow_undo, "Handshake should preserve undo setting.");
        require(parsed_settings.show_legal_moves == network_settings.show_legal_moves, "Handshake should preserve move hint setting.");
        require(parsed_players.red_name == network_players.red_name, "Handshake should round-trip red player name.");
        require(parsed_players.black_name == network_players.black_name, "Handshake should round-trip black player name.");
        require(parsed_first_turn == Side::Black, "Handshake should preserve first turn.");
    }

    {
        LanRoom room;
        room.name = "Host|Room\nOne";
        room.port = 45678;
        room.spectator_count = 2;
        room.accepts_player = false;
        room.accepts_spectators = true;

        const std::string wire = serializeRoomAnnouncement(room);
        require(wire.find('\n') == std::string::npos, "Room announcement should remain one wire line.");

        LanRoom parsed_room;
        require(parseRoomAnnouncement(wire, parsed_room), "Room announcement should parse.");
        require(parsed_room.name == room.name, "Room announcement should preserve room name.");
        require(parsed_room.port == room.port, "Room announcement should preserve port.");
        require(parsed_room.spectator_count == room.spectator_count, "Room announcement should preserve spectator count.");
        require(parsed_room.accepts_player == room.accepts_player, "Room announcement should preserve player availability.");
        require(parsed_room.accepts_spectators == room.accepts_spectators, "Room announcement should preserve spectator availability.");
    }

    {
        require(
            parseLauncherAction("开始对局") == LauncherAction::OpenPlayMenu,
            "Chinese main menu command should open play menu.");
        require(
            parseLauncherAction("联机大厅") == LauncherAction::OpenNetworkMenu,
            "Chinese main menu command should open network menu.");
        require(
            parseLauncherAction("观战回放") == LauncherAction::OpenWatchReplayMenu,
            "Chinese main menu command should open watch/replay menu.");
        require(
            parseLauncherAction("排行测试") == LauncherAction::OpenToolsMenu,
            "Chinese main menu command should open tools menu.");
        require(
            parseLauncherAction("退出") == LauncherAction::Exit,
            "Chinese exit command should quit.");
        require(
            parseLauncherAction("kaishi") == LauncherAction::OpenPlayMenu,
            "Pinyin alias should open play menu.");
        require(
            parseLauncherAction("watch") == LauncherAction::WatchLanGame,
            "English alias should start spectator mode.");
        require(
            parseLauncherAction("揭棋联机主机") == LauncherAction::HostDarkLanGame,
            "Chinese dark chess host command should parse.");
        require(
            parseLauncherAction("返回") == LauncherAction::Back,
            "Chinese back command should parse.");
        require(
            parseLauncherAction("不存在") == LauncherAction::Invalid,
            "Unknown Chinese command should be rejected.");
    }

    {
        require(
            parseMenuSelection("1", playMenuEntries()) == LauncherAction::LocalConsoleGame,
            "Numeric selection should choose the first play menu entry.");
        require(
            parseMenuSelection("2", playMenuEntries()) == LauncherAction::LocalEasyXGame,
            "Numeric selection should choose the second play menu entry.");
        require(
            parseMenuSelection("1", networkMenuEntries()) == LauncherAction::HostLanGame,
            "Numeric selection should be scoped to the network menu.");
        require(
            parseMenuSelection("1", watchReplayMenuEntries()) == LauncherAction::WatchLanGame,
            "Numeric selection should be scoped to the watch/replay menu.");
        require(
            parseMenuSelection("8", playMenuEntries()) == LauncherAction::Invalid,
            "Out-of-range numeric selection should be rejected.");
        require(
            parseLauncherAction("1") == LauncherAction::Invalid,
            "Bare numbers should not become global launcher aliases.");

        const std::string red_piece = "\x1b[31mrK\x1b[0m";
        require(consoleDisplayWidth("..") == 2, "Plain cells should have their visible width measured.");
        require(consoleDisplayWidth(red_piece) == 2, "ANSI color escapes should not count toward cell width.");
        require(
            consoleDisplayWidth(rightAlignConsoleCell(red_piece, 3)) == 3,
            "Colored board cells should still align to their requested visible width.");
    }

    {
        const auto join = parseConnectionRequest(serializeConnectionRequest(ConnectionRole::Player));
        require(join.has_value(), "JOIN_REQ should parse.");
        require(join->role == ConnectionRole::Player, "JOIN_REQ should identify a player connection.");

        const auto watch = parseConnectionRequest(serializeConnectionRequest(ConnectionRole::Spectator));
        require(watch.has_value(), "WATCH_REQ should parse.");
        require(watch->role == ConnectionRole::Spectator, "WATCH_REQ should identify a spectator connection.");

        const auto reconnect = parseConnectionRequest(serializeConnectionRequest(ConnectionRole::Reconnect, Side::Black));
        require(reconnect.has_value(), "REJOIN_REQ should parse.");
        require(reconnect->role == ConnectionRole::Reconnect, "REJOIN_REQ should identify reconnect.");
        require(reconnect->side.has_value() && *reconnect->side == Side::Black, "REJOIN_REQ should preserve reconnect side.");

        require(!parseConnectionRequest("HELLO").has_value(), "Handshake lines are not connection role requests.");
    }

    runLanSpectatorRoutingSelfTest(false);
    runLanSpectatorRoutingSelfTest(true);

    {
        std::vector<std::string> board_lines(10, "__ __ __ __ __ __ __ __ __");
        board_lines[0] = "__ __ __ __ xK __ __ __ __";
        requireThrows<StorageError>(
            [&]()
            {
                (void)Board::deserialize(BoardMode::Standard9x10, board_lines);
            },
            "Board loader should reject invalid side tokens.");

        board_lines[0] = "__ __ __ __ bK __ __ __ __ extra";
        requireThrows<StorageError>(
            [&]()
            {
                (void)Board::deserialize(BoardMode::Standard9x10, board_lines);
            },
            "Board loader should reject rows with extra cells.");
    }

    {
        const auto original_path = std::filesystem::current_path();
        const auto temp_path = std::filesystem::temp_directory_path() /
            ("xiangqi_selftest_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temp_path);

        try
        {
            std::filesystem::current_path(temp_path);

            PlayerInfo leaderboard_players;
            leaderboard_players.red_name = "Red,One";
            leaderboard_players.black_name = "Black \"Two\"";
            GameSession leaderboard_session(GameSettings{}, leaderboard_players);
            leaderboard_session.resign(Side::Red);
            storage::appendLeaderboard(leaderboard_session);

            const auto leaderboard_lines = storage::readLeaderboardLines();
            require(leaderboard_lines.size() == 2, "Leaderboard should contain a header and one row in the temp directory.");
            require(
                leaderboard_lines[1].find("\"Red,One\"") != std::string::npos &&
                    leaderboard_lines[1].find("Black \"\"Two\"\"") != std::string::npos,
                "Leaderboard CSV should quote player names containing commas or quotes: " + leaderboard_lines[1]);

            std::filesystem::current_path(original_path);
            std::filesystem::remove_all(temp_path);
        }
        catch (...)
        {
            std::filesystem::current_path(original_path);
            std::filesystem::remove_all(temp_path);
            throw;
        }
    }

    {
        const auto original_path = std::filesystem::current_path();
        const auto temp_path = std::filesystem::temp_directory_path() /
            ("leaderboard_rank_selftest_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temp_path);

        try
        {
            std::filesystem::current_path(temp_path);

            PlayerInfo alice_vs_bob;
            alice_vs_bob.red_name = "Alice";
            alice_vs_bob.black_name = "Bob";
            GameSession alice_win(GameSettings{}, alice_vs_bob);
            alice_win.resign(Side::Black);
            storage::appendLeaderboard(alice_win);

            GameSession bob_win(GameSettings{}, alice_vs_bob);
            bob_win.resign(Side::Red);
            storage::appendLeaderboard(bob_win);

            PlayerInfo alice_vs_carl;
            alice_vs_carl.red_name = "Alice";
            alice_vs_carl.black_name = "Carl";
            GameSession second_alice_win(GameSettings{}, alice_vs_carl);
            second_alice_win.resign(Side::Black);
            storage::appendLeaderboard(second_alice_win);

            const auto standings = storage::readLeaderboardStandings();
            require(standings.size() == 3, "Leaderboard standings should aggregate three players.");
            require(standings.front().player_name == "Alice", "Leaderboard should sort by win rate: " + standings.front().player_name);
            require(standings.front().games == 3 && standings.front().wins == 2, "Alice should have two wins in three games.");

            std::filesystem::current_path(original_path);
            std::filesystem::remove_all(temp_path);
        }
        catch (...)
        {
            std::filesystem::current_path(original_path);
            std::filesystem::remove_all(temp_path);
            throw;
        }
    }

    {
        storage::LeaderboardStanding alice;
        alice.player_name = "Alice";
        alice.games = 3;
        alice.wins = 2;
        alice.losses = 1;
        alice.total_moves = 55;
        alice.total_duration_seconds = 135;
        alice.win_rate = 66.6667;
        alice.average_moves = 18.3333;

        const auto leaderboard_table = storage::formatLeaderboardTable({ alice }, 10);
        require(!leaderboard_table.empty(), "Leaderboard table should produce display lines.");
        require(leaderboard_table.front().find("Leaderboard by Win Rate") != std::string::npos, "Leaderboard table should have a title.");
        require(
            leaderboard_table.back().find("Alice") != std::string::npos &&
                leaderboard_table.back().find("66.7%") != std::string::npos,
            "Leaderboard table should include player name and win rate: " + leaderboard_table.back());

        const auto empty_table = storage::formatLeaderboardTable({}, 10);
        require(
            !empty_table.empty() && empty_table.back().find("empty") != std::string::npos,
            "Empty leaderboard table should explain that no records exist.");
    }

    {
        const auto original_path = std::filesystem::current_path();
        const auto temp_path = std::filesystem::temp_directory_path() /
            ("save_list_selftest_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temp_path);

        try
        {
            std::filesystem::current_path(temp_path);

            PlayerInfo save_players;
            save_players.red_name = "player 1";
            save_players.black_name = "player/2";
            GameSession save_session(GameSettings{}, save_players);
            const auto save_path = storage::saveGame(save_session, "");
            require(
                save_path.filename().string() == "player_1_vs_player_2.dat",
                "Empty save name should default to a player-vs-player .dat file: " + save_path.filename().string());

            const auto save_files = storage::listSaveFiles();
            require(save_files.size() == 1, "Save listing should include the player-named save file.");
            require(save_files.front().filename() == save_path.filename(), "Save listing should return the saved file.");

            const GameSession loaded_save = storage::loadGame(save_files.front().filename().string());
            require(
                loaded_save.players().red_name == "player 1" && loaded_save.players().black_name == "player/2",
                "Loading a listed save should restore player names.");

            std::filesystem::current_path(original_path);
            std::filesystem::remove_all(temp_path);
        }
        catch (...)
        {
            std::filesystem::current_path(original_path);
            std::filesystem::remove_all(temp_path);
            throw;
        }
    }

    {
        const std::array<RepetitionState, 6> states{
            RepetitionState{ 11, Side::Red },
            RepetitionState{ 21, Side::Black },
            RepetitionState{ 31, Side::Red },
            RepetitionState{ 41, Side::Black },
            RepetitionState{ 51, Side::Red },
            RepetitionState{ 21, Side::Black },
        };
        const std::array<RepetitionPlyInfo, 5> plies{
            RepetitionPlyInfo{ Side::Red, true },
            RepetitionPlyInfo{ Side::Black, false },
            RepetitionPlyInfo{ Side::Red, true },
            RepetitionPlyInfo{ Side::Black, false },
            RepetitionPlyInfo{ Side::Red, true },
        };
        require(
            isPerpetualCheck(states, states.size(), plies, plies.size(), Side::Red),
            "Perpetual-check helper should detect repeated checking loops.");
    }

    {
        const std::array<RepetitionState, 6> states{
            RepetitionState{ 11, Side::Red },
            RepetitionState{ 21, Side::Black },
            RepetitionState{ 31, Side::Red },
            RepetitionState{ 41, Side::Black },
            RepetitionState{ 51, Side::Red },
            RepetitionState{ 21, Side::Black },
        };
        const std::array<RepetitionPlyInfo, 5> plies{
            RepetitionPlyInfo{ Side::Red, false, true },
            RepetitionPlyInfo{ Side::Black, false, false },
            RepetitionPlyInfo{ Side::Red, false, true },
            RepetitionPlyInfo{ Side::Black, false, false },
            RepetitionPlyInfo{ Side::Red, false, true },
        };
        require(
            isPerpetualChase(states, states.size(), plies, plies.size(), Side::Red),
        "Perpetual-chase helper should detect repeated chasing loops.");
    }

    {
        DarkGameSession dark_one(12345u);
        DarkGameSession dark_two(12345u);
        require(dark_one.board().rows() == 4 && dark_one.board().cols() == 8, "Dark chess should use a 4x8 board.");
        require(dark_one.board().occupiedCount() == 32, "Dark chess should start with all 32 pieces face down.");
        require(dark_one.board().privateGridString() == dark_two.board().privateGridString(), "Dark chess seeded setup should be reproducible.");

        const DarkAction first_flip = dark_one.submitAction(DarkAction::flip({ 0, 0 }, DarkSeat::Player1));
        require(first_flip.revealed_piece.has_value(), "A flip should reveal the hidden piece.");
        require(dark_one.colorForSeat(DarkSeat::Player1) == first_flip.revealed_piece->side, "First flipped color should belong to player 1.");
        require(dark_one.currentSeat() == DarkSeat::Player2, "A flip should pass the turn.");

        const std::string saved = dark_one.serialize();
        const DarkGameSession restored = DarkGameSession::deserialize(saved);
        require(restored.board().publicGridString() == dark_one.board().publicGridString(), "Dark chess save should round-trip public board state.");
        require(restored.board().privateGridString() == dark_one.board().privateGridString(), "Dark chess save should round-trip hidden identities locally.");
        require(
            restored.history().size() == 1 && restored.history().front().revealed_piece.has_value(),
            "Dark chess save should round-trip revealed flip identity in history.");
        require(dark_one.undoLastPly(), "Dark chess should undo one applied flip.");
        require(dark_one.currentSeat() == DarkSeat::Player1, "Dark chess undo should restore the previous seat.");
        require(dark_one.board().openCount() == 0, "Dark chess undo should turn the flipped piece face down again.");

        DarkGameSession dark_full_turn_undo(12345u);
        dark_full_turn_undo.submitAction(DarkAction::flip({ 0, 0 }, DarkSeat::Player1));
        dark_full_turn_undo.submitAction(DarkAction::flip({ 0, 1 }, DarkSeat::Player2));
        require(dark_full_turn_undo.undoLastPlies(2) == 2, "Dark chess should undo a full two-ply turn for Human vs AI.");
        require(dark_full_turn_undo.history().empty(), "Dark full-turn undo should clear both dark actions.");
        require(dark_full_turn_undo.currentSeat() == DarkSeat::Player1, "Dark full-turn undo should restore Player1 to move.");
        require(dark_full_turn_undo.board().openCount() == 0, "Dark full-turn undo should hide both flipped pieces again.");
    }

    {
        const std::string fixture =
            "VERSION=1\n"
            "GAME=DarkChess\n"
            "CURRENT_SEAT=Player1\n"
            "RESULT=Ongoing\n"
            "PLAYER1_NAME=One\n"
            "PLAYER2_NAME=Two\n"
            "PLAYER1_COLOR=Red\n"
            "PLAYER2_COLOR=Black\n"
            "MOVE_TIME=60\n"
            "ALLOW_UNDO=1\n"
            "SHOW_LEGAL=1\n"
            "AI_ENABLED=0\n"
            "AI_SEAT=Player2\n"
            "USE_EASYX=0\n"
            "PLAYER1_REMAINING_MS=60000\n"
            "PLAYER2_REMAINING_MS=60000\n"
            "QUIET_PLIES=0\n"
            "BOARD_BEGIN\n"
            "rR+ bR+ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ __\n"
            "BOARD_END\n"
            "HISTORY_BEGIN\n"
            "HISTORY_END\n";
        DarkGameSession dark = DarkGameSession::deserialize(fixture);
        const DarkAction equal_capture = dark.submitAction(DarkAction::move({ 0, 0 }, { 0, 1 }, DarkSeat::Player1));
        require(equal_capture.captured_piece.has_value(), "Equal-rank dark chess capture should be legal.");
        require(dark.board().pieceAt({ 0, 1 }).has_value() && dark.board().pieceAt({ 0, 1 })->side == Side::Red, "Equal-rank attacker should remain on target.");
        require(!dark.board().pieceAt({ 0, 0 }).has_value(), "Source square should be empty after capture.");
        const DarkGameSession restored = DarkGameSession::deserialize(dark.serialize());
        require(
            restored.history().size() == 1 && restored.history().front().captured_piece.has_value(),
            "Dark chess save should round-trip captured piece identity in history.");
    }

    {
        const std::string fixture =
            "VERSION=1\n"
            "GAME=DarkChess\n"
            "CURRENT_SEAT=Player1\n"
            "RESULT=Ongoing\n"
            "PLAYER1_NAME=One\n"
            "PLAYER2_NAME=Two\n"
            "PLAYER1_COLOR=Red\n"
            "PLAYER2_COLOR=Black\n"
            "MOVE_TIME=60\n"
            "ALLOW_UNDO=1\n"
            "SHOW_LEGAL=1\n"
            "AI_ENABLED=0\n"
            "AI_SEAT=Player2\n"
            "USE_EASYX=0\n"
            "PLAYER1_REMAINING_MS=60000\n"
            "PLAYER2_REMAINING_MS=60000\n"
            "QUIET_PLIES=0\n"
            "BOARD_BEGIN\n"
            "rC+ xx bK+ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ __\n"
            "BOARD_END\n"
            "HISTORY_BEGIN\n"
            "HISTORY_END\n";
        DarkGameSession dark = DarkGameSession::deserialize(fixture);
        const DarkAction cannon_capture = dark.submitAction(DarkAction::move({ 0, 0 }, { 0, 2 }, DarkSeat::Player1));
        require(cannon_capture.captured_piece.has_value() && cannon_capture.captured_piece->type == PieceType::King, "Cannon should capture over exactly one screen.");
        require(dark.board().pieceAt({ 0, 2 }).has_value() && dark.board().pieceAt({ 0, 2 })->type == PieceType::Cannon, "Cannon should land on target after capture.");

        const std::string illegal_fixture =
            "VERSION=1\n"
            "GAME=DarkChess\n"
            "CURRENT_SEAT=Player1\n"
            "RESULT=Ongoing\n"
            "PLAYER1_NAME=One\n"
            "PLAYER2_NAME=Two\n"
            "PLAYER1_COLOR=Red\n"
            "PLAYER2_COLOR=Black\n"
            "MOVE_TIME=60\n"
            "ALLOW_UNDO=1\n"
            "SHOW_LEGAL=1\n"
            "AI_ENABLED=0\n"
            "AI_SEAT=Player2\n"
            "USE_EASYX=0\n"
            "PLAYER1_REMAINING_MS=60000\n"
            "PLAYER2_REMAINING_MS=60000\n"
            "QUIET_PLIES=0\n"
            "BOARD_BEGIN\n"
            "rK+ bP+ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ __\n"
            "BOARD_END\n"
            "HISTORY_BEGIN\n"
            "HISTORY_END\n";
        DarkGameSession illegal_dark = DarkGameSession::deserialize(illegal_fixture);
        requireThrows<IllegalMoveError>(
            [&]()
            {
                illegal_dark.submitAction(DarkAction::move({ 0, 0 }, { 0, 1 }, DarkSeat::Player1));
            },
            "King should not be able to capture a pawn in dark chess.");
    }

    {
        GameSettings dark_settings;
        dark_settings.game_kind = GameKind::DarkChess;
        dark_settings.ai_enabled = true;
        dark_settings.dark_ai_seat = DarkSeat::Player2;
        PlayerInfo dark_players;
        dark_players.red_name = "Player One";
        dark_players.black_name = "Player Two";

        DarkGameSession dark_session(dark_settings, dark_players, 2468u);
        dark_session.submitAction(DarkAction::flip({ 0, 0 }, DarkSeat::Player1));

        const auto save_path = storage::saveDarkGame(dark_session, "dark_selftest_save");
        const DarkGameSession loaded_dark = storage::loadDarkGame(save_path.string());
        require(loaded_dark.settings().game_kind == GameKind::DarkChess, "Dark save should preserve game kind.");
        require(loaded_dark.board().privateGridString() == dark_session.board().privateGridString(), "Dark save should preserve private board.");
        require(loaded_dark.board().publicGridString() == dark_session.board().publicGridString(), "Dark save should preserve public board.");
        require(
            loaded_dark.history().size() == 1 && loaded_dark.history().front().revealed_piece.has_value(),
            "Loaded dark save should preserve revealed piece metadata for CDC export.");

        const auto replay_path = storage::saveDarkReplay(dark_session, "dark_selftest_replay");
        const auto replay_record = storage::loadDarkReplay(replay_path.string());
        require(replay_record.actions.size() == 1, "Dark CDC replay should preserve flip action count.");
        require(replay_record.settings.game_kind == GameKind::DarkChess, "Dark CDC replay should preserve game kind.");
        require(replay_record.initial_private_grid == dark_session.initialPrivateGridString(), "Dark CDC replay should preserve initial private setup for replay.");
        {
            std::ifstream dark_replay_input(replay_path, std::ios::binary);
            std::ostringstream dark_replay_text;
            dark_replay_text << dark_replay_input.rdbuf();
            require(dark_replay_text.str().find("a0(") != std::string::npos, "Dark CDC flip notation should be coordinate-based, e.g. a0(rK).");
            require(dark_replay_text.str().find("flip_a0") == std::string::npos, "Dark CDC replay should not use the old flip_a0 notation.");
        }
        const auto loaded_replay_path = storage::saveDarkReplay(loaded_dark, "dark_selftest_loaded_replay");
        {
            std::ifstream loaded_replay_input(loaded_replay_path, std::ios::binary);
            std::ostringstream loaded_replay_text;
            loaded_replay_text << loaded_replay_input.rdbuf();
            require(
                loaded_replay_text.str().find("a0(") != std::string::npos,
                "Dark CDC export after loading a save should keep flip reveal notation.");
        }
        std::error_code ignored;
        std::filesystem::remove(save_path, ignored);
        std::filesystem::remove(replay_path, ignored);
        std::filesystem::remove(loaded_replay_path, ignored);
    }

    {
        const std::string resigned_fixture =
            "VERSION=1\n"
            "GAME=DarkChess\n"
            "CURRENT_SEAT=Player1\n"
            "RESULT=Resign\n"
            "PLAYER1_NAME=One\n"
            "PLAYER2_NAME=Two\n"
            "PLAYER1_COLOR=Red\n"
            "PLAYER2_COLOR=Black\n"
            "MOVE_TIME=60\n"
            "ALLOW_UNDO=1\n"
            "SHOW_LEGAL=1\n"
            "AI_ENABLED=0\n"
            "AI_SEAT=Player2\n"
            "USE_EASYX=0\n"
            "PLAYER1_REMAINING_MS=60000\n"
            "PLAYER2_REMAINING_MS=60000\n"
            "QUIET_PLIES=0\n"
            "BOARD_BEGIN\n"
            "rK+ __ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ bK+\n"
            "BOARD_END\n"
            "HISTORY_BEGIN\n"
            "HISTORY_END\n";
        const DarkGameSession resigned_dark = DarkGameSession::deserialize(resigned_fixture);
        const auto resigned_replay_path = storage::saveDarkReplay(resigned_dark, "dark_selftest_resign_replay");
        std::ifstream resigned_replay_input(resigned_replay_path, std::ios::binary);
        std::ostringstream resigned_replay_text;
        resigned_replay_text << resigned_replay_input.rdbuf();
        require(
            resigned_replay_text.str().find("[Result \"0-1\"]") != std::string::npos,
            "Dark CDC export should record the winner for resignation instead of '*'.");
        std::error_code ignored;
        std::filesystem::remove(resigned_replay_path, ignored);
    }

    {
        const std::string invalid_history_fixture =
            "VERSION=1\n"
            "GAME=DarkChess\n"
            "CURRENT_SEAT=Player1\n"
            "RESULT=Ongoing\n"
            "PLAYER1_NAME=One\n"
            "PLAYER2_NAME=Two\n"
            "PLAYER1_COLOR=Red\n"
            "PLAYER2_COLOR=Black\n"
            "MOVE_TIME=60\n"
            "ALLOW_UNDO=1\n"
            "SHOW_LEGAL=1\n"
            "AI_ENABLED=0\n"
            "AI_SEAT=Player2\n"
            "USE_EASYX=0\n"
            "PLAYER1_REMAINING_MS=60000\n"
            "PLAYER2_REMAINING_MS=60000\n"
            "QUIET_PLIES=0\n"
            "BOARD_BEGIN\n"
            "rK+ __ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ bK+\n"
            "BOARD_END\n"
            "HISTORY_BEGIN\n"
            "X|Player1|a0|a1|||\n"
            "HISTORY_END\n";
        requireThrows<StorageError>(
            [&]()
            {
                (void)DarkGameSession::deserialize(invalid_history_fixture);
            },
            "Dark save loading should reject unknown history action types.");

        const auto invalid_cdc_path = std::filesystem::temp_directory_path() / "invalid_dark_reveal_selftest.pgn";
        {
            std::ofstream invalid_cdc_output(invalid_cdc_path, std::ios::binary);
            invalid_cdc_output
                << "[Game \"Chinese Dark Chess\"]\n"
                << "[GameKind \"DarkChess\"]\n"
                << "[Format \"CDC\"]\n"
                << "[InitialPrivateGrid \"rK-/bK-/__/__/__/__/__/__\"]\n"
                << "[Result \"*\"]\n\n"
                << "1. a0(xK) *\n";
        }
        requireThrows<StorageError>(
            [&]()
            {
                (void)storage::loadDarkReplay(invalid_cdc_path.string());
            },
            "Dark CDC loading should reject invalid revealed piece tokens.");
        std::error_code ignored;
        std::filesystem::remove(invalid_cdc_path, ignored);
    }

    {
        GameSettings dark_network_settings;
        dark_network_settings.game_kind = GameKind::DarkChess;
        dark_network_settings.ai_enabled = false;
        PlayerInfo dark_network_players;
        dark_network_players.red_name = "Seat One";
        dark_network_players.black_name = "Seat Two";

        const std::string wire = serializeHandshake(dark_network_settings, dark_network_players, Side::Red);
        GameSettings parsed_settings;
        PlayerInfo parsed_players;
        Side parsed_first_turn = Side::Black;
        parseHandshake(wire, parsed_settings, parsed_players, parsed_first_turn);
        require(parsed_settings.game_kind == GameKind::DarkChess, "LAN handshake should preserve dark chess game kind.");
        require(parsed_players.red_name == dark_network_players.red_name, "LAN handshake should preserve player 1 name for dark chess.");

        DarkGameSession private_dark(4242u);
        const std::string private_state = private_dark.serialize();
        const std::string public_state = private_dark.serializePublic();
        require(private_state.find('-') != std::string::npos, "Private dark state should keep hidden identities for local save.");
        require(public_state.find("xx") != std::string::npos, "Public dark state should show hidden pieces only as xx.");
        require(public_state.find("rK-") == std::string::npos && public_state.find("bK-") == std::string::npos,
            "Public dark state should not leak hidden king identities.");
    }

    {
        DarkGameSession dark_ai_session(13579u);
        DarkSearchEngine dark_search;
        const auto action = dark_search.chooseAction(dark_ai_session);
        require(action.has_value(), "Dark chess AI should return an opening action.");
        const auto legal_actions = dark_ai_session.legalActionsForCurrentSeat();
        require(
            std::any_of(
                legal_actions.begin(),
                legal_actions.end(),
                [&](const DarkAction& legal)
                {
                    return legal.type == action->type && legal.from == action->from && legal.to == action->to && legal.seat == action->seat;
                }),
            "Dark chess AI should return a legal action from the public action list.");
    }

    {
        const std::string quiet_fixture =
            "VERSION=1\n"
            "GAME=DarkChess\n"
            "CURRENT_SEAT=Player1\n"
            "RESULT=Ongoing\n"
            "PLAYER1_NAME=One\n"
            "PLAYER2_NAME=Two\n"
            "PLAYER1_COLOR=Red\n"
            "PLAYER2_COLOR=Black\n"
            "MOVE_TIME=60\n"
            "ALLOW_UNDO=1\n"
            "SHOW_LEGAL=1\n"
            "AI_ENABLED=0\n"
            "AI_SEAT=Player2\n"
            "USE_EASYX=0\n"
            "PLAYER1_REMAINING_MS=60000\n"
            "PLAYER2_REMAINING_MS=60000\n"
            "QUIET_PLIES=49\n"
            "BOARD_BEGIN\n"
            "rR+ __ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ bR+\n"
            "BOARD_END\n"
            "HISTORY_BEGIN\n"
            "HISTORY_END\n";
        DarkGameSession quiet_dark = DarkGameSession::deserialize(quiet_fixture);
        quiet_dark.submitAction(DarkAction::move({ 0, 0 }, { 0, 1 }, DarkSeat::Player1));
        require(quiet_dark.result() == GameResult::Draw, "Dark chess should draw after 50 quiet plies without flip or capture.");
    }

    {
        const std::string repetition_fixture =
            "VERSION=1\n"
            "GAME=DarkChess\n"
            "CURRENT_SEAT=Player1\n"
            "RESULT=Ongoing\n"
            "PLAYER1_NAME=One\n"
            "PLAYER2_NAME=Two\n"
            "PLAYER1_COLOR=Red\n"
            "PLAYER2_COLOR=Black\n"
            "MOVE_TIME=60\n"
            "ALLOW_UNDO=1\n"
            "SHOW_LEGAL=1\n"
            "AI_ENABLED=0\n"
            "AI_SEAT=Player2\n"
            "USE_EASYX=0\n"
            "PLAYER1_REMAINING_MS=60000\n"
            "PLAYER2_REMAINING_MS=60000\n"
            "QUIET_PLIES=0\n"
            "BOARD_BEGIN\n"
            "rR+ __ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ __\n"
            "__ __ __ __ __ __ __ bR+\n"
            "BOARD_END\n"
            "HISTORY_BEGIN\n"
            "HISTORY_END\n";
        DarkGameSession repetition_dark = DarkGameSession::deserialize(repetition_fixture);
        for (int cycle = 0; cycle < 2 && !repetition_dark.gameOver(); ++cycle)
        {
            repetition_dark.submitAction(DarkAction::move({ 0, 0 }, { 0, 1 }, DarkSeat::Player1));
            repetition_dark.submitAction(DarkAction::move({ 3, 7 }, { 3, 6 }, DarkSeat::Player2));
            repetition_dark.submitAction(DarkAction::move({ 0, 1 }, { 0, 0 }, DarkSeat::Player1));
            repetition_dark.submitAction(DarkAction::move({ 3, 6 }, { 3, 7 }, DarkSeat::Player2));
        }
        require(repetition_dark.result() == GameResult::Draw, "Dark chess should draw on third repeated public/private position with turn.");

        DarkGameSession repeat_then_save = DarkGameSession::deserialize(repetition_fixture);
        repeat_then_save.submitAction(DarkAction::move({ 0, 0 }, { 0, 1 }, DarkSeat::Player1));
        repeat_then_save.submitAction(DarkAction::move({ 3, 7 }, { 3, 6 }, DarkSeat::Player2));
        repeat_then_save.submitAction(DarkAction::move({ 0, 1 }, { 0, 0 }, DarkSeat::Player1));
        repeat_then_save.submitAction(DarkAction::move({ 3, 6 }, { 3, 7 }, DarkSeat::Player2));
        require(repeat_then_save.result() == GameResult::Ongoing, "Dark repetition should not draw on the second occurrence.");

        DarkGameSession loaded_repeat = DarkGameSession::deserialize(repeat_then_save.serialize());
        loaded_repeat.submitAction(DarkAction::move({ 0, 0 }, { 0, 1 }, DarkSeat::Player1));
        loaded_repeat.submitAction(DarkAction::move({ 3, 7 }, { 3, 6 }, DarkSeat::Player2));
        loaded_repeat.submitAction(DarkAction::move({ 0, 1 }, { 0, 0 }, DarkSeat::Player1));
        loaded_repeat.submitAction(DarkAction::move({ 3, 6 }, { 3, 7 }, DarkSeat::Player2));
        require(loaded_repeat.result() == GameResult::Draw, "Dark save/load should preserve repetition history for third-occurrence draws.");
    }

    {
        const auto original_path = std::filesystem::current_path();
        const auto temp_path = std::filesystem::temp_directory_path() /
            ("dark_leaderboard_selftest_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temp_path);

        try
        {
            std::filesystem::current_path(temp_path);
            PlayerInfo dark_leaderboard_players;
            dark_leaderboard_players.red_name = "Dark,One";
            dark_leaderboard_players.black_name = "Dark \"Two\"";
            DarkGameSession dark_leaderboard(GameSettings{}, dark_leaderboard_players, 777u);
            dark_leaderboard.resign(DarkSeat::Player1);
            storage::appendDarkLeaderboard(dark_leaderboard);

            const auto leaderboard_lines = storage::readLeaderboardLines();
            require(leaderboard_lines.size() == 2, "Dark leaderboard should contain a header and one row.");
            require(
                leaderboard_lines[1].find("DarkChess") != std::string::npos &&
                    leaderboard_lines[1].find("\"Dark,One\"") != std::string::npos &&
                    leaderboard_lines[1].find("Dark \"\"Two\"\"") != std::string::npos,
                "Dark leaderboard CSV should record dark mode and quote names: " + leaderboard_lines[1]);

            std::filesystem::current_path(original_path);
            std::filesystem::remove_all(temp_path);
        }
        catch (...)
        {
            std::filesystem::current_path(original_path);
            std::filesystem::remove_all(temp_path);
            throw;
        }
    }

    GameSession undo_session(GameSettings{});
    Move red_pawn_push;
    red_pawn_push.from = { 6, 0 };
    red_pawn_push.to = { 5, 0 };
    undo_session.submitMove(red_pawn_push);

    Move black_pawn_push;
    black_pawn_push.from = { 3, 0 };
    black_pawn_push.to = { 4, 0 };
    undo_session.submitMove(black_pawn_push);

    require(undo_session.undoLastPlies(2) == 2, "Undo should be able to revert a full human-vs-AI turn.");
    require(undo_session.history().empty(), "Undoing both plies should clear the opening history.");
    require(undo_session.currentSide() == Side::Red, "After undoing a full turn, Red should move again.");
    require(
        undo_session.board().pieceAt({ 6, 0 }) != nullptr &&
            undo_session.board().pieceAt({ 6, 0 })->side() == Side::Red &&
            undo_session.board().pieceAt({ 6, 0 })->type() == PieceType::Pawn,
        "The red pawn should return to its starting square after undo.");
    require(
        undo_session.board().pieceAt({ 3, 0 }) != nullptr &&
            undo_session.board().pieceAt({ 3, 0 })->side() == Side::Black &&
            undo_session.board().pieceAt({ 3, 0 })->type() == PieceType::Pawn,
        "The black pawn should return to its starting square after undo.");

    {
        GameSession replay_session(GameSettings{});
        Move red_pawn_push;
        red_pawn_push.from = { 6, 0 };
        red_pawn_push.to = { 5, 0 };
        replay_session.submitMove(red_pawn_push);

        Move black_pawn_push;
        black_pawn_push.from = { 3, 0 };
        black_pawn_push.to = { 4, 0 };
        replay_session.submitMove(black_pawn_push);

        const auto replay_path = storage::saveReplay(replay_session, "selftest_pgn_export");
        const auto second_replay_path = storage::saveReplay(replay_session, "selftest_pgn_export");
        const std::filesystem::path expected_replay_dir{ L"D:\\visualstudio\\\u4E2D\u56FD\u8C61\u68CB\\pgn" };

        const auto cleanup_replays = [&]()
        {
            std::error_code ignored;
            std::filesystem::remove(replay_path, ignored);
            std::filesystem::remove(second_replay_path, ignored);
        };

        require(replay_path.extension() == ".pgn", "Replay export should now use the .pgn extension.");
        require(std::filesystem::exists(replay_path), "PGN replay export file should exist.");
        require(std::filesystem::exists(second_replay_path), "Second PGN replay export file should exist.");
        if (replay_path.parent_path() != expected_replay_dir || second_replay_path.parent_path() != expected_replay_dir)
        {
            cleanup_replays();
            throw std::runtime_error("Replay export should use the fixed pgn directory.");
        }
        if (replay_path == second_replay_path)
        {
            cleanup_replays();
            throw std::runtime_error("Replay export should create unique timestamped PGN files.");
        }

        std::string pgn;
        {
            std::ifstream input(replay_path, std::ios::binary);
            std::ostringstream buffer;
            buffer << input.rdbuf();
            pgn = buffer.str();
        }
        require(pgn.find("[Game \"Chinese Chess\"]") != std::string::npos, "Replay export should contain PGN headers.");
        require(pgn.find("[Format \"WXF\"]") != std::string::npos, "Standard-board PGN export should use WXF notation.");
        require(pgn.find("1.") != std::string::npos, "PGN export should contain move numbers.");
        require(
            pgn.find("P9+1") != std::string::npos && pgn.find("p1+1") != std::string::npos,
            "PGN export should contain WXF movetext.");

        const auto imported_replay = storage::loadReplay(replay_path.string());
        require(imported_replay.settings.board_mode == BoardMode::Standard9x10, "Imported PGN should preserve board mode.");
        require(imported_replay.moves.size() == 2, "Imported PGN should restore the full move list.");
        require(
            imported_replay.moves[0].from == Position<int>{ 6, 0 } && imported_replay.moves[0].to == Position<int>{ 5, 0 },
            "Imported PGN should preserve the first move.");
        require(
            imported_replay.moves[1].from == Position<int>{ 3, 0 } && imported_replay.moves[1].to == Position<int>{ 4, 0 },
            "Imported PGN should preserve the second move.");
        cleanup_replays();
    }

    output << "Engine self-tests passed.";
    return output.str();
}

} // namespace xiangqi::tests
