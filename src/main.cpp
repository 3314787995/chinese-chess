#include "ai/SearchEngine.h"
#include "app/GameSession.h"
#include "tests/EngineSelfTest.h"
#include "ui_console/ConsoleApp.h"

#include <iostream>
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
            catch (const std::exception& ex)
            {
                std::cerr << "Self-test failed: " << ex.what() << '\n';
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
                std::cerr << "No legal opening move.\n";
                return 1;
            }

            xiangqi::SearchEngine search(1);
            auto move = legal.front();
            move.notation = "smoke";
            session.submitMove(move);
            const auto reply = search.chooseBestMove(session);
            if (!reply.has_value())
            {
                std::cerr << "AI failed to find a reply.\n";
                return 1;
            }

            std::cout << "Smoke test passed.\n";
            return 0;
        }
    }

    xiangqi::ConsoleApp app;
    return app.run();
}
