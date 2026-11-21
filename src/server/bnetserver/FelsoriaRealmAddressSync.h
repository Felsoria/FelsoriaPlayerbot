#ifndef FELSORIA_REALM_ADDRESS_SYNC_V4_H
#define FELSORIA_REALM_ADDRESS_SYNC_V4_H

#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include <string>
#include <vector>

// Installed from the actual RealmList.cpp catalog query; not a guessed table.
static char const* const FelsoriaAddressCatalogTable = "felsoria_eu_legion_catalog";

static std::string FelsoriaAddressSqlValue(std::string const& value)
{
    if (value.empty())
        return "''";
    static char const digits[] = "0123456789ABCDEF";
    std::string result = "CONVERT(0x";
    for (unsigned char ch : value)
    {
        result += digits[ch >> 4];
        result += digits[ch & 15];
    }
    return result + " USING utf8mb4)";
}

static void FelsoriaLogRealmAddresses(std::string const& external, std::string const& local)
{
    TC_LOG_INFO(LOG_FILTER_BATTLENET, "External address: %s | Local address: %s", external.c_str(), local.c_str());
}

// Called after REST address resolution, before RealmList is first loaded.
static bool FelsoriaSyncRealmAddress()
{
    std::string const external = sConfigMgr->GetStringDefault("LoginREST.ExternalAddress", "127.0.0.1");
    std::string const local = sConfigMgr->GetStringDefault("LoginREST.LocalAddress", "127.0.0.1");
    if (!sConfigMgr->GetBoolDefault("Felsoria.RealmAddressSync.Enable", false))
    {
        FelsoriaLogRealmAddresses(external, local);
        return true;
    }
    int32 const realmId = sConfigMgr->GetIntDefault("Felsoria.RealmAddressSync.RealmID", 1);
    if (realmId < 1 || realmId > 65535 || external.empty() || local.empty() ||
        external.size() > 255 || local.size() > 255 || external == "0.0.0.0" || local == "0.0.0.0")
    {
        TC_LOG_ERROR(LOG_FILTER_BATTLENET, "Invalid realm ID or advertised address. Address synchronization stopped.");
        return false;
    }

    QueryResult identity = LoginDatabase.Query("SELECT @@hostname, @@port, DATABASE(), CURRENT_USER()");
    if (!identity)
    {
        TC_LOG_ERROR(LOG_FILTER_BATTLENET, "Could not verify the login database connection.");
        return false;
    }
    // AllRealms is explicit: this Felsoria repack advertises one world endpoint
    // through every realmlist entry. Read the current full set, never fixed IDs/counts.
    bool const allRealms = sConfigMgr->GetBoolDefault("Felsoria.RealmAddressSync.AllRealms", false);
    bool const includeAliases = realmId == 1 &&
        sConfigMgr->GetBoolDefault("Felsoria.RealmAddressSync.IncludeVirtualRealms", true) &&
        sConfigMgr->GetBoolDefault("Felsoria.VirtualRealms.Enable", false);
    std::string scope = allRealms ? "1=1" : "r.`id` = " + std::to_string(realmId);
    if (!allRealms && includeAliases)
    {
        scope += " OR r.`id` IN (901,902,903,904,905,906,907)";
        if (sConfigMgr->GetBoolDefault("Felsoria.VirtualRealms.EULegion.Enable", false))
        {
            std::string const table = FelsoriaAddressCatalogTable;
            if (table == "FELSORIA_CATALOG_TABLE_FROM_INSTALLER")
            {
                TC_LOG_ERROR(LOG_FILTER_BATTLENET, "Realm catalog was not configured. Run the installer before compiling.");
                return false;
            }
            // Alias membership is declared by the catalog, independently of
            // Region/Battlegroup. The user's catalog uses 1/1, not 0/0.
            scope += " OR (r.`id` IN "
                "(SELECT `realm_id` FROM `" + table + "` WHERE `enabled` = 1))";
        }
    }

    std::string const columns = "SELECT r.`id`, COALESCE(r.`address`,''), COALESCE(r.`localAddress`,'') FROM `realmlist` r WHERE ";
    QueryResult before = LoginDatabase.Query((columns + "(" + scope + ") ORDER BY r.`id`").c_str());
    if (!before)
    {
        TC_LOG_ERROR(LOG_FILTER_BATTLENET, "Could not read the target realms or catalog. No address was changed.");
        return false;
    }

    std::vector<uint32> ids;
    uint32 changed = 0;
    bool primaryFound = false;
    do
    {
        uint32 const id = (*before)[0].GetUInt32();
        ids.push_back(id);
        primaryFound = primaryFound || id == uint32(realmId);
        if ((*before)[1].GetString() != external || (*before)[2].GetString() != local)
            ++changed;
    } while (before->NextRow());
    if (!primaryFound)
    {
        TC_LOG_ERROR(LOG_FILTER_BATTLENET, "Primary realm id=%d is missing. No address was changed.", realmId);
        return false;
    }

    std::string idList;
    for (uint32 id : ids)
    {
        if (!idList.empty())
            idList += ",";
        idList += std::to_string(id);
    }
    if (changed)
    {
        // One synchronous statement updates the frozen target set.
        // Config is authoritative for the selected primary and its known aliases.
        std::string const updateSql = "UPDATE `realmlist` SET `address` = " + FelsoriaAddressSqlValue(external) +
            ", `localAddress` = " + FelsoriaAddressSqlValue(local) + " WHERE `id` IN (" + idList + ")";
        LoginDatabase.DirectExecute(updateSql.c_str());
    }

    QueryResult after = LoginDatabase.Query((columns + "r.`id` IN (" + idList + ") ORDER BY r.`id`").c_str());
    uint32 verified = 0;
    if (after)
    {
        do
        {
            if ((*after)[1].GetString() != external || (*after)[2].GetString() != local)
                break;
            ++verified;
        } while (after->NextRow());
    }
    if (verified != ids.size())
    {
        TC_LOG_ERROR(LOG_FILTER_BATTLENET,
            "Realm address verification failed: verified=%u expected=%u. Check the SQL error log. Startup stopped.",
            verified, uint32(ids.size()));
        return false;
    }

    FelsoriaLogRealmAddresses(external, local);
    return true;
}

#endif
