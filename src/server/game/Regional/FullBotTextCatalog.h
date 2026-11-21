#ifndef FELSORIA_FULL_BOT_TEXT_CATALOG_H
#define FELSORIA_FULL_BOT_TEXT_CATALOG_H

#include <array>
#include <functional>
#include <istream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>

namespace FelsoriaRegional
{
namespace FullText
{
using Tokens = std::map<std::string, std::string>;
struct Line
{
    std::string id;
    std::array<std::string, 6> text;
};

inline bool TokenStart(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

inline bool Render(std::string const& input, Tokens const& tokens, std::string& output)
{
    output.clear();
    for (std::size_t i = 0; i < input.size();)
    {
        std::size_t end = i;
        if (input.compare(i, 8, "<target>") == 0)
            end += 8;
        else if (input[i] == '%' && i + 1 < input.size() && TokenStart(input[i + 1]))
        {
            end += 2;
            while (end < input.size() && (TokenStart(input[end]) || (input[end] >= '0' && input[end] <= '9')))
                ++end;
        }
        if (end == i)
            output += input[i++];
        else
        {
            auto entry = tokens.find(input.substr(i, end - i));
            if (entry == tokens.end() || entry->second.empty())
                return false;
            output += entry->second;
            i = end;
        }
        if (output.size() > 255)
            return false;
    }
    return !output.empty();
}

class Catalog
{
public:
    using Pools = std::map<std::string, std::vector<Line>>;
    bool Load(std::istream& stream, std::size_t expected, std::function<bool(std::string const&)> const& validUtf8)
    {
        Pools candidate;
        std::map<std::string, std::pair<std::string, Line>> byId;
        std::string row;
        while (std::getline(stream, row))
        {
            if (!row.empty() && row.back() == '\r')
                row.pop_back();
            if (row.empty() || row[0] == '#')
                continue;
            auto first = row.find('\t');
            auto second = first == std::string::npos ? first : row.find('\t', first + 1);
            auto third = second == std::string::npos ? second : row.find('\t', second + 1);
            if (first != 8 || second == std::string::npos || third != second + 2 || row.find('\t', third + 1) != std::string::npos)
                return false;
            std::string id = row.substr(0, first), key = row.substr(first + 1, second - first - 1);
            char locale = row[second + 1];
            std::string value = row.substr(third + 1);
            if ((id.compare(0, 3, "FBT") && id.compare(0, 3, "FBN")) ||
                !std::all_of(id.begin() + 3, id.end(), [](char c) { return c >= '0' && c <= '9'; }) ||
                key.empty() || locale < '0' || locale > '5' || value.empty() || value.size() > 255 ||
                value.find_first_of("\r\n") != std::string::npos || value.find('\0') != std::string::npos || !validUtf8(value))
                return false;
            auto& entry = byId[id];
            Line& line = entry.second;
            if ((!entry.first.empty() && entry.first != key) || !line.text[locale - '0'].empty())
                return false;
            entry.first = key;
            line.id = id;
            line.text[locale - '0'] = std::move(value);
        }
        if (stream.bad() || byId.size() != expected)
            return false;
        for (auto& entry : byId)
        {
            Line& line = entry.second.second;
            if (std::any_of(line.text.begin(), line.text.end(), [](std::string const& value) { return value.empty(); }))
                return false;
            candidate[entry.second.first].push_back(std::move(line));
        }
        _pools.swap(candidate);
        return true;
    }

    std::vector<Line> const* Find(std::string const& key) const
    {
        auto found = _pools.find(key);
        return found == _pools.end() ? nullptr : &found->second;
    }

    static std::string Pick(std::vector<Line> const& pool, unsigned locale, std::size_t cursor, Tokens const& facts, std::size_t& next, std::string* lineId = nullptr)
    {
        if (locale >= 6)
            return {};
        std::string text;
        for (std::size_t offset = 0; offset < pool.size(); ++offset)
        {
            std::size_t index = (cursor + offset) % pool.size();
            if (Render(pool[index].text[locale], facts, text))
            {
                next = (index + 1) % pool.size();
                if (lineId)
                    *lineId = pool[index].id;
                return text;
            }
        }
        return {};
    }

private:
    Pools _pools;
};
}
}
#endif
