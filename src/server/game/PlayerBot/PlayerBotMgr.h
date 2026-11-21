
#ifndef __PLAYERBOTMGR_H__
#define __PLAYERBOTMGR_H__

#include "FelsoriaDynamicLifecycle.h"
#include "FelsoriaDynamicPartyRegistry.h"
//#include <chrono>

#include "Log.h"
#include "Common.h"
#include "SharedDefines.h"
#include "DatabaseEnv.h"
#include "Timer.h"
//#include "Callback.h"
#include "PlayerBotSession.h"
#include "BotAITool.h"
#include "LFGMgr.h"
//#include "ArenaTeamMgr.h"
#include "Containers.h"
#include <set>

#define CONVERT_ARENAAI_TOBG

class ObjectGuid;
struct LFGBotRequirement;
struct BotGlobleSchedule;
class PlayerBotSession;
class WorldSession;
class Battleground;
class Group;

enum ArenaTeamTypes
{
    ARENA_TEAM_2v2      = 2,
    ARENA_TEAM_3v3      = 3,
    ARENA_TEAM_5v5      = 5
};

enum PlayerBotAIType
{
	PBAIT_FIELD,
	PBAIT_BG,
	PBAIT_GROUP,
	PBAIT_DUNGEON,
	PBAIT_DUEL,
	PBAIT_ARENA,
	PBAIT_OVER
};

// Keep faction checks in one place so creation and queue selection agree.
inline TeamId PlayerBotRaceTeam(uint16 race)
{
	switch (race)
	{
		case 1: case 3: case 4: case 7: case 11:
		case 22: case 25: case 29: case 30:
			return TeamId::TEAM_ALLIANCE;
		case 2: case 5: case 6: case 8: case 9: case 10:
		case 26: case 27: case 28:
			return TeamId::TEAM_HORDE;
		default:
			return TeamId::TEAM_NEUTRAL;
	}
}

struct PlayerBotCharBaseInfo
{
	uint64 guid;
	uint32 account;
	std::string name;
	uint16 race;
	uint16 profession;
	uint16 gender;
	uint16 level;

	PlayerBotCharBaseInfo()
	{
		guid = 0;
		account = 0;
		race = profession = gender = level = 0;
	}
	PlayerBotCharBaseInfo(uint64 id, uint32 acc, const std::string &na, uint16 ra, uint16 pro, uint16 gen, uint16 lv) :
		guid(id), account(acc), name(na), race(ra), profession(pro), gender(gen), level(lv)
	{
	}

	std::string GetNameANDClassesText();

	TeamId GetCamp()
	{
		return PlayerBotRaceTeam(race);
	}
};

struct PlayerBotBaseInfo
{
	static PlayerBotCharBaseInfo empty;
	bool isAccountInfo;
	uint32 id;
	std::string username;
	std::string pass;
	using CharInfoMap = std::map<uint64, PlayerBotCharBaseInfo>;
	CharInfoMap characters;
	std::queue<WorldPacket> needCreateBots;

	PlayerBotBaseInfo(uint32 uid, const char *name, std::string &pa, bool isAcc) :
		isAccountInfo(isAcc), id(uid), pass(pa)
	{
		characters.clear();
		username = name;
	}

	bool MatchRaceByFuction(bool fuction, uint16 race)
	{
		return PlayerBotRaceTeam(race) == (fuction ? TeamId::TEAM_ALLIANCE : TeamId::TEAM_HORDE);
	}
	bool ExistClass(bool fuction, uint16 prof)
	{
#ifdef INCOMPLETE_BOT
		if (prof != 1 && prof != 5 && prof != 9)
			return false;
#endif
		for (CharInfoMap::iterator it = characters.begin();
			it != characters.end();
			it++)
		{
			if (it->second.profession == prof)
			{
				uint16 race = it->second.race;
				if (MatchRaceByFuction(fuction, race))
					return true;
			}
		}
		return false;
	}

