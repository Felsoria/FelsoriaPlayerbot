/*
 * Copyright (C) 2008-2016 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
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

#include "RealmList.h"
#include "FelsoriaRealmPolicy.h"
#include "BattlenetRpcErrorCodes.h"
#include "BigNumber.h"
#include "DatabaseEnv.h"
#include "Errors.h"
#include "Log.h"
#include "ProtobufJSON.h"
#include "Util.h"
#include "game_utilities_service.pb.h"
#include "RealmList.pb.h"
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <zlib.h>
#include "RealmList.h"
#include "FelsoriaRealmPolicy.h"
#include "Config.h"

RealmList::RealmList() : _updateInterval(0)
{
}

RealmList::~RealmList() = default;

RealmList* RealmList::Instance()
{
    static RealmList instance;
    return &instance;
}

// Load the realm list from the database
void RealmList::Initialize(boost::asio::io_service& ioService, uint32 updateInterval)
{
    _updateInterval = updateInterval;
    _updateTimer = Trinity::make_unique<boost::asio::deadline_timer>(ioService);
    _resolver = Trinity::make_unique<boost::asio::ip::tcp::resolver>(ioService);

    LoadBuildInfo();
    // Get the content of the realmlist table in the database
    UpdateRealms(boost::system::error_code());
}

void RealmList::Close()
{
    _updateTimer->cancel();
}

void RealmList::LoadBuildInfo()
{
	//                                                              0             1              2              3      4              5              6            7
	if (QueryResult result = LoginDatabase.Query("SELECT majorVersion, minorVersion, bugfixVersion, hotfixVersion, build, win64AuthSeed, mac64AuthSeed, winAuthSeed FROM build_info ORDER BY build ASC"))
	{
		do
		{
			Field* fields = result->Fetch();
			_builds.emplace_back();
			RealmBuildInfo& build = _builds.back();
			build.MajorVersion = fields[0].GetUInt32();
			build.MinorVersion = fields[1].GetUInt32();
			build.BugfixVersion = fields[2].GetUInt32();
			std::string hotfixVersion = fields[3].GetString();
			if (hotfixVersion.length() < build.HotfixVersion.size())
				std::copy(hotfixVersion.begin(), hotfixVersion.end(), build.HotfixVersion.begin());
			else
                std::fill(hotfixVersion.begin(), hotfixVersion.end(), '\0');

            build.Build = fields[4].GetUInt32();
            std::string winAuthSeedHexStr = fields[7].GetString();
            if (winAuthSeedHexStr.length() == build.WinAuthSeed.size() * 2)
                HexStrToByteArray(winAuthSeedHexStr, build.WinAuthSeed.data());

            std::string win64AuthSeedHexStr = fields[5].GetString();
            if (win64AuthSeedHexStr.length() == build.Win64AuthSeed.size() * 2)
                HexStrToByteArray(win64AuthSeedHexStr, build.Win64AuthSeed.data());

            std::string mac64AuthSeedHexStr = fields[6].GetString();
            if (mac64AuthSeedHexStr.length() == build.Mac64AuthSeed.size() * 2)
                HexStrToByteArray(mac64AuthSeedHexStr, build.Mac64AuthSeed.data());

        } while (result->NextRow());
    }
}

void RealmList::UpdateRealm(Battlenet::RealmHandle const& id, uint32 build, std::string const& name, boost::asio::ip::address const& address, boost::asio::ip::address const& localAddr, boost::asio::ip::address const& localSubmask, uint16 port, uint8 icon, RealmFlags flag, uint8 timezone, AccountTypes allowedSecurityLevel, float population)
{
    std::lock_guard<std::recursive_mutex> guard(i_RealmList_lock);
    // Create new if not exist or update existed
    Realm& realm = _realms[id];

    realm.Id = id;
    realm.Build = build;
    realm.Name = name;
    realm.Type = icon;
    realm.Flags = flag;
    realm.Timezone = timezone;
    realm.AllowedSecurityLevel = allowedSecurityLevel;
    realm.PopulationLevel = population;
    if (!realm.ExternalAddress || *realm.ExternalAddress != address)
        realm.ExternalAddress = Trinity::make_unique<boost::asio::ip::address>(address);
    if (!realm.LocalAddress || *realm.LocalAddress != localAddr)
        realm.LocalAddress = Trinity::make_unique<boost::asio::ip::address>(localAddr);
    if (!realm.LocalSubnetMask || *realm.LocalSubnetMask != localSubmask)
        realm.LocalSubnetMask = Trinity::make_unique<boost::asio::ip::address>(localSubmask);
    realm.Port = port;
}

void RealmList::UpdateRealms(boost::system::error_code const& error)
{
    if (error)
        return;

    TC_LOG_TRACE(LOG_FILTER_REALMLIST, "Updating Realm List...");

    // 0   1     2        3             4                5     6     7     8         9                     10          11         12      13
    //"id, name, address, localAddress, localSubnetMask, port, icon, flag, timezone, allowedSecurityLevel, population, gamebuild, Region, Battlegroup
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_REALMLIST);
    PreparedQueryResult result = LoginDatabase.Query(stmt);
    uint32 realmRows = 0; // FELSORIA_P18_REALM_SUMMARY
    uint32 addedRealms = 0;

    std::map<Battlenet::RealmHandle, std::string> existingRealms;
    for (auto const& p : _realms)
        existingRealms[p.first] = p.second.Name;

    std::lock_guard<std::recursive_mutex> guard(i_RealmList_lock);
    _realms.clear();

    // Circle through results and add them to the realm map
    if (result)
    {
        do
        {
            try
            {
                boost::asio::ip::tcp::resolver::iterator end;

                Field* fields = result->Fetch();
                ++realmRows;
                uint32 realmId = fields[0].GetUInt32();
                std::string name = fields[1].GetString();
                boost::asio::ip::tcp::resolver::query externalAddressQuery(boost::asio::ip::tcp::v4(), fields[2].GetString(), "");

                boost::system::error_code ec;
                boost::asio::ip::tcp::resolver::iterator endPoint = _resolver->resolve(externalAddressQuery, ec);
                if (endPoint == end || ec)
                {
                    TC_LOG_ERROR(LOG_FILTER_REALMLIST, "Could not resolve address %s for realm \"%s\" id %u", fields[2].GetString().c_str(), name.c_str(), realmId);
                    continue;
                }

                boost::asio::ip::address externalAddress = endPoint->endpoint().address();

                boost::asio::ip::tcp::resolver::query localAddressQuery(boost::asio::ip::tcp::v4(), fields[3].GetString(), "");
                endPoint = _resolver->resolve(localAddressQuery, ec);
                if (endPoint == end || ec)
                {
                    TC_LOG_ERROR(LOG_FILTER_REALMLIST, "Could not resolve localAddress %s for realm \"%s\" id %u", fields[3].GetString().c_str(), name.c_str(), realmId);
                    continue;
                }

                boost::asio::ip::address localAddress = endPoint->endpoint().address();

                boost::asio::ip::tcp::resolver::query localSubmaskQuery(boost::asio::ip::tcp::v4(), fields[4].GetString(), "");
                endPoint = _resolver->resolve(localSubmaskQuery, ec);
                if (endPoint == end || ec)
                {
                    TC_LOG_ERROR(LOG_FILTER_REALMLIST, "Could not resolve localSubnetMask %s for realm \"%s\" id %u", fields[4].GetString().c_str(), name.c_str(), realmId);
                    continue;
                }

                boost::asio::ip::address localSubmask = endPoint->endpoint().address();

                uint16 port = fields[5].GetUInt16();
                uint8 icon = fields[6].GetUInt8();
                if (icon == REALM_TYPE_FFA_PVP)
                    icon = REALM_TYPE_PVP;
                if (icon >= MAX_CLIENT_REALM_TYPE)
                    icon = REALM_TYPE_NORMAL;
                RealmFlags flag = RealmFlags(fields[7].GetUInt8());
                uint8 timezone = fields[8].GetUInt8();
                uint8 allowedSecurityLevel = fields[9].GetUInt8();
                float pop = fields[10].GetFloat();
                uint32 build = sConfigMgr->GetIntDefault("Game.Build.Version", 26972);
                uint8 region = fields[12].GetUInt8();
                uint8 battlegroup = fields[13].GetUInt8();

                Battlenet::RealmHandle id{ region, battlegroup, realmId };

                _realmIPs[realmId] = externalAddress;

                UpdateRealm(id, build, name, externalAddress, localAddress, localSubmask, port, icon, flag,
                    timezone, (allowedSecurityLevel <= SEC_ADMINISTRATOR ? AccountTypes(allowedSecurityLevel) : SEC_ADMINISTRATOR), pop);

                _subRegions.insert(Battlenet::RealmHandle{ region, battlegroup, 0 }.GetAddressString());

                if (!existingRealms.count(id))
                    ++addedRealms; // P18: one summary instead of per-realm logs.

                existingRealms.erase(id);
            }
            catch (std::exception& ex)
            {
                TC_LOG_ERROR(LOG_FILTER_REALMLIST, "Realmlist::UpdateRealms has thrown an exception: %s", ex.what());
                ABORT();
            }
        }
        while (result->NextRow());
    }

    // Felsoria P06: read catalog once per realm-list refresh, never per bot tick.
    _felsoriaRealmCatalog.clear();
    if (sConfigMgr->GetBoolDefault("Felsoria.VirtualRealms.EULegion.Enable", false))
    {
        if (QueryResult catalog = LoginDatabase.Query(
            "SELECT realm_id,visible FROM felsoria_eu_legion_catalog WHERE enabled=1 ORDER BY realm_id"))
        {
            do
            {
                Field* fields = catalog->Fetch();
                uint32 id = fields[0].GetUInt32();
                if (id > 1 && id <= 65535 && _realms.count(Battlenet::RealmHandle(0, 0, id)))
                    _felsoriaRealmCatalog[id] = fields[1].GetUInt8() != 0;
            } while (catalog->NextRow());
        }
        if (_felsoriaRealmCatalog.empty())
            TC_LOG_ERROR(LOG_FILTER_REALMLIST, "FELSORIA_EU_CATALOG_EMPTY: apply P06 AUTH SQL before enabling EU Legion realms.");
    }

    // Alias status follows the real worldserver, including offline state.
    for (auto& entry : _realms)
    {
        uint32 canonicalId = GetCanonicalRealmId(entry.first.Realm);
        if (canonicalId == entry.first.Realm)
            continue;
        auto canonical = _realms.find(Battlenet::RealmHandle(0, 0, canonicalId));
        if (canonical == _realms.end())
            entry.second.Flags = RealmFlags(entry.second.Flags | REALM_FLAG_OFFLINE);
        else
        {
            entry.second.Flags = canonical->second.Flags;
            entry.second.PopulationLevel = canonical->second.PopulationLevel;
            entry.second.AllowedSecurityLevel = canonical->second.AllowedSecurityLevel;
        }
    }

    // P18: report only complete loads, once on startup/list changes.
    // Resolver failures and catalog errors keep their existing error logs.
    if (result && realmRows && _realms.size() == realmRows &&
        (!sConfigMgr->GetBoolDefault("Felsoria.VirtualRealms.EULegion.Enable", false) || !_felsoriaRealmCatalog.empty()) &&
        (addedRealms || !existingRealms.empty()))
        TC_LOG_INFO(LOG_FILTER_REALMLIST, "All realms loaded successfully (%u realms).", uint32(_realms.size()));

    if (_updateInterval)
    {
        _updateTimer->expires_from_now(boost::posix_time::seconds(_updateInterval));
        _updateTimer->async_wait(std::bind(&RealmList::UpdateRealms, this, std::placeholders::_1));
    }
}

// Felsoria VirtualRealms P01: only the three explicitly reserved test aliases.
uint32 RealmList::GetCanonicalRealmId(uint32 realmId) const
{
    if (!sConfigMgr->GetIntDefault("Felsoria.VirtualRealms.Enable", 0))
        return realmId;

    if (sConfigMgr->GetBoolDefault("Felsoria.VirtualRealms.EULegion.Enable", false))
    {
        std::lock_guard<std::recursive_mutex> guard(i_RealmList_lock);
        if (_felsoriaRealmCatalog.count(realmId))
            return 1;
    }

    switch (realmId)
    {
        case 901:
        case 902:
        case 903:
        case 904:
        case 905:
        case 906:
        case 907:
            return 1;
        default:
            return realmId;
    }
}

// Felsoria P06: return a copy so refreshes cannot invalidate caller iteration.
std::vector<uint32> RealmList::GetFelsoriaRealmIds(bool includeHidden) const
{
    std::lock_guard<std::recursive_mutex> guard(i_RealmList_lock);
    std::vector<uint32> ids;
    if (!sConfigMgr->GetBoolDefault("Felsoria.VirtualRealms.Enable", false))
        return ids;
    if (sConfigMgr->GetBoolDefault("Felsoria.VirtualRealms.EULegion.Enable", false))
    {
        for (auto const& entry : _felsoriaRealmCatalog)
            if (includeHidden || (entry.second && !FelsoriaRealmPolicy::Decoration(entry.first)))
                ids.push_back(entry.first);
    }
    else
    {
        ids = {901u, 902u, 903u, 904u, 905u, 906u};
        if (includeHidden) ids.push_back(907u);
    }
    return ids;
}

bool RealmList::IsFelsoriaRealmVisible(uint32 realmId) const
{
    // P08: list decoration is never a creation/transfer/bot destination.
    if (FelsoriaRealmPolicy::Decoration(realmId)) return false;
    std::lock_guard<std::recursive_mutex> guard(i_RealmList_lock);
    if (!sConfigMgr->GetBoolDefault("Felsoria.VirtualRealms.Enable", false))
        return false;
    if (!sConfigMgr->GetBoolDefault("Felsoria.VirtualRealms.EULegion.Enable", false))
        return realmId >= 901 && realmId <= 907;
    auto it = _felsoriaRealmCatalog.find(realmId);
    return it != _felsoriaRealmCatalog.end() && it->second;
}

bool RealmList::IsFelsoriaRealmKnown(uint32 realmId) const
{
    std::lock_guard<std::recursive_mutex> guard(i_RealmList_lock);
    if (!sConfigMgr->GetBoolDefault("Felsoria.VirtualRealms.Enable", false))
        return false;
    if (!sConfigMgr->GetBoolDefault("Felsoria.VirtualRealms.EULegion.Enable", false))
        return realmId >= 901 && realmId <= 907;
    return _felsoriaRealmCatalog.count(realmId) != 0;
}

uint8 RealmList::GetFelsoriaBGLanguage(uint32 realmId) const
{
    std::lock_guard<std::recursive_mutex> guard(i_RealmList_lock);
    if (!IsFelsoriaRealmVisible(realmId)) return 0;
    Realm const* origin = GetRealm(Battlenet::RealmHandle(0, 0, realmId));
    return origin ? FelsoriaRealmPolicy::LanguageGroup(origin->Timezone) : 0;
}

std::vector<uint32> RealmList::GetFelsoriaBGRealmIds(uint8 languageGroup) const
{
    std::lock_guard<std::recursive_mutex> guard(i_RealmList_lock);
    std::vector<uint32> result;
    if (!languageGroup) return result;
    for (uint32 id : GetFelsoriaRealmIds())
        if (Realm const* origin = GetRealm(Battlenet::RealmHandle(0, 0, id)))
            if (FelsoriaRealmPolicy::LanguageGroup(origin->Timezone) == languageGroup)
                result.push_back(id);
    return result;
}

Realm const* RealmList::GetRealm(Battlenet::RealmHandle const& id) const
{
    auto itr = _realms.find(id);
    if (itr != _realms.end())
        return &itr->second;

    return NULL;
}

RealmBuildInfo const* RealmList::GetBuildInfo(uint32 build) const
{
    for (const auto & clientBuild : _builds)
        if (clientBuild.Build == build)
            return &clientBuild;

    return nullptr;
}

uint32 RealmList::GetMinorMajorBugfixVersionForBuild(uint32 build) const
{
	auto buildInfo = std::lower_bound(_builds.begin(), _builds.end(), build, [](RealmBuildInfo const& buildInfo, uint32 value)
	{
		return buildInfo.Build < value;
	});
	return buildInfo != _builds.end() ? (buildInfo->MajorVersion * 10000 + buildInfo->MinorVersion * 100 + buildInfo->BugfixVersion) : 0;
}

void RealmList::WriteSubRegions(bgs::protocol::game_utilities::v1::GetAllValuesForAttributeResponse* response) const
{
    for (auto const& subRegion : GetSubRegions())
        response->add_attribute_value()->set_string_value(subRegion);
}

std::vector<uint8> RealmList::GetRealmEntryJSON(Battlenet::RealmHandle const& id, uint32 build) const
{
    std::vector<uint8> compressed;
    if (Realm const* realm = GetRealm(id))
    {
        if (!(realm->Flags & REALM_FLAG_OFFLINE) && realm->Build == build)
        {
            JSON::RealmList::RealmEntry realmEntry;
            realmEntry.set_wowrealmaddress(realm->Id.GetAddress());
            realmEntry.set_cfgtimezonesid(1);
            realmEntry.set_populationstate(std::max(uint32(realm->PopulationLevel), 1u));
            realmEntry.set_cfgcategoriesid(realm->Timezone);

            JSON::RealmList::ClientVersion* version = realmEntry.mutable_version();
            if (RealmBuildInfo const* buildInfo = GetBuildInfo(realm->Build))
            {
                version->set_versionmajor(buildInfo->MajorVersion);
                version->set_versionminor(buildInfo->MinorVersion);
                version->set_versionrevision(buildInfo->BugfixVersion);
                version->set_versionbuild(buildInfo->Build);
            }
            else
            {
                version->set_versionmajor(7);
                version->set_versionminor(0);
                version->set_versionrevision(3);
                version->set_versionbuild(realm->Build);
            }

            realmEntry.set_cfgrealmsid(realm->Id.Realm);
            realmEntry.set_flags(realm->Flags);
            realmEntry.set_name(realm->Name);
            realmEntry.set_cfgconfigsid(realm->GetConfigId());
            realmEntry.set_cfglanguagesid(1);

            std::string json = "JamJSONRealmEntry:" + JSON::Serialize(realmEntry);

            uLong compressedLength = compressBound(json.length());
            compressed.resize(compressedLength + 4);
            *reinterpret_cast<uint32*>(compressed.data()) = json.length() + 1;

            if (compress(compressed.data() + 4, &compressedLength, reinterpret_cast<uint8 const*>(json.c_str()), json.length() + 1) == Z_OK)
                compressed.resize(compressedLength + 4);
            else
                compressed.clear();
        }
    }

    return compressed;
}

std::vector<uint8> RealmList::GetRealmList(uint32 build, std::string const& subRegion, uint8 Battlegroup) const
{
    std::lock_guard<std::recursive_mutex> guard(i_RealmList_lock);
    JSON::RealmList::RealmListUpdates realmList;
    for (auto const& realm : _realms)
    {
        // Felsoria P06: keep physical/development rows in AUTH; hide them in the client list.
        if (sConfigMgr->GetBoolDefault("Felsoria.VirtualRealms.EULegion.Enable", false))
        {
            if (sConfigMgr->GetBoolDefault("Felsoria.VirtualRealms.HideDevelopment.Enable", true) &&
                (realm.first.Realm == 1 || realm.second.Timezone == 1))
                continue;
            auto catalogEntry = _felsoriaRealmCatalog.find(realm.first.Realm);
            bool decoration = FelsoriaRealmPolicy::Decoration(realm.first.Realm) &&
                sConfigMgr->GetBoolDefault("Felsoria.VirtualRealms.TournamentDecoration.Enable", true);
            if (catalogEntry != _felsoriaRealmCatalog.end() && !catalogEntry->second && !decoration)
                continue;
        }
        if (realm.second.Id.GetSubRegionAddress() != subRegion)
            continue;

        if (realm.second.Build != build)
            continue;

        if (realm.second.Id.Site != Battlegroup)
            continue;

        JSON::RealmList::RealmState* state = realmList.add_updates();
        state->mutable_update()->set_wowrealmaddress(realm.second.Id.GetAddress());
        state->mutable_update()->set_cfgtimezonesid(1);
        state->mutable_update()->set_populationstate((realm.second.Flags & REALM_FLAG_OFFLINE) ? 0u : std::max(uint32(realm.second.PopulationLevel), 1u));
        state->mutable_update()->set_cfgcategoriesid(realm.second.Timezone);

        JSON::RealmList::ClientVersion* version = state->mutable_update()->mutable_version();
        if (RealmBuildInfo const* buildInfo = GetBuildInfo(realm.second.Build))
        {
            version->set_versionmajor(buildInfo->MajorVersion);
            version->set_versionminor(buildInfo->MinorVersion);
            version->set_versionrevision(buildInfo->BugfixVersion);
            version->set_versionbuild(buildInfo->Build);
        }
        else
        {
            version->set_versionmajor(7);
            version->set_versionminor(0);
            version->set_versionrevision(3);
            version->set_versionbuild(realm.second.Build);
        }

        state->mutable_update()->set_cfgrealmsid(realm.second.Id.Realm);
        state->mutable_update()->set_flags(realm.second.Flags);
        state->mutable_update()->set_name(realm.second.Name);
        state->mutable_update()->set_cfgconfigsid(realm.second.GetConfigId());
        state->mutable_update()->set_cfglanguagesid(1);

        state->set_deleting(false);
    }

    std::string json = "JSONRealmListUpdates:" + JSON::Serialize(realmList);

    uLong compressedLength = compressBound(json.length());
    std::vector<uint8> compressed;
    compressed.resize(4 + compressedLength);
    *reinterpret_cast<uint32*>(compressed.data()) = json.length() + 1;

    compress(compressed.data() + 4, &compressedLength, reinterpret_cast<uint8 const*>(json.c_str()), json.length() + 1);

    compressed.resize(compressedLength + 4);

    return compressed;
}

uint32 RealmList::JoinRealm(uint32 realmAddress, uint32 /*build*/, boost::asio::ip::address const& clientAddress, std::array<uint8, 32> const& clientSecret, LocaleConstant locale, std::string const& os, std::string accountName, bgs::protocol::game_utilities::v1::ClientResponse* response) const
{
    std::lock_guard<std::recursive_mutex> guard(i_RealmList_lock);
    if (auto realm = GetRealm(Battlenet::RealmHandle(realmAddress)))
    {
        if (realm->Timezone == 12)
            TC_LOG_INFO(LOG_FILTER_REALMLIST, "FELSORIA_RUSSIAN_JOIN_RX realm=%u category=%u locale=%u",
                realm->Id.Realm, uint32(realm->Timezone), uint32(locale));

        // Resolve the endpoint from realm 1, not from a duplicated alias address.
        uint32 canonicalId = GetCanonicalRealmId(realm->Id.Realm);
        if (canonicalId != realm->Id.Realm)
        {
            realm = GetRealm(Battlenet::RealmHandle(0, 0, canonicalId));
            if (!realm)
                return ERROR_UTIL_SERVER_UNKNOWN_REALM;
        }
        if (realm->Flags & REALM_FLAG_OFFLINE/* || realm->Build != build*/)
            return ERROR_USER_SERVER_NOT_PERMITTED_ON_REALM;

        JSON::RealmList::RealmListServerIPAddresses serverAddresses;
        auto addressFamily = serverAddresses.add_families();
        addressFamily->set_family(1);

        auto address = addressFamily->add_addresses();
        address->set_ip(realm->GetAddressForClient(clientAddress).to_string());
        address->set_port(realm->Port);

        auto json = "JSONRealmListServerIPAddresses:" + JSON::Serialize(serverAddresses);

        auto compressedLength = compressBound(json.length());
        std::vector<uint8> compressed;
        compressed.resize(4 + compressedLength);
        *reinterpret_cast<uint32*>(compressed.data()) = json.length() + 1;

        if (compress(compressed.data() + 4, &compressedLength, reinterpret_cast<uint8 const*>(json.c_str()), json.length() + 1) != Z_OK)
            return ERROR_UTIL_SERVER_FAILED_TO_SERIALIZE_RESPONSE;

        BigNumber serverSecret;
        serverSecret.SetRand(8 * 32);

        std::array<uint8, 64> keyData;
        memcpy(&keyData[0], clientSecret.data(), 32);
        memcpy(&keyData[32], serverSecret.AsByteArray(32).get(), 32);

        auto stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_ACCOUNT_LOGIN_INFO);
        stmt->setString(0, ByteArrayToHexStr(keyData.data(), keyData.size()));
        stmt->setString(1, clientAddress.to_string());
        stmt->setUInt8(2, locale);
        stmt->setString(3, os);
        stmt->setString(4, accountName);
        LoginDatabase.DirectExecute(stmt);

        bgs::protocol::Attribute* attribute = response->add_attribute();
        attribute->set_name("Param_RealmJoinTicket");
        attribute->mutable_value()->set_blob_value(accountName);

        attribute = response->add_attribute();
        attribute->set_name("Param_ServerAddresses");
        attribute->mutable_value()->set_blob_value(compressed.data(), compressedLength + 4);

        attribute = response->add_attribute();
        attribute->set_name("Param_JoinSecret");
        attribute->mutable_value()->set_blob_value(serverSecret.AsByteArray(32).get(), 32);
        return ERROR_OK;
    }

    return ERROR_UTIL_SERVER_UNKNOWN_REALM;
}

boost::asio::ip::address RealmList::GetAddressForClient(uint32 RealmID)
{
    return _realmIPs[RealmID];
}
