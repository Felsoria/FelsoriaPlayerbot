# Use CMake's OpenSSL 3 finder instead of the legacy TrinityCore module.
list(REMOVE_ITEM CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/macros")
find_package(OpenSSL 3.0 REQUIRED COMPONENTS SSL Crypto)

add_library(openssl INTERFACE)
target_link_libraries(openssl INTERFACE OpenSSL::SSL OpenSSL::Crypto)
