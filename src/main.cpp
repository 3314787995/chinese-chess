#include "ai/SearchEngine.h"
#include "app/GameSession.h"
#include "tests/EngineSelfTest.h"
#include "ui_console/ConsoleApp.h"

#include <exception>
#include <iostream>
#include <new>
#include <string>

int main(int argc, char* argv[])
{
    if (argc > 1)
    {
        const std::string arg = argv[1];
        if (arg == "--selftest")
        {
            try
            {
                std::cout << xiangqi::tests::runAll() << '\n';
                return 0;
            }
            catch (const std::bad_alloc&)
            {
                std::cerr << "自测失败：内存不足。\n";
                return 1;
            }
            catch (const std::exception& ex)
            {
                std::cerr << "自测失败：" << ex.what() << '\n';
                return 1;
            }
        }
        if (arg == "--smoke")
        {
            xiangqi::GameSettings settings;
            xiangqi::GameSession session(settings);
            const auto legal = session.legalMovesForCurrentSide();
            if (legal.empty())
            {
                std::cerr << "没有可用的开局合法走法。\n";
                return 1;
            }

            xiangqi::SearchEngine search(1);
            auto move = legal.front();
            move.notation = "smoke";
            session.submitMove(move);
            const auto reply = search.chooseBestMove(session);
            if (!reply.has_value())
            {
                std::cerr << "AI 未能找到应招。\n";
                return 1;
            }

            std::cout << "冒烟测试通过。\n";
            return 0;
        }
    }

    try
    {
        xiangqi::ConsoleApp app;
        return app.run();
    }
    catch (const std::bad_alloc&)
    {
        std::cerr << "致命错误：内存不足，请关闭其他程序后重试。\n";
    }
    catch (const std::exception& ex)
    {
        std::cerr << "致命错误：" << ex.what() << '\n';
    }
    catch (...)
    {
        std::cerr << "致命错误：发生未知错误。\n";
    }
    return 1;
}
