#ifndef FELSORIA_BOT_ARTIFACT_SKINS_H
#define FELSORIA_BOT_ARTIFACT_SKINS_H

#include "DB2Stores.h"
#include "Item.h"
#include "Player.h"
#include "Log.h"
#include <algorithm>
#include <vector>

namespace FelsoriaBotArtifactSkins
{
    inline uint64 Mix(uint64 value)
    {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }

    inline bool HasModel(Item const* item, uint32 modifier)
    {
        // GetItemModifiedAppearance can silently return modifier 0 when a
        // requested variant is absent. Require the exact item and modifier.
        ItemModifiedAppearanceEntry const* modified =
            sDB2Manager.GetItemModifiedAppearance(item->GetEntry(), modifier);
        if (!modified || modified->ItemID != int32(item->GetEntry()) ||
            modified->ItemAppearanceModifierID != modifier)
            return false;
        // ItemAppearance storage is internal to DB2Stores.cpp in this core.
        // Use its exported display lookup only after the exact-variant check.
        uint32 const displayID = sDB2Manager.GetItemDisplayId(item->GetEntry(), modifier);
        // A zero explicit modifier uses the item's bonus appearance in this
        // core. Exclude it when that would display another variant instead.
        return displayID && displayID <= 0x7fffffffU &&
            (modifier || !item->GetBonus()->AppearanceModID);
    }

    inline bool SetSkin(Player* owner, Item* item, ArtifactAppearanceEntry const* appearance)
    {
        bool changed = false;
        // Existing transmogrification overrides hide the native artifact
        // appearance. Only bot weapon appearance overrides are cleared.
        ItemModifier const overrides[] = {
            ITEM_MODIFIER_TRANSMOG_APPEARANCE_ALL_SPECS,
            ITEM_MODIFIER_TRANSMOG_APPEARANCE_SPEC_1,
            ITEM_MODIFIER_TRANSMOG_APPEARANCE_SPEC_2,
            ITEM_MODIFIER_TRANSMOG_APPEARANCE_SPEC_3,
            ITEM_MODIFIER_TRANSMOG_APPEARANCE_SPEC_4
        };
        for (ItemModifier modifier : overrides)
            if (item->GetModifier(modifier))
            {
                item->SetModifier(modifier, 0);
                changed = true;
            }
        if (item->GetModifier(ITEM_MODIFIER_ARTIFACT_APPEARANCE_ID) != uint32(appearance->ID))
        {
            item->SetModifier(ITEM_MODIFIER_ARTIFACT_APPEARANCE_ID, uint32(appearance->ID));
            changed = true;
        }
        if (item->GetAppearanceModId() != appearance->ItemAppearanceModifierID)
        {
            item->SetAppearanceModId(appearance->ItemAppearanceModifierID);
            changed = true;
        }
        if (changed)
        {
            item->SetState(ITEM_CHANGED, owner);
            owner->SetVisibleItemSlot(item->GetSlot(), item);
        }
        return changed;
    }

    inline bool Apply(Player* owner)
    {
        // This helper is reached by bot preparation, but keep the ownership
        // guard here as well: no real player can receive these appearances.
        if (!owner || !owner->IsPlayerBot() || owner->getLevel() != 110)
            return false;
        uint32 const specID = owner->GetUInt32Value(PLAYER_FIELD_CURRENT_SPEC_ID);
        ChrSpecializationEntry const* spec = sChrSpecializationStore.LookupEntry(specID);
        if (!spec || spec->ClassID != owner->getClass())
            return false;

        uint8 const slots[] = { EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND };
        for (uint8 slot : slots)
        {
            Item* parent = owner->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!parent || !parent->GetTemplate() || sDB2Manager.IsChildItem(parent->GetEntry()))
                continue;
            uint32 const artifactID = parent->GetTemplate()->GetArtifactID();
            ArtifactEntry const* artifact = sArtifactStore.LookupEntry(artifactID);
            if (!artifactID || !artifact || artifact->ChrSpecializationID != specID)
                continue;

            Item* child = nullptr;
            if (ItemChildEquipmentEntry const* link = sDB2Manager.GetItemChildEquipment(parent->GetEntry()))
            {
                child = owner->GetChildItemByGuid(parent->GetChildItem());
                if (!child || child == parent || !child->GetTemplate() || link->ChildItemID <= 0 ||
                    (link->ChildItemEquipSlot != EQUIPMENT_SLOT_MAINHAND && link->ChildItemEquipSlot != EQUIPMENT_SLOT_OFFHAND) ||
                    child->GetEntry() != uint32(link->ChildItemID) ||
                    owner->GetItemByPos(INVENTORY_SLOT_BAG_0, link->ChildItemEquipSlot) != child ||
                    child->GetGuidValue(ITEM_FIELD_CREATOR) != parent->GetGUID())
                    continue;
            }

            std::vector<ArtifactAppearanceEntry const*> candidates;
            for (ArtifactAppearanceEntry const* appearance : sArtifactAppearanceStore)
            {
                if (!appearance || appearance->ID <= 0)
                    continue;
                ArtifactAppearanceSetEntry const* set =
                    sArtifactAppearanceSetStore.LookupEntry(appearance->ArtifactAppearanceSetID);
                if (!set || set->ArtifactID != artifactID)
                    continue;
                // Conditions unlock player collections. Bot cosmetics do not
                // grant achievements, collections, quests or account unlocks.
                if (!HasModel(parent, appearance->ItemAppearanceModifierID) ||
                    (child && !HasModel(child, appearance->ItemAppearanceModifierID)))
                    continue;
                candidates.push_back(appearance);
            }
            if (candidates.empty())
                continue;
            std::sort(candidates.begin(), candidates.end(),
                [](ArtifactAppearanceEntry const* a, ArtifactAppearanceEntry const* b) { return a->ID < b->ID; });
            candidates.erase(std::unique(candidates.begin(), candidates.end(),
                [](ArtifactAppearanceEntry const* a, ArtifactAppearanceEntry const* b) { return a->ID == b->ID; }), candidates.end());
            // Pseudorandom by character and artifact family. Preparation,
            // relog and teleport must not keep rerolling the same weapon.
            uint64 const seed = Mix((uint64(owner->GetGUIDLow()) << 32) ^ artifactID ^ 0x46454c534f524941ULL);
            ArtifactAppearanceEntry const* chosen = candidates[size_t(seed % candidates.size())];
            ArtifactAppearanceEntry const* previous = sArtifactAppearanceStore.LookupEntry(
                parent->GetModifier(ITEM_MODIFIER_ARTIFACT_APPEARANCE_ID));
            bool const parentChanged = SetSkin(owner, parent, chosen);
            bool const childChanged = child && SetSkin(owner, child, chosen);
            if (parentChanged &&
                ((chosen->OverrideShapeshiftDisplayID && chosen->OverrideShapeshiftFormID == owner->GetShapeshiftForm()) ||
                 (previous && previous->OverrideShapeshiftDisplayID && previous->OverrideShapeshiftFormID == owner->GetShapeshiftForm())))
                owner->RestoreDisplayId();
            if (parentChanged || childChanged)
                TC_LOG_DEBUG(LOG_FILTER_GENERAL,
                    "FELSORIA_BOT_ARTIFACT_SKINS_V1_1 guid=%u spec=%u item=%u artifact=%u appearance=%u modifier=%u candidates=%u",
                    owner->GetGUIDLow(), specID, parent->GetEntry(), artifactID, uint32(chosen->ID),
                    uint32(chosen->ItemAppearanceModifierID), uint32(candidates.size()));
            return true;
        }
        return false;
    }
}
#endif
