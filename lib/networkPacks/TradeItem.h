/*
 * TradeItem.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../constants/VariantIdentifier.h"
#include "../constants/EntityIdentifiers.h"

using TradeItemSell = VariantIdentifier<GameResID, SlotID, ArtifactInstanceID>;
// SpellID is appended to preserve the wire/save indices of all legacy trade
// item alternatives.  New Horizons' House of Wisdom reuses RESOURCE_SKILL,
// but carries a spell scroll instead of a secondary skill.
using TradeItemBuy = VariantIdentifier<GameResID, PlayerColor, ArtifactID, SecondarySkill, SpellID>;
