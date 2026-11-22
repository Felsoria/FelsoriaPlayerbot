# Copyright (C) 2005-2009 MaNGOS project <http://getmangos.com/>
# Copyright (C) 2008-2016 TrinityCore <http://www.trinitycore.org/>
#
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

file(GLOB_RECURSE sources *.cpp *.h)

include_directories (
  ${CMAKE_SOURCE_DIR}/src/server/shared
  ${CMAKE_SOURCE_DIR}/dep/CascLib/src
  ${CMAKE_SOURCE_DIR}/dep/cppformat
  ${CMAKE_SOURCE_DIR}/dep/g3dlite/include
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/loadlib
)

include_directories(${include_Dirs})

add_executable(mapextractor
  ${sources}
)

target_link_libraries(mapextractor
  casc
  common
  g3dlib
  ${BZIP2_LIBRARIES}
  ${ZLIB_LIBRARIES}
  ${Boost_LIBRARIES}
)

add_dependencies(mapextractor casc)

# The DB2 extractor takes its list from the DB2 stores compiled into worldserver.
set(DB2_STORES "${CMAKE_SOURCE_DIR}/src/server/game/DataStores/DB2Stores.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DB2_STORES}")
file(STRINGS "${DB2_STORES}" DB2_STORE_DECLARATIONS REGEX "DB2Storage<.*\"[A-Za-z0-9_]+\\.db2\"")
set(DB2_FILES "")
foreach(DB2_STORE IN LISTS DB2_STORE_DECLARATIONS)
  string(REGEX MATCH "\"([A-Za-z0-9_]+\\.db2)\"" DB2_FILENAME "${DB2_STORE}")
  string(APPEND DB2_FILES "DBFilesClient\\${CMAKE_MATCH_1}\n")
endforeach()
if (NOT DB2_FILES)
  message(FATAL_ERROR "No DB2 table names found in ${DB2_STORES}")
endif()

set(DB2_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/FelsoriaDB2Files.txt")
file(GENERATE OUTPUT "${DB2_MANIFEST}" CONTENT "${DB2_FILES}")

if( UNIX )
  install(TARGETS mapextractor DESTINATION bin)
  install(FILES "${DB2_MANIFEST}" DESTINATION bin)
elseif( WIN32 )
  install(TARGETS mapextractor DESTINATION "${CMAKE_INSTALL_PREFIX}")
  install(FILES "${DB2_MANIFEST}" DESTINATION "${CMAKE_INSTALL_PREFIX}")
endif()
