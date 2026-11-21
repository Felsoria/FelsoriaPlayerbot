#include "FelsoriaRegional.h"
#include "AccountMgr.h"
#include "CharacterData.h"
#include "CharacterPackets.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerBotMgr.h"
#include "RealmList.h"
#include "World.h"
#include "WorldSession.h"
#include "Util.h"
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <iterator>
#include <limits>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <stdexcept>

namespace FelsoriaRegional
{
namespace
{
LocaleConstant const Locales[] = { LOCALE_enUS, LOCALE_deDE, LOCALE_frFR, LOCALE_esES, LOCALE_ruRU, LOCALE_itIT };

// Exact, operator-approved guild catalog. A charter-filter exception can never
// apply to arbitrary database text or to names requested by real players.
char const* const ApprovedGuildNames[] = {
    "The Gilded Compass",
    "Amberfall Society",
    "Westbrook Sentinels",
    "Thursday Misfits",
    "Mana Debt Collectors",
    "Nebelwacht",
    "Silberfarn",
    "Die Sternenschmiede",
    "Eulen nach Acht",
    "Loot und Kuchen",
    "La Vigie d'Azur",
    "Les Murmures du Lys",
    "Le Cercle des Brumes",
    "Café Critique",
    "Les Derniers Debout",
    "Centinelas del Alba",
    "La Rosa de Cristal",
    "Vigías del Horizonte",
    "Los del Último Pull",
    "Aquí Nadie Cura",
    "Северный Дозор",
    "Лунная Гавань",
    "Хранители Рассвета",
    "Чай и Криты",
    "Последний Хил",
    "La Compagnia di Rugiada",
    "Sable Warband",
    "Ashfang Collective",
    "The Rusted Crown",
    "Respawn Tax",
    "Accidental Pull",
    "Dornkammsippe",
    "Die Glutfänger",
    "Aschenbanner",
    "Feierabendchaos",
    "Pull auf Verdacht",
    "La Meute Écarlate",
    "Les Crocs du Néant",
    "Le Pacte des Cendres",
    "Pull Sans Plan",
    "Les Rois du Repop",
    "Colmillos de Hierro",
    "Los Hijos del Volcán",
    "El Pacto Carmesí",
    "Falta un Tanque",
    "Otra Más y Dormimos",
    "Пепельный Клык",
    "Степные Охотники",
    "Багровый Круг",
    "Без Хила Не Пулить",
    "После Рейда Спать",
    "I Predoni del Vespro"
};
static_assert(sizeof(ApprovedGuildNames)/sizeof(ApprovedGuildNames[0])==52,"Regional guild catalog must contain exactly 52 names");
bool MatchesApprovedCatalog(GuildSpec const& spec)
{
    if(spec.key<1 || spec.key>52 || spec.locale>5 || spec.faction>1 ||
       !spec.slot || spec.slot>(spec.locale==5?1:5))return false;
    uint32 expectedKey=uint32(spec.faction)*26u+(spec.locale==5?26u:uint32(spec.locale)*5u+spec.slot);
    uint8 const weights[]={32,25,19,14,10};
    if(spec.key!=expectedKey || spec.name!=ApprovedGuildNames[spec.key-1] ||
       spec.weight!=(spec.locale==5?100:weights[spec.slot-1]))return false;
    std::wstring wide;
    return Utf8toWStr(spec.name,wide) && !wide.empty() && wide.size()<=24;
}

uint8 CatalogLanguage(std::string value)
{
    auto first=value.find_first_not_of(" \t\r\n");
    if(first==std::string::npos)return 255;
    value=value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (value == "english" || value == "enus" || value == "engb" || value == "en" || value == "portuguese" || value == "ptpt") return 0;
    if (value == "german" || value == "deutsch" || value == "dede" || value == "de") return 1;
    if (value == "french" || value == "français" || value == "frfr" || value == "fr") return 2;
    if (value == "spanish" || value == "español" || value == "eses" || value == "esmx" || value == "es") return 3;
    if (value == "russian" || value == "русский" || value == "ruru" || value == "ru") return 4;
    if (value == "italian" || value == "italiano" || value == "itit" || value == "it") return 5;
    return 255;
}
std::string OwnershipProof(std::string hash)
{
    std::transform(hash.begin(),hash.end(),hash.begin(),[](unsigned char c){return char(std::toupper(c));});
    unsigned char digest[SHA256_DIGEST_LENGTH];SHA256(reinterpret_cast<unsigned char const*>(hash.data()),hash.size(),digest);
    char const* hex="0123456789abcdef";std::string out;out.reserve(64);
    for(unsigned char c:digest){out.push_back(hex[c>>4]);out.push_back(hex[c&15]);}
    return out;
}
std::string Escape(std::string value) { CharacterDatabase.EscapeString(value); return value; }
// A failed operation is quarantined, not repeatedly retried or adopted by name.
void Failure(char const* reason, uint32 key)
{
    TC_LOG_ERROR(LOG_FILTER_GUILD, "%s key=%u reason=%s",std::string(reason)=="system_account_name_conflict"?"REGIONAL_GUILD_SYSTEM_ACCOUNT_CONFLICT":"REGIONAL_GUILD_VALIDATION_FAILED", key, reason);
}
}
RegionalGuildManager& RegionalGuildManager::Instance() { static RegionalGuildManager instance; return instance; }
bool RegionalGuildManager::Enabled() const { return sConfigMgr->GetBoolDefault("Felsoria.RegionalGuild.Enable", false); }
bool RegionalGuildManager::ProtectedAccount(uint32 value) const { std::lock_guard<std::recursive_mutex> guard(_mutex); return _accounts.count(value) != 0; }
bool RegionalGuildManager::ProtectedCharacter(uint64 value) const { std::lock_guard<std::recursive_mutex> guard(_mutex); return _characters.count(value) != 0; }
bool RegionalGuildManager::ProtectedGuild(uint64 value) const { std::lock_guard<std::recursive_mutex> guard(_mutex); return _guildIds.count(value) != 0; }
uint32 RegionalGuildManager::ProtectedAccountRealm(uint32 account) const
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    for (auto const& entry : _guilds) if (entry.second.account == account)
        for (auto const& realm : _realmLocales) if (realm.second == entry.second.locale) return realm.first;
    return 0;
}
void RegionalGuildManager::LoadProtection()
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    _tables = bool(CharacterDatabase.Query("SHOW TABLES LIKE 'felsoria_rg_guild'"));
    if (!_tables) return;
    // Independently of enable flags: disabling a feature never disables protection.
    if (!CharacterDatabase.Query("SHOW TABLES LIKE 'felsoria_rg_account'"))
        throw std::runtime_error("Regional guild registry incomplete; restore its schema before starting.");
    if (QueryResult accounts = CharacterDatabase.Query("SELECT account_id,account_name,state FROM felsoria_rg_account")) do
    {
        Field* f=accounts->Fetch();uint32 id=f[0].GetUInt32();std::string name=f[1].GetString();
        if(!id)
        {
            if(f[2].GetUInt8()==1 && AccountMgr::GetId(name))
            {
                // Recovery proof is checked before allowing the account pool to load.
                QueryResult proof=CharacterDatabase.PQuery("SELECT expected_proof FROM felsoria_rg_account WHERE account_name='%s'",Escape(name).c_str());
                uint32 existing=AccountMgr::GetId(name);
                QueryResult actual=LoginDatabase.PQuery("SELECT sha_pass_hash FROM account WHERE id=%u",existing);
                if(!proof || !actual || proof->Fetch()[0].GetString().empty() || proof->Fetch()[0].GetString()!=OwnershipProof(actual->Fetch()[0].GetString()))
                    throw std::runtime_error("Unclaimed regional account name conflict; no account was adopted.");
                if(CharacterDatabase.PQuery("SELECT guid FROM characters WHERE account=%u LIMIT 1",existing) || AccountMgr::GetSecurity(existing)!=SEC_PLAYER)
                    throw std::runtime_error("Unclaimed regional account is not empty/unprivileged; initialization stopped.");
                CharacterDatabase.DirectPExecute("UPDATE felsoria_rg_account SET account_id=%u,state=2,expected_proof='' WHERE account_name='%s' AND account_id=0 AND state=1",existing,Escape(name).c_str());
                CharacterDatabase.DirectPExecute("UPDATE felsoria_rg_guild SET account_id=%u WHERE account_name='%s' AND account_id=0",existing,Escape(name).c_str());
                id=existing;
            }
            else continue;
        }
        std::string actualName;
        if(!AccountMgr::GetName(id,actualName) || actualName!=name)throw std::runtime_error("Regional account registry ownership mismatch.");
        _accounts.insert(id);
    } while (accounts->NextRow());
    if (QueryResult guilds = CharacterDatabase.Query("SELECT leader_guid,guild_id FROM felsoria_rg_guild")) do
    {
        Field* f = guilds->Fetch();
        if (f[0].GetUInt64()) _characters.insert(f[0].GetUInt64());
        if (f[1].GetUInt64()) _guildIds.insert(f[1].GetUInt64());
    } while (guilds->NextRow());
    // Reserved but uncommitted leader GUIDs must never be reassigned after a restart.
    if(!_characters.empty())
    {
        uint64 largest=*_characters.rbegin();
        if(largest==std::numeric_limits<uint64>::max())throw std::runtime_error("Invalid reserved regional GUID.");
        auto generator=sObjectMgr->GetGenerator<HighGuid::Player>();
        if(generator->GetNextAfterMaxUsed()<=largest)generator->Set(largest+1);
    }
    if(CharacterDatabase.Query("SELECT r.guild_key FROM felsoria_rg_guild r LEFT JOIN characters c ON c.guid=r.leader_guid LEFT JOIN guild g ON g.guildid=r.guild_id LEFT JOIN guild_member m ON m.guildId=r.guild_id AND m.guid=r.leader_guid WHERE r.guild_id<>0 AND (c.guid IS NULL OR c.account<>r.account_id OR g.guildid IS NULL OR g.leaderguid<>r.leader_guid OR m.guid IS NULL OR m.rank<>0) LIMIT 1"))
        throw std::runtime_error("Regional guild ownership validation failed; repair or restore its registry before starting.");
    if(CharacterDatabase.Query("SELECT g.guildid FROM guild g JOIN felsoria_rg_guild r ON r.leader_guid=g.leaderguid LEFT JOIN guild_member m ON m.guildId=g.guildid AND m.guid=g.leaderguid WHERE m.guid IS NULL OR m.rank<>0 OR (SELECT COUNT(*) FROM guild_rank k WHERE k.guildId=g.guildid)<>5 LIMIT 1"))
        throw std::runtime_error("Incomplete regional guild creation detected; preserve its data and restore/repair before starting.");
    for (uint32 account : _accounts)
    {
        if (AccountMgr::GetSecurity(account) != SEC_PLAYER)
            throw std::runtime_error("Regional system account has elevated privileges; initialization stopped.");
        if (CharacterDatabase.PQuery("SELECT guid FROM characters WHERE account=%u AND guid NOT IN (SELECT leader_guid FROM felsoria_rg_guild WHERE account_id=%u)", account, account))
            throw std::runtime_error("Regional system account contains an unregistered character; initialization stopped.");
    }
}
bool RegionalGuildManager::LoadSpecs()
{
    QueryResult result = CharacterDatabase.Query("SELECT r.guild_key,r.locale_id,r.faction,r.slot,r.weight,r.name,a.account_name,r.leader_name,r.account_id,r.leader_guid,r.guild_id FROM felsoria_rg_guild r JOIN felsoria_rg_account a ON r.account_key=a.account_key ORDER BY r.guild_key");
    if (!result || result->GetRowCount() != 52) return false;
    do
    {
        Field* f = result->Fetch(); GuildSpec spec;
        spec.key=f[0].GetUInt32(); spec.locale=f[1].GetUInt8(); spec.faction=f[2].GetUInt8(); spec.slot=f[3].GetUInt8(); spec.weight=f[4].GetUInt8();
        spec.name=f[5].GetString(); spec.accountName=f[6].GetString(); spec.leaderName=f[7].GetString();
        spec.account=f[8].GetUInt32(); spec.leader=f[9].GetUInt64(); spec.guild=f[10].GetUInt32();
        if (!MatchesApprovedCatalog(spec)) { Failure("approved_catalog_mismatch",spec.key); return false; }
        _guilds.emplace(spec.key,spec);
    } while (result->NextRow());
    return _guilds.size()==52;
}
void RegionalGuildManager::LoadProfiles()
{
    QueryResult result=CharacterDatabase.Query("SELECT identity_id,account_id,realm_id,locale_id,faction,guild_key,personality,dynamic_bot,char_guid FROM felsoria_rg_identity");
    if (!result) return;
    do
    {
        Field* f=result->Fetch(); Profile p;
        p.id=f[0].GetUInt64();p.account=f[1].GetUInt32();p.realm=f[2].GetUInt32();p.locale=f[3].GetUInt8();p.faction=f[4].GetUInt8();p.guildKey=f[5].GetUInt32();p.personality=f[6].GetUInt8();p.dynamic=f[7].GetUInt8()!=0;p.guid=f[8].GetUInt64();
        if (p.locale>5 || p.faction>1 || p.personality>6 || !_guilds.count(p.guildKey) || _guilds[p.guildKey].locale!=p.locale || _guilds[p.guildKey].faction!=p.faction || LocaleForRealm(p.realm)!=p.locale) { _failed=true; Failure("invalid_identity",p.guildKey); return; }
        _profiles.emplace(p.id,p);
        if(p.guid && !CharacterDatabase.PQuery("SELECT guid FROM characters WHERE guid=" UI64FMTD " AND account=%u",p.guid,p.account))
        {
            CharacterDatabase.DirectPExecute("UPDATE felsoria_rg_identity SET char_guid=0 WHERE identity_id=" UI64FMTD,p.id);
            p.guid=0;_profiles[p.id]=p;
        }
        if (p.guid && !_bindings.emplace(p.guid,p.id).second) { _failed=true; Failure("duplicate_character_binding",p.guildKey); return; }
    } while(result->NextRow());
}
void RegionalGuildManager::SeedOfflineProfiles()
{
    QueryResult rows=CharacterDatabase.Query("SELECT c.guid,c.account,c.race,b.realm_id FROM characters c JOIN felsoria_bot_realms b ON b.guid=c.guid WHERE NOT EXISTS (SELECT 1 FROM lf_dynamic_bot_owned d WHERE d.account=c.account)");
    if(!rows)return;
    do
    {
        Field* f=rows->Fetch();uint64 guid=f[0].GetUInt64();uint32 account=f[1].GetUInt32(),origin=f[3].GetUInt32();
        if(_bindings.count(guid) || ProtectedAccount(account) || !sPlayerBotMgr->GetPlayerBotAccountInfo(account))continue;
        uint8 race=f[2].GetUInt8(),locale=LocaleForRealm(origin);if(locale>5 || race==RACE_PANDAREN_NEUTRAL)continue;
        CharacterInfo const* info=sWorld->GetCharacterInfo(ObjectGuid::Create<HighGuid::Player>(guid));
        if(!info || (info->GuildId && !ProtectedGuild(info->GuildId)))continue;
        Profile p;p.guid=guid;p.account=account;p.realm=origin;p.locale=locale;p.faction=Player::TeamForRace(race)==HORDE?1:0;p.guildKey=0;
        if(info->GuildId)for(auto const& g:_guilds)if(g.second.guild==info->GuildId && g.second.locale==locale && g.second.faction==p.faction)p.guildKey=g.first;
        if(!p.guildKey)p.guildKey=ChooseGuild(locale,p.faction);p.personality=urand(0,6);
        if(!p.guildKey)continue;
        CharacterDatabase.DirectPExecute("INSERT IGNORE INTO felsoria_rg_identity(account_id,realm_id,locale_id,faction,guild_key,personality,dynamic_bot,char_guid,anchor_guid) VALUES(%u,%u,%u,%u,%u,%u,0," UI64FMTD "," UI64FMTD ")",account,origin,locale,p.faction,p.guildKey,p.personality,guid,guid);
        QueryResult id=CharacterDatabase.PQuery("SELECT identity_id,guild_key,personality FROM felsoria_rg_identity WHERE anchor_guid=" UI64FMTD " AND account_id=%u AND dynamic_bot=0",guid,account);
        if(!id){_failed=true;Failure("offline_identity_save",0);return;}
        p.id=id->Fetch()[0].GetUInt64();p.guildKey=id->Fetch()[1].GetUInt32();p.personality=id->Fetch()[2].GetUInt8();_profiles[p.id]=p;_bindings[guid]=p.id;
    }while(rows->NextRow());
}
void RegionalGuildManager::Initialize()
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (!Enabled()) return;
    if(sWorld->getIntConfig(CONFIG_CHARACTERS_PER_REALM)<5 || sWorld->getIntConfig(CONFIG_CHARACTERS_PER_ACCOUNT)<5){_failed=true;Failure("character_slot_limit_below_five",0);return;}
    if (!_tables || !LoadSpecs()) { _failed=true; Failure("install_schema_and_seed_first",0); return; }
    if (QueryResult realms=LoginDatabase.Query("SELECT realm_id,language FROM felsoria_eu_legion_catalog WHERE enabled=1")) do
    {
        Field* f=realms->Fetch(); uint8 loc=CatalogLanguage(f[1].GetString());
        if (loc<6) _realmLocales[f[0].GetUInt32()]=loc;
        else TC_LOG_ERROR(LOG_FILTER_GUILD,"REGIONAL_GUILD_REALM_SKIPPED realm=%u language=%s reason=unsupported_catalog_language; no_language_was_assigned",f[0].GetUInt32(),f[1].GetString().c_str());
    } while(realms->NextRow());
    if (_realmLocales.empty()) { _failed=true; Failure("no_supported_catalog_realms",0); return; }
    LoadProfiles();
    if(sConfigMgr->GetBoolDefault("Felsoria.RegionalGuild.AutoAssignBots",true))SeedOfflineProfiles();
    for(auto const& row:_profiles)if(row.second.guid)_pending.insert(row.second.guid);
    TC_LOG_INFO(LOG_FILTER_GUILD,"REGIONAL_GUILD_INIT guilds=%u profiles=%u",uint32(_guilds.size()),uint32(_profiles.size()));
}
uint8 RegionalGuildManager::LocaleForRealm(uint32 realm) const
{
    std::lock_guard<std::recursive_mutex> guard(_mutex); auto it=_realmLocales.find(realm); return it==_realmLocales.end()?255:it->second;
}
uint32 RegionalGuildManager::PreferredBotRealm(uint32 account,uint8 faction,uint32 requested) const
{
    if(requested || !Enabled())return requested;
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    for(auto const& row:_profiles)if(row.second.dynamic && row.second.account==account && row.second.faction==faction && !row.second.guid)return row.second.realm;
    return 0;
}
uint32 RegionalGuildManager::RealmFor(Player const* p) const { return p?Battlenet::RealmHandle(sWorld->GetFelsoriaBotRealmAddress(p->GetGUID())).Realm:0; }
bool RegionalGuildManager::GetProfile(uint64 guid, Profile& p) const
{
    std::lock_guard<std::recursive_mutex> guard(_mutex); auto b=_bindings.find(guid); if(b==_bindings.end())return false; auto it=_profiles.find(b->second); if(it==_profiles.end())return false;p=it->second;return true;
}
void RegionalGuildManager::RegisterBotAccount(uint32 account)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);if(!ProtectedAccount(account))_botAccounts.insert(account);
}
bool RegionalGuildManager::ManagedBot(Player const* p) const
{
    if(!p || !p->GetSession() || !p->GetSession()->IsBotSession())return false;
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    return !_accounts.count(p->GetSession()->GetAccountId()) && _botAccounts.count(p->GetSession()->GetAccountId())!=0;
}
void RegionalGuildManager::Observe(Player* p)
{
    if (!p || !p->GetSession()->IsBotSession()) return;
    if (!p->IsInWorld()) return;
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    Profile profile;
    if(_ready && p->GetGuildId())_pending.erase(p->GetGUID().GetCounter());
    if (_ready && (!GetProfile(p->GetGUID().GetCounter(), profile) ||
        profile.realm != RealmFor(p) || (!p->GetGuildId() && !_reported.count(p->GetGUID().GetCounter()))))
    {
        if (!Assign(p)) _reported.insert(p->GetGUID().GetCounter());
        _pending.erase(p->GetGUID().GetCounter());
    }
}
void RegionalGuildManager::Forget(uint64 guid) { std::lock_guard<std::recursive_mutex> guard(_mutex); _reported.erase(guid); if(_bindings.count(guid))_pending.insert(guid); }
uint32 RegionalGuildManager::ChooseGuild(uint8 loc,uint8 faction) const
{
    uint32 total=0; std::map<uint32,uint32> counts;
    for(auto const& row:_profiles)if(row.second.locale==loc && row.second.faction==faction){++total;++counts[row.second.guildKey];}
    uint32 best=0;int64 deficit=-1000000000;
    for(auto const& row:_guilds)if(row.second.locale==loc && row.second.faction==faction)
    {
        int64 score=int64(total+1)*row.second.weight-int64(counts[row.first])*100;
        if(!best || score>deficit){best=row.first;deficit=score;}
    }
    return best;
}
bool RegionalGuildManager::Bootstrap(GuildSpec& s)
{
    std::string accountName=s.accountName;
    if(!s.account)
    {
        QueryResult row=CharacterDatabase.PQuery("SELECT account_id,state FROM felsoria_rg_account WHERE account_name='%s'",Escape(accountName).c_str());
        if(!row)return false;
        uint32 account=row->Fetch()[0].GetUInt32();
        if(!account)
        {
            uint32 existing=AccountMgr::GetId(accountName);
            // An unowned existing account is never adopted by username.
            if(existing){Failure("system_account_name_conflict",s.key);_failed=true;return false;}
            unsigned char bytes[16];if(RAND_bytes(bytes,sizeof(bytes))!=1){_failed=true;return false;}
            char const* alphabet="ABCDEFGHJKLMNPQRSTUVWXYZ23456789";std::string password;
            for(unsigned char c:bytes)password.push_back(alphabet[c&31]);
            std::string proof=OwnershipProof(AccountMgr::CalculateShaPassHash(accountName,password));
            // Journal proof BEFORE the core API call. After a crash, recovery can verify creation
            // without guessing ownership from a name or retaining a plaintext password.
            CharacterDatabase.DirectPExecute("UPDATE felsoria_rg_account SET state=1,expected_proof='%s' WHERE account_name='%s' AND account_id=0",proof.c_str(),Escape(accountName).c_str());
            QueryResult journal=CharacterDatabase.PQuery("SELECT expected_proof FROM felsoria_rg_account WHERE account_name='%s' AND state=1 AND account_id=0",Escape(accountName).c_str());
            if(!journal || journal->Fetch()[0].GetString()!=proof){_failed=true;return false;}
            AccountOpResult status=AccountMgr::CreateAccount(accountName,password,false);
            std::fill(password.begin(),password.end(),'\0');std::fill(std::begin(bytes),std::end(bytes),0);
            if(status!=AccountOpResult::AOR_OK || !(account=AccountMgr::GetId(accountName))){Failure("account_create_failed",s.key);_failed=true;return false;}
            CharacterDatabase.DirectPExecute("UPDATE felsoria_rg_account SET account_id=%u,state=2,expected_proof='' WHERE account_name='%s' AND account_id=0 AND state=1 AND expected_proof='%s'",account,Escape(accountName).c_str(),proof.c_str());
            QueryResult verified=CharacterDatabase.PQuery("SELECT account_id FROM felsoria_rg_account WHERE account_name='%s' AND state=2",Escape(accountName).c_str());
            if(!verified || verified->Fetch()[0].GetUInt32()!=account){_failed=true;return false;}
            TC_LOG_INFO(LOG_FILTER_GUILD,"REGIONAL_GUILD_SYSTEM_ACCOUNT_CREATED account=%u",account);
        }
        std::string actual;
        if(!AccountMgr::GetName(account,actual) || actual!=accountName || AccountMgr::GetSecurity(account)!=SEC_PLAYER){Failure("system_account_ownership_mismatch",s.key);_failed=true;return false;}
        s.account=account;_accounts.insert(account);
        CharacterDatabase.DirectPExecute("UPDATE felsoria_rg_guild SET account_id=%u WHERE account_name='%s' AND account_id=0",account,Escape(accountName).c_str());
    }
    std::string actualAccount;
    if(!AccountMgr::GetName(s.account,actualAccount) || actualAccount!=accountName || AccountMgr::GetSecurity(s.account)!=SEC_PLAYER)
    {Failure("existing_account_validation",s.key);_failed=true;return false;}
    if(s.guild)
    {
        Guild* g=sGuildMgr->GetGuildById(s.guild);
        if(!g || g->GetName()!=s.name || g->GetLeaderGUID().GetCounter()!=s.leader || !ProtectedCharacter(s.leader))
        {Failure("existing_guild_validation",s.key);_failed=true;return false;}
        return true;
    }
    if(sCharacterDataStore->IsReservedName(s.name)) {Failure("reserved_guild_name",s.key);_failed=true;return false;}
    // The core's charter predicate also runs physical-realm/DBC filters.
    // This initializer may use only the 52 exact, approved regional names.
    // All ordinary player charter/name checks remain unchanged.
    if(!MatchesApprovedCatalog(s)){Failure("approved_catalog_mismatch",s.key);_failed=true;return false;}
    if(!sCharacterDataStore->IsValidCharterName(s.name,Locales[s.locale]))
        TC_LOG_INFO(LOG_FILTER_GUILD,"REGIONAL_GUILD_APPROVED_NAME_EXCEPTION key=%u name=%s realm_zone=%u strict_mask=%u reason=core_charter_filter",s.key,s.name.c_str(),uint32(sWorld->getIntConfig(CONFIG_REALM_ZONE)),uint32(sWorld->getIntConfig(CONFIG_STRICT_CHARTER_NAMES)));
    if(Guild* recovered=sGuildMgr->GetGuildByName(s.name))
    {
        QueryResult owner=s.leader?CharacterDatabase.PQuery("SELECT c.guid FROM characters c JOIN guild_member m ON m.guid=c.guid WHERE c.guid=" UI64FMTD " AND c.account=%u AND m.guildId=" UI64FMTD " AND m.rank=0",s.leader,s.account,uint64(recovered->GetId())):QueryResult();
        if(!owner || !ProtectedCharacter(s.leader) || recovered->GetLeaderGUID().GetCounter()!=s.leader)
        {Failure("guild_name_conflict",s.key);_failed=true;return false;}
        s.guild=uint32(recovered->GetId());_guildIds.insert(s.guild);
        CharacterDatabase.DirectPExecute("UPDATE felsoria_rg_guild SET guild_id=%u WHERE guild_key=%u AND guild_id=0",s.guild,s.key);
        sWorld->UpdateRegionalCharacterGuild(s.leader,s.guild,0);return true;
    }
    // All validation and asynchronous-save polling must precede Player construction.
    // CleanupsBeforeDelete requires a successfully created Player/map context.
    if(!s.leader)
    {
        std::string replacement;
        if(s.key==1 && s.leaderName=="Felsaaa")replacement="Felsara";
        if(s.key==33 && s.leaderName=="Felsbbb")replacement="Felsbra";
        if(!replacement.empty())
        {
            // Only repair this release's uncreated technical names; never rename a character.
            CharacterDatabase.DirectPExecute("UPDATE felsoria_rg_guild SET leader_name='%s' WHERE guild_key=%u AND leader_guid=0 AND guild_id=0 AND leader_name='%s'",Escape(replacement).c_str(),s.key,Escape(s.leaderName).c_str());
            QueryResult repaired=CharacterDatabase.PQuery("SELECT leader_name FROM felsoria_rg_guild WHERE guild_key=%u AND leader_guid=0 AND guild_id=0",s.key);
            if(!repaired || repaired->Fetch()[0].GetString()!=replacement){Failure("pending_leader_name_repair_failed",s.key);_failed=true;return false;}
            s.leaderName=replacement;
        }
        ResponseCodes nameResult=sCharacterDataStore->CheckPlayerName(s.leaderName,Locales[s.locale],true,s.locale==4?12:8);
        if(sCharacterDataStore->IsReservedName(s.leaderName) || nameResult!=CHAR_NAME_SUCCESS)
        {
            TC_LOG_ERROR(LOG_FILTER_GUILD,"REGIONAL_GUILD_LEADER_NAME_REJECTED key=%u code=%u",s.key,uint32(nameResult));
            Failure("leader_name_validation",s.key);_failed=true;return false;
        }
        if(sWorld->GetCharacterInfo(s.leaderName) || CharacterDatabase.PQuery("SELECT guid FROM characters WHERE name='%s'",Escape(s.leaderName).c_str()))
        {Failure("leader_name_conflict",s.key);_failed=true;return false;}
    }
    else
    {
        QueryResult exists=CharacterDatabase.PQuery("SELECT account,name,race,class FROM characters WHERE guid=" UI64FMTD,s.leader);
        if(!exists){if(++_bootstrapAttempts[s.key]>30){_failed=true;Failure("pending_character_not_persisted",s.key);}return false;}
        Field* f=exists->Fetch();
        if(f[0].GetUInt32()!=s.account || f[1].GetString()!=s.leaderName || f[2].GetUInt8()!=(s.faction?RACE_ORC:RACE_HUMAN) || f[3].GetUInt8()!=CLASS_WARRIOR)
        {Failure("leader_owner_mismatch",s.key);_failed=true;return false;}
        QueryResult charCount=CharacterDatabase.PQuery("SELECT COUNT(*) FROM characters WHERE account=%u",s.account);
        if(!charCount || charCount->Fetch()[0].GetUInt32()>12){_failed=true;Failure("leader_account_character_count",s.key);return false;}
        SQLTransaction auth=LoginDatabase.BeginTransaction();
        PreparedStatement* del=LoginDatabase.GetPreparedStatement(LOGIN_DEL_REALM_CHARACTERS_BY_REALM);del->setUInt32(0,s.account);del->setUInt32(1,realm.Id.Realm);auth->Append(del);
        PreparedStatement* ins=LoginDatabase.GetPreparedStatement(LOGIN_INS_REALM_CHARACTERS);ins->setUInt32(0,charCount->Fetch()[0].GetUInt32());ins->setUInt32(1,s.account);ins->setUInt32(2,realm.Id.Realm);auth->Append(ins);LoginDatabase.CommitTransaction(auth);
    }
    WorldSession session(s.account,std::string(accountName),std::shared_ptr<WorldSocket>(),SEC_PLAYER,6,0,"Win",Locales[s.locale],0,false,static_cast<AuthFlags>(0),0);
    WorldPackets::Character::CharacterCreateInfo info;info.Name=s.leaderName;info.Race=s.faction?RACE_ORC:RACE_HUMAN;info.Class=CLASS_WARRIOR;info.Sex=GENDER_MALE;info.CustomDisplay.fill(0);
    Player leader(&session);leader.GetMotionMaster()->Initialize();
    if(!s.leader)
    {
        uint64 guid=sObjectMgr->GetGenerator<HighGuid::Player>()->Generate();
        if(!leader.Create(guid,&info)){Failure("leader_create_failed",s.key);_failed=true;return false;}
        s.leader=leader.GetGUID().GetCounter();_characters.insert(s.leader);
        CharacterDatabase.DirectPExecute("UPDATE felsoria_rg_guild SET leader_guid=" UI64FMTD " WHERE guild_key=%u AND leader_guid=0",s.leader,s.key);
        // Uses the same core create/save path as CharacterHandler; never synthesize character rows.
        leader.setCinematic(1);leader.SaveToDB(true);
        sWorld->AddCharacterInfo(s.leader,leader.GetName(),leader.getGender(),leader.getRace(),leader.getClass(),leader.getLevel(),s.account);
        sWorld->UpdateCharacterAccount(s.leader,s.account);
        uint32 origin=ProtectedAccountRealm(s.account);
        if(origin) CharacterDatabase.DirectPExecute("INSERT INTO felsoria_bot_realms(guid,realm_id) VALUES(" UI64FMTD ",%u) ON DUPLICATE KEY UPDATE guid=guid",s.leader,origin);
        leader.GetAchievementMgr()->ClearMap();leader.CleanupsBeforeDelete();
        TC_LOG_INFO(LOG_FILTER_GUILD,"REGIONAL_GUILD_GUILDMASTER_CREATED key=%u guid=" UI64FMTD,s.key,s.leader);
        return false; // asynchronous SaveToDB must be visible before AddMember.
    }
    // A transient core Player supplies Guild::Create's session contract.
    // A failed Create may have no map context; do not call world cleanup on it.
    if(!leader.Create(s.leader,&info)){Failure("leader_create_failed",s.key);_failed=true;return false;}
    std::unique_ptr<Guild> guild(new Guild());
    if(!guild->Create(&leader,s.name)){Failure("guild_create_failed",s.key);_failed=true;leader.CleanupsBeforeDelete();return false;}
    s.guild=uint32(guild->GetId());sGuildMgr->AddGuild(guild.release());_guildIds.insert(s.guild);
    sWorld->UpdateRegionalCharacterGuild(s.leader,s.guild,0);
    CharacterDatabase.DirectPExecute("UPDATE felsoria_rg_guild SET guild_id=%u WHERE guild_key=%u AND guild_id=0",s.guild,s.key);
    // Newly created ranks only; never rename an existing guild on restart.
    char const* ranks[6][5]={
        {"Guildmaster","Officer","Veteran","Member","Recruit"},
        {"Gildenmeister","Offizier","Veteran","Mitglied","Rekrut"},
        {"Maître de guilde","Officier","Vétéran","Membre","Recrue"},
        {"Maestro de hermandad","Oficial","Veterano","Miembro","Recluta"},
        {"Глава гильдии","Офицер","Ветеран","Участник","Новобранец"},
        {"Capogilda","Ufficiale","Veterano","Membro","Recluta"}};
    sGuildMgr->GetGuildById(s.guild)->SetRegionalRankNames(std::vector<std::string>(ranks[s.locale],ranks[s.locale]+5));
    leader.GetAchievementMgr()->ClearMap();leader.CleanupsBeforeDelete();
    TC_LOG_INFO(LOG_FILTER_GUILD,"REGIONAL_GUILD_CREATED key=%u guild=%u leader=" UI64FMTD,s.key,s.guild,s.leader);
    return true;
}
bool RegionalGuildManager::Assign(Player* player)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if(!_ready || !Enabled() || !sConfigMgr->GetBoolDefault("Felsoria.RegionalGuild.AutoAssignBots",true) || !ManagedBot(player))return false;
    uint64 guid=player->GetGUID().GetCounter();uint32 account=player->GetSession()->GetAccountId(), realm=RealmFor(player);
    uint8 locale=LocaleForRealm(realm), faction=uint8(player->GetTeamId());
    if(locale>5 || faction>1)return false;
    if(player->GetGuildId() && !ProtectedGuild(player->GetGuildId()))return false; // Preserve manual guild membership.
    uint32 previousKey=0;
    if(player->GetGuildId())for(auto const& g:_guilds)if(g.second.guild==player->GetGuildId() && g.second.locale==locale && g.second.faction==faction)previousKey=g.first;
    Profile profile;
    bool have=GetProfile(guid,profile);bool restoring=have;
    if(have && (profile.realm!=realm || profile.locale!=locale || profile.faction!=faction))
    {
        // Real migration: a static character keeps its ID, receives one new assignment.
        if(profile.dynamic) return false; // Dynamic origin changes are not migrations; quarantine.
        bool changedCommunity=profile.locale!=locale || profile.faction!=faction;
        if(changedCommunity)
        {
            if(player->GetGuildId())if(Guild* old=sGuildMgr->GetGuildById(player->GetGuildId()))old->DeleteMember(player->GetGUID());
            profile.guildKey=ChooseGuild(locale,faction);
        }
        profile.realm=realm;profile.locale=locale;profile.faction=faction;
        CharacterDatabase.DirectPExecute("UPDATE felsoria_rg_identity SET realm_id=%u,locale_id=%u,faction=%u,guild_key=%u WHERE identity_id=" UI64FMTD,realm,locale,faction,profile.guildKey,profile.id);
        _profiles[profile.id]=profile;
    }
    if(!have)
    {
        bool dynamic=bool(CharacterDatabase.PQuery("SELECT account FROM lf_dynamic_bot_owned WHERE account=%u AND guid=" UI64FMTD,account,guid));
        // Rebind a persistent dynamic identity in the same realm/faction, not a deleted GUID.
        for(auto const& row:_profiles)if(dynamic && row.second.dynamic && row.second.account==account && row.second.realm==realm && row.second.faction==faction && !row.second.guid){profile=row.second;have=true;break;}
        restoring=have;
        if(!have)
        {
            profile.account=account;profile.realm=realm;profile.locale=locale;profile.faction=faction;profile.guildKey=previousKey?previousKey:ChooseGuild(locale,faction);profile.personality=urand(0,6);profile.dynamic=dynamic;
            if(!profile.guildKey)return false;
            CharacterDatabase.DirectPExecute("INSERT INTO felsoria_rg_identity(account_id,realm_id,locale_id,faction,guild_key,personality,dynamic_bot,char_guid,anchor_guid) VALUES(%u,%u,%u,%u,%u,%u,%u," UI64FMTD "," UI64FMTD ")",account,realm,locale,faction,profile.guildKey,profile.personality,uint32(dynamic),guid,dynamic?uint64(0):guid);
            QueryResult id=CharacterDatabase.PQuery("SELECT identity_id FROM felsoria_rg_identity WHERE char_guid=" UI64FMTD,guid);
            if(!id)return false;profile.id=id->Fetch()[0].GetUInt64();
        }
        else CharacterDatabase.DirectPExecute("UPDATE felsoria_rg_identity SET char_guid=" UI64FMTD " WHERE identity_id=" UI64FMTD " AND char_guid=0",guid,profile.id);
        profile.guid=guid;_profiles[profile.id]=profile;_bindings[guid]=profile.id;
    }
    auto row=_guilds.find(profile.guildKey);if(row==_guilds.end() || !row->second.guild)return false;
    if(player->GetGuildId()==row->second.guild)return true;
    if(player->GetGuildId())
    {
        // Only a positively managed regional membership may be reconciled.
        Guild* old=sGuildMgr->GetGuildById(player->GetGuildId());if(!old || !ProtectedGuild(old->GetId()))return false;
        old->DeleteMember(player->GetGUID());
    }
    Guild* guild=sGuildMgr->GetGuildById(row->second.guild);
    bool ok=guild && guild->AddMember(player->GetGUID(),3);
    if(ok && sConfigMgr->GetBoolDefault("Felsoria.RegionalGuild.Debug",false))TC_LOG_INFO(LOG_FILTER_GUILD,"%s identity=" UI64FMTD " guid=" UI64FMTD " guild=%u",restoring?"REGIONAL_GUILD_BOT_RESTORED":"REGIONAL_GUILD_BOT_ASSIGNED",profile.id,guid,row->second.guild);
    return ok;
}
void RegionalGuildManager::Retired(uint32 account,uint64 guid)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    auto binding=_bindings.find(guid);if(binding==_bindings.end())return;
    auto profile=_profiles.find(binding->second);if(profile==_profiles.end() || profile->second.account!=account)return;
    CharacterDatabase.DirectPExecute("UPDATE felsoria_rg_identity SET char_guid=0 WHERE identity_id=" UI64FMTD " AND account_id=%u AND char_guid=" UI64FMTD,profile->second.id,account,guid);
    profile->second.guid=0;_bindings.erase(binding);_pending.erase(guid);_reported.erase(guid);
}
void RegionalGuildManager::Update(uint32 diff)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if(!Enabled() || _failed || !_tables || _guilds.empty())return;
    _timer+=diff;if(_timer<1000)return;_timer=0;
    if(!_ready)
    {
        for(auto& row:_guilds)
        {
            if(row.second.guild)continue;
            if(!sConfigMgr->GetBoolDefault("Felsoria.RegionalGuild.AutoCreate",true))return;
            Bootstrap(row.second);return; // At most one bootstrap step/second, outside player login.
        }
        for(auto& row:_guilds)if(!Bootstrap(row.second))return;
        _ready=true;TC_LOG_INFO(LOG_FILTER_GUILD,"REGIONAL_GUILD_INIT complete=1 guilds=52 accounts=%u",uint32(_accounts.size()));
    }
    if(!sConfigMgr->GetBoolDefault("Felsoria.RegionalGuild.AutoAssignBots",true))return;
    // Offline identities are assigned once in bounded startup batches. Online actors bind on their own map thread.
    uint32 budget=4;
    for(auto it=_pending.begin();it!=_pending.end() && budget;)
    {
        uint64 guid=*it;--budget;
        if(ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(guid))){++it;continue;}
        it=_pending.erase(it);
        Profile p;if(!GetProfile(guid,p))continue;
        auto row=_guilds.find(p.guildKey);if(row==_guilds.end())continue;
        CharacterInfo const* info=sWorld->GetCharacterInfo(ObjectGuid::Create<HighGuid::Player>(guid));
        if(!info || info->IsDeleted || info->GuildId==row->second.guild)continue;
        if(info->GuildId)
        {
            if(!ProtectedGuild(info->GuildId))continue;
            if(Guild* old=sGuildMgr->GetGuildById(info->GuildId))old->DeleteMember(ObjectGuid::Create<HighGuid::Player>(guid));
        }
        if(Guild* guild=sGuildMgr->GetGuildById(row->second.guild))guild->AddMember(ObjectGuid::Create<HighGuid::Player>(guid),3);
    }
}
}
