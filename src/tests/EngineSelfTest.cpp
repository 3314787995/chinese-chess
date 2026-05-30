#include "tests/EngineSelfTest.h"

#include "ai/SearchEngine.h"
#include "app/GameSession.h"
#include "common/Repetition.h"
#include "engine/ChessEngine.h"
#include "net/NetworkSession.h"
#include "storage/Storage.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

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
