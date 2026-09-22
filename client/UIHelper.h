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
    /// Client-side fast path for the authoritative per-stack Leadership rule.
    /// A false result only suppresses this UI request; the server validates it
    /// again against current state.
    bool checkLeadershipResult(const CArmedInstance * destination, CreatureID creature, TQuantity resultingCount);
    bool checkLeadershipTransfer(const CArmedInstance * source, const CArmedInstance * destination,
        SlotID sourceSlot, SlotID destinationSlot, TQuantity amount);
}
