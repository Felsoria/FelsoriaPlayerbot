# Use CMake's OpenSSL 3 finder instead of the legacy TrinityCore module.
list(REMOVE_ITEM CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/macros")
if(FELSORIA_OPENSSL_VERSION)
  find_package(OpenSSL ${FELSORIA_OPENSSL_VERSION} EXACT REQUIRED COMPONENTS SSL Crypto)
else()
  find_package(OpenSSL 3.0 REQUIRED COMPONENTS SSL Crypto)
endif()

add_library(openssl INTERFACE)
target_link_libraries(openssl INTERFACE OpenSSL::SSL OpenSSL::Crypto)
