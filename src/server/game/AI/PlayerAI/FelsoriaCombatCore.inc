// FELSORIA_COMBAT_CORE_V1 - shared native-cast helpers for PvE and PvP.
// Included once by BotAITool.cpp. Never stores Unit/Spell pointers between ticks.
#include "FelsoriaCombatPolicy.h"
#include "SpellMgr.h"
#include "Config.h"
#include <mutex>
#include <tuple>

bool BotUtility::CombatGlobalCooldown(Player* player, SpellInfo const* info)
{
    return !player || !info || (info->Cooldowns.StartRecoveryTime > 0 &&
        player->GetGlobalCooldownMgr().HasGlobalCooldown(info) &&
        !player->HasAuraTypeWithAffectMask(SPELL_AURA_IGNORE_CD, info));
}

uint32 BotUtility::CombatRetryDelay(SpellCastResult result)
{
    switch (result)
    {
    case SPELL_FAILED_NOT_READY: case SPELL_FAILED_SPELL_IN_PROGRESS:
    case SPELL_FAILED_MOVING: case SPELL_FAILED_OUT_OF_RANGE:
    case SPELL_FAILED_LINE_OF_SIGHT: case SPELL_FAILED_NO_POWER:
        return 150; // Re-evaluate transient readiness on the next ordinary AI tick.
    default: return 1500;
    }
}

bool BotUtility::IsCombatControl(Unit* target)
{
    if (!target) return false;
    if (target->HasAuraType(SPELL_AURA_MOD_CONFUSE) || target->HasAuraType(SPELL_AURA_MOD_FEAR) ||
        target->HasAuraType(SPELL_AURA_MOD_FEAR_2)) return true;
    uint32 const protectedMechanics = (1u << MECHANIC_SLEEP) | (1u << MECHANIC_INCAPACITATE) |
        (1u << MECHANIC_POLYMORPH) | (1u << MECHANIC_SAPPED) | (1u << MECHANIC_FREEZE);
    for (auto const& entry : target->GetAppliedAuras())
    {
        auto const& application = entry.second;
        if (!application || application->IsPositive()) continue;
        SpellInfo const* info = application->GetBase()->GetSpellInfo();
        if (info && (info->GetAllEffectsMechanicMask() & protectedMechanics)) return true;
    }
    return false;
}

bool BotUtility::CanUseBotControl(Unit* target, SpellInfo const* info)
{
    if (!target || !info || target->IsImmunedToSpell(info)) return false;
    DiminishingGroup group = GetDiminishingReturnsGroupForSpell(info, false);
    // Respect the core's own DR classification and its configured enable switch.
    return group == DIMINISHING_NONE || !target->GetCharmerOrOwnerPlayerOrPlayerItself() ||
        target->GetDiminishing(group) < GetDiminishingReturnsMaxLevel(group);
}

namespace FelsoriaCombatCore
{
inline Spell* InterruptibleCast(Unit* target)
{
    if (!target) return nullptr;
    for (uint8 slot : {uint8(CURRENT_GENERIC_SPELL), uint8(CURRENT_CHANNELED_SPELL)})
    {
        Spell* cast = target->GetCurrentSpell(slot);
        if (!cast || !cast->IsInterruptable() ||
            (cast->getState() != SPELL_STATE_PREPARING && cast->getState() != SPELL_STATE_CASTING)) continue;
        SpellInfo const* info = cast->GetSpellInfo();
        if (!info || (cast->getState() == SPELL_STATE_PREPARING && cast->GetCastTime() <= 0)) continue;
        if (!(info->InterruptFlags & SPELL_INTERRUPT_FLAG_INTERRUPT) &&
            !info->HasChannelInterruptFlag(CHANNEL_INTERRUPT_FLAG_INTERRUPT)) continue;
        if (info->Categories.PreventionType != SPELL_PREVENTION_TYPE_SILENCE &&
            info->Categories.PreventionType != SPELL_PREVENTION_TYPE_UNK3 &&
            info->Categories.PreventionType != SPELL_PREVENTION_TYPE_UNK5) continue;
        if (target->HasAuraType(SPELL_AURA_PREVENT_INTERRUPT)) continue;
        return cast;
    }
    return nullptr;
}

inline uint32 InterruptSpell(unsigned spec)
{
    switch (spec)
    {
    case 71: case 72: case 73: return 6552; // Pummel
    case 66: case 70: return 96231; // Rebuke
    case 253: case 254: return 147362; // Counter Shot
    case 255: return 187707; // Muzzle
    case 259: case 260: case 261: return 1766; // Kick
    case 258: return 15487; // Silence
    case 250: case 251: case 252: return 47528; // Mind Freeze
    case 262: case 263: case 264: return 57994; // Wind Shear
    case 62: case 63: case 64: return 2139; // Counterspell
    case 268: case 269: return 116705; // Spear Hand Strike
    case 103: case 104: return 106839; // Skull Bash
    case 102: return 78675; // Solar Beam
    case 577: case 581: return 183752; // Disrupt
    default: return 0;
    }
}

using CastKey = std::tuple<uint32, uint32, unsigned, ObjectGuid, uint32, uint32>;
static std::mutex claimMutex;
static FelsoriaCombatPolicy::CastClaims<CastKey, ObjectGuid> interruptClaims;
}

bool BotUtility::HasBotInterruptibleCast(Unit* target)
{
    return FelsoriaCombatCore::InterruptibleCast(target) != nullptr;
}

