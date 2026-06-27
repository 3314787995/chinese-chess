#include "ui_easyx/EasyXApp.h"

#include "ai/SearchEngine.h"
#include "app/MoveParser.h"
#include "darkchess/DarkChess.h"
#include "storage/Storage.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <commdlg.h>

#if defined(__has_include)
#if __has_include(<graphics.h>)
#define XIANGQI_HAS_EASYX 1
#include <graphics.h>
#include <conio.h>
#else
#define XIANGQI_HAS_EASYX 0
#endif
#else
#define XIANGQI_HAS_EASYX 0
#endif

namespace xiangqi
{

namespace
{

#if XIANGQI_HAS_EASYX
constexpr int kSharedEasyXWidth = 980;
constexpr int kSharedEasyXHeight = 840;
bool g_easyx_window_initialized = false;

HWND showSharedEasyXWindow()
{
    HWND window = nullptr;
    if (g_easyx_window_initialized)
    {
        window = GetHWnd();
    }

    if (!g_easyx_window_initialized || window == nullptr || !IsWindow(window))
    {
        window = initgraph(kSharedEasyXWidth, kSharedEasyXHeight);
        g_easyx_window_initialized = true;
    }
    else
    {
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
    }

    flushmessage();
    FlushMouseMsgBuffer();
    return window;
}

void hideSharedEasyXWindow()
{
    flushmessage();
    FlushMouseMsgBuffer();

    HWND window = GetHWnd();
    if (window != nullptr && IsWindow(window))
    {
        ShowWindow(window, SW_HIDE);
    }
}
#endif

enum class UiScreen
{
    Menu,
    Leaderboard,
    Playing,
};

std::wstring utf8ToWide(const std::string& text)
{
    if (text.empty())
    {
        return {};
    }

    const int required = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0)
    {
        return std::wstring(text.begin(), text.end());
    }

    std::wstring result(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), required);
    return result;
}

std::wstring coordWide(const Position<int> position, const BoardConfig& config)
{
    std::wstring text;
    text.push_back(static_cast<wchar_t>(config.coordinate_files[static_cast<size_t>(position.col)]));
    text.push_back(static_cast<wchar_t>(L'0' + position.row));
    return text;
}

wchar_t pieceFace(const ChessPiece& piece)
{
    switch (piece.type())
    {
    case PieceType::King:
        return piece.side() == Side::Red ? L'\u5E05' : L'\u5C06';
    case PieceType::Advisor:
        return piece.side() == Side::Red ? L'\u4ED5' : L'\u58EB';
    case PieceType::Elephant:
        return piece.side() == Side::Red ? L'\u76F8' : L'\u8C61';
    case PieceType::Knight:
        return L'\u9A6C';
    case PieceType::Rook:
        return L'\u8F66';
    case PieceType::Cannon:
        return L'\u70AE';
    case PieceType::Pawn:
        return piece.side() == Side::Red ? L'\u5175' : L'\u5352';
    default:
        return L'?';
    }
}

wchar_t darkPieceFace(const DarkPiece& piece)
{
    switch (piece.type)
    {
    case PieceType::King:
        return piece.side == Side::Red ? L'\u5E05' : L'\u5C06';
    case PieceType::Advisor:
        return piece.side == Side::Red ? L'\u4ED5' : L'\u58EB';
    case PieceType::Elephant:
        return piece.side == Side::Red ? L'\u76F8' : L'\u8C61';
    case PieceType::Knight:
        return L'\u9A6C';
    case PieceType::Rook:
        return L'\u8F66';
    case PieceType::Cannon:
        return L'\u70AE';
    case PieceType::Pawn:
        return piece.side == Side::Red ? L'\u5175' : L'\u5352';
    default:
        return L'?';
    }
}

std::string darkStateLine(const DarkGameSession& session)
{
    return std::string("DARK_STATE|") + escapeProtocolField(session.serializePublic());
}

