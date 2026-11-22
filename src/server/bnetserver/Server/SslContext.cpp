/*
 * Copyright (C) 2008-2016 TrinityCore <http://www.trinitycore.org/>
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

#include "SslContext.h"
#include "Config.h"
#include "Log.h"
#include <openssl/ssl.h>
#include <openssl/x509.h>

bool Battlenet::SslContext::Initialize()
{
    boost::asio::ssl::context& context = instance();
    boost::system::error_code error;
    context.set_options(boost::asio::ssl::context::no_sslv3, error);
    if (error)
    {
        TC_LOG_ERROR(LOG_FILTER_BATTLENET, "Could not configure Battle.net TLS: %s", error.message().c_str());
        return false;
    }

    std::string const certificate = sConfigMgr->GetStringDefault("TLS.CertificateFile", "bnetserver.cert.pem");
    std::string const privateKey = sConfigMgr->GetStringDefault("TLS.PrivateKeyFile", "bnetserver.key.pem");
    context.use_certificate_chain_file(certificate, error);
    if (error)
    {
        TC_LOG_ERROR(LOG_FILTER_BATTLENET, "Could not load Battle.net TLS certificate '%s' (TLS.CertificateFile): %s",
            certificate.c_str(), error.message().c_str());
        return false;
    }

    context.use_private_key_file(privateKey, boost::asio::ssl::context::pem, error);
    if (error)
    {
        TC_LOG_ERROR(LOG_FILTER_BATTLENET, "Could not load Battle.net TLS private key '%s' (TLS.PrivateKeyFile): %s",
            privateKey.c_str(), error.message().c_str());
        return false;
    }

    if (SSL_CTX_check_private_key(context.native_handle()) != 1)
    {
        TC_LOG_ERROR(LOG_FILTER_BATTLENET, "Battle.net TLS certificate '%s' and private key '%s' do not match.",
            certificate.c_str(), privateKey.c_str());
        return false;
    }

    TC_LOG_INFO(LOG_FILTER_BATTLENET, "Loaded Battle.net TLS certificate '%s'. Use a certificate chain trusted by your Legion client.",
        certificate.c_str());
    if (X509_self_signed(SSL_CTX_get0_certificate(context.native_handle()), 1) == 1)
        TC_LOG_WARN(LOG_FILTER_BATTLENET, "Battle.net TLS uses a self-signed certificate. The Legion client must explicitly trust this certificate.");

    return true;
}

boost::asio::ssl::context& Battlenet::SslContext::instance()
{
    static boost::asio::ssl::context context(boost::asio::ssl::context::sslv23);
    return context;
}
