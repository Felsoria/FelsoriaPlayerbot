
#ifndef __PlayerBotSESSION_H
#define __PlayerBotSESSION_H

#include "WorldSession.h"
#include <set>
#include "PlayerBotMgr.h"
#include "FelsoriaDynamicLifecycle.h"
#include <mutex>
#include <atomic>
#include <ctime>
#include <chrono>

#define CAST_SCHEDULE_TICK 300

enum BotGlobleScheduleType
{
	BGSType_Online,
	BGSType_Offline,
	BGSType_Settting,
	BGSType_InBGQueue,
	BGSType_OutBGQueue,
	BGSType_EnterBG,
	BGSType_LeaveBG,
	BGSType_InAAQueue,
	BGSType_OutAAQueue,
	BGSType_EnterAA,
	BGSType_LeaveAA,
	BGSType_Online_GUID,
	BGSType_DelayLevelup,
	BGSType_InLFGQueue,
	BGSType_OutLFGQueue,
	BGSType_AcceptLFGProposal,
	BGSType_OfferPetitionSign
};
struct BotGlobleSchedule
{
	BotGlobleScheduleType bbgType;
	ObjectGuid playerGUID;
	uint32 parameter1;
	uint32 parameter2;
	uint32 parameter3;
	uint32 parameter4;
	uint32 parameter5;

	uint32 scheduleState;
	int32 processTick;

    BotGlobleSchedule(BotGlobleScheduleType eType, const ObjectGuid& guid) :
        bbgType(eType), playerGUID(guid)
    {
        parameter1 = 0;
        parameter2 = 0;
        parameter3 = 0;
        parameter4 = 0;
        parameter5 = 0;

        scheduleState = 0;
        processTick = 0;
    }

	BotGlobleSchedule(BotGlobleScheduleType eType, uint64 guid) :
		bbgType(eType), playerGUID(ObjectGuid::Create<HighGuid::Player>(guid))
	{
		parameter1 = 0;
		parameter2 = 0;
		parameter3 = 0;
		parameter4 = 0;
		parameter5 = 0;

		scheduleState = 0;
		processTick = 0;
	}
};

class PlayerBotSession : public WorldSession
{
	typedef std::list<BotGlobleSchedule> BotSchedules;

public:
	PlayerBotSession(uint32 id, std::string &name, AccountTypes sec, uint8 expansion, time_t mute_time, LocaleConstant locale, uint32 recruiter, bool isARecruiter);

	~PlayerBotSession()
	{

	}

    void FailBattlegroundEntry();
    bool BattlegroundRetryAllowed() const { return std::time(nullptr) >= m_bgRetryAfter.load(); }
private:
    std::atomic<time_t> m_bgRetryAfter{0};
public:
	bool IsBotSession() override;
	bool HasBGSchedule() override;
	bool Update(uint32 diff, Map* map = nullptr) override;

	void PushScheduleToQueue(BotGlobleSchedule& schedule);
	void RemoveScheduleByType(BotGlobleScheduleType eType);
	bool HasScheduleByType(BotGlobleScheduleType eType);
	bool HasSchedules() override;
	bool IsAccountBotSession() override;
	// Called on the owning map thread; dynamic world bots use the session death timer.
	bool HandlesDynamicWorldRevive(Player const* player) const;
	static bool DynamicWorldReviveDue(std::chrono::steady_clock::time_point death,
		std::chrono::steady_clock::time_point now)
	{
		return now - death >= std::chrono::seconds(30);
	}
	void ClearAllSchedule() { m_Schedules.clear(); }
	void SetAccountBotSession() { m_AccountBot = true; }

