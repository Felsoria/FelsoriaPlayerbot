
#ifndef _BOT_BGAI_MOVEMENT_H_
#define _BOT_BGAI_MOVEMENT_H_

#include "ScriptSystem.h"
#include "PlayerAI.h"
#include "BotAI.h"
#include "BotFieldAI.h"
#include "BotGroupAI.h"
#include "BotArenaAI.h"
#include "BotDuelAI.h"
#include "AIWaypointsMgr.h"
#include "CommandBG/FelsoriaGilneasRecoveryPolicy.h"

//class BotBGAI;
class Pathfinding;

class BotAIVehicleMovement3D
{
public:
	BotAIVehicleMovement3D(Player* player);
	void SetStopMoveGap(float gap) { m_NextMoveGap = gap; }
	void ClearMovement();
	bool HaveNextmovement();
	bool HaveCurrentmovement();
	void AddMovement(Unit* pTarget, float maxOffset);
	bool UpdateVehicleMovement3D();

private:
	Player* m_Player;
	Position m_MovementPos;
	Position m_NextMovementPos;
	float m_NextMoveGap;
};

class BotBGAIMovement
{
public:
	BotBGAIMovement(Player* player, BotBGAI* ai);
	BotBGAIMovement(Player* player, BotFieldAI* ai);
	BotBGAIMovement(Player* player, BotGroupAI* ai);
	BotBGAIMovement(Player* player, BotArenaAI* ai);
	BotBGAIMovement(Player* player, BotDuelAI* ai);
	~BotBGAIMovement();

	ObjectGuid GetTargetObjectID() { return targetGuid; }
	void ClearMovement();
    void PauseForCast();
    void UpdateWSGActivity();
    void ResetWSGTask();
    void ReleaseCombatMovement();
    void AcceptPatrolCommand(AIWaypoint* first, AIWaypoint* second);
    bool IsWSGTargetBlocked(Unit const*) const;
    bool IsWSGDropInProgress() const;
    bool HasWSGFlagTask() const;
    Player* GetWSGAttackCarrier() const;
    bool IsBFGJumpInProgress() const;
    void ResetBFGAfterResurrection();
    FelsoriaGilneasRecovery::MoveResult SettleBFGStartRecoveryFloor();
    bool ProbeBFGRecoveryFloor(float x,float y,float fromZ,float& floor) const;
    bool CanBFGRecoveryWalk(FelsoriaGilneas::Point const&,FelsoriaGilneas::Point const&) const;
    bool CanBFGRecoveryJump(FelsoriaGilneas::Point const&,FelsoriaGilneas::Point const&) const;
    FelsoriaGilneasRecovery::MoveResult MoveBFGEgressPath(std::vector<FelsoriaGilneasRecovery::Step>const&,unsigned& cursor);
    bool ContinueBFGWaterCrossing();
    bool MoveBFGRecordedPath(std::vector<G3D::Vector3> const& path);
    char const* GetBFGMovementReason() const { return m_BFGMovementReason; }
    bool BuildBFGGroundLeg(G3D::Vector3 const& start,G3D::Vector3 const& goal,std::vector<G3D::Vector3>& out);
    bool MoveABRecordedPath(std::vector<G3D::Vector3> const& path);
    bool BuildABGroundPath(float x,float y,float z,std::vector<G3D::Vector3>& path);
    bool BuildAVGroundLeg(G3D::Vector3 const&,G3D::Vector3 const&,std::vector<G3D::Vector3>&);
    bool MoveAVRecordedChunk(std::vector<G3D::Vector3>const&);
    bool RecoverFallenAVBot();
    char const* GetAVMovementRejectReason() const {return m_AVRejectReason;}
	void AcceptCommand(AIWaypoint* targetAIWP, bool isFlag = false);
	void AcceptCommand(ObjectGuid guid, bool isFlag = false);
	void ExecuteMovementCommand();
	bool ExecuteCruxMovementCommand();
	void MovementToAIWP(float offset = 0);
	void MovementTo(float x, float y, float z, float offset = 0);
	void MovementTo(ObjectGuid guid, float offset = 0);
	void MovementToTarget();
	void ApplyFinishPath(PathParameter* pathParam);
	void SyncPosition(Position& pos, bool immed = false);
	bool CanMovementTo(float x, float y, float z);
	bool CanDynamicOutdoorMovementTo(float x, float y, float z);
	bool SimulationMovementTo(float x, float y, float z, Position& outPos);