bool BotUtility::TryCombatInterrupt(Player* player, Unit* target, BotBGAIMovement* movement)
{
    if (!player || !target || !movement || !player->GetSession() ||
        (!player->GetSession()->IsBotSession() && !player->GetSession()->IsAccountBotSession()) ||
        !player->IsInWorld() || !player->isAlive() || player->IsBeingTeleported() || player->IsChangeMap() ||
        !target->IsInWorld() || !target->isAlive() || target->GetMap() != player->GetMap() ||
        !player->InSamePhase(target) || !player->canSeeOrDetect(target) ||
        !player->IsValidAttackTarget(target) || IsCombatControl(target) ||
        FelsoriaQuestHubPvP::IsBlocked(player, target) ||
        sConfigMgr->GetIntDefault("PlayerBot.CombatCore.Interrupts", 1) == 0) return false;
    // This helper may not open an unrelated PvE encounter.
    if (!target->GetCharmerOrOwnerPlayerOrPlayerItself() && !target->isInCombat()) return false;
    Spell* active = FelsoriaCombatCore::InterruptibleCast(target);
    if (!active) return false;
    uint32 now = getMSTime();
    if (!FelsoriaCombatPolicy::Elapsed(now, active->GetStartCastTime(), 150)) return false;
    FelsoriaCombatCore::CastKey key(player->GetMapId(), player->GetInstanceId(),
        unsigned(player->GetBGTeamId()), target->GetGUID(), active->GetSpellInfo()->Id, active->GetStartCastTime());
    ObjectGuid owner = player->GetGUID();
    auto claim = [&]() -> bool {
        std::lock_guard<std::mutex> lock(FelsoriaCombatCore::claimMutex);
        return FelsoriaCombatCore::interruptClaims.Acquire(key, owner, now, 600);
    };
    auto release = [&]() {
        std::lock_guard<std::mutex> lock(FelsoriaCombatCore::claimMutex);
        FelsoriaCombatCore::interruptClaims.Release(key, owner);
    };
    uint32 base = FelsoriaCombatCore::InterruptSpell(player->GetSpecializationId());
    uint32 id = base ? FindMaxRankSpellByExist(player, base) : 0;
    SpellInfo const* info = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
    if (info && info->CalcCastTime(player) == 0 && !info->IsChanneled() &&
        !player->HasUnitState(UNIT_STATE_CASTING) && !player->HasSpellCooldown(id) &&
        !CombatGlobalCooldown(player, info) && !target->IsImmunedToSpell(info) && player->IsWithinLOSInMap(target))
    {
        float maximum = player->GetSpellMaxRangeForTarget(target, info);
        float minimum = player->GetSpellMinRangeForTarget(target, info);
        if ((maximum <= 0 || player->GetDistance(target) <= maximum) &&
            player->GetDistance(target) >= minimum && PrepareBotCast(player, info, target, movement) == SPELL_CAST_OK)
        {
            if (!claim()) return false;
            TriggerCastData data; data.triggerFlags = TriggerCastFlags::TRIGGERED_NONE;
            Spell* spell = new Spell(player, info, data); spell->m_CastItem = nullptr;
            SpellCastTargets targets; targets.SetUnitTarget(target); targets.SetDst(*target);
            SpellCastResult result = spell->prepare(&targets);
            // prepare() owns the spell event on both success and failure.
            if (result == SPELL_CAST_OK)
            {
                if (sConfigMgr->GetIntDefault("PlayerBot.CombatSpellAudit", 0))
                    TC_LOG_INFO(LOG_FILTER_PLAYER, "FELSORIA_COMBAT_INTERRUPT account=%u spell=%u targetSpell=%u pet=0",
                        player->GetSession()->GetAccountId(), id, std::get<4>(key));
                return true;
            }
            release();
        }
    }
    // Pet interruption is independent of the owner's active channel/cast.
    Pet* pet = player->GetPet();
    if (!pet || !pet->IsInWorld() || !pet->isAlive() || pet->GetMap() != player->GetMap() ||
        !pet->InSamePhase(target) || !pet->IsWithinLOSInMap(target)) return false;
    for (uint32 petId : {19647u, 89766u})
    {
        SpellInfo const* petInfo = sSpellMgr->GetSpellInfo(petId);
        if (!petInfo || !pet->HasSpell(petId) || pet->HasSpellCooldown(petId) || target->IsImmunedToSpell(petInfo)) continue;
        if (petId == 89766 && !CanUseBotControl(target, petInfo)) continue;
        TriggerCastData data; data.triggerFlags = TriggerCastFlags::TRIGGERED_NONE;
        Spell* spell = new Spell(pet, petInfo, data); spell->m_CastItem = nullptr;
        if (spell->CheckPetCast(target) != SPELL_CAST_OK) { delete spell; continue; }
        if (!claim()) { delete spell; return false; }
        SpellCastTargets targets; targets.SetUnitTarget(target); targets.SetDst(*target);
        if (spell->prepare(&targets) == SPELL_CAST_OK)
        {
            if (sConfigMgr->GetIntDefault("PlayerBot.CombatSpellAudit", 0))
                TC_LOG_INFO(LOG_FILTER_PLAYER, "FELSORIA_COMBAT_INTERRUPT account=%u spell=%u targetSpell=%u pet=1",
                    player->GetSession()->GetAccountId(), petId, std::get<4>(key));
            return true;
        }
        release();
    }
    return false;
}
