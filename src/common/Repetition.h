#pragma once

#include "common/Types.h"

#include <cstddef>
#include <cstdint>

namespace xiangqi
{

struct RepetitionState
{
    uint64_t key{};
    Side side_to_move{ Side::Red };
};

struct RepetitionPlyInfo
{
    Side mover{ Side::Red };
    bool gives_check{ false };
    bool gives_chase{ false };
};

template <typename StateContainer, typename PlyContainer>
bool isPerpetualCheck(
    const StateContainer& states,
    const size_t state_count,
    const PlyContainer& plies,
    const size_t ply_count,
    const Side mover)
{
    if (state_count < 3 || ply_count + 1 != state_count)
    {
        return false;
    }

    const auto& current_state = states[state_count - 1];
    for (size_t repeat_index = 0; repeat_index + 2 < state_count; ++repeat_index)
    {
        const auto& previous_state = states[repeat_index];
        if (previous_state.key != current_state.key || previous_state.side_to_move != current_state.side_to_move)
        {
            continue;
        }

        bool mover_checked_every_turn = true;
        int mover_check_count = 0;
        for (size_t ply_index = repeat_index; ply_index < ply_count; ++ply_index)
        {
            const auto& ply = plies[ply_index];
            if (ply.mover != mover)
            {
                continue;
            }

            ++mover_check_count;
            if (!ply.gives_check)
            {
                mover_checked_every_turn = false;
                break;
            }
        }

        if (mover_checked_every_turn && mover_check_count >= 2)
        {
            return true;
        }
    }

    return false;
}

template <typename StateContainer, typename PlyContainer>
bool isPerpetualChase(
    const StateContainer& states,
    const size_t state_count,
    const PlyContainer& plies,
    const size_t ply_count,
    const Side mover)
{
    if (state_count < 3 || ply_count + 1 != state_count)
    {
        return false;
    }

    const auto& current_state = states[state_count - 1];
    for (size_t repeat_index = 0; repeat_index + 2 < state_count; ++repeat_index)
    {
        const auto& previous_state = states[repeat_index];
        if (previous_state.key != current_state.key || previous_state.side_to_move != current_state.side_to_move)
        {
            continue;
        }

        bool mover_chased_every_turn = true;
        int mover_chase_count = 0;
        for (size_t ply_index = repeat_index; ply_index < ply_count; ++ply_index)
        {
            const auto& ply = plies[ply_index];
            if (ply.mover != mover)
            {
                continue;
            }

            ++mover_chase_count;
            if (!ply.gives_chase)
            {
                mover_chased_every_turn = false;
                break;
            }
        }

        if (mover_chased_every_turn && mover_chase_count >= 2)
        {
            return true;
        }
    }

    return false;
}

} // namespace xiangqi