    bool HasRdfPreparationTasks() const { return !m_Schedules.empty(); }
    bool HasRdfReservation() const { return m_rdfReservationUntil > time(nullptr); }
    void SetRdfReservationUntil(time_t until) { m_rdfReservationUntil = until; }
    void RequestDynamicPartyProtection()
    {
        m_dynamicRetire.store(false);
        m_dynamicRDFRetire.store(false);
        m_dynamicRetireAcknowledged.store(false);
        m_dynamicDormantLogout.store(false);
        m_dynamicPartyProtection.store(true);
    }
	void RequestDynamicPartySummon(uint32 leader) { m_dynamicPartySummonLeader.store(leader); }
	uint32 DynamicPartySummonLeader() const { return m_dynamicPartySummonLeader.load(); }
	void CancelDynamicPartySummon(uint32 leader)
	{
		std::lock_guard<std::mutex> lock(m_dynamicOpLock);
		if (!m_dynamicPartySummonLeader.compare_exchange_strong(leader, 0))
			return;
		if (m_dynamicPlacement.partyLeader == leader)
		{
			m_dynamicPlacement.pending = false;
			m_dynamicPlacementComplete.store(false);
		}
	}
    void RequestDynamicRDFRetire()
    {
        m_dynamicRetireAcknowledged.store(false);
        m_dynamicRDFRetire.store(true);
        m_dynamicRetire.store(true);
    }
    std::atomic<bool> m_dynamicRDFRetire{false};
	void RequestDynamicRetire() { m_dynamicRetireAcknowledged.store(false); m_dynamicRetire.store(true); }
	void RequestDynamicDormantLogout() { m_dynamicDormantLogout.store(true); }
	bool DynamicRetireComplete() const { return m_dynamicRetireAcknowledged.load() && m_dynamicOffline.load(); }
    bool DynamicOffline() const { return m_dynamicOffline.load(); }
    FelsoriaDynamicLifecycle::WriteFence DynamicWriteFence() const { return m_dynamicWrites; }
    bool DynamicWritesPending() const { return m_dynamicWrites->load() != 0; }
    FelsoriaDynamicLifecycle::Receipt PrepareDynamicLogoutSave()
    {
        std::lock_guard<std::mutex> lock(m_dynamicOpLock);
        m_dynamicLogoutReceipt = std::make_shared<std::atomic<int>>(0);
        return m_dynamicLogoutReceipt;
    }
    int DynamicLogoutSaveStatus()
    {
        std::lock_guard<std::mutex> lock(m_dynamicOpLock);
        return m_dynamicLogoutReceipt ? m_dynamicLogoutReceipt->load() : -1;
    }
    bool DynamicOperationBusy() const { return m_dynamicOperation.Busy(); }
    uint64 DynamicLoginOperation() const { return m_dynamicOperation.Token(FelsoriaDynamicLifecycle::Operation::Login); }
    uint64 DynamicCreateOperation() const { return m_dynamicOperation.Token(FelsoriaDynamicLifecycle::Operation::Create); }
    bool DynamicCreateCurrent(uint64 operation) const
    { return operation && DynamicCreateOperation() == operation && !m_dynamicRetire.load(); }
    void CompleteDynamicCreate(uint64 operation)
    { m_dynamicOperation.Complete(operation, FelsoriaDynamicLifecycle::Operation::Create); }
    bool RequestDynamicLogin(uint32 guid, bool worldPlacement = false, uint64 operation = 0)
    {
        std::lock_guard<std::mutex> lock(m_dynamicOpLock);
        if (!m_dynamicOffline.load() || m_dynamicRetire.load() ||
            !m_dynamicOperation.Begin(operation, FelsoriaDynamicLifecycle::Operation::Login)) return false;
        m_dynamicRetireAcknowledged.store(false);
        m_dynamicLogoutReceipt.reset();
        m_dynamicRDFRetire.store(false);
        m_dynamicAwaitingWorldPlacement.store(worldPlacement);
        m_dynamicLogin.store(guid);
        return true;
    }
    bool WaitingForDynamicWorldPlacement() const { return m_dynamicAwaitingWorldPlacement.load(); }
    bool RequestDynamicCreate(WorldPacket packet, uint64 operation)
    {
        std::lock_guard<std::mutex> lock(m_dynamicOpLock);
        if (!m_dynamicOffline.load() || m_dynamicRetire.load() ||
            !m_dynamicOperation.Begin(operation, FelsoriaDynamicLifecycle::Operation::Create)) return false;
        m_dynamicRetireAcknowledged.store(false);
        m_dynamicLogoutReceipt.reset();
        m_dynamicRDFRetire.store(false);
        m_dynamicCreate.reset(new WorldPacket(std::move(packet)));
        return true;
    }
	void RequestDynamicPlace(uint32 map, float x, float y, float z, float o, uint32 phase,
		std::set<uint32> const& phaseIds, uint32 instanceId = 0, uint32 partyLeader = 0, uint8 requiredClass = 0)
	{
		std::lock_guard<std::mutex> lock(m_dynamicOpLock);
		if (partyLeader && m_dynamicPartySummonLeader.load() != partyLeader)
			return; // A newer invite or cancellation superseded the manager snapshot.
		DynamicPlacement& place = m_dynamicPlacement;
		if ((place.pending || m_dynamicPlacementComplete.load()) &&
			place.map == map && place.instance == instanceId && place.partyLeader == partyLeader &&
			place.x == x && place.y == y && place.z == z && place.o == o)
		{
			place.phase = phase;
			place.phaseIds = phaseIds;
			if (m_dynamicPlacementComplete.load())
				m_dynamicPhase = {map, instanceId, phase, phaseIds, true,
					std::chrono::steady_clock::now()};
			return;
		}
		place = {map, x, y, z, o, phase, phaseIds, instanceId, true};
		place.partyLeader = partyLeader;
		place.requiredClass = requiredClass;
		if (!partyLeader) m_dynamicAwaitingWorldPlacement.store(true);
		m_dynamicPlacementComplete.store(false);
	}
	void InvalidateDynamicPlacement() { m_dynamicPlacementComplete.store(false); m_dynamicAwaitingWorldPlacement.store(true); }
	bool DynamicPlacementComplete() const { return m_dynamicPlacementComplete.load(); }
	// MapManager reads this only while routing a queued dynamic teleport.
	bool GetDynamicPlacementTargetInstance(uint32 mapId, uint32& instanceId, uint8* requiredClass = nullptr)
	{
		std::lock_guard<std::mutex> lock(m_dynamicOpLock);
		if (!m_dynamicPlacement.pending || !m_dynamicPlacement.started ||
			m_dynamicPlacement.map != mapId || !m_dynamicPlacement.instance)
			return false;
        if (m_dynamicPlacement.partyLeader && !m_dynamicPlacement.requiredClass)
            return false;
		instanceId = m_dynamicPlacement.instance;
        if (requiredClass) *requiredClass = m_dynamicPlacement.requiredClass;
		return true;
	}
	// Called only on the player's owning map thread, including by PhaseMgr.
	bool GetDynamicPhaseOverride(Player* bot, uint32& phase, std::set<uint32>& phaseIds);
	void RequestDynamicPhase(uint32 map, uint32 instanceId, uint32 phase,
		std::set<uint32> const& phaseIds)
	{
		std::lock_guard<std::mutex> lock(m_dynamicOpLock);
		m_dynamicPhase = {map, instanceId, phase, phaseIds, true,
			std::chrono::steady_clock::now()};
	}
	void RequestDynamicQueue(BotGlobleSchedule setting, BotGlobleSchedule queue)
	{
		std::lock_guard<std::mutex> lock(m_dynamicOpLock);
		m_dynamicQueueSetting.reset(new BotGlobleSchedule(setting));
		m_dynamicQueueAction.reset(new BotGlobleSchedule(queue));
	}
private:
	bool PlayerIsReady();
	void ProcessNoWorld(uint32 diff);
	void ProcessDynamicWorldRevive();
	void CastSchedule(uint32 diff);
	bool ProcessOnline(BotGlobleSchedule& schedule);
	bool ProcessOnlineByGUID(BotGlobleSchedule& schedule);
	bool ProcessOffline(BotGlobleSchedule& schedule);
	bool ProcessSetting(BotGlobleSchedule& schedule);
	bool ProcessInBGQueue(BotGlobleSchedule& schedule);
	bool ProcessOutBGQueue(BotGlobleSchedule& schedule);
	bool ProcessEnterBG(BotGlobleSchedule& schedule);
	bool ProcessLeaveBG(BotGlobleSchedule& schedule);
	bool ProcessInAAQueue(BotGlobleSchedule& schedule);
	bool ProcessOutAAQueue(BotGlobleSchedule& schedule);
	bool ProcessEnterAA(BotGlobleSchedule& schedule);
	bool ProcessLeaveAA(BotGlobleSchedule& schedule);
	bool ProcessDelayLevelup(BotGlobleSchedule& schedule);
	bool ProcessInLFGQueue(BotGlobleSchedule& schedule);
	bool ProcessOutLFGQueue(BotGlobleSchedule& schedule);
	bool ProcessAcceptLFGProposal(BotGlobleSchedule& schedule);
	bool ProcessOfferPetitionSign(BotGlobleSchedule& schedule);

private:
	BotSchedules m_Schedules;
    time_t m_rdfReservationUntil = 0;
	int m_LastCastTime;
	int m_NoWorldTick;
    uint32 m_RdfStatusTick = 0;
    uint32 m_dynamicPvPTick = 0;
	ObjectGuid m_dynamicDeathGuid = ObjectGuid::Empty;
	std::chrono::steady_clock::time_point m_dynamicDeathSince{};
	uint32 m_dynamicDeathMap = 0;
	uint32 m_dynamicDeathInstance = 0;
	float m_dynamicDeathX = 0.0f, m_dynamicDeathY = 0.0f;
	float m_dynamicDeathZ = 0.0f, m_dynamicDeathO = 0.0f;
    std::atomic<bool> m_dynamicRetire{false};
	std::atomic<bool> m_dynamicDormantLogout{false};
	bool m_dynamicManagedLogout = false; // Owning map thread only.
	std::atomic<bool> m_dynamicRetireAcknowledged{false};
	std::atomic<bool> m_dynamicOffline{true};
	std::atomic<uint32> m_dynamicPartySummonLeader{0};
    std::atomic<bool> m_dynamicPartyProtection{false};
	std::atomic<uint32> m_dynamicLogin{0};
	std::mutex m_dynamicOpLock;
	std::unique_ptr<WorldPacket> m_dynamicCreate;
	std::unique_ptr<BotGlobleSchedule> m_dynamicQueueSetting;
	std::unique_ptr<BotGlobleSchedule> m_dynamicQueueAction;
	struct DynamicPlacement
	{
		uint32 map = 0;
		float x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;
		uint32 phase = 1;
		std::set<uint32> phaseIds;
		uint32 instance = 0;
		bool pending = false;
		bool started = false;
		std::chrono::steady_clock::time_point startedAt{};
		uint32 partyLeader = 0;
		uint8 requiredClass = 0;
	} m_dynamicPlacement;
	struct DynamicPhase
	{
		uint32 map = 0, instance = 0, phase = 1;
		std::set<uint32> phaseIds;
		bool valid = false;
		std::chrono::steady_clock::time_point refreshedAt{};
	} m_dynamicPhase;
    FelsoriaDynamicLifecycle::WriteFence m_dynamicWrites = std::make_shared<std::atomic<std::uint32_t>>(0);
    FelsoriaDynamicLifecycle::Receipt m_dynamicLogoutReceipt;
    FelsoriaDynamicLifecycle::OperationGate m_dynamicOperation;
	std::atomic<bool> m_dynamicPlacementComplete{false};
	std::atomic<bool> m_dynamicAwaitingWorldPlacement{false};
	bool m_dynamicPhaseApplied = false; // Owning map thread only.
	bool m_AccountBot;
	std::mutex m_optQueueLock;
};

#endif //__PlayerBotSESSION_H