	PlayerBotCharBaseInfo& GetRandomCharacterByFuction(bool faction)
	{
		if (characters.size() <= 0)
		{
			return empty;
		}
#ifdef INCOMPLETE_BOT
		for (int i = 0; i < 20; i++)
#else
		for (int i = 0; i < 5; i++)
#endif
		{
			int16 select = irand(0, characters.size() / 2 - 1);
			for (CharInfoMap::iterator it = characters.begin();
				it != characters.end();
				it++)
			{
#ifdef INCOMPLETE_BOT
				if (it->second.profession != 1 && it->second.profession != 5 && it->second.profession != 9)
					continue;
#endif
				if (MatchRaceByFuction(faction, it->second.race))
				{
					if (select <= 0)
						return it->second;
					else
						--select;
				}
			}
		}
		return characters.begin()->second;
	}

	std::vector<uint32> GetNoArenaTeamCharacterIDsByFuction(bool faction, ArenaTeamTypes type)
	{
		std::vector<uint32> outIDs;
		if (characters.size() <= 0)
		{
			return outIDs;
		}
		for (CharInfoMap::iterator it = characters.begin();
			it != characters.end();
			it++)
		{
#ifdef INCOMPLETE_BOT
			if (it->second.profession != 1 && it->second.profession != 5 && it->second.profession != 9)
				continue;
#endif
			if (MatchRaceByFuction(faction, it->second.race))
			{
				//if (sArenaTeamMgr->ExistArenaTeamByType(ObjectGuid(uint64(it->second.guid)), type))
				//	continue;
				outIDs.push_back(it->second.guid);
			}
		}
		Trinity::Containers::RandomShuffle(outIDs);
		//unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
		//std::shuffle(outIDs.begin(), outIDs.end(), std::default_random_engine(time(NULL)));
		return outIDs;
	}

	PlayerBotCharBaseInfo& GetCharacter(bool faction, uint32 prof)
	{
		if (characters.size() <= 0)
			return empty;
		for (CharInfoMap::iterator it = characters.begin();
			it != characters.end();
			it++)
		{
			if (!MatchRaceByFuction(faction, it->second.race))
				continue;
			if (it->second.profession == prof)
				return it->second;
		}
		return empty;
	}

	uint32 GetCharIDByNoArenaType(bool faction, uint32 prof, uint32 arenaType, std::vector<ObjectGuid>& fliters);

	bool ExistCharacterByGUID(ObjectGuid& guid)
	{
		for (CharInfoMap::iterator it = characters.begin();
			it != characters.end();
			it++)
		{
			if (it->second.guid == guid.GetCounter())
				return true;
		}
		return false;
	}

	TeamId GetTeamIDByChar(ObjectGuid& guid)
	{
		uint32 id = guid.GetCounter();
		for (CharInfoMap::iterator it = characters.begin();
			it != characters.end();
			it++)
		{
			if (it->second.guid != id)
				continue;
			return it->second.GetCamp();
		}
		return TeamId::TEAM_NEUTRAL;
	}

	std::string GetCharNameANDClassesText(ObjectGuid& guid)
	{
		uint32 id = guid.GetCounter();
		for (CharInfoMap::iterator it = characters.begin();
			it != characters.end();
			it++)
		{
			if (it->second.guid != id)
				continue;
			return it->second.GetNameANDClassesText();
		}
		return "";
	}

	bool RemoveCharacterByGUID(ObjectGuid& guid)
	{
		uint32 id = guid.GetCounter();
		for (CharInfoMap::iterator it = characters.begin();
			it != characters.end();
			it++)
		{
			if (it->second.guid != id)
				continue;
			characters.erase(it);
			return true;
		}
		return false;
	}
};

using BattlegroundTypeId = uint16;
class PlayerBotMgr
{
	typedef std::vector<BattlegroundTypeId> BattleGroundTypes;

private:
	PlayerBotMgr();
	~PlayerBotMgr();

public:
	PlayerBotMgr(PlayerBotMgr const&) = delete;
	PlayerBotMgr(PlayerBotMgr&&) = delete;

	PlayerBotMgr& operator= (PlayerBotMgr const&) = delete;
	PlayerBotMgr& operator= (PlayerBotMgr&&) = delete;

	static PlayerBotMgr* instance();
	static void SwitchPlayerBotAI(Player* player, PlayerBotAIType aiType, bool force);

	std::string GetPlayerLinkText(Player const* player) const;

