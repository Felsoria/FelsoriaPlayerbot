/*
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <sstream>
#if PLATFORM == TC_PLATFORM_WINDOWS
#include <windows.h>
#endif

#include "AppenderConsole.h"
#include "FelsoriaQuietBotLogs.h"
#include "Config.h"
#include "Util.h"


AppenderConsole::AppenderConsole(uint8 id, std::string const& name, LogLevel level, AppenderFlags flags) :
    Appender(id, name, APPENDER_CONSOLE, level, flags), _colored(false)
{
    for (auto & _color : _colors)
        _color = ColorTypes(MaxColors);
}

void AppenderConsole::InitColors(std::string const& str)
{
    if (str.empty())
    {
        _colored = false;
        return;
    }

    int color[NUM_ENABLED_LOG_LEVELS];

    std::istringstream ss(str);

    for (auto& i : color)
    {
        ss >> i;

        if (!ss)
            return;

        if (i < 0 || i >= MaxColors)
            return;
    }

    for (uint8 i = 0; i < NUM_ENABLED_LOG_LEVELS; ++i)
        _colors[i] = ColorTypes(color[i]);

    _colored = true;
}

void AppenderConsole::SetColor(bool stdout_stream, ColorTypes color)
{
#if PLATFORM == TC_PLATFORM_WINDOWS
    static WORD WinColorFG[MaxColors] =
    {
        0,                                                  // BLACK
        FOREGROUND_RED,                                     // RED
        FOREGROUND_GREEN,                                   // GREEN
        FOREGROUND_RED | FOREGROUND_GREEN,                  // BROWN
        FOREGROUND_BLUE,                                    // BLUE
        FOREGROUND_RED |                    FOREGROUND_BLUE, // MAGENTA
        FOREGROUND_GREEN | FOREGROUND_BLUE,                 // CYAN
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, // WHITE
                                                            // YELLOW
        FOREGROUND_RED | FOREGROUND_GREEN |                   FOREGROUND_INTENSITY,
                                                            // RED_BOLD
        FOREGROUND_RED |                                      FOREGROUND_INTENSITY,
                                                            // GREEN_BOLD
        FOREGROUND_GREEN |                   FOREGROUND_INTENSITY,
        FOREGROUND_BLUE | FOREGROUND_INTENSITY,             // BLUE_BOLD
                                                            // MAGENTA_BOLD
        FOREGROUND_RED |                    FOREGROUND_BLUE | FOREGROUND_INTENSITY,
                                                            // CYAN_BOLD
        FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
                                                            // WHITE_BOLD
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY
    };

    HANDLE hConsole = GetStdHandle(stdout_stream ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    SetConsoleTextAttribute(hConsole, WinColorFG[color]);
#else
    enum ANSITextAttr
    {
        TA_BOLD                                  = 1,
        TA_BLINK                                 = 5,
        TA_REVERSE                               = 7
    };

    enum ANSIFgTextAttr
    {
        FG_BLACK                                 = 30,
        FG_RED,
        FG_GREEN,
        FG_BROWN,
        FG_BLUE,
        FG_MAGENTA,
        FG_CYAN,
        FG_WHITE,
        FG_YELLOW
    };

    enum ANSIBgTextAttr
    {
        BG_BLACK                                 = 40,
        BG_RED,
        BG_GREEN,
        BG_BROWN,
        BG_BLUE,
        BG_MAGENTA,
        BG_CYAN,
        BG_WHITE
    };

    static uint8 UnixColorFG[MaxColors] =
    {
        FG_BLACK,                                          // BLACK
        FG_RED,                                            // RED
        FG_GREEN,                                          // GREEN
        FG_BROWN,                                          // BROWN
        FG_BLUE,                                           // BLUE
        FG_MAGENTA,                                        // MAGENTA
        FG_CYAN,                                           // CYAN
        FG_WHITE,                                          // WHITE
        FG_YELLOW,                                         // YELLOW
        FG_RED,                                            // LRED
        FG_GREEN,                                          // LGREEN
        FG_BLUE,                                           // LBLUE
        FG_MAGENTA,                                        // LMAGENTA
        FG_CYAN,                                           // LCYAN
        FG_WHITE                                           // LWHITE
    };

    fprintf((stdout_stream ? stdout : stderr), "\x1b[%d%sm", UnixColorFG[color], (color >= YELLOW && color < MaxColors ? ";1" : ""));
#endif
}

void AppenderConsole::ResetColor(bool stdout_stream)
{
#if PLATFORM == TC_PLATFORM_WINDOWS
    auto hConsole = GetStdHandle(stdout_stream ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
#else
    fprintf((stdout_stream ? stdout : stderr), "\x1b[0m");
#endif
}

void AppenderConsole::_write(LogMessage const* message)
{
    // FELSORIA_QUIET_CONSOLE_V1_1: selected errors stay in file/DB logs.
    if (message && FelsoriaQuietBotLogs::ShouldSuppressConsole(message->level, message->text.c_str()))
        return;
    if (message->felsoriaStartupHidden)
        return;

    bool stdout_stream = !(message->level == LOG_LEVEL_ERROR || message->level == LOG_LEVEL_FATAL);

    if (_colored)
    {
        uint8 index;
        switch (message->level)
        {
            case LOG_LEVEL_TRACE:
               index = 5;
             break;
            case LOG_LEVEL_DEBUG:
               index = 4;
              break;
            case LOG_LEVEL_INFO:
              index = 3;
              break;
           case LOG_LEVEL_WARN:
               index = 2;
               break;
           case LOG_LEVEL_FATAL:
               index = 0;
               break;
            case LOG_LEVEL_ERROR: // No break on purpose
            default:
                index = 1;
                break;
        }

        SetColor(stdout_stream, _colors[index]);
        utf8printf(stdout_stream ? stdout : stderr, "%s%s", message->prefix.c_str(), message->text.c_str());
        ResetColor(stdout_stream);
    }
    else
        utf8printf(stdout_stream ? stdout : stderr, "%s%s", message->prefix.c_str(), message->text.c_str());
}


// FELSORIA_CLEAN_STARTUP_V1
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#if PLATFORM != TC_PLATFORM_WINDOWS
#include <unistd.h>
#endif

namespace FelsoriaStartup
{
    namespace
    {
        struct State
        {
            std::mutex mutex;
            std::condition_variable wake;
            bool active = false;
            bool animated = false;
            FILE* file = nullptr;
            std::thread spinner;
            std::string lastError;
            std::string path;
            std::chrono::steady_clock::time_point started;

            void Stop(bool success)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (!active)
                        return;
                    active = false;
                    wake.notify_all();
                }
                if (spinner.joinable())
                    spinner.join();
                std::lock_guard<std::mutex> lock(mutex);
                auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - started).count();
                if (file)
                {
                    std::fprintf(file, "\n[FELSORIA] Startup %s after %lld seconds.\n",
                        success ? "completed" : "failed or interrupted", static_cast<long long>(seconds));
                    std::fclose(file);
                    file = nullptr;
                }
                if (animated)
                    std::fputs("\r                                                                                \r", stdout);
                if (success)
                    std::fprintf(stdout, "      [ OK ] Worldserver is ready. You can now connect. (%lld seconds)\n",
                        static_cast<long long>(seconds));
                else
                {
                    std::fprintf(stdout, "      [ FAILED ] Startup failed or was interrupted. See %s\n", path.c_str());
                    if (!lastError.empty())
                        std::fprintf(stdout, "      Last error: %s\n", lastError.c_str());
                }
                std::fflush(stdout);
            }

            ~State() { Stop(false); }
        };

        State& GetState()
        {
            static State state;
            return state;
        }
    }

    void Begin()
    {
        if (!sConfigMgr->GetBoolDefault("Felsoria.Startup.Quiet", true))
            return;
        State& state = GetState();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.active)
            return;
        state.path = sConfigMgr->GetStringDefault("LogsDir", "");
        if (!state.path.empty() && state.path.back() != '/' && state.path.back() != '\\')
            state.path += '/';
        state.path += "Startup.log";
        state.file = std::fopen(state.path.c_str(), "a");
        if (!state.file)
        {
            std::fprintf(stderr, "Felsoria: cannot open %s; keeping normal startup output.\n", state.path.c_str());
            std::fflush(stderr);
            return;
        }
        std::fputs("\n========== PROJECT FELSORIA STARTUP ==========\n", state.file);
        std::fflush(state.file);
        state.active = true;
        state.lastError.clear();
        state.started = std::chrono::steady_clock::now();
#if PLATFORM == TC_PLATFORM_WINDOWS
        DWORD mode = 0;
        state.animated = GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode) != 0;
#else
        state.animated = isatty(fileno(stdout)) != 0;
#endif
        state.animated = state.animated && sConfigMgr->GetBoolDefault("Felsoria.Startup.Spinner", true);
        std::fputs("      [ | ] Starting worldserver...", stdout);
        if (!state.animated)
            std::fputc('\n', stdout);
        std::fflush(stdout);
        if (state.animated)
        {
            try
            {
                state.spinner = std::thread([&state]()
                {
                    std::unique_lock<std::mutex> lock(state.mutex);
                    char const frames[] = {'|', '/', '-', '\\'};
                    unsigned frame = 0;
                    while (state.active)
                    {
                        std::fprintf(stdout, "\r      [ %c ] Starting worldserver...", frames[frame++ % 4]);
                        std::fflush(stdout);
                        state.wake.wait_for(lock, std::chrono::milliseconds(120), [&state]() { return !state.active; });
                    }
                });
            }
            catch (...)
            {
                // Startup can continue with a static indicator if thread creation fails.
                state.animated = false;
                std::fputc('\n', stdout);
                std::fflush(stdout);
            }
        }
    }

    void Finish(bool success) { GetState().Stop(success); }

    void Capture(LogMessage* message)
    {
        State& state = GetState();
        bool stop = false;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (!state.active)
                return;
            int result = std::fprintf(state.file, "%s %-5s [%s] %s\n",
                message->getTimeStr().c_str(), Appender::getLogLevelString(message->level),
                Appender::getLogFilterTypeString(message->type), message->text.c_str());
            bool persisted = result >= 0 && std::fflush(state.file) == 0;
            if (message->level >= LOG_LEVEL_ERROR)
            {
                state.lastError = message->text.substr(0, 600);
                for (char& c : state.lastError)
                    if (c == '\r' || c == '\n') c = ' ';
            }
            message->felsoriaStartupHidden = persisted && message->level != LOG_LEVEL_FATAL;
            stop = !persisted || message->level == LOG_LEVEL_FATAL;
            if (!persisted)
                state.lastError = "Unable to write Startup.log; normal console logging restored.";
        }
        if (stop)
            Finish(false);
    }
}