std::string escapeDarkStateField(const std::string& value)
{
    std::string escaped;
    for (const char ch : value)
    {
        switch (ch)
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '=':
            escaped += "\\e";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

bool replayFileLooksDark(const std::filesystem::path& path)
{
    try
    {
        const auto lines = storage::loadReplayLines(path);
        for (const auto& line : lines)
        {
            if (line.find("[Format \"CDC\"]") != std::string::npos ||
                line.find("[GameKind \"DarkChess\"]") != std::string::npos)
            {
                return true;
            }
        }
    }
    catch (...)
    {
    }
    return false;
}

DarkGameSession makeDarkReplaySession(const storage::DarkReplayRecord& record)
{
    GameSettings settings = record.settings;
    settings.game_kind = GameKind::DarkChess;
    settings.ai_enabled = false;
    settings.use_easyx = true;

    DarkGameSession session(settings, record.players, 1);
    if (!record.initial_private_grid.empty())
    {
        std::ostringstream fixture;
        fixture << "VERSION=1\n"
                << "GAME=DarkChess\n"
                << "CURRENT_SEAT=Player1\n"
                << "RESULT=Ongoing\n"
                << "PLAYER1_NAME=" << escapeDarkStateField(record.players.red_name) << "\n"
                << "PLAYER2_NAME=" << escapeDarkStateField(record.players.black_name) << "\n"
                << "PLAYER1_COLOR=Unknown\n"
                << "PLAYER2_COLOR=Unknown\n"
                << "MOVE_TIME=" << settings.move_time_limit_seconds << "\n"
                << "ALLOW_UNDO=0\n"
                << "SHOW_LEGAL=1\n"
                << "AI_ENABLED=0\n"
                << "AI_SEAT=Player2\n"
                << "USE_EASYX=1\n"
                << "PLAYER1_REMAINING_MS=60000\n"
                << "PLAYER2_REMAINING_MS=60000\n"
                << "QUIET_PLIES=0\n"
                << "INITIAL_BOARD_BEGIN\n" << record.initial_private_grid << "\nINITIAL_BOARD_END\n"
                << "BOARD_BEGIN\n" << record.initial_private_grid << "\nBOARD_END\n"
                << "HISTORY_BEGIN\n"
                << "HISTORY_END\n";
        session = DarkGameSession::deserialize(fixture.str());
    }

    for (auto action : record.actions)
    {
        session.submitAction(action);
    }
    return session;
}

bool insideRect(const RECT& rect, const int x, const int y)
{
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

std::vector<std::string> splitProtocol(const std::string& line)
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
    return fields;
}

struct PendingAuxConnection
{
    std::string request;
    NetworkSession::AcceptedConnection connection;
};

std::string coordText(const Position<int> position, const BoardConfig& config)
{
    std::string text;
    text += config.coordinate_files[static_cast<size_t>(position.col)];
    text += static_cast<char>('0' + position.row);
    return text;
}

std::string moveLine(const std::string& tag, const Move& move, const BoardConfig& config)
{
    return tag + "|" + coordText(move.from, config) + "|" + coordText(move.to, config);
}

Side parseWireSide(const std::string& text)
{
    return text == "Black" ? Side::Black : Side::Red;
}

struct ResultBanner
{
    std::wstring title;
    std::wstring subtitle;
    COLORREF accent{ RGB(40, 40, 40) };
};

struct AiTaskState
{
    unsigned long long generation{ 0 };
    std::atomic_bool ready{ false };
    std::optional<Move> move;
    std::string error;
    std::mutex mutex;
};

constexpr wchar_t kCloseRequestedProp[] = L"XiangqiCloseRequested";
constexpr wchar_t kOriginalWndProcProp[] = L"XiangqiOriginalWndProc";

LRESULT CALLBACK easyxCloseAwareWindowProc(HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam)
{
    if (message == WM_CLOSE || (message == WM_SYSCOMMAND && (wparam & 0xFFF0) == SC_CLOSE))
    {
        if (auto* close_requested = reinterpret_cast<std::atomic_bool*>(GetPropW(window, kCloseRequestedProp)))
        {
            close_requested->store(true);
        }
        return 0;
    }

    if (WNDPROC original = reinterpret_cast<WNDPROC>(GetPropW(window, kOriginalWndProcProp)))
    {
        return CallWindowProcW(original, window, message, wparam, lparam);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

#if XIANGQI_HAS_EASYX
struct EasyXCloseState
{
    HWND window{ nullptr };
    std::atomic_bool close_requested{ false };
    WNDPROC original_window_proc{ nullptr };
};

void installEasyXCloseHook(EasyXCloseState& state, HWND window)
{
    state.window = window;
    state.close_requested = false;
    state.original_window_proc = nullptr;
    if (window == nullptr)
    {
        return;
    }

    state.original_window_proc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
    SetPropW(window, kCloseRequestedProp, &state.close_requested);
    SetPropW(window, kOriginalWndProcProp, state.original_window_proc);
    SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(easyxCloseAwareWindowProc));
}

bool shouldCloseEasyXWindow(EasyXCloseState& state)
{
    if (state.window != nullptr && IsWindow(state.window))
    {
        MSG close_message{};
        while (PeekMessageW(&close_message, state.window, WM_CLOSE, WM_CLOSE, PM_REMOVE))
        {
            state.close_requested = true;
        }

        while (PeekMessageW(&close_message, state.window, WM_SYSCOMMAND, WM_SYSCOMMAND, PM_NOREMOVE))
        {
            if ((close_message.wParam & 0xFFF0) != SC_CLOSE)
            {
                break;
            }
            PeekMessageW(&close_message, state.window, WM_SYSCOMMAND, WM_SYSCOMMAND, PM_REMOVE);
            state.close_requested = true;
        }

        while (PeekMessageW(&close_message, state.window, WM_NCLBUTTONDOWN, WM_NCLBUTTONDOWN, PM_NOREMOVE))
        {
            if (close_message.wParam != HTCLOSE)
            {
                break;
            }
            PeekMessageW(&close_message, state.window, WM_NCLBUTTONDOWN, WM_NCLBUTTONDOWN, PM_REMOVE);
            state.close_requested = true;
        }
    }
    return state.close_requested.load() || (GetAsyncKeyState(VK_ESCAPE) & 0x0001) != 0;
}

void restoreEasyXCloseHook(EasyXCloseState& state)
{
    if (state.window != nullptr && IsWindow(state.window))
    {
        if (state.original_window_proc != nullptr)
        {
            SetWindowLongPtrW(state.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(state.original_window_proc));
        }
        RemovePropW(state.window, kCloseRequestedProp);
        RemovePropW(state.window, kOriginalWndProcProp);
    }
}
#endif

std::optional<std::filesystem::path> browseForPgnFile()
{
    storage::ensureDirectories();
    wchar_t file_buffer[MAX_PATH]{};
    const std::wstring initial_dir = storage::replayDirectory().wstring();
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = nullptr;
    dialog.lpstrFilter = L"PGN Files (*.pgn)\0*.pgn\0All Files (*.*)\0*.*\0";
    dialog.lpstrFile = file_buffer;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    dialog.lpstrDefExt = L"pgn";
    dialog.lpstrInitialDir = initial_dir.c_str();

    if (GetOpenFileNameW(&dialog) == 0)
    {
        return std::nullopt;
    }
    return std::filesystem::path(file_buffer);
}

ResultBanner makeResultBanner(const GameSession& session)
{
    ResultBanner banner;
    switch (session.result())
    {
    case GameResult::RedWin:
        banner.title = L"RED WINS";
        banner.subtitle = L"Black has no legal escape.";
        banner.accent = RGB(196, 34, 34);
        break;
    case GameResult::BlackWin:
        banner.title = L"BLACK WINS";
        banner.subtitle = L"Red has no legal escape.";
        banner.accent = RGB(34, 34, 34);
        break;
    case GameResult::Draw:
        banner.title = L"DRAW";
        banner.subtitle = L"Repeated position.";
        banner.accent = RGB(82, 82, 82);
        break;
    case GameResult::Timeout:
        if (session.currentSide() == Side::Red)
        {
            banner.title = L"BLACK WINS";
            banner.subtitle = L"Red ran out of time.";
            banner.accent = RGB(34, 34, 34);
        }
        else
        {
            banner.title = L"RED WINS";
            banner.subtitle = L"Black ran out of time.";
            banner.accent = RGB(196, 34, 34);
        }
        break;
    case GameResult::Resign:
        if (session.currentSide() == Side::Red)
        {
            banner.title = L"BLACK WINS";
            banner.subtitle = L"Red resigned.";
            banner.accent = RGB(34, 34, 34);
        }
        else
        {
            banner.title = L"RED WINS";
            banner.subtitle = L"Black resigned.";
            banner.accent = RGB(196, 34, 34);
        }
        break;
    default:
        banner.title = L"GAME OVER";
        banner.subtitle = utf8ToWide(session.resultText());
        banner.accent = RGB(60, 60, 60);
        break;
    }
    return banner;
}

int runInternal(
    GameSettings settings,
    PlayerInfo players,
    std::unique_ptr<NetworkSession> network,
    const std::optional<Side> local_side_override,
    const std::optional<storage::ReplayRecord>& startup_replay,
    SpectatorConnections preconnected_spectators = {})
{
#if XIANGQI_HAS_EASYX
    GameSettings current_settings = std::move(settings);
    PlayerInfo current_players = std::move(players);
    const bool network_enabled = network != nullptr;
    const Side local_side = local_side_override.value_or(Side::Red);
    if (network_enabled)
    {
        current_settings.ai_enabled = false;
    }

    GameSession session(current_settings, current_players);
    MoveParser parser;
    SearchEngine search(current_settings.ai_depth);

    constexpr int cell = 56;
    constexpr int margin = 40;
    constexpr int side_panel = 320;
    constexpr int max_cols = 11;
    constexpr int width = margin * 2 + cell * (max_cols - 1) + side_panel;

    HWND easyx_window = showSharedEasyXWindow();
    std::atomic_bool close_requested{ false };
    WNDPROC original_window_proc = nullptr;
    if (easyx_window != nullptr)
    {
        original_window_proc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(easyx_window, GWLP_WNDPROC));
        SetPropW(easyx_window, kCloseRequestedProp, &close_requested);
        SetPropW(easyx_window, kOriginalWndProcProp, original_window_proc);
        SetWindowLongPtrW(easyx_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(easyxCloseAwareWindowProc));
    }

    auto shouldCloseWindow = [&]()
    {
        if (easyx_window != nullptr && IsWindow(easyx_window))
        {
            MSG close_message{};
            while (PeekMessageW(&close_message, easyx_window, WM_CLOSE, WM_CLOSE, PM_REMOVE))
            {
                close_requested = true;
            }

            while (PeekMessageW(&close_message, easyx_window, WM_SYSCOMMAND, WM_SYSCOMMAND, PM_NOREMOVE))
            {
                if ((close_message.wParam & 0xFFF0) != SC_CLOSE)
                {
                    break;
                }
                PeekMessageW(&close_message, easyx_window, WM_SYSCOMMAND, WM_SYSCOMMAND, PM_REMOVE);
                close_requested = true;
            }

            while (PeekMessageW(&close_message, easyx_window, WM_NCLBUTTONDOWN, WM_NCLBUTTONDOWN, PM_NOREMOVE))
            {
                if (close_message.wParam != HTCLOSE)
                {
                    break;
                }
                PeekMessageW(&close_message, easyx_window, WM_NCLBUTTONDOWN, WM_NCLBUTTONDOWN, PM_REMOVE);
                close_requested = true;
            }
        }
        return close_requested.load() || (GetAsyncKeyState(VK_ESCAPE) & 0x0001) != 0;
    };

    auto restoreWindowProc = [&]()
    {
        if (easyx_window != nullptr && IsWindow(easyx_window))
        {
            if (original_window_proc != nullptr)
            {
                SetWindowLongPtrW(easyx_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_window_proc));
            }
            RemovePropW(easyx_window, kCloseRequestedProp);
            RemovePropW(easyx_window, kOriginalWndProcProp);
        }
    };

    BeginBatchDraw();
    setbkcolor(RGB(247, 223, 180));
    setbkmode(TRANSPARENT);
    cleardevice();

    UiScreen screen = UiScreen::Playing;
    std::optional<Position<int>> selected;
    std::vector<Move> legal_from_selected;
    std::optional<Move> hinted_move;
    bool leaderboard_recorded = false;
    bool needs_redraw = true;
    bool awaiting_remote_reply = false;
    bool replay_locked = false;
    int last_red_seconds = session.remainingSeconds(Side::Red);
    int last_black_seconds = session.remainingSeconds(Side::Black);
    int last_result_pulse = -1;
    std::wstring status_text = network_enabled
        ? (local_side == Side::Red ? L"LAN host connected. You play Red." : L"LAN client connected. You play Black.")
        : (current_settings.ai_enabled ? L"Human vs AI mode. Click a piece to move." : L"Local two-player mode.");

    std::mutex inbox_mutex;
    std::vector<std::string> inbox_lines;

    std::atomic_bool receiver_active{ false };
    std::atomic_bool stop_receiver{ false };
    std::thread receiver_thread;
    std::atomic_bool network_lost{ false };
    std::mutex aux_mutex;
    std::vector<PendingAuxConnection> pending_aux_connections;
    std::vector<NetworkSession::AcceptedConnection> spectator_connections = std::move(preconnected_spectators);
    std::atomic_bool stop_aux_acceptor{ false };
    std::atomic_int spectator_count{ static_cast<int>(spectator_connections.size()) };
    std::thread aux_acceptor_thread;
    std::thread room_advertiser_thread;
    std::shared_ptr<AiTaskState> ai_task;
    unsigned long long ai_generation = 0;

    const RECT start_button{ width / 2 - 130, 220, width / 2 + 130, 272 };
    const RECT load_button{ width / 2 - 130, 292, width / 2 + 130, 344 };
    const RECT import_replay_button{ width / 2 - 130, 364, width / 2 + 130, 416 };
    const RECT leaderboard_button{ width / 2 - 130, 436, width / 2 + 130, 488 };
    const RECT menu_exit_button{ width / 2 - 130, 508, width / 2 + 130, 560 };
    const RECT leaderboard_back_button{ width / 2 - 130, 742, width / 2 + 130, 794 };

    const RECT status_rect{ width - side_panel + 20, 84, width - 24, 130 };
    const RECT undo_button{ width - side_panel + 20, 150, width - 40, 190 };
    const RECT hint_button{ width - side_panel + 20, 200, width - 40, 240 };
    const RECT save_button{ width - side_panel + 20, 250, width - 40, 290 };
    const RECT replay_button{ width - side_panel + 20, 300, width - 40, 340 };
    const RECT restart_button{ width - side_panel + 20, 350, width - 40, 390 };
    const RECT menu_button{ width - side_panel + 20, 400, width - 40, 440 };
    const RECT exit_button{ width - side_panel + 20, 450, width - 40, 490 };
    auto clearHint = [&]()
    {
        hinted_move.reset();
    };

    auto resetSelection = [&]()
    {
        selected.reset();
        legal_from_selected.clear();
        clearHint();
        needs_redraw = true;
    };

    auto selectPiece = [&](const Position<int> position)
    {
        clearHint();
        selected = position;
        legal_from_selected = session.legalMovesFrom(position);
        needs_redraw = true;
    };

    auto showHintMove = [&](const Move& move)
    {
        hinted_move = move;
        selected = move.from;
        legal_from_selected = session.legalMovesFrom(move.from);
        needs_redraw = true;
    };

    auto setStatus = [&](const std::wstring& text)
    {
        status_text = text;
        needs_redraw = true;
    };

    auto setStatusUtf8 = [&](const std::string& text)
    {
        setStatus(utf8ToWide(text));
    };

    auto invalidateAiTask = [&]()
    {
        ++ai_generation;
        ai_task.reset();
    };

    auto aiThinking = [&]() -> bool
    {
        return ai_task != nullptr && !ai_task->ready.load();
    };

    auto refreshClocks = [&]()
    {
        last_red_seconds = session.remainingSeconds(Side::Red);
        last_black_seconds = session.remainingSeconds(Side::Black);
    };

    auto stateLine = [&]()
    {
        return std::string("STATE|") + escapeProtocolField(session.serialize());
    };

    // 观战端不参与裁决，只接收主机序列化后的完整局面。
    // 这样所有走子合法性仍由主机控制，观战人数变化不会影响双方对局状态。
    auto broadcastStateToSpectators = [&]()
    {
        if (!network_enabled || local_side != Side::Red)
        {
            return;
        }

        const std::string state = stateLine();
        for (auto it = spectator_connections.begin(); it != spectator_connections.end();)
        {
            try
            {
                NetworkSession::sendLine(*it, state);
                ++it;
            }
            catch (...)
            {
                NetworkSession::closeConnection(*it);
                it = spectator_connections.erase(it);
            }
        }
        spectator_count = static_cast<int>(spectator_connections.size());
    };

    auto initializePreconnectedSpectators = [&]()
    {
        if (!network_enabled || local_side != Side::Red)
        {
            return;
        }

        for (auto it = spectator_connections.begin(); it != spectator_connections.end();)
        {
            try
            {
                NetworkSession::sendLine(*it, serializeHandshake(current_settings, current_players, session.currentSide()));
                NetworkSession::sendLine(*it, stateLine());
                ++it;
            }
            catch (...)
            {
                NetworkSession::closeConnection(*it);
                it = spectator_connections.erase(it);
            }
        }
        spectator_count = static_cast<int>(spectator_connections.size());
    };

    auto shouldExportPgn = [&]()
    {
        return !network_enabled || local_side == Side::Red;
    };

    auto saveCurrentGame = [&](const std::string& name_hint) -> std::optional<std::filesystem::path>
    {
        storage::saveGame(session, name_hint);
        if (!shouldExportPgn())
        {
            return std::nullopt;
        }
        return storage::saveReplay(session, name_hint);
    };

    auto startFreshGame = [&]()
    {
        invalidateAiTask();
        session = GameSession(current_settings, current_players);
        search = SearchEngine(current_settings.ai_depth);
        leaderboard_recorded = false;
        awaiting_remote_reply = false;
        replay_locked = false;
        refreshClocks();
        resetSelection();
        screen = UiScreen::Playing;
        setStatus(current_settings.ai_enabled ? L"Human vs AI mode. Click a piece to move." : L"Local two-player mode.");
    };

    auto boardLeft = [&](const Board& board) -> int
    {
        return margin + ((max_cols - board.config().cols) * cell) / 2;
    };

    auto boardTop = [&]() -> int
    {
        return 92;
    };

    auto cellCenter = [&](const Board& board, const Position<int> p) -> POINT
    {
        POINT point{};
        point.x = boardLeft(board) + p.col * cell;
        point.y = boardTop() + p.row * cell;
        return point;
    };

    auto hitCell = [&](const GameSession& view_session, const int mouse_x, const int mouse_y) -> std::optional<Position<int>>
    {
        const Board& board = view_session.board();
        const int col = static_cast<int>((mouse_x - boardLeft(board) + cell / 2) / cell);
        const int row = static_cast<int>((mouse_y - boardTop() + cell / 2) / cell);
        Position<int> position{ row, col };
        if (board.isInside(position))
        {
            const POINT center = cellCenter(board, position);
            if (std::abs(mouse_x - center.x) <= cell / 2 && std::abs(mouse_y - center.y) <= cell / 2)
            {
                return position;
            }
        }
        return std::nullopt;
    };

    auto drawButton = [&](const RECT& rect, const wchar_t* text, const bool enabled = true)
    {
        setlinecolor(enabled ? BLACK : RGB(140, 140, 140));
        setfillcolor(enabled ? RGB(238, 238, 238) : RGB(220, 220, 220));
        solidrectangle(rect.left, rect.top, rect.right, rect.bottom);
        rectangle(rect.left, rect.top, rect.right, rect.bottom);
        settextcolor(enabled ? BLACK : RGB(110, 110, 110));
        drawtext(text, const_cast<RECT*>(&rect), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        settextcolor(BLACK);
    };

    auto moveText = [&](const GameSession& view_session, const Move& move) -> std::wstring
    {
        if (!move.notation.empty())
        {
            return utf8ToWide(move.notation);
        }
        std::wstring text = coordWide(move.from, view_session.board().config());
        text += L" ";
        text += coordWide(move.to, view_session.board().config());
        return text;
    };

    auto subtitleText = [&]() -> std::wstring
    {
        if (replay_locked)
        {
            return L"PGN Replay";
        }
        if (network_enabled)
        {
            return local_side == Side::Red ? L"LAN Host" : L"LAN Client";
        }
        return current_settings.ai_enabled ? L"Human vs AI" : L"Local Two-Player";
    };

    auto drawMoveList = [&](const GameSession& view_session)
    {
        settextstyle(18, 0, L"Consolas");
        settextcolor(BLACK);
        outtextxy(width - side_panel + 20, 620, L"Moves");

        const auto& history = view_session.history();
        const size_t start = history.size() > 6 ? history.size() - 6 : 0;
        int y = 652;
        for (size_t index = start; index < history.size(); ++index)
        {
            const std::wstring text = std::to_wstring(index + 1) + L". " + moveText(view_session, history[index]);
            RECT line_rect{ width - side_panel + 20, y, width - 28, y + 24 };
            drawtext(text.c_str(), &line_rect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            y += 26;
        }
    };

    auto drawBoardFor = [&](
                            const GameSession& view_session,
                            const std::optional<Position<int>>& view_selected,
                            const std::vector<Move>& highlights,
                            const std::optional<Move>& view_hint)
    {
        cleardevice();
        const Board& board = view_session.board();
        const int left = boardLeft(board);
        const int top = boardTop();

        setlinecolor(BLACK);
        settextcolor(BLACK);
        settextstyle(34, 0, L"Microsoft YaHei");
        outtextxy(left, 22, L"Xiangqi");
        settextstyle(22, 0, L"Microsoft YaHei");
        const std::wstring subtitle = subtitleText();
        outtextxy(left + 170, 30, subtitle.c_str());

        for (int row = 0; row < board.config().rows; ++row)
        {
            const POINT start = cellCenter(board, { row, 0 });
            const POINT end = cellCenter(board, { row, board.config().cols - 1 });
            line(start.x, start.y, end.x, end.y);
        }
        for (int col = 0; col < board.config().cols; ++col)
        {
            const POINT top_point = cellCenter(board, { 0, col });
            const POINT river_top = cellCenter(board, { board.config().river_split_top_row, col });
            const POINT river_bottom = cellCenter(board, { board.config().river_split_top_row + 1, col });
            const POINT bottom = cellCenter(board, { board.config().rows - 1, col });
            line(top_point.x, top_point.y, river_top.x, river_top.y);
            line(river_bottom.x, river_bottom.y, bottom.x, bottom.y);
        }

        const int min_col = board.config().palace_min_col;
        const int max_col = board.config().palace_max_col;
        line(cellCenter(board, { 0, min_col }).x, cellCenter(board, { 0, min_col }).y, cellCenter(board, { 2, max_col }).x, cellCenter(board, { 2, max_col }).y);
        line(cellCenter(board, { 0, max_col }).x, cellCenter(board, { 0, max_col }).y, cellCenter(board, { 2, min_col }).x, cellCenter(board, { 2, min_col }).y);
        line(cellCenter(board, { 7, min_col }).x, cellCenter(board, { 7, min_col }).y, cellCenter(board, { 9, max_col }).x, cellCenter(board, { 9, max_col }).y);
        line(cellCenter(board, { 7, max_col }).x, cellCenter(board, { 7, max_col }).y, cellCenter(board, { 9, min_col }).x, cellCenter(board, { 9, min_col }).y);

        settextstyle(26, 0, L"Microsoft YaHei");
        outtextxy(left + cell * ((board.config().cols - 1) / 2) - 56, top + cell * 4 + 10, L"\u695A\u6CB3\u6C49\u754C");

        for (const auto& move : highlights)
        {
            const POINT center = cellCenter(board, move.to);
            setfillcolor(RGB(120, 220, 120));
            solidcircle(center.x, center.y, 8);
        }

        if (view_selected.has_value())
        {
            const POINT center = cellCenter(board, *view_selected);
            const bool hint_source = view_hint.has_value() && *view_selected == view_hint->from;
            setlinecolor(hint_source ? RGB(255, 140, 0) : RGB(255, 200, 0));
            circle(center.x, center.y, 25);
            if (hint_source)
            {
                circle(center.x, center.y, 28);
            }
            setlinecolor(BLACK);
        }

        settextstyle(24, 0, L"Microsoft YaHei");
        for (int row = 0; row < board.config().rows; ++row)
        {
            for (int col = 0; col < board.config().cols; ++col)
            {
                const Position<int> position{ row, col };
                const auto* piece = board.pieceAt(position);
                if (piece == nullptr)
                {
                    continue;
                }

                const POINT center = cellCenter(board, position);
                setfillcolor(piece->side() == Side::Red ? RGB(255, 245, 245) : RGB(240, 240, 240));
                solidcircle(center.x, center.y, 22);
                setlinecolor(piece->side() == Side::Red ? RGB(210, 0, 0) : RGB(10, 10, 10));
                circle(center.x, center.y, 22);
                settextcolor(piece->side() == Side::Red ? RGB(210, 0, 0) : RGB(10, 10, 10));
                wchar_t text[2]{ pieceFace(*piece), 0 };
                const int text_x = center.x - textwidth(text) / 2;
                const int text_y = center.y - textheight(text) / 2;
                outtextxy(text_x, text_y, text);
            }
        }

        if (view_hint.has_value())
        {
            const POINT center = cellCenter(board, view_hint->to);
            setlinecolor(RGB(48, 144, 255));
            circle(center.x, center.y, 27);
            circle(center.x, center.y, 31);
            setfillcolor(RGB(48, 144, 255));
            solidcircle(center.x, center.y, 6);
            if (board.pieceAt(view_hint->to) == nullptr)
            {
                circle(center.x, center.y, 14);
            }
            setlinecolor(BLACK);
        }

        settextcolor(BLACK);
        settextstyle(20, 0, L"Microsoft YaHei");
        const std::wstring top_line = replay_locked
            ? L"Mode: Imported PGN Replay"
            : (network_enabled
                   ? (local_side == Side::Red ? L"Network: Host / You: Red" : L"Network: Client / You: Black")
                   : (current_settings.ai_enabled ? L"Mode: Human vs AI" : L"Mode: Local Two-Player"));
        outtextxy(width - side_panel + 20, 20, top_line.c_str());
        const std::wstring mode_text = L"Board: " + utf8ToWide(toString(view_session.boardMode()));
        outtextxy(width - side_panel + 20, 48, mode_text.c_str());
        drawtext(status_text.c_str(), const_cast<RECT*>(&status_rect), DT_WORDBREAK | DT_NOPREFIX);

        const bool controls_enabled = (!network_enabled || !awaiting_remote_reply) && !replay_locked;
        drawButton(undo_button, L"Undo", controls_enabled);
        drawButton(hint_button, L"Hint", controls_enabled);
        drawButton(save_button, L"Save", !replay_locked);
        drawButton(replay_button, L"Replay", !network_enabled);
        drawButton(restart_button, L"Restart", !network_enabled);
        drawButton(menu_button, network_enabled ? L"Leave LAN" : L"Menu");
        drawButton(exit_button, L"Exit");

        const std::wstring red_time = L"Red time: " + std::to_wstring(view_session.remainingSeconds(Side::Red)) + L"s";
        const std::wstring black_time = L"Black time: " + std::to_wstring(view_session.remainingSeconds(Side::Black)) + L"s";
        outtextxy(width - side_panel + 20, 524, red_time.c_str());
        outtextxy(width - side_panel + 20, 552, black_time.c_str());

        const std::wstring turn_text = L"Current: " + utf8ToWide(toString(view_session.currentSide()));
        outtextxy(width - side_panel + 20, 580, turn_text.c_str());

        drawMoveList(view_session);

        if (view_session.gameOver())
        {
            const ResultBanner banner = makeResultBanner(view_session);
            const int pulse = static_cast<int>((GetTickCount64() / 220) % 3);
            const RECT overlay_rect{
                left + 18,
                top + cell * 2,
                left + cell * (board.config().cols - 1) + 18,
                top + cell * 6 + 20
            };
            const COLORREF fill_color = pulse == 1 ? RGB(255, 252, 244) : RGB(252, 248, 236);
            const COLORREF shadow_color = pulse == 2 ? RGB(210, 210, 210) : RGB(188, 188, 188);

            setfillcolor(shadow_color);
            solidrectangle(overlay_rect.left + 8, overlay_rect.top + 8, overlay_rect.right + 8, overlay_rect.bottom + 8);
            setfillcolor(fill_color);
            solidrectangle(overlay_rect.left, overlay_rect.top, overlay_rect.right, overlay_rect.bottom);

            setlinecolor(banner.accent);
            for (int border = 0; border < 3 + pulse; ++border)
            {
                rectangle(
                    overlay_rect.left - border,
                    overlay_rect.top - border,
                    overlay_rect.right + border,
                    overlay_rect.bottom + border);
            }

            settextcolor(banner.accent);
            settextstyle(58, 0, L"Arial Black");
            RECT title_rect{ overlay_rect.left + 20, overlay_rect.top + 48, overlay_rect.right - 20, overlay_rect.top + 170 };
            drawtext(banner.title.c_str(), &title_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            settextcolor(BLACK);
            settextstyle(26, 0, L"Microsoft YaHei");
            RECT subtitle_rect{ overlay_rect.left + 30, overlay_rect.top + 178, overlay_rect.right - 30, overlay_rect.bottom - 34 };
            drawtext(banner.subtitle.c_str(), &subtitle_rect, DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
        }

        FlushBatchDraw();
    };

    auto drawMenu = [&]()
    {
        cleardevice();
        settextcolor(BLACK);
        settextstyle(38, 0, L"Microsoft YaHei");
        outtextxy(width / 2 - 120, 90, L"Xiangqi");

        settextstyle(22, 0, L"Microsoft YaHei");
        const std::wstring mode_text = L"Board: " + utf8ToWide(toString(current_settings.board_mode));
        const std::wstring ai_text = current_settings.ai_enabled ? L"AI: enabled" : L"AI: disabled";
        outtextxy(width / 2 - 160, 145, mode_text.c_str());
        outtextxy(width / 2 - 160, 175, ai_text.c_str());

        drawButton(start_button, L"New Game");
        drawButton(load_button, L"Load Autosave");
        drawButton(import_replay_button, L"Open PGN Replay");
        drawButton(leaderboard_button, L"Leaderboard");
        drawButton(menu_exit_button, L"Return");
        FlushBatchDraw();
    };

    auto drawLeaderboard = [&]()
    {
        cleardevice();
        settextcolor(BLACK);
        settextstyle(34, 0, L"Microsoft YaHei");
        outtextxy(78, 54, L"Leaderboard");

        std::vector<std::string> table_lines;
        try
        {
            table_lines = storage::formatLeaderboardTable(storage::readLeaderboardStandings(), 12);
        }
        catch (const std::exception& ex)
        {
            table_lines = {
                "Leaderboard by Win Rate",
                std::string("Failed to read leaderboard: ") + ex.what()
            };
        }

        settextstyle(20, 0, L"Consolas");
        int y = 126;
        for (size_t i = 0; i < table_lines.size(); ++i)
        {
            RECT line_rect{ 78, y, width - 78, y + 30 };
            const std::wstring line = utf8ToWide(table_lines[i]);
            drawtext(line.c_str(), &line_rect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            y += i == 0 ? 44 : 32;
        }

        settextstyle(18, 0, L"Microsoft YaHei");
        RECT note_rect{ 78, 646, width - 78, 708 };
        drawtext(
            L"Records are written automatically when EasyX games finish. Rankings are grouped by player and sorted by win rate.",
            &note_rect,
            DT_WORDBREAK | DT_NOPREFIX);

        drawButton(leaderboard_back_button, L"Back");
        FlushBatchDraw();
    };

    auto importReplayRecord = [&](const storage::ReplayRecord& record, const bool autoplay)
    {
        invalidateAiTask();
        current_settings = record.settings;
        current_settings.ai_enabled = false;
        current_settings.use_easyx = true;
        current_players = record.players;
        search = SearchEngine(current_settings.ai_depth);
        session = GameSession(current_settings, current_players);
        leaderboard_recorded = record.result != GameResult::Ongoing;
        awaiting_remote_reply = false;
        replay_locked = true;
        refreshClocks();
        resetSelection();
        screen = UiScreen::Playing;

        if (autoplay && !record.moves.empty())
        {
            drawBoardFor(session, std::nullopt, {}, std::nullopt);
            Sleep(320);
        }

        for (const auto& move : record.moves)
        {
            if (shouldCloseWindow())
            {
                break;
            }
            auto imported_move = move;
            session.submitMove(imported_move);
            if (autoplay)
            {
                drawBoardFor(session, std::nullopt, {}, std::nullopt);
                Sleep(280);
            }
        }

        if (!session.gameOver() && record.result != GameResult::Ongoing)
        {
            session.applyImportedResult(record.result, record.result_side);
        }

        refreshClocks();
        resetSelection();
        setStatus(L"Imported PGN replay loaded. The board is read-only.");
    };

    auto replaySession = [&](const GameSession& source)
    {
        if (source.history().empty())
        {
            setStatus(L"No moves to replay yet.");
            return;
        }

        GameSettings replay_settings = source.settings();
        replay_settings.ai_enabled = false;
        GameSession replay(replay_settings, source.players());
        drawBoardFor(replay, std::nullopt, {}, std::nullopt);
        Sleep(300);
        for (const auto& move : source.history())
        {
            if (shouldCloseWindow())
            {
                break;
            }
            replay.submitMove(move);
            drawBoardFor(replay, std::nullopt, {}, std::nullopt);
            Sleep(280);
        }
        if (!shouldCloseWindow() && source.gameOver() && !replay.gameOver())
        {
            replay.applyImportedResult(source.result(), source.currentSide());
            drawBoardFor(replay, std::nullopt, {}, std::nullopt);
            Sleep(320);
        }
        needs_redraw = true;
    };

    auto assignNotation = [&](Move& move, const bool prefer_chinese)
    {
        move.notation = parser.moveToCoordinateText(move, session.board().config());
        if (prefer_chinese && session.boardMode() == BoardMode::Standard9x10)
        {
            move.notation = parser.moveToChineseText(move, session);
        }
    };

    auto moveLabel = [&](Move move, const bool prefer_chinese) -> std::wstring
    {
        assignNotation(move, prefer_chinese);
        return utf8ToWide(move.notation);
    };

    auto launchAiTask = [&]()
    {
        if (network_enabled || !current_settings.ai_enabled || screen != UiScreen::Playing || session.gameOver() ||
            session.currentSide() != current_settings.ai_side || ai_task != nullptr)
        {
            return;
        }

        auto task = std::make_shared<AiTaskState>();
        task->generation = ++ai_generation;
        GameSession snapshot = session;
        const int ai_depth = current_settings.ai_depth;
        ai_task = task;
        setStatus(L"AI thinking... You can press Undo to cancel your last move.");

        std::thread(
            [task, snapshot = std::move(snapshot), ai_depth]() mutable
            {
                try
                {
                    SearchEngine worker(ai_depth);
                    const auto best = worker.chooseBestMove(snapshot);
                    std::lock_guard<std::mutex> lock(task->mutex);
                    task->move = best;
                }
                catch (const std::exception& ex)
                {
                    std::lock_guard<std::mutex> lock(task->mutex);
                    task->error = ex.what();
                }

                task->ready.store(true);
            })
            .detach();
    };

    auto consumeFinishedAiTask = [&]()
    {
        if (ai_task == nullptr || !ai_task->ready.load())
        {
            return;
        }

        const auto finished_task = ai_task;
        ai_task.reset();

        std::optional<Move> best_move;
        std::string error;
        {
            std::lock_guard<std::mutex> lock(finished_task->mutex);
            best_move = finished_task->move;
            error = finished_task->error;
        }

        if (finished_task->generation != ai_generation || session.gameOver() ||
            session.currentSide() != current_settings.ai_side)
        {
            return;
        }

        if (!error.empty())
        {
            setStatusUtf8(error);
            return;
        }

        if (!best_move.has_value())
        {
            setStatus(L"AI has no legal move.");
            return;
        }

        Move move = *best_move;
        assignNotation(move, true);
        session.submitMove(move);
        refreshClocks();
        resetSelection();
        setStatus(L"AI: " + utf8ToWide(move.notation));
        Sleep(120);
    };

    auto startReceiver = [&]()
    {
        if (!network_enabled || receiver_active.load())
        {
            return;
        }

        receiver_active = true;
        stop_receiver = false;
        receiver_thread = std::thread(
            [&]()
            {
                while (!stop_receiver)
                {
                    try
                    {
                        const std::string line = network->receiveLine();
                        std::lock_guard<std::mutex> lock(inbox_mutex);
                        inbox_lines.push_back(line);
                    }
                    catch (const std::exception& ex)
                    {
                        if (!stop_receiver)
                        {
                            std::lock_guard<std::mutex> lock(inbox_mutex);
                            inbox_lines.push_back(std::string("ERROR|") + ex.what());
                        }
                        break;
                    }
                }
                receiver_active = false;
            });
    };

    auto stopReceiver = [&]()
    {
        if (!receiver_active.load() && !receiver_thread.joinable())
        {
            return;
        }

        stop_receiver = true;
        if (network != nullptr)
        {
            network->close();
        }
        if (receiver_thread.joinable())
        {
            receiver_thread.join();
        }
        receiver_active = false;
    };

    auto startAuxiliaryNetworking = [&]()
    {
        if (!network_enabled || local_side != Side::Red || network == nullptr || !network->canAcceptConnections())
        {
            return;
        }

        stop_aux_acceptor = false;
        aux_acceptor_thread = std::thread(
            [&]()
            {
                while (!stop_aux_acceptor)
                {
                    try
                    {
                        auto accepted = network->acceptConnection(250);
                        if (!accepted.has_value())
                        {
                            continue;
                        }

                        std::string request = NetworkSession::receiveLine(*accepted);
                        std::lock_guard<std::mutex> lock(aux_mutex);
                        pending_aux_connections.push_back({ std::move(request), std::move(*accepted) });
                    }
                    catch (...)
                    {
                    }
                }
            });

        room_advertiser_thread = std::thread(
            [&]()
            {
                while (!stop_aux_acceptor)
                {
                    try
                    {
                        LanRoom room;
                        room.name = current_players.red_name + "'s Xiangqi room";
                        room.port = network->listeningPort();
                        room.spectator_count = spectator_count.load();
                        room.accepts_player = network_lost.load();
                        room.accepts_spectators = true;
                        broadcastLanRoom(room);
                    }
                    catch (...)
                    {
                    }
                    for (int i = 0; i < 20 && !stop_aux_acceptor; ++i)
                    {
                        Sleep(100);
                    }
                }
            });
    };

    auto stopAuxiliaryNetworking = [&]()
    {
        stop_aux_acceptor = true;
        if (aux_acceptor_thread.joinable())
        {
            aux_acceptor_thread.join();
        }
        if (room_advertiser_thread.joinable())
        {
            room_advertiser_thread.join();
        }

        {
            std::lock_guard<std::mutex> lock(aux_mutex);
            for (auto& pending : pending_aux_connections)
            {
                NetworkSession::closeConnection(pending.connection);
            }
            pending_aux_connections.clear();
        }
        for (auto& spectator : spectator_connections)
        {
            NetworkSession::closeConnection(spectator);
        }
        spectator_connections.clear();
        spectator_count = 0;
    };

    auto processAuxiliaryConnections = [&]()
    {
        if (!network_enabled || local_side != Side::Red)
        {
            return;
        }

        std::vector<PendingAuxConnection> pending;
        {
            std::lock_guard<std::mutex> lock(aux_mutex);
            pending.swap(pending_aux_connections);
        }

        for (auto& connection : pending)
        {
            const auto request = parseConnectionRequest(connection.request);
            try
            {
                if (request.has_value() && request->role == ConnectionRole::Spectator)
                {
                    NetworkSession::sendLine(connection.connection, serializeHandshake(current_settings, current_players, session.currentSide()));
                    NetworkSession::sendLine(connection.connection, stateLine());
                    spectator_connections.push_back(std::move(connection.connection));
                    spectator_count = static_cast<int>(spectator_connections.size());
                    setStatus(L"Spectator joined.");
                }
                else if (request.has_value() && request->role == ConnectionRole::Reconnect && network_lost.load())
                {
                    NetworkSession::sendLine(connection.connection, serializeHandshake(current_settings, current_players, session.currentSide()));
                    NetworkSession::sendLine(connection.connection, stateLine());
                    if (receiver_thread.joinable())
                    {
                        receiver_thread.join();
                    }
                    network->replaceConnection(connection.connection);
                    network_lost = false;
                    awaiting_remote_reply = false;
                    startReceiver();
                    setStatus(L"LAN peer reconnected.");
                }
                else if (request.has_value() && request->role == ConnectionRole::Player)
                {
                    NetworkSession::sendLine(connection.connection, "ERROR|对局已经开始，不能作为玩家加入。");
                    NetworkSession::closeConnection(connection.connection);
                }
                else
                {
                    NetworkSession::sendLine(connection.connection, "ERROR|无法识别的连接请求。");
                    NetworkSession::closeConnection(connection.connection);
                }
            }
            catch (...)
            {
                NetworkSession::closeConnection(connection.connection);
            }
        }
    };

    auto handleInboundLine = [&](const std::string& line)
    {
        const auto fields = splitProtocol(line);
        if (fields.empty())
        {
            return;
        }

        try
        {
            if (fields[0] == "MOVE_REQ" && network_enabled && local_side == Side::Red && fields.size() >= 3)
            {
                ParsedCommand parsed = parser.parse(fields[1] + " " + fields[2], session);
                Move move = *parsed.move;
                assignNotation(move, true);
                const Move applied = session.submitMove(move);
                network->sendLine(moveLine("MOVE_OK", applied, session.board().config()));
                broadcastStateToSpectators();
                awaiting_remote_reply = false;
                refreshClocks();
                resetSelection();
                setStatus(L"Opponent moved.");
            }
            else if (fields[0] == "MOVE_OK" && network_enabled && local_side == Side::Black && fields.size() >= 3)
            {
                ParsedCommand parsed = parser.parse(fields[1] + " " + fields[2], session);
                Move move = *parsed.move;
                assignNotation(move, true);
                session.submitMove(move);
                awaiting_remote_reply = false;
                refreshClocks();
                resetSelection();
                setStatus(L"Move synchronized.");
            }
            else if (fields[0] == "STATE" && fields.size() >= 2)
            {
                session = GameSession::deserialize(unescapeProtocolField(fields[1]));
                current_settings = session.settings();
                current_players = session.players();
                search = SearchEngine(current_settings.ai_depth);
                leaderboard_recorded = session.gameOver();
                awaiting_remote_reply = false;
                refreshClocks();
                resetSelection();
                screen = UiScreen::Playing;
                setStatus(L"LAN state synchronized.");
            }
            else if (fields[0] == "UNDO_REQ" && network_enabled && local_side == Side::Red)
            {
                if (session.undoLastPly())
                {
                    network->sendLine("UNDO_OK");
                    broadcastStateToSpectators();
                    refreshClocks();
                    resetSelection();
                    setStatus(L"Undo accepted.");
                }
                else
                {
                    network->sendLine("ERROR|Undo is not available.");
                }
            }
            else if (fields[0] == "UNDO_OK" && network_enabled)
            {
                if (session.undoLastPly())
                {
                    refreshClocks();
                    resetSelection();
                    setStatus(L"Undo synchronized.");
                }
                awaiting_remote_reply = false;
            }
            else if (fields[0] == "RESIGN_REQ" && network_enabled && local_side == Side::Red)
            {
                session.resign(Side::Black);
                network->sendLine("RESIGN_OK|Black");
                broadcastStateToSpectators();
                setStatus(L"Opponent resigned.");
            }
            else if (fields[0] == "RESIGN_OK" && network_enabled && fields.size() >= 2)
            {
                session.resign(parseWireSide(fields[1]));
                awaiting_remote_reply = false;
                setStatus(L"LAN game ended.");
            }
            else if (fields[0] == "ERROR" && fields.size() >= 2)
            {
                awaiting_remote_reply = false;
                if (network_enabled && local_side == Side::Red)
                {
                    network_lost = true;
                    if (receiver_thread.joinable())
                    {
                        receiver_thread.join();
                    }
                    setStatus(L"LAN peer disconnected. Waiting for reconnect; spectators may keep watching.");
                }
                else
                {
                    setStatusUtf8(fields[1]);
                }
            }
        }
        catch (const std::exception& ex)
        {
            awaiting_remote_reply = false;
            setStatusUtf8(ex.what());
        }
    };

    if (network_enabled)
    {
        startReceiver();
        startAuxiliaryNetworking();
        initializePreconnectedSpectators();
    }
    else if (startup_replay.has_value())
    {
        importReplayRecord(*startup_replay, true);
    }
    else
    {
        startFreshGame();
    }

    while (true)
    {
        if (shouldCloseWindow())
        {
            break;
        }

        if (network_enabled)
        {
            processAuxiliaryConnections();
            std::vector<std::string> pending_lines;
            {
                std::lock_guard<std::mutex> lock(inbox_mutex);
                pending_lines.swap(inbox_lines);
            }
            for (const auto& line : pending_lines)
            {
                handleInboundLine(line);
            }
        }

        if (screen == UiScreen::Playing && !session.gameOver())
        {
            const bool was_over = session.gameOver();
            session.tickClock();
            if (!was_over && session.gameOver())
            {
                broadcastStateToSpectators();
            }
            const int red_seconds = session.remainingSeconds(Side::Red);
            const int black_seconds = session.remainingSeconds(Side::Black);
            if (red_seconds != last_red_seconds || black_seconds != last_black_seconds)
            {
                refreshClocks();
                needs_redraw = true;
            }
        }
        else if (screen == UiScreen::Playing && session.gameOver())
        {
            const int result_pulse = static_cast<int>((GetTickCount64() / 220) % 3);
            if (result_pulse != last_result_pulse)
            {
                last_result_pulse = result_pulse;
                needs_redraw = true;
            }
        }
        else
        {
            last_result_pulse = -1;
        }

        if (!network_enabled && current_settings.ai_enabled)
        {
            consumeFinishedAiTask();
            launchAiTask();
        }

        if (screen == UiScreen::Playing && session.gameOver() && !leaderboard_recorded)
        {
            leaderboard_recorded = true;
            try
            {
                storage::appendLeaderboard(session);
                saveCurrentGame("easyx_autosave");
            }
            catch (const std::exception& ex)
            {
                setStatus(L"Failed to update save or leaderboard: " + utf8ToWide(ex.what()));
            }
            needs_redraw = true;
        }

        if (needs_redraw)
        {
            if (screen == UiScreen::Menu)
            {
                drawMenu();
            }
            else if (screen == UiScreen::Leaderboard)
            {
                drawLeaderboard();
            }
            else
            {
                drawBoardFor(session, selected, legal_from_selected, hinted_move);
            }
            needs_redraw = false;
        }

        if (!MouseHit())
        {
            Sleep(10);
            continue;
        }

        const MOUSEMSG msg = GetMouseMsg();
        if (msg.uMsg != WM_LBUTTONDOWN)
        {
            continue;
        }

        try
        {
            if (screen == UiScreen::Menu)
            {
                if (insideRect(start_button, msg.x, msg.y))
                {
                    startFreshGame();
                }
                else if (insideRect(load_button, msg.x, msg.y))
                {
                    invalidateAiTask();
                    session = storage::loadGame("easyx_autosave");
                    current_settings = session.settings();
                    current_players = session.players();
                    search = SearchEngine(current_settings.ai_depth);
                    leaderboard_recorded = session.gameOver();
                    awaiting_remote_reply = false;
                    replay_locked = false;
                    refreshClocks();
                    resetSelection();
                    screen = UiScreen::Playing;
                    setStatus(L"Autosave loaded.");
                }
                else if (insideRect(import_replay_button, msg.x, msg.y))
                {
                    if (const auto selected_path = browseForPgnFile(); selected_path.has_value())
                    {
                        const auto record = storage::loadReplay(selected_path->string());
                        importReplayRecord(record, true);
                    }
                }
                else if (insideRect(leaderboard_button, msg.x, msg.y))
                {
                    screen = UiScreen::Leaderboard;
                    needs_redraw = true;
                }
                else if (insideRect(menu_exit_button, msg.x, msg.y))
                {
                    break;
                }
                continue;
            }

            if (screen == UiScreen::Leaderboard)
            {
                if (insideRect(leaderboard_back_button, msg.x, msg.y))
                {
                    screen = UiScreen::Menu;
                    needs_redraw = true;
                }
                continue;
            }

            auto resignAndExit = [&]()
            {
                invalidateAiTask();
                if (network_enabled && network != nullptr && !session.gameOver())
                {
                    if (local_side == Side::Red)
                    {
                        session.resign(Side::Red);
                        network->sendLine("RESIGN_OK|Red");
                        broadcastStateToSpectators();
                    }
                    else
                    {
                        session.resign(Side::Black);
                        network->sendLine("RESIGN_REQ");
                    }
                }

                try
                {
                    if (!replay_locked)
                    {
                        saveCurrentGame("easyx_autosave");
                    }
                }
                catch (...)
                {
                }
            };

            if (insideRect(exit_button, msg.x, msg.y))
            {
                resignAndExit();
                break;
            }
            if (insideRect(menu_button, msg.x, msg.y))
            {
                if (network_enabled)
                {
                    resignAndExit();
                    break;
                }
                invalidateAiTask();
                resetSelection();
                screen = UiScreen::Menu;
                needs_redraw = true;
                continue;
            }
            if (insideRect(restart_button, msg.x, msg.y))
            {
                if (network_enabled)
                {
                    setStatus(L"Restart is disabled during LAN play.");
                }
                else
                {
                    startFreshGame();
                }
                continue;
            }
            if (insideRect(replay_button, msg.x, msg.y))
            {
                if (network_enabled)
                {
                    setStatus(L"Replay is disabled during LAN play.");
                }
                else
                {
                    replaySession(session);
                }
                continue;
            }
            if (insideRect(save_button, msg.x, msg.y))
            {
                if (replay_locked)
                {
                    setStatus(L"Imported PGN replay is read-only.");
                    continue;
                }
                const auto replay_path = saveCurrentGame("easyx_autosave");
                if (replay_path.has_value())
                {
                    setStatus(L"PGN: " + replay_path->wstring());
                }
                else
                {
                    setStatus(L"Saved game. PGN is saved by the LAN host.");
                }
                continue;
            }
            if (insideRect(undo_button, msg.x, msg.y))
            {
                if (replay_locked)
                {
                    setStatus(L"Imported PGN replay is read-only.");
                    continue;
                }
                if (awaiting_remote_reply)
                {
                    setStatus(L"Wait for the pending LAN action first.");
                }
                else if (network_enabled)
                {
                    if (local_side == Side::Red)
                    {
                        if (session.undoLastPly())
                        {
                            network->sendLine("UNDO_OK");
                            broadcastStateToSpectators();
                            refreshClocks();
                            resetSelection();
                            setStatus(L"Undo broadcast to LAN peer.");
                        }
                        else
                        {
                            setStatus(L"Undo is not available.");
                        }
                    }
                    else
                    {
                        network->sendLine("UNDO_REQ");
                        awaiting_remote_reply = true;
                        setStatus(L"Undo request sent to host.");
                    }
                }
                else if (current_settings.ai_enabled)
                {
                    const bool was_ai_thinking = aiThinking();
                    invalidateAiTask();
                    const int undone = was_ai_thinking ? session.undoLastPly() : session.undoLastPlies(2);
                    if (undone >= 2)
                    {
                        refreshClocks();
                        resetSelection();
                        setStatus(L"Last full turn undone.");
                    }
                    else if (undone == 1)
                    {
                        refreshClocks();
                        resetSelection();
                        setStatus(L"AI thinking canceled. Your last move was undone.");
                    }
                    else
                    {
                        setStatus(L"Undo is not available.");
                    }
                }
                else if (session.undoLastPly())
                {
                    refreshClocks();
                    resetSelection();
                    setStatus(L"Move undone.");
                }
                else
                {
                    setStatus(L"Undo is not available.");
                }
                continue;
            }
            if (insideRect(hint_button, msg.x, msg.y))
            {
                if (replay_locked)
                {
                    setStatus(L"Hint is disabled while viewing an imported replay.");
                    continue;
                }
                if (!network_enabled && current_settings.ai_enabled && session.currentSide() == current_settings.ai_side)
                {
                    setStatus(L"Hint is only available on your turn.");
                }
                else if (network_enabled && (awaiting_remote_reply || session.currentSide() != local_side))
                {
                    setStatus(L"Hint is only available on your own turn.");
                }
                else if (const auto best = search.chooseBestMove(session); best.has_value())
                {
                    Move move = *best;
                    showHintMove(move);
                    setStatus(L"Hint: " + moveLabel(move, true));
                }
                else
                {
                    setStatus(L"No legal move available.");
                }
                continue;
            }

            const auto hit = hitCell(session, msg.x, msg.y);
            if (!hit.has_value() || session.gameOver())
            {
                resetSelection();
                continue;
            }

            if (replay_locked)
            {
                setStatus(L"Imported PGN replay is read-only. Use Replay to watch it again.");
                continue;
            }

            if (network_enabled && (awaiting_remote_reply || session.currentSide() != local_side))
            {
                setStatus(awaiting_remote_reply ? L"Waiting for LAN confirmation." : L"Waiting for the opponent's move.");
                continue;
            }

            if (!network_enabled && current_settings.ai_enabled && session.currentSide() == current_settings.ai_side)
            {
                setStatus(aiThinking() ? L"AI is thinking... You can press Undo." : L"Waiting for AI move.");
                continue;
            }

            if (!selected.has_value())
            {
                const auto* piece = session.board().pieceAt(*hit);
                if (piece != nullptr && piece->side() == session.currentSide())
                {
                    selectPiece(*hit);
                }
                continue;
            }

            const auto found = std::find_if(
                legal_from_selected.begin(),
                legal_from_selected.end(),
                [&](const Move& move)
                {
                    return move.to == *hit;
                });

            if (found != legal_from_selected.end())
            {
                Move move = *found;
                if (network_enabled)
                {
                    if (local_side == Side::Red)
                    {
                        assignNotation(move, true);
                        const Move applied = session.submitMove(move);
                        network->sendLine(moveLine("MOVE_OK", applied, session.board().config()));
                        broadcastStateToSpectators();
                        refreshClocks();
                        resetSelection();
                        setStatus(L"Move sent to LAN peer.");
                    }
                    else
                    {
                        network->sendLine(moveLine("MOVE_REQ", move, session.board().config()));
                        awaiting_remote_reply = true;
                        resetSelection();
                        setStatus(L"Move request sent to host.");
                    }
                }
                else
                {
                    invalidateAiTask();
                    assignNotation(move, true);
                    session.submitMove(move);
                    refreshClocks();
                    resetSelection();
                    setStatus(L"Move applied.");
                }
            }
            else
            {
                const auto* piece = session.board().pieceAt(*hit);
                if (piece != nullptr && piece->side() == session.currentSide())
                {
                    selectPiece(*hit);
                }
                else
                {
                    resetSelection();
                }
            }
        }
        catch (const std::exception& ex)
        {
            setStatusUtf8(ex.what());
        }
    }

    restoreWindowProc();
    stopAuxiliaryNetworking();
    stopReceiver();
    flushmessage();
    FlushMouseMsgBuffer();
    EndBatchDraw();
    hideSharedEasyXWindow();
    return 0;
#else
    (void)settings;
    (void)players;
    (void)network;
    (void)local_side_override;
    (void)startup_replay;
    (void)preconnected_spectators;
    return 1;
#endif
}

int runDarkInternal(
    GameSettings settings,
    PlayerInfo players,
    std::unique_ptr<NetworkSession> network,
    const std::optional<Side> local_side_override,
    const std::optional<DarkGameSession> startup_session = std::nullopt,
    SpectatorConnections preconnected_spectators = {})
{
#if XIANGQI_HAS_EASYX
    settings.game_kind = GameKind::DarkChess;
    DarkGameSession session = startup_session.has_value()
        ? *startup_session
        : DarkGameSession(settings, std::move(players));
    DarkSearchEngine search;
    const bool network_enabled = network != nullptr;
    const DarkSeat local_seat = local_side_override.has_value() && *local_side_override == Side::Black
        ? DarkSeat::Player2
        : DarkSeat::Player1;

    if (network_enabled)
    {
        if (local_seat == DarkSeat::Player1)
        {
            network->sendLine(darkStateLine(session));
        }
        else
        {
            const auto fields = splitProtocol(network->receiveLine());
            if (fields.size() >= 2 && fields[0] == "DARK_STATE")
            {
                session = DarkGameSession::deserialize(unescapeProtocolField(fields[1]));
            }
            else
            {
                throw NetworkError("Expected initial dark chess state from host.");
            }
        }
    }

    const int width = 980;
    const int cell = 70;
    const int left = 60;
    const int top = 80;
    const int side_left = 690;
    const RECT undo_button{ side_left, 178, side_left + 210, 220 };
    const RECT hint_button{ side_left, 232, side_left + 210, 274 };
    const RECT save_button{ side_left, 286, side_left + 210, 328 };
    const RECT restart_button{ side_left, 340, side_left + 210, 382 };
    const RECT menu_button{ side_left, 394, side_left + 210, 436 };
    const RECT exit_button{ side_left, 448, side_left + 210, 490 };

    showSharedEasyXWindow();
    setbkcolor(RGB(245, 238, 220));
    setbkmode(TRANSPARENT);

    std::optional<Position<int>> selected;
    std::vector<DarkAction> legal_from_selected;
    std::wstring status = L"Dark chess ready.";
    bool needs_redraw = true;
    bool dark_leaderboard_recorded = false;
    bool network_lost = false;
    std::atomic_bool stop_aux_acceptor{ false };
    std::atomic_int spectator_count{ 0 };
    std::thread aux_acceptor_thread;
    std::thread room_advertiser_thread;
    std::mutex aux_mutex;
    std::vector<PendingAuxConnection> pending_aux_connections;
    std::vector<NetworkSession::AcceptedConnection> spectator_connections = std::move(preconnected_spectators);
    spectator_count = static_cast<int>(spectator_connections.size());

    auto seatName = [](const DarkSeat seat)
    {
        return seat == DarkSeat::Player1 ? L"Player1" : L"Player2";
    };

    auto hitCell = [&](const int x, const int y) -> std::optional<Position<int>>
    {
        const int col = (x - left) / cell;
        const int row = (y - top) / cell;
        if (row < 0 || row >= DarkBoard::kRows || col < 0 || col >= DarkBoard::kCols)
        {
            return std::nullopt;
        }
        const int cell_left = left + col * cell;
        const int cell_top = top + row * cell;
        if (x < cell_left || x > cell_left + cell || y < cell_top || y > cell_top + cell)
        {
            return std::nullopt;
        }
        return Position<int>{ row, col };
    };

    auto drawButton = [&](const RECT& rect, const wchar_t* text, const bool enabled = true)
    {
        setfillcolor(enabled ? RGB(255, 255, 248) : RGB(225, 225, 225));
        solidrectangle(rect.left, rect.top, rect.right, rect.bottom);
        setlinecolor(enabled ? RGB(80, 80, 80) : RGB(150, 150, 150));
        rectangle(rect.left, rect.top, rect.right, rect.bottom);
        settextcolor(enabled ? BLACK : RGB(130, 130, 130));
        drawtext(text, const_cast<RECT*>(&rect), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };

    auto drawDarkBoard = [&]()
    {
        cleardevice();
        settextcolor(BLACK);
        settextstyle(34, 0, L"Microsoft YaHei");
        outtextxy(left, 24, L"Dark Chess");
        settextstyle(18, 0, L"Microsoft YaHei");
        outtextxy(left + 190, 36, L"4 x 8 Banqi");

        setlinecolor(RGB(80, 60, 40));
        for (int row = 0; row <= DarkBoard::kRows; ++row)
        {
            line(left, top + row * cell, left + DarkBoard::kCols * cell, top + row * cell);
        }
        for (int col = 0; col <= DarkBoard::kCols; ++col)
        {
            line(left + col * cell, top, left + col * cell, top + DarkBoard::kRows * cell);
        }

        for (const auto& action : legal_from_selected)
        {
            const int cx = left + action.to.col * cell + cell / 2;
            const int cy = top + action.to.row * cell + cell / 2;
            setfillcolor(RGB(120, 220, 120));
            solidcircle(cx, cy, 8);
        }

        if (selected.has_value())
        {
            setlinecolor(RGB(255, 170, 0));
            rectangle(
                left + selected->col * cell + 5,
                top + selected->row * cell + 5,
                left + (selected->col + 1) * cell - 5,
                top + (selected->row + 1) * cell - 5);
        }

        settextstyle(28, 0, L"Microsoft YaHei");
        for (int row = 0; row < DarkBoard::kRows; ++row)
        {
            for (int col = 0; col < DarkBoard::kCols; ++col)
            {
                const Position<int> position{ row, col };
                if (!session.board().isOccupied(position))
                {
                    continue;
                }

                const int cx = left + col * cell + cell / 2;
                const int cy = top + row * cell + cell / 2;
                const auto piece = session.board().pieceAt(position);
                if (!piece.has_value() || !piece->is_open)
                {
                    setfillcolor(RGB(130, 88, 54));
                    solidcircle(cx, cy, 26);
                    setlinecolor(RGB(70, 42, 22));
                    circle(cx, cy, 26);
                    settextcolor(WHITE);
                    outtextxy(cx - textwidth(L"?") / 2, cy - textheight(L"?") / 2, L"?");
                    continue;
                }

                setfillcolor(piece->side == Side::Red ? RGB(255, 245, 245) : RGB(240, 240, 240));
                solidcircle(cx, cy, 26);
                setlinecolor(piece->side == Side::Red ? RGB(210, 0, 0) : RGB(10, 10, 10));
                circle(cx, cy, 26);
                settextcolor(piece->side == Side::Red ? RGB(210, 0, 0) : RGB(10, 10, 10));
                wchar_t text[2]{ darkPieceFace(*piece), 0 };
                outtextxy(cx - textwidth(text) / 2, cy - textheight(text) / 2, text);
            }
        }

        settextstyle(20, 0, L"Microsoft YaHei");
        settextcolor(BLACK);
        RECT status_rect{ side_left, 32, width - 32, 112 };
        drawtext(status.c_str(), &status_rect, DT_WORDBREAK | DT_NOPREFIX);
        const std::wstring current = L"Current: " + std::wstring(seatName(session.currentSeat()));
        outtextxy(side_left, 118, current.c_str());
        const auto p1 = session.colorForSeat(DarkSeat::Player1);
        const auto p2 = session.colorForSeat(DarkSeat::Player2);
        const std::wstring colors = L"P1: " + utf8ToWide(p1.has_value() ? toString(*p1) : "Unknown") +
            L"  P2: " + utf8ToWide(p2.has_value() ? toString(*p2) : "Unknown");
        outtextxy(side_left, 146, colors.c_str());
        const bool undo_enabled = session.settings().allow_undo && !session.history().empty();
        drawButton(undo_button, L"Undo", undo_enabled);
        drawButton(hint_button, L"Hint");
        drawButton(save_button, L"Save");
        drawButton(restart_button, L"Restart", !network_enabled);
        drawButton(menu_button, network_enabled ? L"Leave LAN" : L"Menu");
        drawButton(exit_button, L"Exit");

        if (session.gameOver())
        {
            settextstyle(26, 0, L"Microsoft YaHei");
            settextcolor(RGB(190, 40, 40));
            const std::wstring result = utf8ToWide(session.resultText());
            outtextxy(left, top + DarkBoard::kRows * cell + 36, result.c_str());
        }
        FlushBatchDraw();
    };

    auto resetSelection = [&]()
    {
        selected.reset();
        legal_from_selected.clear();
    };

    // 揭棋观战只能发送公开局面，隐藏棋子身份必须继续保持为 xx。
    // 这条广播路径不能复用本地存档的私有序列化结果，否则会泄露暗子。
    auto broadcastDarkStateToSpectators = [&]()
    {
        if (!network_enabled || local_seat != DarkSeat::Player1)
        {
            return;
        }
        const std::string state = darkStateLine(session);
        for (auto it = spectator_connections.begin(); it != spectator_connections.end();)
        {
            try
            {
                NetworkSession::sendLine(*it, state);
                ++it;
            }
            catch (...)
            {
                NetworkSession::closeConnection(*it);
                it = spectator_connections.erase(it);
            }
        }
        spectator_count = static_cast<int>(spectator_connections.size());
    };

    auto initializePreconnectedDarkSpectators = [&]()
    {
        if (!network_enabled || local_seat != DarkSeat::Player1)
        {
            return;
        }

        for (auto it = spectator_connections.begin(); it != spectator_connections.end();)
        {
            try
            {
                NetworkSession::sendLine(*it, serializeHandshake(settings, session.players(), Side::Red));
                NetworkSession::sendLine(*it, darkStateLine(session));
                ++it;
            }
            catch (...)
            {
                NetworkSession::closeConnection(*it);
                it = spectator_connections.erase(it);
            }
        }
        spectator_count = static_cast<int>(spectator_connections.size());
    };

    auto startAuxiliaryNetworking = [&]()
    {
        if (!network_enabled || local_seat != DarkSeat::Player1 || network == nullptr || !network->canAcceptConnections())
        {
            return;
        }

        stop_aux_acceptor = false;
        aux_acceptor_thread = std::thread(
            [&]()
            {
                while (!stop_aux_acceptor)
                {
                    try
                    {
                        auto accepted = network->acceptConnection(250);
                        if (!accepted.has_value())
                        {
                            continue;
                        }
                        std::string request = NetworkSession::receiveLine(*accepted);
                        std::lock_guard<std::mutex> lock(aux_mutex);
                        pending_aux_connections.push_back({ std::move(request), std::move(*accepted) });
                    }
                    catch (...)
                    {
                    }
                }
            });

        room_advertiser_thread = std::thread(
            [&]()
            {
                while (!stop_aux_acceptor)
                {
                    try
                    {
                        LanRoom room;
                        room.name = session.players().red_name + "'s Dark Chess room";
                        room.port = network->listeningPort();
                        room.spectator_count = spectator_count.load();
                        room.accepts_player = network_lost;
                        room.accepts_spectators = true;
                        broadcastLanRoom(room);
                    }
                    catch (...)
                    {
                    }
                    for (int i = 0; i < 20 && !stop_aux_acceptor; ++i)
                    {
                        Sleep(100);
                    }
                }
            });
    };

    auto stopAuxiliaryNetworking = [&]()
    {
        stop_aux_acceptor = true;
        if (aux_acceptor_thread.joinable())
        {
            aux_acceptor_thread.join();
        }
        if (room_advertiser_thread.joinable())
        {
            room_advertiser_thread.join();
        }
        {
            std::lock_guard<std::mutex> lock(aux_mutex);
            for (auto& pending : pending_aux_connections)
            {
                NetworkSession::closeConnection(pending.connection);
            }
            pending_aux_connections.clear();
        }
        for (auto& spectator : spectator_connections)
        {
            NetworkSession::closeConnection(spectator);
        }
        spectator_connections.clear();
        spectator_count = 0;
    };

    auto processAuxiliaryConnections = [&]()
    {
        if (!network_enabled || local_seat != DarkSeat::Player1)
        {
            return;
        }
        std::vector<PendingAuxConnection> pending;
        {
            std::lock_guard<std::mutex> lock(aux_mutex);
            pending.swap(pending_aux_connections);
        }
        for (auto& connection : pending)
        {
            const auto request = parseConnectionRequest(connection.request);
            try
            {
                if (request.has_value() && request->role == ConnectionRole::Spectator)
                {
                    NetworkSession::sendLine(connection.connection, serializeHandshake(settings, session.players(), Side::Red));
                    NetworkSession::sendLine(connection.connection, darkStateLine(session));
                    spectator_connections.push_back(std::move(connection.connection));
                    spectator_count = static_cast<int>(spectator_connections.size());
                    status = L"Dark chess spectator joined.";
                }
                else if (request.has_value() && request->role == ConnectionRole::Reconnect && network_lost)
                {
                    NetworkSession::sendLine(connection.connection, serializeHandshake(settings, session.players(), Side::Red));
                    NetworkSession::sendLine(connection.connection, darkStateLine(session));
                    network->replaceConnection(connection.connection);
                    network_lost = false;
                    status = L"Dark chess LAN peer reconnected.";
                }
                else if (request.has_value() && request->role == ConnectionRole::Player)
                {
                    NetworkSession::sendLine(connection.connection, "ERROR|揭棋对局已经开始，不能作为玩家加入。");
                    NetworkSession::closeConnection(connection.connection);
                }
                else
                {
                    NetworkSession::sendLine(connection.connection, "ERROR|无法识别的连接请求。");
                    NetworkSession::closeConnection(connection.connection);
                }
            }
            catch (...)
            {
                NetworkSession::closeConnection(connection.connection);
            }
        }
    };

    auto applyNetworkState = [&](const std::string& line)
    {
        const auto fields = splitProtocol(line);
        if (fields.size() >= 2 && fields[0] == "DARK_STATE")
        {
            session = DarkGameSession::deserialize(unescapeProtocolField(fields[1]));
            resetSelection();
            status = L"LAN state updated.";
            needs_redraw = true;
        }
        else if (fields.size() >= 2 && fields[0] == "ERROR")
        {
            status = utf8ToWide(fields[1]);
            needs_redraw = true;
        }
    };

    startAuxiliaryNetworking();
    initializePreconnectedDarkSpectators();
    BeginBatchDraw();
    while (true)
    {
        processAuxiliaryConnections();
        if (!session.gameOver())
        {
            const bool was_over = session.gameOver();
            session.tickClock();
            if (!was_over && session.gameOver())
            {
                broadcastDarkStateToSpectators();
            }
        }
        if (session.gameOver() && !dark_leaderboard_recorded)
        {
            dark_leaderboard_recorded = true;
            try
            {
                storage::appendDarkLeaderboard(session);
            }
            catch (...)
            {
            }
        }

        if (!network_enabled && settings.ai_enabled && session.currentSeat() == settings.dark_ai_seat && !session.gameOver())
        {
            if (const auto action = search.chooseAction(session); action.has_value())
            {
                session.submitAction(*action);
                resetSelection();
                status = L"AI moved.";
                needs_redraw = true;
            }
        }

        if (network_enabled && session.currentSeat() != local_seat && !session.gameOver())
        {
            status = L"Waiting for LAN peer...";
            needs_redraw = true;
            drawDarkBoard();
            try
            {
                const std::string line = network->receiveLine();
                if (local_seat == DarkSeat::Player1)
                {
                    const auto fields = splitProtocol(line);
                    if (fields.size() >= 2 && fields[0] == "DARK_REQ")
                    {
                        DarkAction action = parseDarkActionText(unescapeProtocolField(fields[1]), session);
                        session.submitAction(action);
                        network->sendLine(darkStateLine(session));
                        broadcastDarkStateToSpectators();
                        status = L"LAN move accepted.";
                    }
                    else if (!fields.empty() && fields[0] == "DARK_UNDO_REQ")
                    {
                        if (session.undoLastPly())
                        {
                            network->sendLine(darkStateLine(session));
                            broadcastDarkStateToSpectators();
                            resetSelection();
                            status = L"LAN undo accepted.";
                        }
                        else
                        {
                            network->sendLine("ERROR|Undo is not available.");
                            status = L"LAN undo rejected.";
                        }
                    }
                }
                else
                {
                    applyNetworkState(line);
                }
            }
            catch (const std::exception& ex)
            {
                if (local_seat == DarkSeat::Player1)
                {
                    network_lost = true;
                }
                status = utf8ToWide(ex.what());
            }
            needs_redraw = true;
        }

        if (needs_redraw)
        {
            drawDarkBoard();
            needs_redraw = false;
        }

        ExMessage msg{};
        if (!peekmessage(&msg, EX_MOUSE | EX_KEY))
        {
            Sleep(12);
            continue;
        }

        if (msg.message == WM_KEYDOWN && msg.vkcode == VK_ESCAPE)
        {
            break;
        }
        if (msg.message != WM_LBUTTONDOWN)
        {
            continue;
        }

        if (insideRect(exit_button, msg.x, msg.y))
        {
            stopAuxiliaryNetworking();
            flushmessage();
            FlushMouseMsgBuffer();
            EndBatchDraw();
            hideSharedEasyXWindow();
            return 0;
        }
        if (insideRect(menu_button, msg.x, msg.y))
        {
            break;
        }
        if (insideRect(undo_button, msg.x, msg.y))
        {
            try
            {
                if (!session.settings().allow_undo || session.history().empty())
                {
                    status = L"Undo is not available.";
                }
                else if (network_enabled && local_seat == DarkSeat::Player2)
                {
                    network->sendLine("DARK_UNDO_REQ");
                    applyNetworkState(network->receiveLine());
                }
                else
                {
                    const int undone = (!network_enabled && settings.ai_enabled)
                        ? session.undoLastPlies(session.currentSeat() == settings.dark_ai_seat ? 1 : 2)
                        : (session.undoLastPly() ? 1 : 0);
                    if (undone > 0)
                    {
                        resetSelection();
                        if (network_enabled)
                        {
                            network->sendLine(darkStateLine(session));
                            broadcastDarkStateToSpectators();
                        }
                        status = undone >= 2 ? L"Last full turn undone." : L"Move undone.";
                    }
                    else
                    {
                        status = L"Undo is not available.";
                    }
                }
            }
            catch (const std::exception& ex)
            {
                status = utf8ToWide(ex.what());
            }
            needs_redraw = true;
            continue;
        }
        if (insideRect(restart_button, msg.x, msg.y) && !network_enabled)
        {
            session = DarkGameSession(settings, session.players());
            resetSelection();
            status = L"Dark chess restarted.";
            needs_redraw = true;
            continue;
        }
        if (insideRect(save_button, msg.x, msg.y))
        {
            try
            {
                storage::saveDarkGame(session, "easyx_dark_autosave");
                storage::saveDarkReplay(session, "easyx_dark_replay");
                status = L"Dark chess saved.";
            }
            catch (const std::exception& ex)
            {
                status = utf8ToWide(ex.what());
            }
            needs_redraw = true;
            continue;
        }
        if (insideRect(hint_button, msg.x, msg.y))
        {
            if (const auto hint = search.chooseAction(session); hint.has_value())
            {
                status = L"Hint: " + utf8ToWide(darkActionToText(*hint));
            }
            else
            {
                status = L"No legal action.";
            }
            needs_redraw = true;
            continue;
        }

        const auto hit = hitCell(msg.x, msg.y);
        if (!hit.has_value() || session.gameOver())
        {
            resetSelection();
            needs_redraw = true;
            continue;
        }
        if (network_enabled && session.currentSeat() != local_seat)
        {
            status = L"Waiting for LAN peer.";
            needs_redraw = true;
            continue;
        }

        try
        {
            std::optional<DarkAction> chosen;
            if (session.board().isOccupied(*hit) && !session.board().isOpen(*hit) && !selected.has_value())
            {
                chosen = DarkAction::flip(*hit, session.currentSeat());
            }
            else if (!selected.has_value())
            {
                const auto piece = session.board().pieceAt(*hit);
                if (piece.has_value() && piece->is_open)
                {
                    selected = *hit;
                    legal_from_selected = session.legalActionsFrom(*hit);
                }
            }
            else
            {
                const auto found = std::find_if(
                    legal_from_selected.begin(),
                    legal_from_selected.end(),
                    [&](const DarkAction& action)
                    {
                        return action.to == *hit;
                    });
                if (found != legal_from_selected.end())
                {
                    chosen = *found;
                }
                else
                {
                    resetSelection();
                }
            }

            if (chosen.has_value())
            {
                if (network_enabled && local_seat == DarkSeat::Player2)
                {
                    network->sendLine(std::string("DARK_REQ|") + escapeProtocolField(darkActionToText(*chosen)));
                    applyNetworkState(network->receiveLine());
                }
                else
                {
                    session.submitAction(*chosen);
                    if (network_enabled)
                    {
                        network->sendLine(darkStateLine(session));
                        broadcastDarkStateToSpectators();
                    }
                    status = L"Action applied.";
                }
                resetSelection();
            }
        }
        catch (const std::exception& ex)
        {
            status = utf8ToWide(ex.what());
            resetSelection();
        }
        needs_redraw = true;
    }

    stopAuxiliaryNetworking();
    flushmessage();
    FlushMouseMsgBuffer();
    EndBatchDraw();
    hideSharedEasyXWindow();
    return 0;
#else
    (void)settings;
    (void)players;
    (void)network;
    (void)local_side_override;
    (void)startup_session;
    (void)preconnected_spectators;
    return 1;
#endif
}

int runSpectatorInternal(
    GameSettings settings,
    PlayerInfo players,
    std::unique_ptr<NetworkSession> network,
    const Side first_turn)
{
#if XIANGQI_HAS_EASYX
    if (network == nullptr)
    {
        return 1;
    }

    settings.ai_enabled = false;
    settings.use_easyx = true;

    constexpr int cell = 56;
    constexpr int margin = 40;
    constexpr int side_panel = 320;
    constexpr int max_cols = 11;
    constexpr int width = margin * 2 + cell * (max_cols - 1) + side_panel;
    constexpr int board_top = 92;

    HWND easyx_window = showSharedEasyXWindow();
    EasyXCloseState close_state;
    installEasyXCloseHook(close_state, easyx_window);

    BeginBatchDraw();
    setbkcolor(RGB(247, 223, 180));
    setbkmode(TRANSPARENT);
    cleardevice();

    std::mutex state_mutex;
    std::optional<GameSession> standard_session;
    std::optional<DarkGameSession> dark_session;
    std::wstring status = L"Waiting for board state. First turn: " + utf8ToWide(toString(first_turn));
    std::atomic_bool stop_receiver{ false };
    std::atomic_bool needs_redraw{ true };

    // Spectators are strictly read-only: the receiver only accepts public board snapshots
    // and never sends moves, save requests, or replay commands back to the host.
    std::thread receiver_thread(
        [&]()
        {
            while (!stop_receiver.load())
            {
                try
                {
                    const auto fields = splitProtocol(network->receiveLine());
                    if (fields.empty())
                    {
                        continue;
                    }

                    std::lock_guard<std::mutex> lock(state_mutex);
                    if (settings.game_kind == GameKind::DarkChess && fields[0] == "DARK_STATE" && fields.size() >= 2)
                    {
                        dark_session = DarkGameSession::deserialize(unescapeProtocolField(fields[1]));
                        status = L"Spectating dark chess.";
                        needs_redraw = true;
                    }
                    else if (fields[0] == "STATE" && fields.size() >= 2)
                    {
                        standard_session = GameSession::deserialize(unescapeProtocolField(fields[1]));
                        status = L"Spectating xiangqi.";
                        needs_redraw = true;
                    }
                    else if (fields[0] == "ERROR" && fields.size() >= 2)
                    {
                        status = L"Network error: " + utf8ToWide(unescapeProtocolField(fields[1]));
                        needs_redraw = true;
                    }
                }
                catch (const std::exception& ex)
                {
                    if (!stop_receiver.load())
                    {
                        std::lock_guard<std::mutex> lock(state_mutex);
                        status = L"Connection ended: " + utf8ToWide(ex.what());
                        needs_redraw = true;
                    }
                    break;
                }
            }
        });

    auto boardLeft = [&](const Board& board)
    {
        return margin + ((max_cols - board.config().cols) * cell) / 2;
    };

    auto cellCenter = [&](const Board& board, const Position<int> position)
    {
        const int left = boardLeft(board);
        return POINT{ left + position.col * cell, board_top + position.row * cell };
    };

    auto drawWaiting = [&](const std::wstring& current_status)
    {
        cleardevice();
        settextcolor(BLACK);
        settextstyle(34, 0, L"Microsoft YaHei");
        outtextxy(54, 42, settings.game_kind == GameKind::DarkChess ? L"Dark Chess Spectator" : L"Xiangqi Spectator");
        settextstyle(22, 0, L"Microsoft YaHei");
        outtextxy(58, 110, current_status.c_str());
        const std::wstring player_line = L"Red: " + utf8ToWide(players.red_name) + L"    Black: " + utf8ToWide(players.black_name);
        outtextxy(58, 148, player_line.c_str());
        FlushBatchDraw();
    };

    auto drawStandardSpectator = [&](const std::optional<GameSession>& snapshot, const std::wstring& current_status)
    {
        if (!snapshot.has_value())
        {
            drawWaiting(current_status);
            return;
        }

        const GameSession& session = *snapshot;
        cleardevice();
        const Board& board = session.board();
        const int left = boardLeft(board);
        const int side_left = width - side_panel + 20;

        setlinecolor(BLACK);
        settextcolor(BLACK);
        settextstyle(34, 0, L"Microsoft YaHei");
        outtextxy(left, 24, L"Xiangqi Spectator");

        for (int row = 0; row < board.config().rows; ++row)
        {
            const POINT start = cellCenter(board, { row, 0 });
            const POINT end = cellCenter(board, { row, board.config().cols - 1 });
            line(start.x, start.y, end.x, end.y);
        }
        for (int col = 0; col < board.config().cols; ++col)
        {
            const POINT top_point = cellCenter(board, { 0, col });
            const POINT river_top = cellCenter(board, { board.config().river_split_top_row, col });
            const POINT river_bottom = cellCenter(board, { board.config().river_split_top_row + 1, col });
            const POINT bottom = cellCenter(board, { board.config().rows - 1, col });
            line(top_point.x, top_point.y, river_top.x, river_top.y);
            line(river_bottom.x, river_bottom.y, bottom.x, bottom.y);
        }

        const int min_col = board.config().palace_min_col;
        const int max_col = board.config().palace_max_col;
        line(cellCenter(board, { 0, min_col }).x, cellCenter(board, { 0, min_col }).y, cellCenter(board, { 2, max_col }).x, cellCenter(board, { 2, max_col }).y);
        line(cellCenter(board, { 0, max_col }).x, cellCenter(board, { 0, max_col }).y, cellCenter(board, { 2, min_col }).x, cellCenter(board, { 2, min_col }).y);
        line(cellCenter(board, { 7, min_col }).x, cellCenter(board, { 7, min_col }).y, cellCenter(board, { 9, max_col }).x, cellCenter(board, { 9, max_col }).y);
        line(cellCenter(board, { 7, max_col }).x, cellCenter(board, { 7, max_col }).y, cellCenter(board, { 9, min_col }).x, cellCenter(board, { 9, min_col }).y);

        settextstyle(26, 0, L"Microsoft YaHei");
        outtextxy(left + cell * ((board.config().cols - 1) / 2) - 56, board_top + cell * 4 + 10, L"\u695A\u6CB3\u6C49\u754C");

        settextstyle(24, 0, L"Microsoft YaHei");
        for (int row = 0; row < board.config().rows; ++row)
        {
            for (int col = 0; col < board.config().cols; ++col)
            {
                const Position<int> position{ row, col };
                const auto* piece = board.pieceAt(position);
                if (piece == nullptr)
                {
                    continue;
                }

                const POINT center = cellCenter(board, position);
                setfillcolor(piece->side() == Side::Red ? RGB(255, 245, 245) : RGB(240, 240, 240));
                solidcircle(center.x, center.y, 22);
                setlinecolor(piece->side() == Side::Red ? RGB(210, 0, 0) : RGB(10, 10, 10));
                circle(center.x, center.y, 22);
                settextcolor(piece->side() == Side::Red ? RGB(210, 0, 0) : RGB(10, 10, 10));
                wchar_t text[2]{ pieceFace(*piece), 0 };
                outtextxy(center.x - textwidth(text) / 2, center.y - textheight(text) / 2, text);
            }
        }

        settextcolor(BLACK);
        settextstyle(20, 0, L"Microsoft YaHei");
        outtextxy(side_left, 32, L"Read-only spectator");
        const std::wstring red = L"Red: " + utf8ToWide(session.players().red_name);
        const std::wstring black = L"Black: " + utf8ToWide(session.players().black_name);
        outtextxy(side_left, 72, red.c_str());
        outtextxy(side_left, 102, black.c_str());

        RECT status_rect{ side_left, 142, width - 28, 226 };
        drawtext(current_status.c_str(), &status_rect, DT_WORDBREAK | DT_NOPREFIX);
        const std::wstring current = L"Current: " + utf8ToWide(toString(session.currentSide())) +
            L" / " + utf8ToWide(session.currentPlayerName());
        outtextxy(side_left, 248, current.c_str());
        const std::wstring red_time = L"Red time: " + std::to_wstring(session.remainingSeconds(Side::Red)) + L"s";
        const std::wstring black_time = L"Black time: " + std::to_wstring(session.remainingSeconds(Side::Black)) + L"s";
        outtextxy(side_left, 278, red_time.c_str());
        outtextxy(side_left, 308, black_time.c_str());

        settextstyle(18, 0, L"Consolas");
        outtextxy(side_left, 356, L"Recent moves");
        const auto& history = session.history();
        const size_t start = history.size() > 9 ? history.size() - 9 : 0;
        int y = 386;
        for (size_t index = start; index < history.size(); ++index)
        {
            const std::wstring text = std::to_wstring(index + 1) + L". " +
                utf8ToWide(coordText(history[index].from, board.config()) + " " + coordText(history[index].to, board.config()));
            RECT line_rect{ side_left, y, width - 28, y + 24 };
            drawtext(text.c_str(), &line_rect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            y += 26;
        }

        if (session.gameOver())
        {
            settextstyle(26, 0, L"Microsoft YaHei");
            settextcolor(RGB(190, 40, 40));
            const std::wstring result = utf8ToWide(session.resultText());
            RECT result_rect{ left + 20, board_top + cell * 4 - 36, left + cell * (board.config().cols - 1) - 20, board_top + cell * 5 + 42 };
            setfillcolor(RGB(255, 252, 244));
            solidrectangle(result_rect.left, result_rect.top, result_rect.right, result_rect.bottom);
            rectangle(result_rect.left, result_rect.top, result_rect.right, result_rect.bottom);
            drawtext(result.c_str(), &result_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        FlushBatchDraw();
    };

    auto drawDarkSpectator = [&](const std::optional<DarkGameSession>& snapshot, const std::wstring& current_status)
    {
        if (!snapshot.has_value())
        {
            drawWaiting(current_status);
            return;
        }

        const DarkGameSession& session = *snapshot;
        constexpr int dark_cell = 70;
        constexpr int left = 60;
        constexpr int top = 90;
        constexpr int side_left = 690;

        cleardevice();
        settextcolor(BLACK);
        settextstyle(34, 0, L"Microsoft YaHei");
        outtextxy(left, 28, L"Dark Chess Spectator");
        settextstyle(18, 0, L"Microsoft YaHei");
        outtextxy(left + 250, 40, L"Read-only");

        setlinecolor(RGB(80, 60, 40));
        for (int row = 0; row <= DarkBoard::kRows; ++row)
        {
            line(left, top + row * dark_cell, left + DarkBoard::kCols * dark_cell, top + row * dark_cell);
        }
        for (int col = 0; col <= DarkBoard::kCols; ++col)
        {
            line(left + col * dark_cell, top, left + col * dark_cell, top + DarkBoard::kRows * dark_cell);
        }

        settextstyle(28, 0, L"Microsoft YaHei");
        for (int row = 0; row < DarkBoard::kRows; ++row)
        {
            for (int col = 0; col < DarkBoard::kCols; ++col)
            {
                const Position<int> position{ row, col };
                if (!session.board().isOccupied(position))
                {
                    continue;
                }

                const int cx = left + col * dark_cell + dark_cell / 2;
                const int cy = top + row * dark_cell + dark_cell / 2;
                const auto piece = session.board().pieceAt(position);
                if (!piece.has_value() || !piece->is_open)
                {
                    setfillcolor(RGB(130, 88, 54));
                    solidcircle(cx, cy, 26);
                    setlinecolor(RGB(70, 42, 22));
                    circle(cx, cy, 26);
                    settextcolor(WHITE);
                    outtextxy(cx - textwidth(L"?") / 2, cy - textheight(L"?") / 2, L"?");
                    continue;
                }

                setfillcolor(piece->side == Side::Red ? RGB(255, 245, 245) : RGB(240, 240, 240));
                solidcircle(cx, cy, 26);
                setlinecolor(piece->side == Side::Red ? RGB(210, 0, 0) : RGB(10, 10, 10));
                circle(cx, cy, 26);
                settextcolor(piece->side == Side::Red ? RGB(210, 0, 0) : RGB(10, 10, 10));
                wchar_t text[2]{ darkPieceFace(*piece), 0 };
                outtextxy(cx - textwidth(text) / 2, cy - textheight(text) / 2, text);
            }
        }

        settextstyle(20, 0, L"Microsoft YaHei");
        settextcolor(BLACK);
        outtextxy(side_left, 34, L"Spectator");
        const std::wstring p1 = L"P1: " + utf8ToWide(session.players().red_name);
        const std::wstring p2 = L"P2: " + utf8ToWide(session.players().black_name);
        outtextxy(side_left, 74, p1.c_str());
        outtextxy(side_left, 104, p2.c_str());

        RECT status_rect{ side_left, 144, width - 28, 226 };
        drawtext(current_status.c_str(), &status_rect, DT_WORDBREAK | DT_NOPREFIX);
        const std::wstring current = L"Current: " + std::wstring(session.currentSeat() == DarkSeat::Player1 ? L"Player1" : L"Player2") +
            L" / " + utf8ToWide(session.currentPlayerName());
        outtextxy(side_left, 250, current.c_str());

        const auto p1_color = session.colorForSeat(DarkSeat::Player1);
        const auto p2_color = session.colorForSeat(DarkSeat::Player2);
        const std::wstring colors = L"P1 color: " + utf8ToWide(p1_color.has_value() ? toString(*p1_color) : "Unknown") +
            L"    P2 color: " + utf8ToWide(p2_color.has_value() ? toString(*p2_color) : "Unknown");
        outtextxy(side_left, 280, colors.c_str());
        const std::wstring times = L"P1 time: " + std::to_wstring(session.remainingSeconds(DarkSeat::Player1)) +
            L"s    P2 time: " + std::to_wstring(session.remainingSeconds(DarkSeat::Player2)) + L"s";
        outtextxy(side_left, 310, times.c_str());

        if (session.gameOver())
        {
            settextstyle(26, 0, L"Microsoft YaHei");
            settextcolor(RGB(190, 40, 40));
            const std::wstring result = utf8ToWide(session.resultText());
            outtextxy(left, top + DarkBoard::kRows * dark_cell + 38, result.c_str());
        }

        FlushBatchDraw();
    };

    while (!shouldCloseEasyXWindow(close_state))
    {
        if (needs_redraw.exchange(false))
        {
            std::optional<GameSession> standard_snapshot;
            std::optional<DarkGameSession> dark_snapshot;
            std::wstring status_snapshot;
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                standard_snapshot = standard_session;
                dark_snapshot = dark_session;
                status_snapshot = status;
            }

            if (settings.game_kind == GameKind::DarkChess)
            {
                drawDarkSpectator(dark_snapshot, status_snapshot);
            }
            else
            {
                drawStandardSpectator(standard_snapshot, status_snapshot);
            }
        }

        ExMessage msg{};
        while (peekmessage(&msg, EX_KEY))
        {
            if (msg.message == WM_KEYDOWN && msg.vkcode == VK_ESCAPE)
            {
                close_state.close_requested = true;
            }
        }
        Sleep(16);
    }

    stop_receiver = true;
    network->close();
    if (receiver_thread.joinable())
    {
        receiver_thread.join();
    }
    restoreEasyXCloseHook(close_state);
    flushmessage();
    FlushMouseMsgBuffer();
    EndBatchDraw();
    hideSharedEasyXWindow();
    return 0;
#else
    (void)settings;
    (void)players;
    (void)network;
    (void)first_turn;
    return 1;
#endif
}

} // namespace

bool EasyXApp::isAvailable() const noexcept
{
    return XIANGQI_HAS_EASYX != 0;
}

int EasyXApp::run(GameSettings settings, PlayerInfo players)
{
    if (settings.game_kind == GameKind::DarkChess)
    {
        return runDarkInternal(std::move(settings), std::move(players), nullptr, std::nullopt);
    }
    return runInternal(std::move(settings), std::move(players), nullptr, std::nullopt, std::nullopt);
}

int EasyXApp::runNetworkGame(
    GameSettings settings,
    PlayerInfo players,
    std::unique_ptr<NetworkSession> network,
    const Side local_side,
    SpectatorConnections preconnected_spectators)
{
    if (settings.game_kind == GameKind::DarkChess)
    {
        return runDarkInternal(
            std::move(settings),
            std::move(players),
            std::move(network),
            local_side,
            std::nullopt,
            std::move(preconnected_spectators));
    }
    return runInternal(
        std::move(settings),
        std::move(players),
        std::move(network),
        local_side,
        std::nullopt,
        std::move(preconnected_spectators));
}

int EasyXApp::runSpectatorGame(
    GameSettings settings,
    PlayerInfo players,
    std::unique_ptr<NetworkSession> network,
    const Side first_turn)
{
    return runSpectatorInternal(std::move(settings), std::move(players), std::move(network), first_turn);
}

int EasyXApp::runReplayFile(const std::filesystem::path& path)
{
    if (replayFileLooksDark(path))
    {
        auto dark_replay = storage::loadDarkReplay(path.string());
        DarkGameSession replay_session = makeDarkReplaySession(dark_replay);
        GameSettings replay_settings = replay_session.settings();
        replay_settings.ai_enabled = false;
        replay_settings.use_easyx = true;
        replay_settings.game_kind = GameKind::DarkChess;
        return runDarkInternal(replay_settings, replay_session.players(), nullptr, std::nullopt, replay_session);
    }
    auto replay = storage::loadReplay(path.string());
    replay.settings.ai_enabled = false;
    replay.settings.use_easyx = true;
    return runInternal(replay.settings, replay.players, nullptr, std::nullopt, std::optional<storage::ReplayRecord>{ replay });
}

int EasyXApp::runReplayBrowser()
{
    if (const auto path = browseForPgnFile(); path.has_value())
    {
        return runReplayFile(*path);
    }
    return 0;
}

} // namespace xiangqi
