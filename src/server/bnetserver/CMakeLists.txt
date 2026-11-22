# Copyright (C) 2008-2017 TrinityCore <http://www.trinitycore.org/>
#
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

########### bnetserver ###############

CollectSourceFiles(
  ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE_SOURCES
  # Exclude
  ${CMAKE_CURRENT_SOURCE_DIR}/PrecompiledHeaders)

if (WIN32)
  list(APPEND PRIVATE_SOURCES ${sources_windows})
  if (MSVC)
    list(APPEND PRIVATE_SOURCES bnetserver.rc)
  endif()
endif()

if (USE_COREPCH)
  set(PRIVATE_PCH_HEADER PrecompiledHeaders/bnetPCH.h)
  set(PRIVATE_PCH_SOURCE PrecompiledHeaders/bnetPCH.cpp)
  if (MSVC)
    list(INSERT PRIVATE_SOURCES 0 PrecompiledHeaders/bnetPCH.cpp)
  endif (MSVC)
endif()

GroupSources(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(bnetserver
  ${PRIVATE_SOURCES}
)

if (NOT WIN32)
  set_target_properties(bnetserver PROPERTIES
    COMPILE_DEFINITIONS _TRINITY_BNET_CONFIG="${CONF_DIR}/bnetserver.conf"
  )
endif()

target_link_libraries(bnetserver
  PUBLIC
    shared)

CollectIncludeDirectories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC_INCLUDES
  # Exclude
  ${CMAKE_CURRENT_SOURCE_DIR}/PrecompiledHeaders)

target_include_directories(bnetserver
  PUBLIC
    ${PUBLIC_INCLUDES}
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR})

set_target_properties(bnetserver
    PROPERTIES
      FOLDER
        "server")

set(BNETSERVER_CERTIFICATE "${CMAKE_CURRENT_SOURCE_DIR}/bnetserver.cert.pem")
set(BNETSERVER_PRIVATE_KEY "${CMAKE_CURRENT_SOURCE_DIR}/bnetserver.key.pem")
if (EXISTS "${BNETSERVER_CERTIFICATE}" AND EXISTS "${BNETSERVER_PRIVATE_KEY}")
  set(BNETSERVER_TLS_FILES "${BNETSERVER_CERTIFICATE}" "${BNETSERVER_PRIVATE_KEY}")
endif()

if (WIN32)
  add_custom_command(TARGET bnetserver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/bnetserver.conf.dist"
            "$<TARGET_FILE_DIR:bnetserver>"
    VERBATIM)
  if (BNETSERVER_TLS_FILES)
    add_custom_command(TARGET bnetserver POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${BNETSERVER_TLS_FILES} "$<TARGET_FILE_DIR:bnetserver>"
      VERBATIM)
  endif()
endif()

if (UNIX)
  install(TARGETS bnetserver DESTINATION bin)
  install(FILES bnetserver.conf.dist DESTINATION ${CONF_DIR})
  if (BNETSERVER_TLS_FILES)
    install(FILES ${BNETSERVER_TLS_FILES} DESTINATION bin)
  endif()
elseif (WIN32)
  install(TARGETS bnetserver DESTINATION "${CMAKE_INSTALL_PREFIX}")
  install(FILES bnetserver.conf.dist DESTINATION "${CMAKE_INSTALL_PREFIX}")
  if (BNETSERVER_TLS_FILES)
    install(FILES ${BNETSERVER_TLS_FILES} DESTINATION "${CMAKE_INSTALL_PREFIX}")
  endif()
endif()

# Generate precompiled header
if (USE_COREPCH)
  add_cxx_pch(bnetserver ${PRIVATE_PCH_HEADER} ${PRIVATE_PCH_SOURCE})
endif()
