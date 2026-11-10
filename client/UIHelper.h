/*
 * UIHelper.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "StdInc.h"

#include "../lib/CSoundBase.h"
#include "../lib/constants/EntityIdentifiers.h"
#include "../lib/texts/MetaString.h"

#include <cstdint>
#include <span>

struct MoveArtifactInfo;
struct Component;
class CArtifactSet;
class CArmedInstance;
class CGHeroInstance;
class CStackBasicDescriptor;

namespace newHorizonsNecromancy
{
struct NecromancyResult;
}

namespace newHorizonsHeroes
{
struct RecruitmentSlot;
}

namespace UIHelper
{
    std::vector<Component> getArtifactsComponents(const CArtifactSet & artSet, const std::vector<MoveArtifactInfo> & movedPack);
    std::vector<Component> getSpellsComponents(const std::set<SpellID> & spells);
    soundBase::soundID getNecromancyInfoWindowSound();
    std::string getNecromancyInfoWindowText(const CStackBasicDescriptor & stack);
    /// Build the authoritative New Horizons post-battle Necromancy summary.
    /// Counts and blocked state come from the server packet; the client does
    /// not infer them from the current army after the result is applied.
    std::vector<Component> getNewHorizonsNecromancyComponents(const newHorizonsNecromancy::NecromancyResult & result);
    std::string getNewHorizonsNecromancyInfoWindowText(const newHorizonsNecromancy::NecromancyResult & result);
    std::string getArtifactsInfoWindowText();
    std::string getEagleEyeInfoWindowText(const CGHeroInstance & hero, const std::set<SpellID> & spells);
    /// Shared legality is a UI preflight only; the simulation validates every request again.
    std::string armySlotProblem(const CArmedInstance * army, SlotID slot, CreatureID creature);
    bool checkArmySlot(const CArmedInstance * army, SlotID slot, CreatureID creature);
    std::string armySlotPreview(const CArmedInstance * army, SlotID slot, CreatureID creature);
    std::string armyPositionName(const CArmedInstance * army, SlotID slot);
    /// Recruitment requests let simulation apply a complete shared rearrangement plan.
    SlotID recruitmentPosition(const CArmedInstance * army, CreatureID creature, int64_t count);
    bool checkRecruitmentPlan(const CArmedInstance * army,
        std::span<const newHorizonsHeroes::RecruitmentSlot> incoming);
}
