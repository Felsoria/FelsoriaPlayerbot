#ifndef FELSORIA_REGIONAL_V1_H
#define FELSORIA_REGIONAL_V1_H
#include "FullBotTextCatalog.h"
#include "Define.h"
#include "ObjectGuid.h"
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
class Player;
class WorldSession;
class Channel;
class Guild;
class Unit;
namespace FelsoriaRegional
{
enum class Event : uint8 { Presence, Login, Zone, Quest, WorldQuest, Loot, Dungeon, BossKill, Wipe, BGStart, BGObjective, BGEnd, Death, Resurrect, Greeting, Help, Agreement, Clarification, Retry, BGGain, BGLoss, BGLead, BGDeficit, BaseGain, BaseLoss, PartyJoined };
struct FullTextFacts
{
    uint32 item=0, extraItem=0, quest=0, spell=0, dungeon=0, faction=0, level=0, guild=0;
    uint32 cooldown=20;
    uint64 recipient=0;
    int32 available=-1, required=0;
    bool matchQuest=false, questShared=false, tradeItem=false, allowUnknownItem=false;
    bool emptyTarget=false, targetIsItem=false, reciprocal=false, rewards=false;
    std::string name, objective;
    std::vector<std::string> fragments;
};
struct FullTextState
{
    uint64 next=0, ambient=0, group=0;
    uint32 instance=0;
    uint8 health=0, bases=0;
    bool mana=false;
};
struct FullTextNotice
{
    std::string key;
    uint8 channel=0;
    FullTextFacts facts;
    uint64 created=0;
    uint32 map=0, instance=0;
};
struct GuildSpec
{
    uint32 key = 0, guild = 0, account = 0;
    uint64 leader = 0;
    uint8 locale = 0, faction = 0, slot = 0, weight = 0;
    std::string name, accountName, leaderName;
};
struct Profile
{
    uint64 id = 0, guid = 0;
    uint32 account = 0, realm = 0, guildKey = 0;
    uint8 locale = 0, faction = 0, personality = 0;
    bool dynamic = false;
};
class RegionalGuildManager
{
public:
    static RegionalGuildManager& Instance();
    // Loaded before account pools and guild validation, even when disabled.
    void LoadProtection();
    void Initialize();
    void Update(uint32 diff);
    bool ProtectedAccount(uint32 account) const;
    bool ProtectedCharacter(uint64 guid) const;
    bool ProtectedGuild(uint64 guild) const;
    uint32 ProtectedAccountRealm(uint32 account) const;
    bool Ready() const { return _ready; }
    bool Enabled() const;
    uint8 LocaleForRealm(uint32 realm) const;
    uint32 PreferredBotRealm(uint32 account, uint8 faction, uint32 requested) const;
    std::map<uint32,uint32> _bootstrapAttempts;
    uint32 RealmFor(Player const* player) const;
    bool GetProfile(uint64 guid, Profile& result) const;
    bool ManagedBot(Player const* player) const;
    void RegisterBotAccount(uint32 account);
    void Observe(Player* player);
    void Forget(uint64 guid);
    bool Assign(Player* player);
    void Retired(uint32 account, uint64 guid);
private:
    bool Bootstrap(GuildSpec& spec);
    bool LoadSpecs();
    void LoadProfiles();
    void SeedOfflineProfiles();
    uint32 ChooseGuild(uint8 locale, uint8 faction) const;
    mutable std::recursive_mutex _mutex;
    bool _tables = false, _failed = false;
    std::atomic<bool> _ready{false};
    uint32 _timer = 0;
    std::map<uint32, GuildSpec> _guilds;
    std::set<uint32> _accounts, _botAccounts;
    std::set<uint64> _characters, _guildIds;
    std::map<uint32, uint8> _realmLocales;
    std::map<uint64, Profile> _profiles; // stable ID -> profile, including offline
    std::map<uint64, uint64> _bindings; // live GUID -> stable ID
    std::set<uint64> _pending;
    std::set<uint64> _reported;
};
struct Phrase { std::string id, text; uint8 locale = 0, tone = 0; };
struct Dialogue
{
    uint32 id = 0;
    uint8 locale = 0, tone = 0, channel = 0, event = 0;
    std::string topic;
    std::vector<std::string> phrases;
    uint32 zone = 0, cooldown = 1800, weight = 1;
};
class RegionalBotChatManager
{
public:
    static RegionalBotChatManager& Instance();
    void Initialize();
    void Observe(Player* player);
    void Forget(uint64 guid);
    void Dispatch(Player* player);
    void FullTick(Player* bot);
    void FullHear(Player* human, std::string const& message, uint8 route, uint64 asked=0);
    void FullEvent(Player* bot, char const* key, uint8 channel, FullTextFacts facts={}, Player* recipient=nullptr);
    void FullSignal(Player* player, char const* key, uint8 channel, FullTextFacts facts={});
    void FullLoot(Player* player, uint32 item, uint32 quality);
    void FullObjective(Player* player, uint32 quest, uint32 item, std::string const& objective, int32 count, int32 required);
    void FullKill(Player* player, Unit* victim);
    void FullGuildRank(Player* member, uint32 guild, bool demoted);
    void FullReport(Player* bot, char const* key, FullTextFacts facts={});
    void FullControl(Player* bot, Player* requester, char const* key, uint32 item=0, uint32 spell=0,
        std::string const& target={}, char const* fragment=nullptr, int32 count=-1);
    bool FullReady() const { return _fullReady.load(); }
    void EventOccurred(Player* player, Event event, uint32 detail=0);
    void HumanMessage(Player* player,std::string const& text,uint8 channel,uint64 recipient=0);
    void Update(uint32 diff);
    void BattlegroundScore(uint32 map,uint32 instance,uint8 team,int32 points);
    void BattlegroundBases(uint32 map,uint32 instance,uint8 team,uint32 bases);
    bool ChannelAudience(Player const* sender,Player const* receiver,Channel const* channel=nullptr) const;
    bool IsGeneralChannel(Channel const* channel) const;
    Channel* ResolveGeneralChannel(Player const* player) const;
    void EnsureGeneralChannel(Player* player) const;
    bool OwnsAutomaticChat(Player const* player) const;
private:
    struct Actor
    {
        uint64 guid=0,group=0,seen=0,identity=0;
        uint32 realm=0,zone=0,map=0,instance=0;
        uint8 locale=255,faction=0,bgTeam=0,personality=0;
        bool bot=false,combat=false,alive=true,dungeon=false,bg=false,ready=false,started=false,quest=false,general=false;
        uint32 guild=0;
        FullTextState full;
    };
    struct Request { uint64 conversation=0; uint8 line=0; };
    struct PendingEvent { uint64 guid=0; Event event=Event::Presence; uint32 detail=0; uint64 at=0,target=0; };
    struct Conversation
    {
        uint64 id=0,target=0,next=0,expires=0;
        uint32 dialogue=0;
        Actor audience;
        std::vector<uint64> speakers;
        uint8 line=0;
        bool waiting=false;
    };
    struct BGState { int32 score[2]={0,0}; uint32 bases[2]={0,0}; uint8 scoresKnown=0,basesKnown=0; uint64 seen=0; };
    bool LoadDialogueFile(std::string const& path);
    void LoadFullTexts();
    bool FullDeliver(Player* bot, FullTextNotice const& note);
    void FullAmbient(Player* bot, uint64 now);
    void FullBattleground(Player* bot, FullTextState& state);
    std::string FullFragment(char const* key, uint8 locale, FullText::Tokens const& facts, std::size_t seed=0) const;
    FullText::Catalog _fullCatalog;
    std::atomic<bool> _fullReady{false};
    std::mutex _fullQueueMutex;
    std::map<uint64, std::vector<FullTextNotice>> _fullQueue;
    std::map<std::pair<std::string,uint8>,std::size_t> _fullCursors;
    bool Eligible(Player* player,uint8 channel) const;
    bool SameAudience(Player const* first,Player const* second,uint8 channel) const;
    bool Send(Player* player,Dialogue const& dialogue,Phrase const& phrase,Actor const& audience,uint64 target);
    bool Start(Actor const& bot,Actor const& human,Event event,uint8 channel,uint64 target=0);
    bool Same(Actor const& first,Actor const& second,uint8 channel) const;
    bool MatchesHuman(Actor const& human,Actor const& audience,uint8 channel) const;
    bool Audience(Actor const& audience,uint8 channel) const;
    bool Attempt(Actor const& human,Event event,uint8 channel,uint64 bot=0,uint64 target=0);
    void QueueBG(uint32 map,uint32 instance,uint8 team,Event event,uint32 detail);
    std::atomic<bool> _ready{false};
    uint32 _timer=0;
    mutable std::mutex _mutex;
    std::map<uint64,Actor> _actors;
    std::map<uint64,std::vector<Request>> _requests;
    uint64 _nextConversation=1;
    std::vector<PendingEvent> _events;
    std::map<std::string,Phrase> _phrases;
    std::map<uint32,Dialogue> _dialogues;
    std::vector<Conversation> _active;
    std::map<std::string,uint64> _cooldowns,_recentTexts;
    std::map<uint64,uint64> _botCooldown,_humanCooldown,_lastSpeaker;
    std::map<std::string,BGState> _battlegrounds;
};
}
#define sRegionalGuildMgr FelsoriaRegional::RegionalGuildManager::Instance()
#define sRegionalBotChatMgr FelsoriaRegional::RegionalBotChatManager::Instance()
#endif
