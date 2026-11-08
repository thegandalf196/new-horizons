/*
 * NewHorizonsDivineRetribution.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsDivineRetribution.h"
#include "NewHorizonsMetamagicEffects.h"
#include "../bonuses/BonusParameters.h"

#include "CSpell.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../GameLibrary.h"
#include "../constants/StringConstants.h"
#include "../modding/CModHandler.h"
#include "../modding/IdentifierStorage.h"
#include "../modding/ModScope.h"
#include <stdexcept>
#include <cmath>

namespace newHorizonsDivineRetribution
{
SpellID spellId(const JsonNode & savedRules)
{
	if(!LIBRARY || !LIBRARY->modh)
		throw std::runtime_error("Divine Retribution identity requires an initialized library");
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell",
		"new-horizons:divineRetribution", true);
	if(id)
		return SpellID(*id);
	const bool legacy = savedRules.isNull() || (savedRules.isStruct() && savedRules.Struct().empty());
	const bool moduleLoaded = vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE);
	if(!legacy || moduleLoaded)
		throw std::runtime_error("Divine Retribution identity is missing required New Horizons content");
	return SpellID();
}

std::optional<int32_t> capturedPayoutRatioBasisPoints(const Bonus & marker)
{
	const auto parameters = marker.parameters ? marker.parameters->toJsonNode() : JsonNode{};
	if(!parameters.isStruct() || !parameters.Struct().contains("payoutRatioBasisPoints"))
		return std::nullopt;
	const auto & ratio = parameters["payoutRatioBasisPoints"];
	if(ratio.getType() != JsonNode::JsonType::DATA_INTEGER || ratio.Integer() < 3000 || ratio.Integer() > 6900)
		throw std::runtime_error("Invalid captured Divine Retribution payout ratio");
	return static_cast<int32_t>(ratio.Integer());
}

void canonicalizeScriptPayoutRatio(Bonus & marker)
{
	if(marker.type != BonusType::DIVINE_RETRIBUTION && marker.type != BonusType::DIVINE_RETRIBUTION_JUDGED)
		return;
	auto parameters = marker.parameters ? marker.parameters->toJsonNode() : JsonNode{};
	if(!parameters.isStruct() || !parameters.Struct().contains("payoutRatioBasisPoints"))
		return; // Preserve the actual absent historical tuple.
	const auto & ratio = parameters["payoutRatioBasisPoints"];
	if(ratio.getType() != JsonNode::JsonType::DATA_FLOAT)
	{
		(void)capturedPayoutRatioBasisPoints(marker); // Existing typed validation, including malformed presence.
		return;
	}
	// LuaStack represents every Lua number as a JSON double. Canonicalize only
	// this producer's exact integral scalar, never the native/save reader.
	const auto number = ratio.Float();
	if(!std::isfinite(number) || std::floor(number) != number || number < 3000 || number > 6900)
		throw std::runtime_error("Invalid scripted Divine Retribution payout ratio");
	parameters["payoutRatioBasisPoints"] = JsonNode{};
	parameters["payoutRatioBasisPoints"].Integer() = static_cast<int32_t>(number);
	auto canonical = marker;
	canonical.parameters = std::make_shared<BonusParameters>(parameters);
	(void)capturedPayoutRatioBasisPoints(canonical);
	marker.parameters = std::move(canonical.parameters); // Do not mutate a shared source payload.
}

int32_t payoutRatioBasisPoints(const Bonus & marker)
{
	return capturedPayoutRatioBasisPoints(marker).value_or(3000);
}

int64_t recipientDamage(const CBattleInfoCallback & battle,
	const battle::Unit * recipient, int64_t rawPayout, PlayerColor spellOwner, const JsonNode * capturedOffensive)
{
	if(!recipient || rawPayout <= 0)
		return 0;
	const auto * state = battle.getBattle();
	const JsonNode absentRules;
	const auto & rules = state ? state->getMagicRules() : absentRules;
	const SpellID spell = spellId(rules);
	if(!state || !spell.hasValue() || !newHorizonsMagic::rulesActive(rules)
		|| rules["rulesetVersion"].Integer() != newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		|| !newHorizonsMagic::spellAllowedBySavedRoster(rules, spell))
		return newHorizonsMetamagic::deferredSpellDamage(rawPayout, capturedOffensive, battle, recipient, spellOwner);
	// A delayed judgment is not another cast. Its cap already captures the
	// original SP scaling; do not synthesize a caster or apply that scaling again.
	const auto ordinary = spell.toSpell()->adjustRawDamage(nullptr, recipient, rawPayout, 0,
		battle.battleGetHoldTheLineMagicalReductionBasisPoints(recipient), 100,
		battle.battleUsesNewHorizonsMultiplicativeMDR(), true, false,
		battle.battleGetPerkMagicalReductionBasisPoints(recipient));
	return newHorizonsMetamagic::deferredSpellDamage(ordinary, capturedOffensive, battle, recipient, spellOwner);
}
}
