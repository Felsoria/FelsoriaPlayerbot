#ifndef FELSORIA_DYNAMIC_PARTY_REGISTRY_H
#define FELSORIA_DYNAMIC_PARTY_REGISTRY_H

#include <algorithm>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace FelsoriaDynamicPartyRegistry
{
// Only native membership events may remove an observed owner or member.
// Readiness, map transfers, an empty DB read and snapshot age do not do so.
class Registry
{
    struct Party
    {
        std::vector<std::uint32_t> members;
        std::map<std::uint32_t, std::uint32_t> humans; // character -> account
    };
    mutable std::mutex lock;
    std::map<std::uint64_t, Party> parties;
    std::set<std::uint64_t> closed;

public:
    bool Observe(std::uint64_t group, std::vector<std::uint32_t> members,
        std::map<std::uint32_t, std::uint32_t> humans)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (!group || closed.count(group)) return false;
        for (auto it = humans.begin(); it != humans.end(); )
            if (std::find(members.begin(), members.end(), it->first) == members.end())
                it = humans.erase(it);
            else
                ++it;
        auto found = parties.find(group);
        if (found != parties.end())
            for (auto const& human : found->second.humans)
                if (std::find(members.begin(), members.end(), human.first) != members.end())
                    humans.emplace(human);
        if (humans.empty())
            return parties.erase(group) != 0;
        if (found != parties.end() && found->second.members == members && found->second.humans == humans)
            return false;
        parties[group] = {std::move(members), std::move(humans)};
        return true;
    }

    bool Remove(std::uint64_t group, std::uint32_t member)
    {
        std::lock_guard<std::mutex> guard(lock);
        auto found = parties.find(group);
        if (found == parties.end()) return false;
        auto& party = found->second;
        party.members.erase(std::remove(party.members.begin(), party.members.end(), member), party.members.end());
        party.humans.erase(member);
        if (party.humans.empty()) parties.erase(found);
        return true;
    }

    bool Forget(std::uint64_t group)
    {
        std::lock_guard<std::mutex> guard(lock);
        return parties.erase(group) != 0;
    }

    bool Close(std::uint64_t group)
    {
        std::lock_guard<std::mutex> guard(lock);
        closed.insert(group);
        return parties.erase(group) != 0;
    }

    bool Contains(std::uint64_t group) const
    {
        std::lock_guard<std::mutex> guard(lock);
        return parties.count(group) != 0;
    }

    bool Protects(std::uint32_t member, std::uint64_t group = 0) const
    {
        std::lock_guard<std::mutex> guard(lock);
        for (auto const& entry : parties)
            if ((!group || entry.first == group) && !entry.second.humans.count(member) &&
                std::find(entry.second.members.begin(), entry.second.members.end(), member) != entry.second.members.end())
                return true;
        return false;
    }

    std::map<std::uint32_t, std::uint32_t> Owners(std::uint32_t member) const
    {
        std::lock_guard<std::mutex> guard(lock);
        for (auto const& entry : parties)
            if (!entry.second.humans.count(member) &&
                std::find(entry.second.members.begin(), entry.second.members.end(), member) != entry.second.members.end())
                return entry.second.humans;
        return {};
    }
};
}
#endif