	std::string RandomArenaName();
	void ReleasePendingBotName(std::string const& name);
	PlayerBotSession* GetBotSessionByCharGUID(ObjectGuid& guid);
	TeamId GetTeamIDByPlayerBotGUID(ObjectGuid& guid);
	bool IsPlayerBot(WorldSession *pSession);
	bool IsBotAccuntName(std::string name);
	bool IsIDLEPlayerBot(Player* player);
	void DestroyBotMail(uint32 guid);
	void LoadPlayerBotBaseInfo();
	void AddNewAccountBotBaseInfo(std::string name);
	std::set<uint32> GetArenaTeamPlayerBotIDCountByTeam(TeamId team, int32 count, ArenaTeamTypes type);
	PlayerBotBaseInfo* GetPlayerBotAccountInfo(uint32 guid);
	PlayerBotBaseInfo* GetAccountBotAccountInfo(uint32 guid);

	void UpdateLastAccountIndex(std::string& username);
	void UpAllPlayerBotSession();

	void OnPlayerBotCreate(ObjectGuid const& guid, uint32 accountId, std::string const& name, uint8 gender, uint8 race, uint8 playerClass, uint8 level, uint64 operation = 0);
	void OnAccountBotCreate(ObjectGuid const& guid, uint32 accountId, std::string const& name, uint8 gender, uint8 race, uint8 playerClass, uint8 level);
	void OnAccountBotDelete(ObjectGuid& guid, uint32 accountId);
	void OnPlayerBotLogin(WorldSession* pSession, Player* pPlayer);
	void OnPlayerBotLogout(WorldSession* pSession);
	void OnPlayerBotLeaveOriginalGroup(Player* pPlayer);
	void LoginGroupBotByPlayer(Player* pPlayer);
	void LoginFriendBotByPlayer(Player* pPlayer);
	void LogoutAllGroupPlayerBot(Group* pGroup, bool force);

	void AllPlayerBotRandomLogin(const char* name = "");
	void AllPlayerBotLogout();
	bool PlayerBotLogout(uint32 account);
	bool AllPlayerLeaveBG(uint32 account);
	void SupplementPlayerBot();
	bool DynamicBotsEnabled() const;
	bool IsDynamicBotPartyProtected(Player const* player) const;
    bool IsDynamicBotPartyHumanConnected(Player const* player) const;
    void SyncDynamicPartyGroup(Group const* group, bool initialOnly = false);
    void RemoveDynamicPartyMember(ObjectGuid const& group, ObjectGuid const& member);
    void ForgetDynamicPartyGroup(ObjectGuid const& group);
    bool IsDynamicPartyMemberProtected(ObjectGuid const& group, ObjectGuid const& member) const;
    bool IsRememberedDynamicParty(uint32 guid) const;
    bool IsRememberedDynamicPartyOwner(uint32 member, uint32 human) const;
    bool RememberedDynamicPartyConnected(uint32 guid) const;
	bool IsDynamicBotHumanActivityProtected(uint32 accountId) const;
	bool DynamicHumanIsConnectedGuid(uint32 guid) const; // Caller holds m_dynamicLock.
	bool DynamicGroupHasConnectedHumanGuid(uint32 guid) const; // Caller holds m_dynamicLock.
	static uint8 GetDynamicAreaRequiredClass(uint32 map, uint32 zone, uint32 area);
	static bool IsDynamicRaceAllowedAt(uint32 map, uint32 zone, uint32 area, uint8 race, uint8 team);
	uint32 GetDynamicBotTargetLevel(uint32 accountId) const;
	void PublishDynamicSnapshot(WorldSession* session);
	bool IsPreservedDynamicAccount(uint32 accountId) const;
	void OnDynamicBotCreateFailed(uint32 accountId, uint64 operation = 0, bool cancelled = false);
    void OnDynamicBotSaveQueued(uint32 accountId, uint64 operation, FelsoriaDynamicLifecycle::Receipt receipt);
	void OnDynamicBotQueueFailed(uint32 accountId);
    void RequestDynamicRDFPartyCleanup(Group* group, ObjectGuid leavingGuid);
    uint32 GetFelsoriaDynamicBGRealm(uint32 accountId) const;
    void BindFelsoriaBGPlan(Battleground const* bg, uint32 queuedType);
    bool IsFelsoriaBGBotAllowed(Player* player, Battleground const* bg, uint32 team) const;

