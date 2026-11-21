#ifndef FELSORIA_QUIET_BOT_LOGS_H
#define FELSORIA_QUIET_BOT_LOGS_H
#include "LogCommon.h"
#include <atomic>
#include <cstring>

namespace FelsoriaQuietBotLogs
{
inline std::atomic<bool>& Setting()
{
    static std::atomic<bool> enabled{true};
    return enabled;
}
inline void SetEnabled(bool enabled)
{
    Setting().store(enabled, std::memory_order_relaxed);
}
template<std::size_t N>
inline bool StartsWith(char const* text, char const (&prefix)[N])
{
    return std::strncmp(text, prefix, N - 1) == 0;
}
inline bool MissingBotSpell(char const* text)
{
    if (!StartsWith(text, "1 bot spell ") && !StartsWith(text, "2 bot spell "))
        return false;
    char const* id = text + sizeof("1 bot spell ") - 1;
    if (*id < '0' || *id > '9')
        return false;
    do { ++id; } while (*id >= '0' && *id <= '9');
    return std::strcmp(id, " not found") == 0 ||
           std::strcmp(id, " not found\n") == 0 ||
           std::strcmp(id, " not found\r\n") == 0;
}
template<std::size_t N>
inline bool ExactLine(char const* text, char const (&line)[N])
{
    std::size_t length = std::strlen(text);
    while (length && (text[length - 1] == '\r' || text[length - 1] == '\n'))
        --length;
    return length == N - 1 && std::memcmp(text, line, N - 1) == 0;
}
inline bool ShouldSuppressConsole(LogLevel level, char const* text)
{
    if (!text || !Setting().load(std::memory_order_relaxed) || level != LOG_LEVEL_ERROR)
        return false;
    // These errors remain available to file/DB appenders and startup capture.
    return ExactLine(text, "Unknown item enchantment (id = 5852) display type: 12") ||
           ExactLine(text, "Unknown item enchantment (id = 5852) display type: 9") ||
           ExactLine(text, "Creature (entry: 200301) has no model defined in table `creature_template`, can't load.") ||
           StartsWith(text, "DYNAMIC_BOT_CREATE_FAILED ") ||
           StartsWith(text, "BOT_ARTIFACT_SLOT_REJECTED ") ||
           StartsWith(text, "Received CMSG_DISCARDED_TIME_SYNC_ACKS from player ") ||
           StartsWith(text, "BattlegroundQueue: couldn't find player to remove GUID: ") ||
           (StartsWith(text, "SQL(p): INSERT INTO character_talent ") &&
            std::strstr(text, "[1213]") &&
            std::strstr(text, "Deadlock found when trying to get lock"));
}
inline bool ShouldSuppress(LogLevel level, char const* text)
{
    if (!text || !Setting().load(std::memory_order_relaxed))
        return false;
    if (level == LOG_LEVEL_ERROR)
        return MissingBotSpell(text);
    if (level < LOG_LEVEL_TRACE || level > LOG_LEVEL_INFO)
        return false;
    return StartsWith(text, "DYNAMIC_") ||
           StartsWith(text, "FELSORIA_AB_") ||
           StartsWith(text, "REGIONAL_BOTCHAT_") ||
           StartsWith(text, "TalentID:") ||
           StartsWith(text, "BOT_MOVIE_SKIPPED") ||
           StartsWith(text, "BOT_ARTIFACT_REPAIR_READY ") ||
           ExactLine(text, "server.loading");
}
}
#endif
