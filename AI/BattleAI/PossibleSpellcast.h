/*
 * PossibleSpellcast.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <vcmi/spells/Magic.h>
#include "../../lib/constants/EntityIdentifiers.h"

#include "../../lib/battle/Destination.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/FocusFireState.h"

class CSpell;

class PossibleSpellcast
{
public:
	using ValueMap = std::map<uint32_t, int64_t>;

	HeroCommand command = HeroCommand::NONE;
	std::optional<FocusFireState> focusFire;
	/// Unit identities carried by a targeted Order.  Focus Fire keeps its
	/// validated value object above for compatibility with its existing
	/// hypothetical-battle path; the remaining Orders use this value-only
	/// payload so the AI can rank one-target and pair-target choices without
	/// mutating the authoritative battle.
	std::vector<uint32_t> commandTargets;
	/// Orders whose effects are contextual (for example Protect and Second
	/// Wind) are scored by a deterministic read-only heuristic.  A non-zero
	/// value marks that the generic spell projection must not overwrite it.
	float commandHeuristicValue = 0.0f;
	std::string name() const;
	const CSpell * spell;
	int32_t spellOvercharge = 0;
	bool spellSelectiveDispel = false;
	SpellID spellCureAffliction = SpellID::NONE;
	/// Selected source-spell groups for canonical New Horizons Purify. Physical
	/// Poison is automatic and is tracked separately for detached evaluation.
	std::vector<std::pair<int32_t, SpellID>> spellPurifyChoices;
	std::vector<int32_t> spellPurifyPhysicalTargets;
	float spellPurifyHeuristicValue = 0.0f;
	/// Requests the once-per-combat Sorcery Temporal Field variant of Slow.
	bool spellMassSlow = false;
	/// Canonical New Horizons Shadow Gift sacrifice tier (10, 20, or 30).
	/// Zero is reserved for every other cast.
	int32_t spellShadowGiftSacrificePercent = 0;
	/// Marks the canonical multi-target damage spell so its probabilistic magic
	/// resistance is valued as expected damage after the shared cast forecast.
	bool spellStormOfDaggers = false;
	/// The cast is an immediate, non-chaining Tower Metamagic follow-up.
	bool metamagicFollowup = false;
	bool metamagicGrand = false; // projected automatic outcome, never a player request
	/// Delayed placement value for canonical New Horizons Land Mine.  Mines do
	/// not change unit health during hypothetical cast evaluation, so the
	/// targeting evaluator supplies this read-only pressure score explicitly.
	float spellPlacementHeuristicValue = 0.0f;
	/// Expected signed battle value for canonical Hand of Fate.  This is
	/// precomputed across every possible spill recipient so generic castEval does
	/// not score RNGStub's single hypothetical recipient as though it were the
	/// authoritative random result.
	std::optional<float> spellHandOfFateExpectedValue;
	/// Signed expected offensive pressure reduction for battle-form effects.
	/// This is precomputed over the effect's full uniform form pool so generic
	/// castEval does not value RNGStub's midpoint form as the random outcome.
	std::optional<float> spellBattleFormExpectedValue;
	/// Marginal three-activation physical Poison value for canonical Nature
	/// Poison, whose immediate cast does not change health.
	float spellNaturePoisonValue = 0.0f;
	/// Expected two-round echo value for canonical Soul Chain, whose cast adds
	/// relationships but does not immediately change unit health.
	float spellSoulChainDelayedValue = 0.0f;
	/// Net three-round offense minus real HP cost for canonical Shadow Gift.
	float spellShadowGiftHeuristicValue = 0.0f;
	/// Canonical New Horizons Fire Wall placement direction.  The target vector
	/// used during AI evaluation contains the complete three-hex footprint, but
	/// the authoritative action protocol carries only its start hex and this
	/// direction.
	BattleHex::EDir spellFireWallDirection = BattleHex::NONE;
	spells::Target dest;
	float value;

	PossibleSpellcast();
	virtual ~PossibleSpellcast();
};
