/*
 * NewHorizonsMusterUI.h, part of VCMI / New Horizons
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../lib/entities/creature/NewHorizonsMusterRules.h"

#include <optional>
#include <string>
#include <vector>

class CGTownInstance;
class CCreature;
class CGHeroInstance;

namespace newHorizonsMusterUI
{
/// The town Muster offer, including the four active Recruitment perk
/// modifiers. External dwellings remain outside this UI contract; the server
/// remains authoritative for the final amount and legality check.
struct Offer
{
	const CGTownInstance * town = nullptr;
	const CGHeroInstance * hero = nullptr;
	int recruitmentRank = 0;
	int currentWeek = 0;
	bool usedThisWeek = false;
	bool targetUsedThisWeek = false;
	int usesThisWeek = 0;
	int maximumUses = 1;
	::newHorizonsMuster::PerkModifiers modifiers;
};

struct Target
{
	int row = -1;
	CreatureID creature = CreatureID::NONE;
	newHorizonsCreatures::CreatureCategory category = newHorizonsCreatures::CreatureCategory::CORE;
	int amount = 0;
	const CCreature * creatureType = nullptr;
};

/// Returns the offer if the active New Horizons Recruitment skill is present
/// on the hero serving the town. Legacy worlds and external dwellings
/// intentionally return no offer.
std::optional<Offer> offerFor(const CGTownInstance * town);

/// Returns the legal town dwelling rows for the offer.  Rows remain in their
/// historical order; category labels are presentation only and never replace
/// the authoritative row identity sent to the server.
std::vector<Target> targetsFor(const Offer & offer);

/// Whether the town recruitment UI should expose the action at all.
bool isEligible(const CGTownInstance * town);

/// Opens the row-selection dialog and submits one authoritative Muster request
/// after the player confirms a legal row.
void open(const CGTownInstance * town);

/// Human-readable action status for a button tooltip/status bar.
std::string status(const Offer & offer);
}
