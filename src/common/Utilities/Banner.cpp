/*
 * Copyright (C) 2008-2019 TrinityCore <https://www.trinitycore.org/>
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

#include "Banner.h"
#include "GitRevision.h"
#include "StringFormat.h"
#include <string>

void Trinity::Banner::Show(char const* applicationName, void(*log)(char const* text), void(*logExtraInfo)())
{
    std::string version = GitRevision::GetFullVersion();
    std::string buildInfo;
    std::string::size_type position = version.find(" (");
    if (position != std::string::npos)
        buildInfo = version.substr(position);

    log(Trinity::StringFormat("Project Felsoria%s (%s)", buildInfo.c_str(), applicationName).c_str());
    log("<Ctrl-C> to stop.");
    log("");
    log("\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88 \xE2\x96\x84\xE2\x96\x84\xE2\x96\x84\xE2\x96\x84\xE2\x96\x84 \xE2\x96\x84\xE2\x96\x84     \xE2\x96\x84\xE2\x96\x84\xE2\x96\x84\xE2\x96\x84  \xE2\x96\x84\xE2\x96\x84\xE2\x96\x84  \xE2\x96\x84\xE2\x96\x84\xE2\x96\x84\xE2\x96\x84  \xE2\x96\x84\xE2\x96\x84  \xE2\x96\x84\xE2\x96\x84\xE2\x96\x84  ");
    log("\xE2\x96\x88\xE2\x96\x88\xE2\x96\x84\xE2\x96\x84   \xE2\x96\x88\xE2\x96\x88\xE2\x96\x84\xE2\x96\x84  \xE2\x96\x88\xE2\x96\x88    \xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x84\xE2\x96\x84 \xE2\x96\x88\xE2\x96\x88\xE2\x96\x80\xE2\x96\x88\xE2\x96\x88 \xE2\x96\x88\xE2\x96\x88\xE2\x96\x84\xE2\x96\x88\xE2\x96\x84 \xE2\x96\x88\xE2\x96\x88 \xE2\x96\x88\xE2\x96\x88\xE2\x96\x80\xE2\x96\x88\xE2\x96\x88 ");
    log("\xE2\x96\x88\xE2\x96\x88     \xE2\x96\x88\xE2\x96\x88\xE2\x96\x84\xE2\x96\x84\xE2\x96\x84 \xE2\x96\x88\xE2\x96\x88\xE2\x96\x84\xE2\x96\x84\xE2\x96\x84 \xE2\x96\x84\xE2\x96\x84\xE2\x96\x88\xE2\x96\x88\xE2\x96\x80 \xE2\x96\x80\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x80 \xE2\x96\x88\xE2\x96\x88 \xE2\x96\x88\xE2\x96\x88 \xE2\x96\x88\xE2\x96\x88 \xE2\x96\x88\xE2\x96\x88\xE2\x96\x80\xE2\x96\x88\xE2\x96\x88 ");
    log("");
    log("                   PROJECT FELSORIA");
    log("               Legion 7.3.5 - Playerbot System");
    log("");

    if (logExtraInfo)
        logExtraInfo();
}

/*  log("██╗     ███████╗ ██████╗ ██╗ ██████╗ ███╗   ██╗ ██████╗ ██████╗ ██████╗ ███████╗ ");
    log("██║     ██╔════╝██╔════╝ ██║██╔═══██╗████╗  ██║██╔════╝██╔═══██╗██╔══██╗██╔════╝ ");
    log("██║     █████╗  ██║  ███╗██║██║   ██║██╔██╗ ██║██║     ██║   ██║██████╔╝█████╗   ");
    log("██║     ██╔══╝  ██║   ██║██║██║   ██║██║╚██╗██║██║     ██║   ██║██╔══██╗██╔══╝   ");
    log("███████╗███████╗╚██████╔╝██║╚██████╔╝██║ ╚████║╚██████╗╚██████╔╝██║  ██║███████╗ ");
    log("╚══════╝╚══════╝ ╚═════╝ ╚═╝ ╚═════╝ ╚═╝  ╚═══╝ ╚═════╝ ╚═════╝ ╚═╝  ╚═╝╚══════╝ "); */