	void OnRealPlayerJoinBattlegroundQueue(uint32 bgTypeId, uint32 level);
	void OnRealPlayerLeaveBattlegroundQueue(uint32 bgTypeId, uint32 level);
	void OnRealPlayerLeaveArenaQueue(uint32 bgTypeId, uint32 level, uint32 aaType);
	void OnRealPlayerEnterBattleground(uint32 bgTypeId, uint32 level);
	void OnRealPlayerLeaveBattleground(const Player* player);

	void Update();
	uint32 GetOnlineBotCount(TeamId team, bool hasReal);
	uint32 GetOnlineBotCount2(TeamId team, bool hasReal);

	bool LoginBotByAccountIndex(uint32 account, uint32 index);
	void DelayLoginPlayerBotByGUID(ObjectGuid& guid) { m_DelayOnlineBots.push(guid); }
	bool AddNewPlayerBotByGUID(ObjectGuid& guid);
	bool AddNewPlayerBotByGUID2(ObjectGuid& guid);
	void AddNewPlayerBot(bool faction, Classes prof, uint32 count);
	void AddNewAccountBot(bool faction, Classes prof);
	void AddNewPlayerBotByClass(uint32 count, Classes prof);
	bool ChangePlayerBotSetting(uint32 account, uint32 minLV, uint32 maxLV, uint32 talent);
	lfg::LfgRoles GetPlayerBotCurrentLFGRoles(Player* player);
	ObjectGuid GetNoArenaMatchCharacter(TeamId team, uint32 arenaType, Classes cls, std::vector<ObjectGuid>& fliters);
	std::string GetNameANDClassesText(ObjectGuid& guid);
	bool CanReadyArenaByArenaTeamID(uint32 arenaTeamId);
    void SetMax(int max) { m_MaxOnlineBot = max; }
    int32 m_MaxOnlineBot;
    int32 m_BotOnlineCount;

private:
	bool ExistClassByRace(uint8 race, uint8 prof);
	void InitializeCreatePlayerBotName();
	std::string RandomName(uint8 race);
	uint8 RandomRace(bool group, uint8 prof, uint32 targetLevel = 110);
	uint8 RandomSkinColor(uint8 race, uint8 gender, uint8 prof);
	uint8 RandomFace(uint8 race, uint8 gender, uint8 skinColor, uint8 prof);
	uint8 RandomHair(uint8 race, uint8 gender, uint8 prof);
	uint8 RandomHairColor(uint8 race, uint8 gender, uint8 hairID, uint8 prof);
	uint8 RandomFacialHair(uint8 race, uint8 gender, uint8 hairColor, uint8 prof);
	PlayerBotSession* UpPlayerBotSessionByBaseInfo(PlayerBotBaseInfo *pAcc, bool accountInfo);
	WorldPacket BuildCreatePlayerData(bool group, uint8 prof, uint32 targetLevel = 110, uint8 preferredRace = RACE_NONE);
	void CreateOncePlayerBot();

	void ClearBaseInfo();
	void SupplementAccount();
	void LoadCharBaseInfo();
	void LoadSessionPermissionsCallback(PreparedQueryResult result);

