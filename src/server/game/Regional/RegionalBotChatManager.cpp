#include "GroupReference.h"
#include "GameObject.h"
#include "BattlegroundArathiBasin.h"
#include "BattlegroundBattleForGilneas.h"
#include "BattlegroundTwinPeaks.h"
#include "BattlegroundWarsongGulch.h"
#include "LFGMgr.h"
#include "ReputationMgr.h"
#include "SpellMgr.h"
#include "Spell.h"
#include "QuestDef.h"
#include "QuestData.h"
#include "Guild.h"
#include "BotGroupAI.h"
#include "ItemTemplate.h"
#include "Item.h"
#include "Bag.h"
#include "FelsoriaRegional.h"
#include "Battleground.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "ObjectMgr.h"
#include "ChatPackets.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RealmList.h"
#include "SocialMgr.h"
#include "Util.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <sstream>
#include <fstream>
#include <array>
#include <limits>
namespace FelsoriaRegional
{
namespace
{
uint64 Clock() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
bool Active() { return sConfigMgr->GetBoolDefault("Felsoria.BotChat.Enable",false); }
uint32 Bounded(char const* key,int def,int low,int high) { return uint32(std::max(low,std::min(high,sConfigMgr->GetIntDefault(key,def)))); }
std::string Context(uint32 realm,uint32 zone,uint8 faction,uint8 locale,uint8 channel,uint64 group=0)
{ return std::to_string(realm)+":"+std::to_string(zone)+":"+std::to_string(faction)+":"+std::to_string(locale)+":"+std::to_string(channel)+":"+std::to_string(group); }
// Community intervals are shared by the whole BG team, not by individual raid subgroups.
bool IsReply(Event event)
{
    return event==Event::Greeting || event==Event::Help || (uint8(event)>=16 && uint8(event)<=18);
}
uint8 Priority(Event event)
{
    if(IsReply(event))return 4;
    if(event==Event::BossKill || event==Event::Wipe || (uint8(event)>=19 && uint8(event)<=24))return 3;
    return event==Event::Presence?1:2;
}
std::string AudienceKey(uint32 realm,uint32 zone,uint32 map,uint32 instance,uint8 faction,uint8 bgTeam,uint8 locale,uint8 channel,uint64 group)
{
    if(channel==3)return Context(0,map,bgTeam,locale,channel,instance);
    if(channel==2 || channel==4)return Context(0,map,0,locale,channel,group)+":"+std::to_string(instance);
    return Context(realm,zone,faction,locale,channel);
}
uint32 ScaledDialogueCooldown(uint32 seconds)
{
    return uint32(uint64(seconds)*Bounded("Felsoria.BotChat.DialogueCooldownScalePercent",5,1,100)/100);
}
bool GeneralDefinition(ChatChannelsEntry const* d)
{
    if(!d)return false;
    // Canonical localized DB2 names are accepted before legacy flag interpretation.
    // Custom channels have no DB2 entry and cannot enter this path.
    if(d->Name)for(auto const* text:d->Name->Str)
    {
        if(!text)continue;
        std::string name=text;
        char const* names[]={"General","Général","Allgemein","Общий","Generale"};
        for(auto canonical:names)
        {
            std::string prefix=canonical;
            if(name==prefix || name.compare(0,prefix.size()+2,prefix+" -")==0 || name.compare(0,prefix.size()+1,prefix+":")==0)return true;
        }
    }
    uint32 blocked=CHANNEL_DBC_FLAG_TRADE|CHANNEL_DBC_FLAG_LFG|CHANNEL_DBC_FLAG_GLOBAL|CHANNEL_DBC_FLAG_DEFENSE|CHANNEL_DBC_FLAG_GUILD_REQ;
    return !(d->Flags & blocked) && (d->Flags & CHANNEL_DBC_FLAG_UNK1) && (d->Flags & CHANNEL_DBC_FLAG_ZONE_DEP);
}
std::string NativeGeneralName(Player const* player,ChatChannelsEntry const* d)
{
    if(!player || !d || !d->Name)return {};
    AreaTableEntry const* area=sAreaTableStore.LookupEntry(player->GetZoneId());
    if(!area || !area->ZoneName)return {};
    auto firstNonempty=[](auto const* localized)->char const*
    {
        for(auto const* text:localized->Str)if(text && *text)return text;
        return nullptr;
    };
    char const* format=d->Name->Str[player->GetSession()->GetSessionDbLocaleIndex()];
    if(!format || !*format)format=firstNonempty(d->Name);
    // Match Player::UpdateLocalChannels exactly: format uses the session locale,
    // while the zone text uses the server's DB2 locale.
    char const* zone=area->ZoneName->Str[sObjectMgr->GetDBCLocaleIndex()];
    if(!zone || !*zone)zone=firstNonempty(area->ZoneName);
    if(!format || !zone)return {};
    std::string name=format;std::size_t pos=0;
    while((pos=name.find("%s",pos))!=std::string::npos){name.replace(pos,2,zone);pos+=std::char_traits<char>::length(zone);}
    if(name.empty() || name.size()>=100 || name.find('%')!=std::string::npos)return {};
    return name;
}
thread_local Player const* ChatAnchor=nullptr;
struct DeliveryScope
{
    Player const* previous;
    explicit DeliveryScope(Player const* anchor):previous(ChatAnchor){ChatAnchor=anchor;}
    ~DeliveryScope(){ChatAnchor=previous;}
};
bool ChannelEnabled(uint8 c)
{
    char const* keys[]={"General","Say","Party","Battleground","Instance","Whisper"};
    return c<6 && sConfigMgr->GetBoolDefault(std::string("Felsoria.BotChat.")+keys[c],true);
}
}
RegionalBotChatManager& RegionalBotChatManager::Instance() { static RegionalBotChatManager instance; return instance; }
void RegionalBotChatManager::Initialize()
{
    if(!Active())return;
    LoadFullTexts();
    _ready=_fullReady.load();
    if(!CharacterDatabase.Query("SHOW TABLES LIKE 'felsoria_bc_phrase'")){TC_LOG_ERROR(LOG_FILTER_GUILD,"REGIONAL_BOTCHAT_VALIDATION_FAILED missing_schema");return;}
    if(QueryResult rows=CharacterDatabase.Query("SELECT phrase_id,locale_id,tone,text FROM felsoria_bc_phrase ORDER BY phrase_id"))do
    {
        Field* f=rows->Fetch();Phrase p;p.id=f[0].GetString();p.locale=f[1].GetUInt8();p.tone=f[2].GetUInt8();p.text=f[3].GetString();
        std::wstring wide;
        if(p.locale>5 || p.tone>2 || p.text.empty() || p.text.size()>1023 || p.text.find_first_of("\n\r|")!=std::string::npos || !Utf8toWStr(p.text,wide) || !_phrases.emplace(p.id,p).second)
        {TC_LOG_ERROR(LOG_FILTER_GUILD,"REGIONAL_BOTCHAT_VALIDATION_FAILED phrase");return;}
    }while(rows->NextRow());
    if(CharacterDatabase.Query("SELECT phrase_id FROM felsoria_bc_phrase WHERE SHA2(text,256)<>content_hash LIMIT 1"))return;
    uint32 counts[6]={};for(auto const& p:_phrases)++counts[p.second.locale];
    if(_phrases.size()!=1350 || std::any_of(std::begin(counts),std::end(counts),[](uint32 n){return n!=225;}))return;
    if(QueryResult rows=CharacterDatabase.Query("SELECT dialogue_id,locale_id,tone,channel,event_id,topic,zone_id,cooldown_seconds,weight FROM felsoria_bc_dialogue WHERE enabled=1 ORDER BY dialogue_id"))do
    {
        Field* f=rows->Fetch();Dialogue d;d.id=f[0].GetUInt32();d.locale=f[1].GetUInt8();d.tone=f[2].GetUInt8();d.channel=f[3].GetUInt8();d.event=f[4].GetUInt8();d.topic=f[5].GetString();d.zone=f[6].GetUInt32();d.cooldown=std::max(600u,f[7].GetUInt32());d.weight=std::max(1u,std::min(10u,f[8].GetUInt32()));
        if(d.locale>5 || d.channel>5 || d.tone>2)return;
        _dialogues.emplace(d.id,d);
    }while(rows->NextRow());
    if(QueryResult rows=CharacterDatabase.Query("SELECT dialogue_id,phrase_id,sequence_id FROM felsoria_bc_line ORDER BY dialogue_id,sequence_id"))do
    {
        Field* f=rows->Fetch();auto d=_dialogues.find(f[0].GetUInt32());if(d==_dialogues.end())continue;
        auto p=_phrases.find(f[1].GetString());
        if(p==_phrases.end() || p->second.locale!=d->second.locale || f[2].GetUInt8()!=d->second.phrases.size())return;
        d->second.phrases.push_back(p->first);
    }while(rows->NextRow());
    for(auto const& d:_dialogues)if(d.second.phrases.size()<2 || d.second.phrases.size()>4 || (d.second.tone && d.second.phrases.size()>3))return;
    // Supplemental metadata contains phrase IDs only; approved texts remain in MySQL.
    if(!LoadDialogueFile(sConfigMgr->GetStringDefault("Felsoria.BotChat.DialogueFile","./Data/Felsoria/BotChat-V5.tsv")))return;
    _ready=_fullReady.load() || !_dialogues.empty();
    TC_LOG_INFO(LOG_FILTER_GUILD,"REGIONAL_BOTCHAT_INIT version=audience-v5 phrases=%u dialogues=%u ready=%u",uint32(_phrases.size()),uint32(_dialogues.size()),uint32(_ready));
}
bool RegionalBotChatManager::OwnsAutomaticChat(Player const* p) const
{
    return p && p->GetSession() && p->GetSession()->IsBotSession() &&
        !sRegionalGuildMgr.ProtectedAccount(p->GetSession()->GetAccountId()) &&
        !sRegionalGuildMgr.ProtectedCharacter(p->GetGUID().GetCounter());
}
bool RegionalBotChatManager::LoadDialogueFile(std::string const& path)
{
    std::ifstream file(path);std::string line;uint32 count=0,localeCounts[6]={};
    if(!file){TC_LOG_ERROR(LOG_FILTER_GUILD,"REGIONAL_BOTCHAT_VALIDATION_FAILED metadata_file_missing");return false;}
    while(std::getline(file,line))
    {
        if(!line.empty() && line.back()=='\r')line.pop_back();
        if(line.empty() || line[0]=='#')continue;
        std::vector<std::string> columns;std::istringstream row(line);std::string cell;
        while(std::getline(row,cell,'\t'))columns.push_back(cell);
        if(columns.size()!=9)return false;
        uint32 values[7]={};uint8 cols[]={0,1,2,3,4,6,7};
        for(uint8 i=0;i<7;++i)
        {
            std::istringstream number(columns[cols[i]]);char tail;
            if(!(number>>values[i]) || (number>>tail))return false;
        }
        if(values[0]<50000 || values[0]>=60000 || values[1]>5 || values[2]>25 || values[3]>5 || values[4]>2 || values[5]>86400 || values[6]<1 || values[6]>10)return false;
        Dialogue d;d.id=values[0];d.locale=uint8(values[1]);d.event=uint8(values[2]);d.channel=uint8(values[3]);d.tone=uint8(values[4]);d.topic=columns[5];d.cooldown=values[5];d.weight=values[6];
        std::istringstream ids(columns[8]);
        while(std::getline(ids,cell,','))
        {
            auto phrase=_phrases.find(cell);
            if(phrase==_phrases.end() || phrase->second.locale!=d.locale)return false;
            d.phrases.push_back(cell);
        }
        if(d.topic.empty() || d.phrases.empty() || d.phrases.size()>4 || _dialogues.count(d.id))return false;
        _dialogues.emplace(d.id,d);++count;++localeCounts[d.locale];
    }
    if(count<6 || std::any_of(std::begin(localeCounts),std::end(localeCounts),[](uint32 n){return n<50;}))
    {TC_LOG_ERROR(LOG_FILTER_GUILD,"REGIONAL_BOTCHAT_VALIDATION_FAILED metadata_locale_coverage");return false;}
    return true;
}
bool RegionalBotChatManager::IsGeneralChannel(Channel const* channel) const
{
    return channel && GeneralDefinition(sChatChannelsStore.LookupEntry(channel->GetChannelId()));
}
Channel* RegionalBotChatManager::ResolveGeneralChannel(Player const* player) const
{
    if(!player)return nullptr;
    ChannelMgr* manager=channelMgr(player->GetTeam());if(!manager)return nullptr;
    for(ChatChannelsEntry const* d:sChatChannelsStore)
    {
        if(!GeneralDefinition(d))continue;
        std::string name=NativeGeneralName(player,d);if(name.empty())continue;
        Channel* channel=manager->GetChannel(name,const_cast<Player*>(player),false);
        if(channel && IsGeneralChannel(channel) && channel->IsOn(player->GetGUID()))return channel;
    }
    return nullptr;
}
void RegionalBotChatManager::EnsureGeneralChannel(Player* player) const
{
    if(!player || player->GetSession()->IsBotSession() || player->GetSession()->PlayerLoading() || player->IsBeingTeleportedFar() ||
        !player->IsInWorld() || player->GetMap()->IsDungeon() || player->InBattleground() || player->InArena() || player->GetRegionalGeneralChannel())return;
    AreaTableEntry const* area=sAreaTableStore.LookupEntry(player->GetZoneId());
    ChannelMgr* manager=channelMgr(player->GetTeam());if(!area || !manager)return;
    for(ChatChannelsEntry const* d:sChatChannelsStore)
    {
        if(!GeneralDefinition(d) || !player->CanJoinConstantChannelInZone(d,area))continue;
        std::string name=NativeGeneralName(player,d);if(name.empty())continue;
        Channel* channel=manager->GetJoinChannel(name,d->ID);
        if(!channel || !IsGeneralChannel(channel))continue;
        if(!channel->IsOn(player->GetGUID()))channel->JoinChannel(player,"");
        if(sConfigMgr->GetBoolDefault("Felsoria.BotChat.Debug",false))TC_LOG_INFO(LOG_FILTER_GUILD,"REGIONAL_BOTCHAT_GENERAL_ENSURE realm=%u map=%u zone=%u channel=%u member=%u",sRegionalGuildMgr.RealmFor(player),player->GetMapId(),player->GetZoneId(),d->ID,uint32(channel->IsOn(player->GetGUID())));
        return;
    }
}

bool RegionalBotChatManager::ChannelAudience(Player const* sender,Player const* receiver,Channel const* channel) const
{
    if(!sender || !receiver)return false;
    if(ChatAnchor && OwnsAutomaticChat(sender))return SameAudience(sender,receiver,channel?0:1);
    if(!OwnsAutomaticChat(sender) && !IsGeneralChannel(channel))return true;
    // Native/manual messages retain their original realm routing.
    return sRegionalGuildMgr.RealmFor(sender)==sRegionalGuildMgr.RealmFor(receiver) &&
        sender->GetZoneId()==receiver->GetZoneId() && sender->GetTeamId()==receiver->GetTeamId() &&
        sender->GetMapId()==receiver->GetMapId() && sender->GetInstanceId()==receiver->GetInstanceId();
}
void RegionalBotChatManager::Observe(Player* p)
{
    if(!_ready || !Active() || !p || !p->IsInWorld() || sRegionalGuildMgr.ProtectedAccount(p->GetSession()->GetAccountId()) || sRegionalGuildMgr.ProtectedCharacter(p->GetGUID().GetCounter()))return;
    Actor a;a.guid=p->GetGUID().GetCounter();a.realm=sRegionalGuildMgr.RealmFor(p);a.locale=sRegionalGuildMgr.LocaleForRealm(a.realm);a.zone=p->GetZoneId();a.map=p->GetMapId();a.instance=p->GetInstanceId();a.faction=uint8(p->GetTeamId());a.bgTeam=uint8(p->GetBGTeamId());
    a.group=p->GetGroup()?p->GetGroup()->GetGUID().GetCounter():0;a.bot=OwnsAutomaticChat(p);a.combat=p->isInCombat();a.alive=p->isAlive();a.dungeon=p->GetMap()->IsDungeon();a.bg=p->InBattleground()||p->InArena();a.ready=!p->GetSession()->PlayerLoading()&&!p->IsBeingTeleportedFar()&&(!a.bot || p->CanSpeak());a.seen=Clock();a.started=p->GetBattleground()&&p->GetBattleground()->GetStatus()==STATUS_IN_PROGRESS;
    for(uint16 slot=0;slot<MAX_QUEST_LOG_SIZE;++slot)if(p->GetQuestSlotQuestId(slot)){a.quest=true;break;}
    // Chat eligibility is independent of guild migration and profile creation.
    // A positively identified bot session is enough; protected leaders are excluded above.
    a.identity=a.guid;
    Profile profile;if(a.bot && sRegionalGuildMgr.GetProfile(a.guid,profile)){a.personality=profile.personality;}
    bool entering=false;
    {
        std::lock_guard<std::mutex> guard(_mutex);auto old=_actors.find(a.guid);
        entering=old==_actors.end() || old->second.zone!=a.zone || old->second.map!=a.map || (!old->second.ready && a.ready);
    }
    if(!a.bot && entering && a.ready && sConfigMgr->GetBoolDefault("Felsoria.BotChat.AutoJoinGeneral",true))EnsureGeneralChannel(p);
    a.general=!a.bot && !a.dungeon && !a.bg && p->GetRegionalGeneralChannel();
    std::lock_guard<std::mutex> guard(_mutex);auto old=_actors.find(a.guid);
    if(_events.size()<256 && a.ready)
    {
        Event e=Event::Presence;
        if(old==_actors.end() || (!old->second.ready && a.ready) || old->second.instance!=a.instance || old->second.map!=a.map)
            e=a.bg?Event::BGStart:(a.dungeon?Event::Dungeon:(a.group?Event::PartyJoined:Event::Presence));
        else if(a.group && old->second.group!=a.group)e=a.bg?Event::BGStart:(a.dungeon?Event::Dungeon:Event::PartyJoined);
        else if(a.bg && a.started && !old->second.started)e=Event::BGStart;
        if(e!=Event::Presence)_events.push_back({a.guid,e,0,a.seen});
    }
    if (_fullReady.load())
    {
        a.guild=p->GetGuildId();
        if (old!=_actors.end())
            a.full=old->second.full;
    }
    _actors[a.guid]=a;
    if(!a.bot && sConfigMgr->GetBoolDefault("Felsoria.BotChat.Debug",false))
    {
        std::string key="diag-audience:"+std::to_string(a.guid);
        if(_cooldowns[key]<=a.seen)
        {
            uint32 bots=0,ready=0,place=0,party=0;
            for(auto const& row:_actors)if(row.second.bot){++bots;if(row.second.ready)++ready;if(Same(row.second,a,0))++place;if(Same(row.second,a,2))++party;}
            TC_LOG_INFO(LOG_FILTER_GUILD,"REGIONAL_BOTCHAT_AUDIENCE version=audience-v5 realm=%u map=%u zone=%u locale=%u general=%u group=" UI64FMTD " instance=%u dungeon=%u bg=%u bots=%u readyBots=%u placeBots=%u partyBots=%u",a.realm,a.map,a.zone,uint32(a.locale),uint32(a.general),a.group,a.instance,uint32(a.dungeon),uint32(a.bg),bots,ready,place,party);
            _cooldowns[key]=a.seen+30000;
        }
    }
}
void RegionalBotChatManager::Forget(uint64 guid)
{
    if (_fullReady.load())
    {
        std::lock_guard<std::mutex> queueLock(_fullQueueMutex);
        _fullQueue.erase(guid);
    }
    std::lock_guard<std::mutex> guard(_mutex);_actors.erase(guid);_requests.erase(guid);
    // Speaker disappearance is repaired by Update. A remaining human keeps the conversation alive.
}
void RegionalBotChatManager::EventOccurred(Player* p,Event event,uint32 detail)
{
    if (_fullReady.load() && p && p->IsInWorld())
    {
        FullTextFacts facts;
        facts.quest=detail;
        facts.name=p->GetName();
        if (event==Event::Quest)
            FullEvent(p, "broadcast_quest_update_complete", 8, facts);
        if (event==Event::Resurrect && (p->InBattleground() || p->GetGroup()))
        {
            facts.cooldown=40;
            FullEvent(p, urand(0,1) ? "reply_death" : "reply_retry", 8, facts);
        }
    }
    if(!_ready || !Active() || !p || !p->IsInWorld())return;
    std::lock_guard<std::mutex> guard(_mutex);
    if(_actors.count(p->GetGUID().GetCounter()) && _events.size()<256)_events.push_back({p->GetGUID().GetCounter(),event,detail,Clock()});
}
void RegionalBotChatManager::HumanMessage(Player* p,std::string const& text,uint8 channel,uint64 recipient)
{
    FullHear(p,text,channel,recipient);
    if(!_ready || !Active() || !p || p->GetSession()->IsBotSession() || text.empty() || text.size()>512)return;
    if((channel==2 || channel==4) && p->InBattleground() && !p->InArena())channel=3;
    else if(channel==2 && p->GetMap()->IsDungeon())channel=4;
    if(channel>5)return;
    uint64 now=Clock();uint32 realm=sRegionalGuildMgr.RealmFor(p);
    uint8 locale=sRegionalGuildMgr.LocaleForRealm(realm);if(locale>5)return;
    std::lock_guard<std::mutex> guard(_mutex);
    if(channel==5 && (!recipient || !_actors.count(recipient) || !_actors[recipient].bot))return;
    if(channel<=1)_cooldowns["human:"+Context(realm,p->GetZoneId(),uint8(p->GetTeamId()),locale,0)]=now+Bounded("Felsoria.BotChat.HumanSilenceSeconds",4,2,120)*1000ull;
    if(_humanCooldown[p->GetGUID().GetCounter()]>now || _events.size()>=256)return;
    std::wstring words;if(!Utf8toWStr(text,words))return;wstrToLower(words);
    while(!words.empty() && (iswspace(words.back()) || words.back()==L'!' || words.back()==L'?' || words.back()==L'.'))words.pop_back();
    while(!words.empty() && (iswspace(words.front()) || words.front()==L'¿' || words.front()==L'¡'))words.erase(words.begin());
    std::wstring first=words.substr(0,words.find_first_of(L" ,.!?:"));
    auto token=[&](wchar_t const* wanted)
    {
        std::wstring word=wanted;std::size_t pos=0;
        while((pos=words.find(word,pos))!=std::wstring::npos)
        {
            if((!pos || !iswalnum(words[pos-1])) && (pos+word.size()==words.size() || !iswalnum(words[pos+word.size()])))return true;
            ++pos;
        }
        return false;
    };
    wchar_t const* greetings[]={L"hi",L"hello",L"hey",L"hallo",L"moin",L"salut",L"bonjour",L"hola",L"привет",L"ciao"};
    Event event=static_cast<Event>(17);bool recognized=false;
    for(auto w:greetings)if(first==w){event=Event::Greeting;recognized=true;}
    if(token(L"help") || token(L"hilfe") || token(L"aide") || token(L"ayuda") || token(L"помогите") || token(L"aiuto")){event=Event::Help;recognized=true;}
    wchar_t const* agreement[]={L"ok",L"okay",L"yes",L"sure",L"ready",L"ja",L"bereit",L"oui",L"prêt",L"si",L"sí",L"listo",L"да",L"готов",L"sì",L"pronto"};
    for(auto w:agreement)if(first==w){event=static_cast<Event>(16);recognized=true;}
    wchar_t const* retry[]={L"again",L"try again",L"nochmal",L"encore",L"otra vez",L"ещё раз",L"ancora"};
    for(auto w:retry)if(words==w){event=static_cast<Event>(18);recognized=true;}
    auto actor=_actors.find(p->GetGUID().GetCounter());if(actor==_actors.end())return;
    Actor const& human=actor->second;
    std::string key=AudienceKey(human.realm,human.zone,human.map,human.instance,human.faction,human.bgTeam,human.locale,channel,human.group);
    // Unknown text gets a clarification only after a recent bot line or a direct whisper.
    if(!recognized && channel!=5 && _cooldowns["last-speech:"+key]<=now)return;
    _events.push_back({human.guid,event,channel,now,recipient});
    _humanCooldown[human.guid]=now+Bounded("Felsoria.BotChat.HumanReplyCooldownSeconds",8,5,300)*1000ull;
}

bool RegionalBotChatManager::Same(Actor const& a,Actor const& b,uint8 channel) const
{
    // Physical eligibility only. Content language and recipient realm are checked separately.
    if(a.map!=b.map || a.instance!=b.instance)return false;
    if(channel==2 || channel==4)return a.group && a.group==b.group;
    if(channel==3)return a.bg && b.bg && a.bgTeam==b.bgTeam;
    if(channel==5)return a.zone==b.zone && a.faction==b.faction;
    return !a.dungeon && !b.dungeon && !a.bg && !b.bg && a.zone==b.zone && a.faction==b.faction;
}
bool RegionalBotChatManager::MatchesHuman(Actor const& human,Actor const& audience,uint8 channel) const
{
    if(human.bot || !human.ready || human.locale>5 || human.locale!=audience.locale || Clock()-human.seen>=15000 || !Same(human,audience,channel))return false;
    if(channel<=1 || channel==5)if(human.realm!=audience.realm)return false;
    return channel!=0 || human.general;
}
bool RegionalBotChatManager::Audience(Actor const& audience,uint8 channel) const
{
    for(auto const& row:_actors)if(MatchesHuman(row.second,audience,channel))return true;
    return false;
}
bool RegionalBotChatManager::SameAudience(Player const* a,Player const* b,uint8 channel) const
{
    if(!a || !b)return false;
    Player const* anchor=ChatAnchor?ChatAnchor:a;
    if(a->GetMapId()!=anchor->GetMapId() || a->GetInstanceId()!=anchor->GetInstanceId() || b->GetMapId()!=anchor->GetMapId() || b->GetInstanceId()!=anchor->GetInstanceId())return false;
    uint8 language=sRegionalGuildMgr.LocaleForRealm(sRegionalGuildMgr.RealmFor(anchor));
    if(language>5)return false;
    bool bot=b->GetSession()->IsBotSession();
    if(!bot && sRegionalGuildMgr.LocaleForRealm(sRegionalGuildMgr.RealmFor(b))!=language)return false;
    if(channel==2 || channel==4)return anchor->GetGroup() && a->GetGroup()==anchor->GetGroup() && b->GetGroup()==anchor->GetGroup();
    if(channel==3)return anchor->InBattleground() && a->GetBattleground()==anchor->GetBattleground() && b->GetBattleground()==anchor->GetBattleground() && a->GetBGTeamId()==anchor->GetBGTeamId() && b->GetBGTeamId()==anchor->GetBGTeamId();
    return a->GetZoneId()==anchor->GetZoneId() && b->GetZoneId()==anchor->GetZoneId() && a->GetTeamId()==anchor->GetTeamId() && b->GetTeamId()==anchor->GetTeamId() &&
        (bot || sRegionalGuildMgr.RealmFor(b)==sRegionalGuildMgr.RealmFor(anchor));
}
bool RegionalBotChatManager::Eligible(Player* p,uint8 channel) const
{
    if(!p || !OwnsAutomaticChat(p) || !p->IsInWorld() || p->IsBeingTeleportedFar() || !p->CanSpeak() || p->GetSession()->PlayerLoading())return false;
    if(channel==1 && p->isInCombat())return false;
    if(channel<=1 && (!p->isAlive() || p->GetMap()->IsDungeon() || p->InBattleground() || p->InArena()))return false;
    if(channel==4 && !p->GetMap()->IsDungeon())return false;
    if(channel==3 && (!p->InBattleground() || p->InArena()))return false;
    return true;
}
bool RegionalBotChatManager::Send(Player* p,Dialogue const& d,Phrase const& phrase,Actor const& audience,uint64 target)
{
    if(!Eligible(p,d.channel) || !ChannelEnabled(d.channel) || phrase.locale!=audience.locale)return false;
    Player* anchor=nullptr;
    for(auto const& row:_actors)if(MatchesHuman(row.second,audience,d.channel))
        if(Player* human=ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(row.first)))
            if(human->IsInWorld() && human->CanContact() && !human->GetSocial()->HasIgnore(p->GetGUID()) && sRegionalGuildMgr.LocaleForRealm(sRegionalGuildMgr.RealmFor(human))==audience.locale &&
                ((d.channel==2 || d.channel==3 || d.channel==4) || sRegionalGuildMgr.RealmFor(human)==audience.realm))
            {anchor=human;break;}
    if(!anchor)return false;
    DeliveryScope scope(anchor);
    if(!SameAudience(p,anchor,d.channel) || (d.zone && p->GetZoneId()!=d.zone))return false;
    bool hasQuest=false;for(uint16 slot=0;slot<MAX_QUEST_LOG_SIZE;++slot)if(p->GetQuestSlotQuestId(slot)){hasQuest=true;break;}
    if(d.topic=="quest" && !hasQuest)return false;
    uint32 language=p->GetTeam()==HORDE?LANG_ORCISH:LANG_COMMON;
    if(d.channel==0)
    {
        std::set<Channel*> channels;
        for(auto const& row:_actors)if(MatchesHuman(row.second,audience,0))
            if(Player* human=ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(row.first)))
                if(human->IsInWorld() && human->CanContact() && SameAudience(p,human,0) && !human->GetSocial()->HasIgnore(p->GetGUID()))if(Channel* channel=human->GetRegionalGeneralChannel())channels.insert(channel);
        bool sent=false;
        for(Channel* c:channels)
        {
            bool joined=!c->IsOn(p->GetGUID());if(joined)c->JoinChannel(p,"");
            if(c->IsOn(p->GetGUID()) && !(c->GetPlayerFlags(p->GetGUID()) & MEMBER_FLAG_MUTED)){c->Say(p->GetGUID(),phrase.text,language);sent=true;}
            if(joined && c->IsOn(p->GetGUID()))c->LeaveChannel(p,false);
        }
        return sent;
    }
    if(d.channel==5)
    {
        Player* human=ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(target));
        if(!human || human->GetSession()->IsBotSession() || !SameAudience(p,human,5) || human->GetSocial()->HasIgnore(p->GetGUID()))return false;
        p->Whisper(phrase.text,LANG_UNIVERSAL,human->GetGUID());return true;
    }
    if(d.channel==1)
    {
        if(!p->IsWithinDistInMap(anchor,sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_SAY)) || p->GetPhases()!=anchor->GetPhases() || !(p->GetPhaseMask()&anchor->GetPhaseMask()))return false;
        p->Say(phrase.text,language);return true;
    }
    WorldPackets::Chat::Chat packet;packet.Initialize(d.channel==3||d.channel==4?CHAT_MSG_INSTANCE_CHAT:(p->GetGroup() && p->GetGroup()->isRaidGroup()?CHAT_MSG_RAID:CHAT_MSG_PARTY),LANG_UNIVERSAL,p,nullptr,phrase.text);
    bool sent=false;
    for(auto const& row:_actors)if(!row.second.bot && MatchesHuman(row.second,audience,d.channel))
        if(Player* human=ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(row.first)))
            if(human->IsInWorld() && human->CanContact() && SameAudience(p,human,d.channel) && p->InSamePhase(human) && !human->GetSocial()->HasIgnore(p->GetGUID())){human->SendDirectMessage(packet.Write());sent=true;}
    return sent;
}
bool RegionalBotChatManager::Start(Actor const& bot,Actor const& human,Event event,uint8 channel,uint64 target)
{
    uint64 now=Clock();bool reply=IsReply(event),urgent=reply || event!=Event::Presence;
    if(!bot.bot || !bot.ready || now-bot.seen>=15000 || human.bot || human.locale>5 || !human.ready || !Same(bot,human,channel) || !ChannelEnabled(channel) || !Audience(human,channel))return false;
    std::string key=AudienceKey(human.realm,human.zone,human.map,human.instance,human.faction,human.bgTeam,human.locale,channel,human.group);
    if(reply && _cooldowns["reply:"+key]>now)return false;
    if(!urgent && (_cooldowns[key]>now || _botCooldown[bot.identity]>now))return false;
    if(!urgent && channel<=1 && _cooldowns["human:"+Context(human.realm,human.zone,human.faction,human.locale,0)]>now)return false;
    if(urgent && !reply && _cooldowns["event:"+key+":"+std::to_string(uint8(event))]>now)return false;
    uint8 priority=Priority(event);
    for(auto const& c:_active)
    {
        auto d=_dialogues.find(c.dialogue);if(d==_dialogues.end())continue;
        if(d->second.channel!=channel || AudienceKey(c.audience.realm,c.audience.zone,c.audience.map,c.audience.instance,c.audience.faction,c.audience.bgTeam,c.audience.locale,channel,c.audience.group)!=key)continue;
        uint8 existing=Priority(Event(d->second.event));
        if(existing>=priority)return false;
    }
    bool negative=event==Event::Wipe || event==Event::BGLoss || event==Event::BGDeficit || event==Event::BaseLoss;
    bool banter=sConfigMgr->GetBoolDefault("Felsoria.BotChat.Banter.Enable",true);
    uint8 tone=negative&&banter?(urand(1,100)<=65?1:2):0;
    std::vector<Dialogue const*> choices;
    for(uint8 pass=0;pass<2 && choices.empty();++pass)
    {
        for(auto const& row:_dialogues)
        {
            Dialogue const& d=row.second;
            if(d.locale!=human.locale || d.channel!=channel || d.event!=uint8(event) || d.tone!=tone || d.phrases.empty() || (d.zone && d.zone!=bot.zone))continue;
            if(!reply && (d.phrases.size()<Bounded("Felsoria.BotChat.MinConversationLines",2,2,4) || d.phrases.size()>Bounded("Felsoria.BotChat.MaxConversationLines",4,2,4)))continue;
            if(d.channel<=1 && (!bot.alive || bot.bg || bot.dungeon))continue;
            if(d.channel==1 && bot.combat)continue;
            if(d.channel==2 && (bot.dungeon || bot.bg))continue;
            if(d.channel==4 && (!bot.dungeon || (!urgent && bot.combat)))continue;
            if(d.channel==3 && (!bot.bg || (!urgent && (!bot.started || !bot.alive))))continue;
            if(channel==5 && !target)continue;
            if(d.topic=="quest" && !bot.quest)continue;
            if(!urgent && _cooldowns[key+":"+d.topic]>now)continue;
            bool repeated=false;
            for(auto const& id:d.phrases)if(_recentTexts[key+":"+id]>now)repeated=true;
            if(!urgent && repeated)continue;
            for(uint32 n=0;n<d.weight;++n)choices.push_back(&d);
        }
        if(negative && tone!=0)tone=pass==0?(tone==1?2:1):0;
    }
    if(choices.empty())return false;
    Dialogue const& d=*choices[urand(0,uint32(choices.size()-1))];
    std::vector<Actor const*> pool;
    for(auto const& row:_actors)
    {
        Actor const& b=row.second;
        if(!b.bot || !b.ready || now-b.seen>=15000 || !Same(b,human,channel) || (!urgent && _botCooldown[b.identity]>now))continue;
        if((channel<=1 && !b.alive) || (channel==1 && b.combat) || (channel==4 && !urgent && b.combat) || (d.topic=="quest" && !b.quest))continue;
        if(target && channel==5 && b.guid!=bot.guid)continue;
        pool.push_back(&b);
    }
    if(pool.empty())return false;
    std::stable_sort(pool.begin(),pool.end(),[&](Actor const* a,Actor const* b){return _lastSpeaker[a->guid]<_lastSpeaker[b->guid];});
    // Equal-age speakers rotate with a small random offset; actual send time breaks ties next time.
    size_t tied=1;while(tied<pool.size() && _lastSpeaker[pool[tied]->guid]==_lastSpeaker[pool[0]->guid])++tied;
    if(tied>1)std::rotate(pool.begin(),pool.begin()+urand(0,uint32(tied-1)),pool.begin()+tied);
    Conversation c;c.id=_nextConversation++;c.dialogue=d.id;c.audience=human;c.target=target;c.expires=now+45000;c.next=now+(reply?0:urand(500,1500));
    size_t lines=reply?1:d.phrases.size();
    for(size_t i=0;i<lines;++i)c.speakers.push_back(pool[i%pool.size()]->guid);
    _active.erase(std::remove_if(_active.begin(),_active.end(),[&](Conversation const& old)
    {
        auto previous=_dialogues.find(old.dialogue);
        return previous!=_dialogues.end() && previous->second.channel==channel && AudienceKey(old.audience.realm,old.audience.zone,old.audience.map,old.audience.instance,old.audience.faction,old.audience.bgTeam,old.audience.locale,channel,old.audience.group)==key;
    }),_active.end());
    if(reply)_cooldowns["reply:"+key]=now+4000;
    else if(urgent)_cooldowns["event:"+key+":"+std::to_string(uint8(event))]=now+12000;
    _active.push_back(c);
    if(sConfigMgr->GetBoolDefault("Felsoria.BotChat.Debug",false))TC_LOG_INFO(LOG_FILTER_GUILD,"REGIONAL_BOTCHAT_CONVERSATION_STARTED id=" UI64FMTD " dialogue=%u event=%u channel=%u audienceRealm=%u locale=%u speakers=%u",c.id,d.id,uint32(event),uint32(channel),human.realm,uint32(human.locale),uint32(pool.size()));
    return true;
}
bool RegionalBotChatManager::Attempt(Actor const& human,Event event,uint8 channel,uint64 bot,uint64 target)
{
    std::vector<Actor const*> pool;
    for(auto const& row:_actors)if(row.second.bot && row.second.ready && Clock()-row.second.seen<15000 && (!bot || row.first==bot) && Same(row.second,human,channel) &&
        (event!=Event::Presence || (_botCooldown[row.second.identity]<=Clock() && (channel!=4 || !row.second.combat))))pool.push_back(&row.second);
    std::stable_sort(pool.begin(),pool.end(),[&](Actor const* a,Actor const* b){return _lastSpeaker[a->guid]<_lastSpeaker[b->guid];});
    return !pool.empty() && Start(*pool.front(),human,event,channel,target);
}
void RegionalBotChatManager::Dispatch(Player* p)
{
    if(!_ready || !Active() || !p || !OwnsAutomaticChat(p))return;
    std::lock_guard<std::mutex> guard(_mutex);
    auto request=_requests.find(p->GetGUID().GetCounter());if(request==_requests.end())return;
    auto requests=std::move(request->second);_requests.erase(request);
    for(auto const& r:requests)
    {
        auto c=std::find_if(_active.begin(),_active.end(),[&](Conversation const& item){return item.id==r.conversation;});
        if(c==_active.end() || c->line!=r.line || c->line>=c->speakers.size() || c->speakers[c->line]!=p->GetGUID().GetCounter())continue;
        c->waiting=false;
        auto d=_dialogues.find(c->dialogue);if(d==_dialogues.end() || c->line>=d->second.phrases.size()){_active.erase(c);continue;}
        auto phrase=_phrases.find(d->second.phrases[c->line]);if(phrase==_phrases.end()){_active.erase(c);continue;}
        std::string key=AudienceKey(c->audience.realm,c->audience.zone,c->audience.map,c->audience.instance,c->audience.faction,c->audience.bgTeam,c->audience.locale,d->second.channel,c->audience.group);
        uint64 now=Clock(),spacing=60000ull/Bounded("Felsoria.BotChat.MaxMessagesPerMinute",12,6,20);
        uint64 debt=_cooldowns["line-budget:"+key];
        if(debt>now+spacing){c->next=debt-spacing;continue;}
        if(!Send(p,d->second,phrase->second,c->audience,c->target))
        {
            // Retry another speaker next tick; do not silently destroy a conversation after line one.
            c->next=now+1000;c->speakers[c->line]=0;
            _cooldowns["send-failed:"+std::to_string(p->GetGUID().GetCounter())]=now+5000;
            if(sConfigMgr->GetBoolDefault("Felsoria.BotChat.Debug",false))TC_LOG_INFO(LOG_FILTER_GUILD,"REGIONAL_BOTCHAT_SEND_RETRY conversation=" UI64FMTD " guid=" UI64FMTD " channel=%u audienceRealm=%u locale=%u",c->id,p->GetGUID().GetCounter(),uint32(d->second.channel),c->audience.realm,uint32(c->audience.locale));
            continue;
        }
        _cooldowns["line-budget:"+key]=std::max(now,debt)+spacing;
        _cooldowns["last-speech:"+key]=now+45000;
        _lastSpeaker[p->GetGUID().GetCounter()]=now;
        _botCooldown[p->GetGUID().GetCounter()]=now+Bounded("Felsoria.BotChat.BotCooldownSeconds",8,3,120)*1000ull;
        bool reply=IsReply(Event(d->second.event));
        if(c->line==0 && !reply)
        {
            char const* intervalKey=d->second.channel==3?"Felsoria.BotChat.BattlegroundIntervalSeconds":d->second.channel==4?"Felsoria.BotChat.InstanceIntervalSeconds":d->second.channel==2?"Felsoria.BotChat.PartyIntervalSeconds":"Felsoria.BotChat.GeneralIntervalSeconds";
            uint32 interval=Bounded(intervalKey,18,8,3600);
            if(d->second.channel==1)interval=Bounded("Felsoria.BotChat.SayIntervalSeconds",600,300,3600);
            _cooldowns[key]=now+interval*1000ull+urand(0,3000);
            _cooldowns[key+":"+d->second.topic]=now+std::max(d->second.cooldown,Bounded("Felsoria.BotChat.TopicCooldownSeconds",60,20,86400))*1000ull;
        }
        if(!reply)_recentTexts[key+":"+phrase->first]=now+Bounded("Felsoria.BotChat.RepetitionSeconds",90,30,86400)*1000ull;
        ++c->line;c->next=now+urand(3000,5000);
        if(sConfigMgr->GetBoolDefault("Felsoria.BotChat.Debug",false))TC_LOG_INFO(LOG_FILTER_GUILD,"REGIONAL_BOTCHAT_MESSAGE_SENT conversation=" UI64FMTD " line=%u phrase=%s guid=" UI64FMTD " channel=%u audienceRealm=%u locale=%u",c->id,uint32(c->line),phrase->first.c_str(),p->GetGUID().GetCounter(),uint32(d->second.channel),c->audience.realm,uint32(c->audience.locale));
    }
}
void RegionalBotChatManager::QueueBG(uint32 map,uint32 instance,uint8 team,Event event,uint32 detail)
{
    // Called under _mutex. One representative human per language/audience, never every bot.
    std::set<uint8> locales;
    for(auto const& row:_actors)
    {
        Actor const& a=row.second;
        if(a.bot || !a.bg || a.map!=map || a.instance!=instance || a.bgTeam!=team || a.locale>5 || !a.ready || Clock()-a.seen>=15000 || !locales.insert(a.locale).second)continue;
        if(_events.size()<256)_events.push_back({a.guid,event,detail,Clock()});
    }
}
void RegionalBotChatManager::BattlegroundScore(uint32 map,uint32 instance,uint8 team,int32 points)
{
    if(!_ready || !Active() || team>1)return;
    std::lock_guard<std::mutex> guard(_mutex);
    std::string key=std::to_string(map)+":"+std::to_string(instance);BGState& state=_battlegrounds[key];state.seen=Clock();
    int32 old=state.score[team],other=state.score[1-team];bool known=state.scoresKnown&(1<<team);
    state.score[team]=points;state.scoresKnown|=1<<team;
    if(!known || old==points)return;
    if(_cooldowns["score-event:"+key]<=Clock())
    {
        QueueBG(map,instance,team,points>old?Event::BGGain:Event::BGLoss,0);
        QueueBG(map,instance,1-team,points>old?Event::BGLoss:Event::BGGain,0);
        _cooldowns["score-event:"+key]=Clock()+20000;
    }
    if(state.scoresKnown==3 && (old<=other && points>other || old>=other && points<other))
    {
        QueueBG(map,instance,team,points>other?Event::BGLead:Event::BGDeficit,0);
        QueueBG(map,instance,1-team,points>other?Event::BGDeficit:Event::BGLead,0);
    }
}
void RegionalBotChatManager::BattlegroundBases(uint32 map,uint32 instance,uint8 team,uint32 bases)
{
    if(!_ready || !Active() || team>1)return;
    std::lock_guard<std::mutex> guard(_mutex);BGState& state=_battlegrounds[std::to_string(map)+":"+std::to_string(instance)];state.seen=Clock();
    uint32 old=state.bases[team];bool known=state.basesKnown&(1<<team);state.bases[team]=bases;state.basesKnown|=1<<team;
    if((known || bases>0) && bases!=old)QueueBG(map,instance,team,bases>old?Event::BaseGain:Event::BaseLoss,0);
}
void RegionalBotChatManager::Update(uint32 diff)
{
    if(!_ready || !Active())return;
    _timer+=diff;if(_timer<Bounded("Felsoria.BotChat.SchedulerIntervalMs",500,250,30000))return;_timer=0;
    std::lock_guard<std::mutex> guard(_mutex);uint64 now=Clock();
    for(auto it=_actors.begin();it!=_actors.end();)if(now-it->second.seen>30000){_requests.erase(it->first);it=_actors.erase(it);}else ++it;
    for(auto it=_active.begin();it!=_active.end();)
    {
        auto d=_dialogues.find(it->dialogue);
        if(d==_dialogues.end() || now>it->expires || it->line>=it->speakers.size() || !Audience(it->audience,d->second.channel))
        {it=_active.erase(it);continue;}
        if(it->waiting && now>it->next+5000){it->waiting=false;it->speakers[it->line]=0;}
        if(!it->waiting && now>=it->next)
        {
            uint64& speaker=it->speakers[it->line];auto a=_actors.find(speaker);
            if(a==_actors.end() || !a->second.ready || now-a->second.seen>=15000 || !Same(a->second,it->audience,d->second.channel))
            {
                speaker=0;uint64 oldest=std::numeric_limits<uint64>::max();
                for(auto const& row:_actors)if(row.second.bot && row.second.ready && now-row.second.seen<15000 && Same(row.second,it->audience,d->second.channel))
                    if(_cooldowns["send-failed:"+std::to_string(row.first)]<=now && _lastSpeaker[row.first]<oldest){oldest=_lastSpeaker[row.first];speaker=row.first;}
            }
            if(speaker){_requests[speaker].push_back({it->id,it->line});it->waiting=true;it->next=now;}
            else{it=_active.erase(it);continue;}
        }
        ++it;
    }
    bool humanPresent=false;for(auto const& row:_actors)if(!row.second.bot && row.second.ready){humanPresent=true;break;}
    if(!humanPresent)
    {
        _active.clear();_requests.clear();_events.clear();
        for(auto it=_battlegrounds.begin();it!=_battlegrounds.end();)if(now-it->second.seen>3600000)it=_battlegrounds.erase(it);else ++it;
        return;
    }
    auto events=std::move(_events);_events.clear();
    // Replies are dispatched before world events and ambient opportunities.
    std::stable_sort(events.begin(),events.end(),[](PendingEvent const& a,PendingEvent const& b){return IsReply(a.event)>IsReply(b.event);});
    std::set<std::string> attemptedEvents;
    for(auto const& e:events)
    {
        if(now-e.at>(IsReply(e.event)?10000:30000))continue;
        auto source=_actors.find(e.guid);if(source==_actors.end())continue;
        for(auto const& row:_actors)
        {
            Actor const& human=row.second;if(human.bot || !human.ready || human.locale>5 || now-human.seen>=15000)continue;
            if(IsReply(e.event) && human.guid!=e.guid)continue;
            uint8 channel=IsReply(e.event)?uint8(e.detail):(human.bg?3:(human.dungeon?4:(human.group?2:0)));
            if(!IsReply(e.event) && !Same(source->second,human,channel))continue;
            std::string key=AudienceKey(human.realm,human.zone,human.map,human.instance,human.faction,human.bgTeam,human.locale,channel,human.group)+":"+std::to_string(uint8(e.event));
            if(!attemptedEvents.insert(key).second)continue;
            if(!Attempt(human,e.event,channel,IsReply(e.event)?e.target:0,channel==5?e.guid:0) && IsReply(e.event) && _events.size()<256)_events.push_back(e);
        }
    }
    std::set<std::string> attempted;
    for(auto const& row:_actors)
    {
        Actor const& human=row.second;if(human.bot || !human.ready || human.locale>5 || now-human.seen>=15000)continue;
        std::vector<uint8> channels;
        if(human.bg){if(human.started && sConfigMgr->GetBoolDefault("Felsoria.BotChat.BattlegroundAmbient",true))channels.push_back(3);}
        else if(human.dungeon){if(human.group && !human.combat)channels.push_back(4);}
        else {if(human.general)channels.push_back(0);if(human.group)channels.push_back(2);channels.push_back(1);}
        for(uint8 channel:channels)
        {
            std::string key=AudienceKey(human.realm,human.zone,human.map,human.instance,human.faction,human.bgTeam,human.locale,channel,human.group);
            if(attempted.insert(key).second && _cooldowns[key]<=now)Attempt(human,Event::Presence,channel);
        }
    }
    for(auto it=_cooldowns.begin();it!=_cooldowns.end();)if(it->second<now)it=_cooldowns.erase(it);else ++it;
    for(auto it=_recentTexts.begin();it!=_recentTexts.end();)if(it->second<now)it=_recentTexts.erase(it);else ++it;
    for(auto it=_botCooldown.begin();it!=_botCooldown.end();)if(it->second<now)it=_botCooldown.erase(it);else ++it;
    for(auto it=_humanCooldown.begin();it!=_humanCooldown.end();)if(it->second<now)it=_humanCooldown.erase(it);else ++it;
    for(auto it=_lastSpeaker.begin();it!=_lastSpeaker.end();)if(!_actors.count(it->first))it=_lastSpeaker.erase(it);else ++it;
    for(auto it=_battlegrounds.begin();it!=_battlegrounds.end();)if(now-it->second.seen>3600000)it=_battlegrounds.erase(it);else ++it;
}

}

#include "FelsoriaFullBotChat.inc"
