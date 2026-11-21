/*
 * Copyright (C) 2008-2014 TrinityCore <http://www.trinitycore.org/>
 *
 * This program is free software; as a special exception the author gives
 * unlimited permission to copy and/or distribute it, with or without
 * modifications, as long as this notice is preserved.
 */

#include <OpenSSLCrypto.h>
#include <openssl/opensslv.h>

void OpenSSLCrypto::threadsSetup()
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    // Old OpenSSL 1.0.x required external locking callbacks.
    // This build targets OpenSSL 1.1+ / 3.x, where OpenSSL handles
    // thread synchronization internally.
#endif
}

void OpenSSLCrypto::threadsCleanup()
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    // No-op for OpenSSL 1.1+ / 3.x.
#endif
}