	void ClearEmptyNeedPlayer();
	void ClearNeedPlayer(uint32 bgTypeID, uint32 bracketID);
	void AddNewPlayerBotToBG(TeamId team, uint32 minLV, uint32 maxLV, BattlegroundTypeId bgTypeID);
	//void AddNewPlayerBotToLFG(lfg::LFGBotRequirement* botRequirement);
	void AddNewPlayerBotToAA(TeamId team, BattlegroundTypeId bgTypeID, uint32 bracketID, uint32 aaType);
	void AddTeamBotToRatedArena(uint32 arenaTeamId);
	//bool FillOnlineBotScheduleByLFGRequirement(lfg::LFGBotRequirement* botRequirement, BotGlobleSchedule* botSchedule);
	uint32 GetScheduleTalentByLFGRequirement(lfg::LfgRoles roles, uint32 botCls);
	void QueryBattlegroundRequirement();
	void QueryRatedArenaRequirement();
	void QueryNonRatedArenaRequirement();
	void OnlinePlayerBotByGUIDQueue();
	void UpdateDynamicBots();
    uint8 GetFelsoriaBGTeamGroup(uint32 type, uint32 bracket, uint32 instance,
        uint32 humanGuid, uint32 originRealm, uint8 humanTeam, uint8 botTeam, uint32 joinTime);
	void LoadDynamicAccounts();
	uint32 DynamicUsableAccountCount() const;
	void QuarantineDynamicAccount(uint32 account, char const* reason);
    void RefreshDynamicCreates(uint32 now);
    void PruneDynamicCache();
    uint32 ReuseDynamicWorldBot(uint32 plannedAccount);
	void CreateDynamicAccount();
	void CreateDynamicCharacter();
	void RetireDynamicCharacter(uint32 account);
	void FinishDynamicRetire(uint32 account);
	bool DynamicPartyMemberIsHuman(uint32 guid) const;
    bool DynamicGroupHasHuman(Player const* player) const;
	bool DynamicGroupHasHumanGuid(uint32 guid) const;
    Group* FindDynamicPartyGroup(uint32 guid, bool& unresolved) const;
    void ProtectDynamicPartyLeases(uint32 now);
	bool ExistUnBGPlayerBot();
	PVPDifficultyEntry const* FindBGBracketEntry(Battleground* bg_template, uint32 level);

private:
	uint32 m_BotAccountAmount;
	uint32 m_LastBotAccountIndex;
	uint32 m_LFGSearchTick;
	uint32 m_ArenaSearchTick;
	std::map<uint32, PlayerBotBaseInfo*> m_idPlayerBotBase;
	std::map<uint32, PlayerBotBaseInfo*> m_idAccountBotBase;