	static uint32 GetTargetFindpathPointCount(Player* self, Unit* pTarget);

private:
	bool IsNearToPosition(float x, float y, float z, float range);
    bool ProbeBFGRecoveryJump(FelsoriaGilneas::Point const&,FelsoriaGilneas::Point const&,float& duration,float& speedXY,float& speedZ) const;
    void MoveABGroundTo(float x,float y,float z);
    void MoveAVGroundTo(float x,float y,float z);
    bool BuildAVGroundPath(float x,float y,float z,std::vector<G3D::Vector3>& path);
    bool GetAVFloor(G3D::Vector3 const& point,float& floor) const;
    bool ValidateAVGroundPath(std::vector<G3D::Vector3>const& raw,std::vector<G3D::Vector3>& out);
    void ObserveWSGGround();
    bool RestoreWSGTask();
    bool TryWSGGraveyardLanding();
    bool TryWSGTaskRoute(G3D::Vector3 const& goal);
    bool TryWSGGraveyardExit(G3D::Vector3 const& goal);
    bool WSGMovementProtected() const;
    bool IsWSGOnNavGround() const;
    void LogWSGGroundReject(char const* reason, G3D::Vector3 const& goal);
    bool WaitForWSGGroundSpline();
    bool ValidateWSGGroundPath(Pathfinding const&, G3D::Vector3 const&, G3D::Vector3 const&,
        std::vector<G3D::Vector3>&, G3D::Vector3*) const;
    bool BuildWSGGroundLeg(G3D::Vector3 const&, G3D::Vector3 const&,
        std::vector<G3D::Vector3>&, G3D::Vector3*) const;
    bool BuildWSGGroundPath(G3D::Vector3 const&, G3D::Vector3 const&,
        std::vector<G3D::Vector3>&) const;
    bool LaunchWSGGroundPath(std::vector<G3D::Vector3> const&);
    void MoveWSGGroundTo(G3D::Vector3 const&);
    void MoveWSGTo(float,float,float);
    bool GetWSGRecordedDrop(int16,int16&,int16&) const;
    bool HandleWSGRecordedDrop(int16);
    void TrackWSGUnreachableTarget(G3D::Vector3 const&);
    bool RecoverFallenWSGBot();
    Player* GetWSGEscortCarrier() const;
    void ResetWSGEscort();
    void FollowWSGCarrier(Player* carrier);
	void TeleportToValidPosition();
	bool IsDynamicOutdoorBot() const;
	bool IsSafeDynamicOutdoorPath(Pathfinding const& path, float x, float y, float z) const;
	void WarnRejectedDynamicPath(Pathfinding const& path, float x, float y, float z);

private:
    bool m_AVSpline=false, m_AVPreparing=false, m_AVRecorded=false, m_AVHasSafeGround=false;
    bool m_AVReadyChosen=false;
    uint32 m_AVReadyInstance=0,m_AVReadyAttempt=0;
    Position m_AVReadyGoal;
    char const* m_AVRejectReason="none";
    uint32 m_AVInstance=0, m_AVProgressAt=0, m_AVAttemptAt=0;
    float m_AVSpeed=0.0f;
    Position m_AVGoal, m_AVProgress;
    Position m_AVSafeGround;
    uint32 m_AVSafeAt=0, m_AVBelowSince=0, m_AVRecoveryUntil=0, m_AVSafeInstance=0;
    bool m_BFGWaterActive=false,m_BFGHopActive=false,m_BFGWaterReverse=false;
    uint32 m_BFGWaterStarted=0,m_BFGHopStarted=0,m_BFGWaterInstance=0;
    unsigned m_BFGWaterLanding=0;Position m_BFGWaterGoal;
    bool m_BFGRecoveryHop=false;
    uint32 m_BFGRecoveryHopAt=0,m_BFGRecoveryInstance=0;
    Position m_BFGRecoveryLanding;
    bool m_BFGStartSettling=false;
    uint32 m_BFGStartSettleAt=0,m_BFGStartSettleInstance=0;
    Position m_BFGStartSettleLanding;
    char const* m_BFGMovementReason="not_called";
    bool m_ABSpline=false;
    bool m_ABRecorded=false;
    unsigned m_ABPathCursor=0;
    float m_ABSpeed=0.0f;
    Position m_ABPathStart;
    uint32 m_ABProgressAt=0,m_ABAttemptAt=0,m_ABRejectAt=0;
    Position m_ABGoal,m_ABProgress;
	Player* m_Player;
	BotBGAI* m_BGAI;
	BotFieldAI* m_FieldAI;
	BotGroupAI* m_GroupAI;
	BotArenaAI* m_ArenaAI;
	BotDuelAI* m_DuelAI;

