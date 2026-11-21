// Project Felsoria - Eternal Vigil V3. Ground idle fix and spaced owner-trajectory following.
#ifndef FELSORIA_ETERNAL_VIGIL_V3_H
#define FELSORIA_ETERNAL_VIGIL_V3_H

#include "Player.h"
#include "QuestDef.h"
#include "TemporarySummon.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "Map.h"
#include <array>
#include <vector>
#include <map>
#include <mutex>
#include <cmath>
#include <algorithm>

namespace FelsoriaEternalVigil
{
    enum : uint32 { Quest = 42869, PlayerGlide = 131347, NpcGlide = 199303, JumpPoint = 428690, StationPoint = 428691 };
    // About two walking steps. Follow the owner's travelled route in single file,
    // so spacing never requires a sideways jump through the railing.
    static float const Spacing = 3.5f;
    static uint32 const Entries[3] = { 102799, 102798, 102797 };
    static Position const Start = { -842.0f, 4271.0f, 746.28f, 4.17f };
    static Position const Landing = { -939.0f, 4102.0f, 648.38f, 3.95f };

    inline float D2(Position const& a, Position const& b)
    {
        float x = a.GetPositionX() - b.GetPositionX(), y = a.GetPositionY() - b.GetPositionY();
        return std::sqrt(x * x + y * y);
    }
    inline float D3(Position const& a, Position const& b)
    {
        float z = a.GetPositionZ() - b.GetPositionZ(), xy = D2(a, b);
        return std::sqrt(xy * xy + z * z);
    }
    inline bool Active(Player* p)
    {
        QuestStatus q = p->GetQuestStatus(Quest);
        return q == QUEST_STATUS_INCOMPLETE || q == QUEST_STATUS_COMPLETE;
    }
    inline bool Eligible(Player* p)
    {
        if (!p->IsInWorld() || p->getClass() != CLASS_DEMON_HUNTER || p->GetMapId() != 1220)
            return false;
        if (p->GetQuestRewardStatus(41033) || p->GetQuestRewardStatus(41221))
            return false;
        if (Active(p) || p->GetQuestRewardStatus(Quest)) return true;
        for (uint32 q : { 39247u, 41863u, 40249u, 41119u })
            if (p->GetQuestStatus(q) != QUEST_STATUS_NONE) return true;
        return false;
    }
    inline bool Grounded(Player* p)
    {
        if (p->HasUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR | MOVEMENTFLAG_FLYING))
            return false;
        float ground = p->GetHeight(p->GetPositionX(), p->GetPositionY(), p->GetPositionZ() + 1.0f, true, 5.0f);
        return ground > -100000.0f && std::fabs(p->GetPositionZ() - ground) < 1.5f;
    }
    inline bool AtRedoubt(Player* p)
    {
        return p->GetPositionZ() < 655.0f && D2(*p, Landing) < 85.0f && Grounded(p);
    }
    inline Position Station(uint8 rank)
    {
        Position pos = Landing;
        pos.m_positionX += rank == 0 ? 0.0f : (rank == 1 ? 3.5f : -3.5f);
        return pos;
    }
    inline void ClearAirState(Creature* actor)
    {
        actor->RemoveAurasDueToSpell(NpcGlide);
        actor->SetFeatherFall(false);
        actor->SetSwim(false);
        // In this core SetCanFly(false) unconditionally sets FALLING, even
        // when CAN_FLY was already clear. Never call it just to initialise.
        if (actor->HasUnitMovementFlag(MOVEMENTFLAG_CAN_FLY)) actor->SetCanFly(false);
        if (actor->HasUnitMovementFlag(MOVEMENTFLAG_DISABLE_GRAVITY)) actor->SetDisableGravity(false);
        actor->SetFall(false);
        actor->SetAnimTier(0);
    }
    inline void Stand(Creature* actor, Player* face = nullptr)
    {
        actor->StopMoving();
        actor->GetMotionMaster()->Clear(false);
        actor->GetMotionMaster()->MoveIdle();
        ClearAirState(actor);
        actor->RemoveUnitMovementFlag(MOVEMENTFLAG_MASK_MOVING_OR_TURN | MOVEMENTFLAG_FALLING_FAR);
        actor->SetUInt32Value(UNIT_FIELD_EMOTE_STATE, 0);
        if (face) actor->SetOrientation(actor->GetAngle(face));
        // SetFacingToObject launches a zero-distance FORWARD spline in this
        // core. Send the native stop/facing packet instead, also if finalized.
        Movement::MoveSplineInit(*actor).Stop(false, true);
    }
    inline Position Between(Position const& a, Position const& b, float f)
    {
        Position pos = a;
        pos.m_positionX += (b.GetPositionX() - a.GetPositionX()) * f;
        pos.m_positionY += (b.GetPositionY() - a.GetPositionY()) * f;
        pos.m_positionZ += (b.GetPositionZ() - a.GetPositionZ()) * f;
        return pos;
    }
    inline bool Trail(std::vector<Position> const& route, size_t end, float behind, Position& goal, size_t& segment)
    {
        if (route.empty()) return false;
        for (size_t n = end; n > 0; --n)
        {
            float length = D3(route[n - 1], route[n]);
            if (length > 0.001f && behind < length)
            {
                goal = Between(route[n - 1], route[n], (length - behind) / length);
                segment = n - 1;
                return true;
            }
            behind -= length;
        }
        return false; // Not enough history: keep the initial separated posts.
    }
    inline void GlideState(Creature* actor)
    {
        // 199303 is the NPC Demon Hunter Glide State in the supplied 7.3.5 DB2.
        // Unlike 131347 it does not invoke the player's 196353 knockback/timer.
        // Do not enable spline flying or animation tier 3 (air-swim animation).
        actor->SetSwim(false);
        if (!actor->HasAura(NpcGlide)) actor->AddAura(NpcGlide, actor);
        actor->SetFeatherFall(true);
    }

    class Actor : public CreatureScript
    {
    public:
        Actor() : CreatureScript("npc_felsoria_eternal_vigil_actor") { }
        struct AI : public ScriptedAI
        {
            explicit AI(Creature* c) : ScriptedAI(c) { }
            bool jumped = false;
            bool stationed = false;
            uint32 missingOwner = 0;
            void Reset() override
            {
                jumped = stationed = false;
                missingOwner = 0;
                me->SetReactState(REACT_PASSIVE);
                me->setActive(true);
                Stand(me);
                if (me->GetEntry() != Entries[0]) me->SetUInt32Value(UNIT_FIELD_NPC_FLAGS, 0);
            }
            void AttackStart(Unit*) override { }
            void MovementInform(uint32 type, uint32 point) override
            {
                if (type == EFFECT_MOTION_TYPE && point == JumpPoint) jumped = true;
                if (type == POINT_MOTION_TYPE && point == StationPoint) stationed = true;
            }
            void UpdateAI(uint32 diff) override
            {
                TempSummon* summon = me->ToTempSummon();
                Unit* owner = summon ? summon->GetSummoner() : nullptr;
                if (!owner || !owner->ToPlayer() || owner->ToPlayer()->GetMapId() != 1220)
                {
                    missingOwner += diff;
                    if (missingOwner >= 15000) me->DespawnOrUnsummon();
                }
                else missingOwner = 0;
                // Phase changes and quest turn-in never delete a live actor.
            }
        };
        CreatureAI* GetAI(Creature* c) const override { return new AI(c); }
    };

    class Scene : public PlayerScript
    {
        enum TravelStage { Ground, Jump, Glide, ToStation, Docked };
        struct Travel
        {
            ObjectGuid guid;
            TravelStage stage = Ground;
            size_t nextGround = 0;
            uint32 retry = 0;
            Position goal;
            bool goalSent = false;
            bool idle = true;
        };
        struct State
        {
            uint32 timer = 0;
            bool armed = false, departed = false, landed = false, greeted = false;
            Position takeoff, airAnchor, ownerLanding, previousAir;
            float directionX = -0.5f, directionY = -0.86f;
            size_t takeoffIndex = 0;
            std::vector<Position> route;
            std::array<Travel, 3> actors;
        };
        std::map<ObjectGuid, State> states;
        std::mutex lock;

        static Creature* Get(Player* p, Travel const& travel)
        {
            return travel.guid.IsEmpty() || !p->GetMap() ? nullptr : p->GetMap()->GetCreature(travel.guid);
        }
        static void Despawn(Player* p, State const& s)
        {
            for (Travel const& travel : s.actors)
                if (Creature* actor = Get(p, travel)) actor->DespawnOrUnsummon();
        }
        static void Phase(Player* p, Creature* actor)
        {
            if (actor->GetPhaseMask() != p->GetPhaseMask() || actor->GetPhases() != p->GetPhases())
            {
                actor->SetPhaseMask(p->GetPhaseMask(), false);
                actor->SetPhaseId(p->GetPhases(), true);
            }
        }
        static void SpawnMissing(Player* p, State& s)
        {
            for (uint8 i = 0; i < 3; ++i)
            {
                Travel& travel = s.actors[i];
                if (Get(p, travel)) continue;
                Position pos = s.landed ? Station(i) : Start;
                TravelStage stage = s.landed ? Docked : Ground;
                if (s.departed && !s.landed)
                {
                    size_t segment = 0;
                    if (!Trail(s.route, s.route.size() - 1, Spacing * (i + 1), pos, segment))
                    {
                        pos = s.airAnchor;
                        pos.m_positionZ += Spacing * i;
                    }
                    stage = Glide;
                }
                else if (!s.landed)
                    pos.m_positionX += i == 0 ? 0.0f : (i == 1 ? 3.5f : -3.5f);
                if (TempSummon* actor = p->SummonCreature(Entries[i], pos, TEMPSUMMON_MANUAL_DESPAWN, 0, 0, p->GetGUID()))
                {
                    travel = Travel();
                    travel.guid = actor->GetGUID();
                    travel.stage = stage;
                    travel.idle = stage != Glide;
                    if (stage == Glide)
                    {
                        actor->GetMotionMaster()->Clear(false);
                        actor->GetMotionMaster()->MoveIdle();
                        GlideState(actor);
                    }
                    if (stage == Docked) actor->SetHomePosition(Station(i));
                    if (stage != Glide) Stand(actor, !i ? p : nullptr);
                }
            }
        }
        static void Observe(Player* p, State& s)
        {
            if (!s.departed)
            {
                if (Grounded(p))
                {
                    Position pos = p->GetPosition();
                    if (s.route.empty() || D3(s.route.back(), pos) >= 0.75f)
                        s.route.push_back(pos);
                    s.takeoff = pos;
                    if (s.route.size() > 512)
                    {
                        s.route.erase(s.route.begin());
                        for (Travel& t : s.actors)
                        {
                            if (t.nextGround) --t.nextGround;
                        }
                    }
                }
                else if (!s.route.empty() &&
                    (p->HasAura(PlayerGlide) || p->HasUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR)) &&
                    p->GetPositionZ() < s.takeoff.GetPositionZ() - 2.0f &&
                    D2(*p, s.takeoff) < 45.0f && D2(*p, Landing) + 2.0f < D2(s.takeoff, Landing))
                {
                    // The opening is learned from the owner's ground path.
                    // There is no guessed edge coordinate or lateral wall jump.
                    if (D3(s.route.back(), s.takeoff) > 0.05f) s.route.push_back(s.takeoff);
                    s.takeoffIndex = s.route.size() - 1;
                    s.departed = true;
                    s.airAnchor = s.previousAir = p->GetPosition();
                    s.route.push_back(s.airAnchor);
                }
            }
            if (s.departed && !s.landed)
            {
                Position pos = p->GetPosition();
                float dx = pos.GetPositionX() - s.previousAir.GetPositionX();
                float dy = pos.GetPositionY() - s.previousAir.GetPositionY();
                float distance = std::sqrt(dx * dx + dy * dy);
                if (distance > 0.2f) { s.directionX = dx / distance; s.directionY = dy / distance; }
                s.previousAir = pos;
                if (D3(s.route.back(), pos) >= 0.5f) s.route.push_back(pos);
                if (AtRedoubt(p)) { s.landed = true; s.ownerLanding = pos; }
            }
        }
        static void Wait(Creature* actor, Travel& t)
        {
            if (!t.idle) Stand(actor);
            t.idle = true;
            t.goalSent = false;
        }
        static void GroundMove(Creature* actor, Travel& t, Position const& goal)
        {
            if (D3(*actor, goal) < 0.55f) { Wait(actor, t); return; }
            if (!t.goalSent || D3(goal, t.goal) > 0.5f ||
                (actor->movespline->Finalized() && t.retry >= 1000))
            {
                t.goal = goal;
                t.goalSent = true;
                t.idle = false;
                t.retry = 0;
                actor->SetWalk(false);
                actor->GetMotionMaster()->MovePoint(428692, goal, true);
            }
        }
        static void StepGround(Player* p, Creature* actor, State& s, Travel& t, uint8 rank)
        {
            Position goal;
            size_t segment = 0;
            bool enough = Trail(s.route, s.route.empty() ? 0 : s.route.size() - 1,
                Spacing * (rank + 1), goal, segment);
            if (!enough) { Wait(actor, t); return; }
            if (s.departed && (s.landed || segment >= s.takeoffIndex))
            {
                // Each actor's airborne destination is a different point on the
                // owner's actual trajectory. Never jump the group onto one GPS.
                if (s.landed) goal = Station(rank);
                bool room = true;
                for (uint8 i = 0; i < 3; ++i)
                    if (i != rank)
                        if (Creature* other = Get(p, s.actors[i]))
                            if ((s.actors[i].stage == Jump && D3(goal, s.actors[i].goal) < Spacing * 0.9f) ||
                                (s.actors[i].stage != Ground && D3(goal, *other) < Spacing * 0.9f)) room = false;
                bool outside = D2(goal, s.takeoff) > 2.0f && goal.GetPositionZ() < s.takeoff.GetPositionZ() - 0.7f;
                if (room && outside && D2(*actor, s.takeoff) < 9.0f &&
                    actor->IsWithinLOS(goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ() + 1.0f))
                {
                    t.stage = Jump;
                    t.retry = 0;
                    t.goal = goal;
                    t.goalSent = true;
                    t.idle = false;
                    static_cast<Actor::AI*>(actor->AI())->jumped = false;
                    GlideState(actor);
                    actor->GetMotionMaster()->MoveJump(goal.GetPositionX(), goal.GetPositionY(),
                        goal.GetPositionZ(), 14.0f, 7.0f, JumpPoint);
                    return;
                }
                // Queue at the real opening, one two-step space per preceding
                // ground actor. A blocked jump cannot pile everyone onto the lip.
                uint8 ahead = 0;
                for (uint8 i = 0; i < rank; ++i) if (s.actors[i].stage == Ground) ++ahead;
                if (ahead && !Trail(s.route, s.takeoffIndex, Spacing * ahead, goal, segment))
                { Wait(actor, t); return; }
                if (!ahead) { goal = s.takeoff; segment = s.takeoffIndex; }
            }
            // Retain intermediate ground corners rather than shortcutting the
            // player's route. The final target is interpolated, not shared.
            while (t.nextGround < segment && D3(*actor, s.route[t.nextGround]) < 0.65f) ++t.nextGround;
            if (t.nextGround < segment) GroundMove(actor, t, s.route[t.nextGround]);
            else GroundMove(actor, t, goal);
        }
        static Position Touchdown(Player* p, State const& s, uint8 rank)
        {
            Position goal = s.ownerLanding;
            float side = rank == 0 ? 0.0f : (rank == 1 ? Spacing : -Spacing);
            goal.m_positionX -= s.directionX * Spacing;
            goal.m_positionY -= s.directionY * Spacing;
            goal.m_positionX -= s.directionY * side;
            goal.m_positionY += s.directionX * side;
            float z = p->GetHeight(goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ() + 2.0f, true, 6.0f);
            if (z > -100000.0f && std::fabs(z - s.ownerLanding.GetPositionZ()) < 3.0f) goal.m_positionZ = z;
            else goal = Station(rank);
            return goal;
        }
        static void StepGlide(Player* p, Creature* actor, State& s, Travel& t, uint8 rank)
        {
            GlideState(actor);
            Position goal;
            size_t segment = 0;
            if (s.landed) goal = Touchdown(p, s, rank);
            else if (!Trail(s.route, s.route.size() - 1, Spacing * (rank + 1), goal, segment)) return;
            if (s.landed && D3(*actor, goal) < 1.2f)
            {
                ClearAirState(actor);
                t.stage = ToStation;
                t.retry = 0;
                Actor::AI* ai = static_cast<Actor::AI*>(actor->AI());
                ai->stationed = false;
                actor->GetMotionMaster()->Clear(false);
                actor->SetWalk(true);
                actor->GetMotionMaster()->MovePoint(StationPoint, Station(rank), true);
                return;
            }
            if (!t.goalSent || D3(goal, t.goal) > 0.75f || (actor->movespline->Finalized() && D3(*actor, goal) > 0.5f))
            {
                t.goal = goal;
                t.goalSent = true;
                float speed = std::max(8.0f, std::min(24.0f, D3(*actor, goal) / 0.45f));
                Movement::MoveSplineInit move(*actor);
                move.MoveTo(goal, false);
                move.SetWalk(false);
                move.SetUncompressed();
                move.SetVelocity(speed);
                move.Launch();
            }
        }
        static void Step(Player* p, Creature* actor, State& s, Travel& t, uint8 rank)
        {
            t.retry += 100;
            if (t.stage == Ground) StepGround(p, actor, s, t, rank);
            if (t.stage == Jump)
            {
                Actor::AI* ai = static_cast<Actor::AI*>(actor->AI());
                if (ai->jumped || (actor->movespline->Finalized() && D3(*actor, t.goal) < 2.0f))
                {
                    t.stage = Glide;
                    t.retry = 0;
                    t.goalSent = false;
                    actor->GetMotionMaster()->Clear(false);
                    actor->GetMotionMaster()->MoveIdle();
                }
            }
            if (t.stage == Glide) StepGlide(p, actor, s, t, rank);
            if (t.stage == ToStation)
            {
                Actor::AI* ai = static_cast<Actor::AI*>(actor->AI());
                if (ai->stationed || D3(*actor, Station(rank)) < 1.0f)
                {
                    t.stage = Docked;
                    Stand(actor, p);
                    t.idle = true;
                    actor->SetHomePosition(Station(rank));
                }
                else if (t.retry >= 5000)
                {
                    t.retry = 0;
                    actor->GetMotionMaster()->MovePoint(StationPoint, Station(rank), true);
                }
            }
        }

    public:
        Scene() : PlayerScript("player_felsoria_eternal_vigil") { }
        void OnLogout(Player* p) override
        {
            std::lock_guard<std::mutex> guard(lock);
            auto it = states.find(p->GetGUID());
            if (it != states.end()) { Despawn(p, it->second); states.erase(it); }
        }
        void OnUpdate(Player* p, uint32 diff) override
        {
            if (p->getClass() != CLASS_DEMON_HUNTER) return;
            std::lock_guard<std::mutex> guard(lock);
            auto it = states.find(p->GetGUID());
            if (!Eligible(p))
            {
                if (it != states.end()) { Despawn(p, it->second); states.erase(it); }
                return;
            }
            if (it == states.end())
            {
                bool redoubt = AtRedoubt(p) && (Active(p) || p->GetQuestRewardStatus(Quest));
                bool krasus = !p->GetQuestRewardStatus(Quest) && p->GetPositionZ() > 700.0f && D2(*p, Start) < 125.0f && Grounded(p);
                if (!redoubt && !krasus) return;
                State& added = states[p->GetGUID()];
                added.landed = added.departed = redoubt;
                added.ownerLanding = p->GetPosition();
                it = states.find(p->GetGUID());
            }
            State& s = it->second;
            if (s.timer > diff) { s.timer -= diff; return; }
            s.timer = 100;
            // Explicit abandonment may reset the quest. Normal completion,
            // reward, and phase changes preserve all existing GUIDs and motion.
            if (s.armed && !Active(p) && !p->GetQuestRewardStatus(Quest))
            {
                Despawn(p, s);
                states.erase(it);
                return;
            }
            SpawnMissing(p, s);
            if (!s.armed && Active(p))
            {
                s.armed = true;
                if (!s.departed)
                    if (Creature* korvas = Get(p, s.actors[0])) korvas->AI()->Talk(3, p->GetGUID());
            }
            if (s.armed && !s.landed) Observe(p, s);
            bool allDocked = true;
            for (uint8 i = 0; i < 3; ++i)
                if (Creature* actor = Get(p, s.actors[i]))
                {
                    Phase(p, actor);
                    if (s.armed && s.actors[i].stage != Docked) Step(p, actor, s, s.actors[i], i);
                    if (s.actors[i].stage != Docked) allDocked = false;
                }
                else allDocked = false;
            if (s.landed && allDocked && !s.greeted && Active(p))
                if (Creature* korvas = Get(p, s.actors[0]))
                    if (Creature* jace = korvas->FindNearestCreature(109196, 45.0f, true))
                    {
                        if (jace->IsAIEnabled) jace->AI()->Talk(0, p->GetGUID());
                        s.greeted = true;
                    }
        }
    };
}

// Preserve the already-installed dalaran.cpp registration; V3 replaces this
// header only and does not overwrite the user's other Dalaran scripts or DB.
inline void AddSC_felsoria_eternal_vigil_v1()
{
    new FelsoriaEternalVigil::Actor();
    new FelsoriaEternalVigil::Scene();
}
#endif
