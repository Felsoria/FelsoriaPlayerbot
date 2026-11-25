/* Felsoria objective assignment for normal Twin Peaks and Temple of Kotmogu. */
#ifndef FELSORIA_OBJECTIVE_POLICY_H
#define FELSORIA_OBJECTIVE_POLICY_H

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace FelsoriaObjectives
{
enum class TwinTask { ReturnHome, ReturnFlag, TakeFlag, Intercept, Escort, Defend };

inline TwinTask PlanTwin(bool carrying, bool ownDropped, bool ownStolen,
    bool friendlyCarrier, bool enemyAvailable, unsigned rank, unsigned count)
{
    unsigned recoverers = std::max(1u, count / 3);
    if (ownDropped && (carrying || rank < recoverers)) return TwinTask::ReturnFlag;
    if (carrying) return TwinTask::ReturnHome;
    if (ownStolen && rank < recoverers) return TwinTask::Intercept;
    if (friendlyCarrier)
        return rank % 2 == 0 ? TwinTask::Escort :
            (ownStolen ? TwinTask::Intercept : TwinTask::Defend);
    if (enemyAvailable)
        return !ownDropped && !ownStolen && count >= 6 && rank >= count - 2 ?
            TwinTask::Defend : TwinTask::TakeFlag;
    return ownStolen ? TwinTask::Intercept : TwinTask::Defend;
}

// At most one living, non-carrier bot from this team is sent to each free orb.
// Costs are squared distances; infinity excludes an unavailable bot/orb pair.
inline std::vector<int> AssignOrbs(std::vector<std::array<float, 4>> const& costs,
    std::array<bool, 4> const& free)
{
    std::vector<int> assignments(costs.size(), -1);
    for (unsigned orb = 0; orb < 4; ++orb)
    {
        if (!free[orb]) continue;
        std::size_t best = costs.size();
        float distance = std::numeric_limits<float>::infinity();
        for (std::size_t bot = 0; bot < costs.size(); ++bot)
            if (assignments[bot] < 0 && std::isfinite(costs[bot][orb]) && costs[bot][orb] < distance)
            { best = bot; distance = costs[bot][orb]; }
        if (best != costs.size()) assignments[best] = int(orb);
    }
    return assignments;
}
}
#endif