	bool m_IsFlagTarget;
	ObjectGuid targetGuid;
	AIWaypoint* pTargetAIWP;
	int32 m_MovementTick;
	uint32 lastPathfindSure;
	uint32 m_LastSyncTick;
    uint32 m_IdleSplineProgressTime=0;
    Position m_IdleSplineProgressPosition;
    // Commander intent survives a combat/cast movement reset. Chase splines
    // and their coordinates are deliberately not used as strategic tasks.
    uint32 m_WSGTaskInstance=0;
    ObjectGuid m_WSGTaskGuid;
    AIWaypoint* m_WSGTaskWaypoint=nullptr;
    AIWaypoint* m_WSGPatrolFirst=nullptr;
    AIWaypoint* m_WSGPatrolSecond=nullptr;
    bool m_WSGTaskFlag=false;
    bool m_WSGTaskRouting=false;
    bool m_WSGExecutingTaskRoute=false;
    Position m_WSGTaskRouteGoal;
    bool m_WSGExitingGraveyard=false;
    bool m_WSGExecutingExit=false;
    uint8 m_WSGExitVariant=0;
    uint32 m_WSGGroundInstance=0;
    uint32 m_WSGProgressTime=0;
    uint32 m_WSGRetryUntil=0;
    uint32 m_WSGSafeGroundTime=0;
    int16 m_WSGChunkStart=-1;
    bool m_WSGHasSafeGround=false;
    bool m_WSGGroundRoutePath=false;
    bool m_WSGWalkFallback=false;
    bool m_WSGDropActive=false;
    uint32 m_WSGDropTime=0;
    uint32 m_WSGActivityTime=0;
    uint32 m_WSGFailedTargetTime=0;
    uint32 m_WSGBlockedUntil=0;
    bool m_WSGHasObjective=false;
    ObjectGuid m_WSGFailedTarget;
    ObjectGuid m_WSGBlockedTarget;
    Position m_WSGActivityPosition;
    Position m_WSGFailedTargetPosition;
    Position m_WSGObjective;
    Position m_WSGSafeGround;
    Position m_WSGProgressPosition;
    Position m_WSGGoal;
    Position m_WSGRequestedGoal;
    uint32 m_WSGExitRejectLogTime = 0;
    int16 m_WSGRouteIndex = -1;
    ObjectGuid m_WSGEscortGuid;
    bool m_WSGEscortSpline = false;
    uint8 m_WSGEscortRetryTick = 0;
    int8 m_WSGRouteDirection = 0;
    uint8 m_WSGRecoveryWait = 0;
    uint8 m_WSGRouteVariant = 0; // 0: unset, 1: main, 2/3: side alternatives, 4: Alliance FC, 5: Horde FC
};

#endif // !_BOT_BGAI_MOVEMENT_H_
