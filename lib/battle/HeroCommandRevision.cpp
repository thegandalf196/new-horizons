/*
 * HeroCommandRevision.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCommandRevision.h"
#include "HeroCommand.h"
#include "NewHorizonsCombatSkills.h"
#include "NewHorizonsDivineMandate.h"
#include "Unit.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace heroCommands
{
void RevisedOrderSnapshot::validate(HeroCommand command, int32_t issuedRound) const
{
	if((command != HeroCommand::FEINT && command != HeroCommand::VANGUARD)
		|| expiresAfterRound < issuedRound || static_cast<int64_t>(expiresAfterRound) - issuedRound > 2
		|| !std::is_sorted(recipientUnitIds.begin(), recipientUnitIds.end())
		|| std::adjacent_find(recipientUnitIds.begin(), recipientUnitIds.end()) != recipientUnitIds.end()
		|| std::ranges::any_of(recipientUnitIds, [](uint32_t id)
			{ return id > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()); })
		|| feintCap < 0 || feintCap > 60 || vanguardStatCap < 0 || vanguardStatCap > 3
		|| vanguardInitiativeCap < 0 || vanguardInitiativeCap > 18
		|| feintCompanionPercent < 0 || feintCompanionPercent > 100
		|| feintReductionPercent < 0 || vanguardStatBonus < 0 || vanguardInitiativePercent < 0
		|| (potencyFinalized && (feintReductionPercent > feintCap
			|| vanguardStatBonus > vanguardStatCap || vanguardInitiativePercent > vanguardInitiativeCap)))
		throw std::runtime_error("Invalid revised Order snapshot");
	if(crown && (crown->feintReductionPercent < 0 || crown->vanguardStatBonus < 0
		|| crown->vanguardInitiativePercent < 0
		|| (potencyFinalized && (crown->feintReductionPercent > feintCap
			|| crown->vanguardStatBonus > vanguardStatCap || crown->vanguardInitiativePercent > vanguardInitiativeCap))
		|| (command == HeroCommand::FEINT && (crown->vanguardStatBonus != 0 || crown->vanguardInitiativePercent != 0))
		|| (command == HeroCommand::VANGUARD && crown->feintReductionPercent != 0)))
		throw std::runtime_error("Invalid Crown and Altar revised Order potency");
	if(command == HeroCommand::FEINT && (expiresAfterRound != issuedRound
		|| vanguardStatBonus != 0 || vanguardInitiativePercent != 0 || !recipientUnitIds.empty()))
		throw std::runtime_error("Invalid Feint snapshot");
	if(command == HeroCommand::VANGUARD && (feintReductionPercent != 0 || feintCompanionPercent != 0 || recipientUnitIds.empty()))
		throw std::runtime_error("Invalid Vanguard snapshot");
}

namespace
{
void fields(const JsonNode & node, std::initializer_list<const char *> names)
{
	if(!node.isStruct() || node.Struct().size() != names.size())
		throw std::runtime_error("Invalid revised Order fields");
	for(const auto * name : names)
		if(!node.Struct().contains(name))
			throw std::runtime_error("Missing revised Order field");
}
void formula(const JsonNode & node, std::initializer_list<const char *> names)
{
	fields(node, names);
	for(const auto * name : names)
		if(!node[name].isNumber() || !std::isfinite(node[name].Float())
			|| node[name].Float() < 0 || node[name].Float() > MAX_TARGETED_COEFFICIENT)
			throw std::runtime_error("Invalid revised Order coefficient");
}
}

void validateRevisedRulesSerialization(const JsonNode & rules, bool supported)
{
	if(isRevisedRules(rules) && !supported)
		throw std::runtime_error("Revised Order rules require the revised save format");
}

void validateRevisedRules(const JsonNode & rules)
{
	fields(rules, {"schemaVersion", "rulesetVersion", "commands", "perkBonuses"});
	if(rules["schemaVersion"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| rules["schemaVersion"].Integer() != 1)
		throw std::runtime_error("Invalid revised Order schema version");
	fields(rules["perkBonuses"], {"spearheadDamagePercent", "shieldMasterReductionPercent", "combinedArmsFeintReductionPercent"});
	for(const auto & [name, value] : rules["perkBonuses"].Struct())
		if(value.getType() != JsonNode::JsonType::DATA_INTEGER || value.Integer() < 0 || value.Integer() > 100)
			throw std::runtime_error("Invalid revised Order perk bonus");
	const auto & commands = rules["commands"];
	fields(commands, {"charge", "holdTheLine", "focusFire", "riposte", "brace", "feint", "vanguard", "secondWind"});
	const auto & feint = commands["feint"];
	const auto & vanguard = commands["vanguard"];
	for(const auto * definition : {&feint, &vanguard})
	{
		fields(*definition, {"kind", "coverage", "duration", "effects"});
		if(!(*definition)["kind"].isString() || (*definition)["kind"].String() != "order")
			throw std::runtime_error("Invalid revised Order kind");
	}
	if(!feint["coverage"].isString() || !feint["duration"].isString() || !vanguard["coverage"].isString()
		|| feint["coverage"].String() != "ownLivingNonWarMachines" || feint["duration"].String() != "round"
		|| vanguard["coverage"].String() != "ownNonUndeadNonMechanicalAtIssue" || !vanguard["duration"].isStruct())
		throw std::runtime_error("Invalid revised Order coverage or duration");
	fields(feint["effects"], {"retaliationDamageReductionPercent"});
	formula(feint["effects"]["retaliationDamageReductionPercent"], {"base", "leadership", "cap"});
	fields(vanguard["effects"], {"primaryAndMorale", "initiativePercent"});
	formula(vanguard["effects"]["primaryAndMorale"], {"base", "ratingStep", "cap"});
	formula(vanguard["effects"]["initiativePercent"], {"base", "minimumRating", "cap"});
	formula(vanguard["duration"], {"leadershipPerRound", "minimum", "maximum"});
	if(feint["effects"]["retaliationDamageReductionPercent"]["cap"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| vanguard["effects"]["primaryAndMorale"]["cap"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| vanguard["effects"]["initiativePercent"]["cap"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| vanguard["duration"]["minimum"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| vanguard["duration"]["maximum"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| vanguard["effects"]["primaryAndMorale"]["ratingStep"].Float() <= 0
		|| vanguard["duration"]["leadershipPerRound"].Float() <= 0
		|| vanguard["duration"]["minimum"].Float() != 1 || vanguard["duration"]["maximum"].Float() > 3
		|| vanguard["duration"]["maximum"].Float() < 1
		|| feint["effects"]["retaliationDamageReductionPercent"]["cap"].Float() > 60
		|| vanguard["effects"]["primaryAndMorale"]["cap"].Float() > 3
		|| vanguard["effects"]["initiativePercent"]["cap"].Float() > 18)
		throw std::runtime_error("Invalid revised Order bounds");
	// Reuse the historical strict validator for the six unchanged definitions.
	JsonNode historical = rules;
	historical.Struct().erase("perkBonuses");
	historical["rulesetVersion"].Integer() = ORDERS_ONLY_RULESET_VERSION;
	historical["commands"].Struct().erase("feint");
	historical["commands"].Struct().erase("vanguard");
	JsonNode protect = feint;
	protect["target"].String() = "protectorAndWard";
	protect["effects"].clear();
	protect["effects"]["interceptedDamageReductionPercent"]["base"].Integer() = 5;
	protect["effects"]["interceptedDamageReductionPercent"]["attack"].Integer() = 0;
	protect["effects"]["interceptedDamageReductionPercent"]["defense"].Float() = .15;
	historical["commands"]["protect"] = protect;
	JsonNode flank = protect;
	flank["target"].String() = "enemyUnit";
	flank["effects"].clear();
	flank["effects"]["meleeDamagePercent"] = protect["effects"]["interceptedDamageReductionPercent"];
	historical["commands"]["flank"] = flank;
	validateRules(historical);
}

RevisedOrderSnapshot nominalRevisedOrderSnapshot(HeroCommand command, int32_t issuedRound,
	int attack, int defense, int64_t leadership, const JsonNode & definition)
{
	auto snapshot = scaledRevisedOrderSnapshot(command, issuedRound, attack, defense, leadership, definition, 100, 100, 100);
	snapshot.feintReductionPercent = std::min(snapshot.feintReductionPercent, snapshot.feintCap);
	snapshot.vanguardStatBonus = std::min(snapshot.vanguardStatBonus, snapshot.vanguardStatCap);
	snapshot.vanguardInitiativePercent = std::min(snapshot.vanguardInitiativePercent, snapshot.vanguardInitiativeCap);
	return snapshot;
}

RevisedOrderSnapshot scaledRevisedOrderSnapshot(HeroCommand command, int32_t issuedRound,
	int attack, int defense, int64_t leadership, const JsonNode & definition,
	int attackEfficiencyPercent, int defenseEfficiencyPercent, int leadershipEfficiencyPercent)
{
	const auto boundedRound = [](double value)
	{
		return static_cast<int>(std::lround(std::clamp(value, 0.0,
			static_cast<double>(std::numeric_limits<int32_t>::max()))));
	};
	RevisedOrderSnapshot result;
	result.expiresAfterRound = issuedRound;
	const auto & effects = definition["effects"];
	if(command == HeroCommand::FEINT)
	{
		const auto & f = effects["retaliationDamageReductionPercent"];
		result.feintCap = static_cast<int>(f["cap"].Float());
		result.feintReductionPercent = boundedRound(f["base"].Float()
			+ f["leadership"].Float() * std::max<int64_t>(0, leadership) * leadershipEfficiencyPercent / 100.0);
	}
	if(command == HeroCommand::VANGUARD)
	{
		const auto rating = std::min(std::max(0, attack) * (attackEfficiencyPercent / 100.0),
			std::max(0, defense) * (defenseEfficiencyPercent / 100.0));
		const auto & stat = effects["primaryAndMorale"];
		result.vanguardStatCap = static_cast<int>(stat["cap"].Float());
		result.vanguardStatBonus = boundedRound(stat["base"].Float() + std::floor(rating / stat["ratingStep"].Float()));
		const auto & initiative = effects["initiativePercent"];
		result.vanguardInitiativeCap = static_cast<int>(initiative["cap"].Float());
		result.vanguardInitiativePercent = boundedRound(initiative["base"].Float() + initiative["minimumRating"].Float() * rating);
		const auto & duration = definition["duration"];
		const int rounds = static_cast<int>(std::clamp(std::floor(std::max<int64_t>(0, leadership)
			/ duration["leadershipPerRound"].Float()), duration["minimum"].Float(), duration["maximum"].Float()));
		if(issuedRound > std::numeric_limits<int32_t>::max() - rounds + 1)
			throw std::runtime_error("Revised Order expiry overflow");
		result.expiresAfterRound += rounds - 1;
	}
	return result;
}

int revisedPerkBonus(const JsonNode & rules, const CGHeroInstance * hero, HeroCommand command)
{
	if(!isRevisedRules(rules) || !hero)
		return 0;
	if(command == HeroCommand::CHARGE && hero->hasActivePerk("new-horizons:offense", "new-horizons:offense.encirclement"))
		return rules["perkBonuses"]["spearheadDamagePercent"].Integer();
	if(command == HeroCommand::HOLD_THE_LINE && hero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.shieldMaster"))
		return rules["perkBonuses"]["shieldMasterReductionPercent"].Integer();
	if(command == HeroCommand::FEINT && hasCombinedArms(hero))
		return rules["perkBonuses"]["combinedArmsFeintReductionPercent"].Integer();
	return 0;
}

JsonNode crownRevisedOrderDefinition(const JsonNode & definition, HeroCommand command)
{
	JsonNode coefficient;
	coefficient["attack"].Float() = 1;
	const double multiplier = newHorizonsDivineMandate::crownOrderFormula(coefficient, true)["attack"].Float();
	auto result = definition;
	if(command == HeroCommand::FEINT)
		result["effects"]["retaliationDamageReductionPercent"]["leadership"].Float() *= multiplier;
	if(command == HeroCommand::VANGUARD)
	{
		result["effects"]["primaryAndMorale"]["ratingStep"].Float() /= multiplier;
		result["effects"]["initiativePercent"]["minimumRating"].Float() *= multiplier;
	}
	return result;
}

int revisedFeintReductionFor(const HeroOrderState & order, uint32_t unitId)
{
	if(order.command != HeroCommand::FEINT || !order.revision)
		return 0;
	return order.revision->crown && order.crownAndAltarAppliesTo(unitId)
		? order.revision->crown->feintReductionPercent : order.revision->feintReductionPercent;
}

void finalizeRevisedOrderSnapshot(HeroOrderState & order, const JsonNode & magicRules)
{
	if(!order.revision || order.revision->potencyFinalized)
		return;
	auto & snapshot = *order.revision;
	const auto finalize = [&](int intrinsic, int withCrown, int companion, int cap)
	{
		return static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(orderPotency(intrinsic, order, magicRules))
			+ static_cast<int64_t>(withCrown) - intrinsic + companion, 0, cap));
	};
	if(snapshot.crown)
	{
		auto & crown = *snapshot.crown;
		crown.feintReductionPercent = finalize(snapshot.feintReductionPercent,
			crown.feintReductionPercent, snapshot.feintCompanionPercent, snapshot.feintCap);
		crown.vanguardStatBonus = finalize(snapshot.vanguardStatBonus,
			crown.vanguardStatBonus, 0, snapshot.vanguardStatCap);
		crown.vanguardInitiativePercent = finalize(snapshot.vanguardInitiativePercent,
			crown.vanguardInitiativePercent, 0, snapshot.vanguardInitiativeCap);
	}
	snapshot.feintReductionPercent = finalize(snapshot.feintReductionPercent,
		snapshot.feintReductionPercent, snapshot.feintCompanionPercent, snapshot.feintCap);
	snapshot.vanguardStatBonus = finalize(snapshot.vanguardStatBonus, snapshot.vanguardStatBonus, 0, snapshot.vanguardStatCap);
	snapshot.vanguardInitiativePercent = finalize(snapshot.vanguardInitiativePercent,
		snapshot.vanguardInitiativePercent, 0, snapshot.vanguardInitiativeCap);
	snapshot.potencyFinalized = true;
}

bool isVanguardBonus(const Bonus * bonus)
{
	return bonus && bonus->source == BonusSource::HERO_COMMAND && bonus->stacking == "vanguard"
		&& bonus->duration == BonusDuration::N_TURNS
		&& (bonus->type == BonusType::PRIMARY_SKILL || bonus->type == BonusType::MORALE
			|| bonus->type == BonusType::STACKS_INITIATIVE
			|| (bonus->type == BonusType::NONE && (bonus->val == 1 || bonus->val == 2)));
}

std::optional<ActiveVanguardBonuses> activeVanguardBonuses(const battle::Unit * unit)
{
	if(!unit)
		return {};
	ActiveVanguardBonuses result;
	for(const auto & bonus : *unit->getBonuses(CSelector(isVanguardBonus)))
	{
		if(bonus->type == BonusType::NONE)
			continue; // Entitlement metadata is not a standalone stat benefit.
		result.roundsRemaining = std::max<int>(result.roundsRemaining, bonus->turnsRemain);
		if(bonus->type == BonusType::PRIMARY_SKILL)
			result.statBonus = std::max(result.statBonus, bonus->val);
		if(bonus->type == BonusType::STACKS_INITIATIVE)
			result.initiativePercent = std::max(result.initiativePercent, bonus->val);
	}
	return result.roundsRemaining > 0 ? std::optional<ActiveVanguardBonuses>(result) : std::nullopt;
}

bool isVanguardCreature(const battle::Unit * unit)
{
	return unit && unit->alive() && !unit->isGhost() && !unit->isTimeStopped()
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(unit)
		&& unit->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
		&& !unit->hasBonusOfType(BonusType::UNDEAD)
		&& !unit->hasBonusOfType(BonusType::MECHANICAL);
}
}
