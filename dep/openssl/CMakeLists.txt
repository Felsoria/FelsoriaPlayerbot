# OpenSSL 3.x support for LegionPlayerBot on Windows

set(OPENSSL_INCLUDE_DIR "D:/OpenSSL-Win64/include")

set(OPENSSL_CRYPTO_LIBRARY
    "D:/OpenSSL-Win64/lib/VC/x64/MD/libcrypto.lib")

set(OPENSSL_SSL_LIBRARY
    "D:/OpenSSL-Win64/lib/VC/x64/MD/libssl.lib")

set(OPENSSL_LIBRARIES
    ${OPENSSL_SSL_LIBRARY}
    ${OPENSSL_CRYPTO_LIBRARY})

add_library(openssl INTERFACE)

target_link_libraries(openssl
    INTERFACE
        ${OPENSSL_LIBRARIES})

target_include_directories(openssl
    INTERFACE
        ${OPENSSL_INCLUDE_DIR})