	BattleGroundTypes m_BGTypes;
	std::vector<std::string> allName;
	std::vector<std::string> allArenaName;
	std::set<std::string> m_PendingBotNames;
	mutable std::mutex m_pendingBotNamesLock;
	std::queue<ObjectGuid> m_DelayOnlineBots;
	struct DynamicLease
	{
		uint32 guid = 0;
		uint32 targetLevel = 0;
		uint32 targetMap = 0;
		uint32 targetZone = 0;
		uint32 targetArea = 0;
		uint32 targetInstance = 0;
        uint32 bgOriginRealm = 0; // P08: set before Create, only for purpose 3.
        uint8 bgLanguageGroup = 0;
		uint32 humanGuid = 0;
		uint32 queueId = 0;
		uint32 phase = 1;
		std::set<uint32> phaseIds;
		uint8 role = 0;
		uint32 dungeons[3] = {};
		uint8 dungeonCount = 0;
		uint8 team = 0;
		uint8 purpose = 0; // 1: world, 2: LFG, 3: battleground
        FelsoriaDynamicLifecycle::Receipt saveReceipt;
        FelsoriaDynamicLifecycle::WriteFence writes;
        uint64 operation = 0;
        uint32 createStartedAt = 0, commitCheckedAt = 0, pendingGuid = 0;
        uint32 loginStartedAt = 0, retireStartedAt = 0;
        bool cached = false, discard = false, recoverable = false, originReleased = false;
		bool creating = false;
		// FELSORIA_DYNAMIC_CAPACITY_V1: ambiguous accounts never reserve capacity.
		bool quarantined = false;
		bool retiring = false;
		bool deleteSubmitted = false;
		uint32 changedAt = 0;
		uint32 allocatedAt = 0;
        // Diagnostic timestamps are observed at the existing manager/snapshot cadence.
        uint32 perfCreateAt = 0, perfCreatedAt = 0, perfLoginAt = 0, perfReadyAt = 0;
        bool perfActiveReported = false;
		uint32 prepareTraceAt = 0; // RDF preparation state, at most once per 10 seconds.
		uint32 lastHumanGroupSeen = 0;
		float x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;
		bool placed = false;
		bool queueScheduled = false;
		bool Available() const
		{
			return !quarantined && !guid && !purpose && !creating && !retiring;
		}
	};
    struct FelsoriaBGPlan
    {
        uint8 groups[2] = {0, 0};
        uint32 anchor = 0;
        uint32 joinTime = 0;
        uint32 boundInstance = 0;
    };
    std::map<std::pair<uint32, uint32>, FelsoriaBGPlan> m_felsoriaBGQueuedPlans;
    std::map<uint32, FelsoriaBGPlan> m_felsoriaBGInstancePlans;
    mutable std::mutex m_felsoriaBGLock;
	std::map<uint32, DynamicLease> m_dynamicLeases;
	mutable std::mutex m_dynamicLock;
    FelsoriaDynamicPartyRegistry::Registry m_dynamicPartyRegistry;
    struct DynamicRDFDeparture
    {
        ObjectGuid group;
        ObjectGuid leaving;
        std::vector<uint32> members;
    };
    // Hooks never acquire m_dynamicLock while holding LFG/group state.
    std::mutex m_dynamicRDFDepartureLock;
    std::vector<DynamicRDFDeparture> m_dynamicRDFDepartures;
    uint64 m_dynamicNextOperation = 0;
    uint32 m_dynamicCommitCursor = 0, m_dynamicCreateCursor = 0;
    uint32 m_dynamicProvisionAccount = 0, m_dynamicProvisionAt = 0;
    bool m_dynamicProvisionCreated = false;
    std::string m_dynamicProvisionName, m_dynamicProvisionPassword;
    uint32 m_dynamicReused = 0, m_dynamicLastPipelineStatus = 0;
    uint32 m_dynamicLastBGEntryTrace = 0;
    uint8 m_dynamicBGTeamCursor = 0;
	uint32 m_dynamicNextAccount = 1;
	uint32 m_dynamicLastUpdate = 0;
	uint32 m_dynamicLastCreate = 0;
	uint32 m_dynamicRetireCursor = 0;
	std::map<uint32, std::pair<uint32, uint32>> m_dynamicHumanZone; // account -> (zone, first observed ms)
	uint32 m_dynamicWorldCapacity = 50;
	uint32 m_dynamicLastStatus = 0;
	struct DynamicSpawnPoint
	{
		float x, y, z, o;
		uint32 area;
	};
	struct DynamicSnapshot
	{
		uint32 account = 0;
		uint32 guid = 0;
		uint32 map = 0;
		uint32 zone = 0;
		uint32 area = 0;
		uint32 placementArea = 0;
		std::vector<DynamicSpawnPoint> placements;
		uint32 phase = 1;
		uint32 instance = 0;
		std::set<uint32> phaseIds;
		uint32 level = 0;
		uint8 klass = 0;
		uint8 race = 0;
		uint32 group = 0;
		uint32 groupSize = 0;
        ObjectGuid nativeGroup;
        uint32 groupLeader = 0;
        bool lfgGroup = false;
        std::vector<uint32> groupMembers;
		uint32 updated = 0;
		uint8 team = 0;
		uint8 territory = 0;
		bool bot = false;
		bool dungeon = false;
		bool battleground = false;
		bool bgQueue = false;
		bool bgInvited = false;
        bool normalBG = false;
        uint8 bgTeam = 0;
        uint32 originRealm = 0;
        std::map<uint8, uint32> normalBGQueues; // Queue type -> join timestamp; no arenas/rated queues.
		bool lfgQueue = false;
		bool busy = false;
		bool ready = false;
        bool preparationFailed = false; // Published on the owning session thread.
		bool placementValid = false;
		uint32 lastPlacementWarning = 0;
        uint32 perfContextAt = 0;
		float x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;
		float placementX = 0.0f, placementY = 0.0f, placementZ = 0.0f, placementO = 0.0f;
	};
	std::map<uint32, DynamicSnapshot> m_dynamicSnapshots;
    struct DynamicPerfContext
    {
        uint32 startedAt = 0;
        bool firstReported = false, fullReported = false;
    };
    std::map<uint32, DynamicPerfContext> m_dynamicPerfContexts;

public:
	static std::map<uint32, std::list<UnitAI*> > m_DelayDestroyAIs;
	static std::mutex g_uniqueLock;
};

#define sPlayerBotMgr PlayerBotMgr::instance()

#endif // __PLAYERBOTMGR_H__
