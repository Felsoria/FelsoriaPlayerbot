# Copyright (C) 2008-2017 TrinityCore <http://www.trinitycore.org/>
#
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

CollectSourceFiles(
  ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE_SOURCES
  # Exclude
  ${CMAKE_CURRENT_SOURCE_DIR}/PrecompiledHeaders)

if (USE_COREPCH)
  set(PRIVATE_PCH_HEADER PrecompiledHeaders/gamePCH.h)
  set(PRIVATE_PCH_SOURCE PrecompiledHeaders/gamePCH.cpp)
  if (MSVC)
    list(INSERT PRIVATE_SOURCES 0 PrecompiledHeaders/gamePCH.cpp)
  endif (MSVC)
endif ()

GroupSources(${CMAKE_CURRENT_SOURCE_DIR})

add_library(game
  ${PRIVATE_SOURCES}
)

CollectIncludeDirectories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC_INCLUDES
  # Exclude
  ${CMAKE_CURRENT_SOURCE_DIR}/PrecompiledHeaders)

target_include_directories(game
  PUBLIC
    ${PUBLIC_INCLUDES}
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(game
  PUBLIC
    shared
    Detour)

set_target_properties(game
    PROPERTIES
      FOLDER
        "server")

# Generate precompiled header
if (USE_COREPCH)
  add_cxx_pch(game ${PRIVATE_PCH_HEADER} ${PRIVATE_PCH_SOURCE})
endif ()

# Felsoria Regional V1: preserve UTF-8 literals in the new components only.
if(MSVC)
  # /Y- does not cancel the core's target-level /FIgamePCH.h.
  # Supply that header directory for these non-PCH UTF-8 translation units.
  set_source_files_properties(
    Regional/RegionalGuildManager.cpp
    Regional/RegionalBotChatManager.cpp
    PROPERTIES
      COMPILE_FLAGS "/Y- /utf-8"
      INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/PrecompiledHeaders"
  )
endif()
