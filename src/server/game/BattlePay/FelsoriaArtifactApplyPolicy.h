#ifndef FELSORIA_ARTIFACT_APPLY_POLICY_H
#define FELSORIA_ARTIFACT_APPLY_POLICY_H
#include <cstdint>
#include <limits>
#include <string>
namespace FelsoriaArtifactApply
{
    constexpr char Prefix[] = "FelsArtSkin";
    constexpr std::uint32_t Protocol = 2;
    constexpr std::uint32_t RequestInterval = 100;
    struct Request
    {
        std::uint32_t Token = 0;
        std::uint32_t ItemID = 0;
        std::uint32_t AppearanceID = 0;
    };
    inline bool ParsePositive(std::string const& text, std::size_t& offset, std::uint32_t& value, bool last)
    {
        std::uint32_t number = 0;
        std::size_t const start = offset;
        while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9')
        {
            std::uint32_t const digit = std::uint32_t(text[offset++] - '0');
            if (number > ((std::numeric_limits<std::uint32_t>::max)() - digit) / 10)
                return false;
            number = number * 10 + digit;
        }
        if (offset == start || !number || offset - start > 10 || text[start] == '0')
            return false;
        if (last ? offset != text.size() : offset == text.size() || text[offset++] != ':')
            return false;
        value = number;
        return true;
    }
    inline bool ParseRequest(std::string const& message, Request& result)
    {
        if (message.size() < 11 || message.size() > 38 || message.compare(0, 6, "APPLY:") != 0)
            return false;
        Request parsed;
        std::size_t offset = 6;
        if (!ParsePositive(message, offset, parsed.Token, false) ||
            !ParsePositive(message, offset, parsed.ItemID, false) ||
            !ParsePositive(message, offset, parsed.AppearanceID, true))
            return false;
        result = parsed;
        return true;
    }
}
#endif
