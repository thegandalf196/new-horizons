/*
 * HeroStartingProjection.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroStartingProjection.h"
#include "CHeroClass.h"
#include "../../IGameSettings.h"
#include "../../GameLibrary.h"
#include "../../CSkillHandler.h"
#include "../../CCreatureHandler.h"
#include "../../spells/CSpellHandler.h"
#include "../../spells/NewHorizonsMagic.h"
#include "../../spells/NewHorizonsSpellAvailability.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../mapObjects/army/CStackInstance.h"

namespace newHorizonsHeroes
{
std::optional<SpellID> authoredRemainingSpellReplacement(const CHero & hero, const JsonNode & development, const JsonNode & magic, SpellID source)
{
	if(!newHorizonsHeroes::usesRemainingSpellSpecialties(development)
		|| !newHorizonsMagic::rulesActive(magic)
		|| magic["rulesetVersion"].Integer()
			< newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		return std::nullopt;
	for(const auto & producer : hero.spellSpecialtySuccessorProducers)
		if(producer.source == source)
			for(const auto & spell : LIBRARY->spellh->objects)
				if(spell && spell->getJsonKey() == producer.target
					&& newHorizonsMagic::spellAllowedByHeroRoster(magic, spell->getId()))
					return spell->getId();
	return std::nullopt;
}

std::optional<SpellID> authoredDamageSpellReplacement(const CHero & hero, const JsonNode & development, const JsonNode & magic, SpellID source)
{
	if(source != SpellID::SLAYER
		|| hero.getJsonKey() != "core:coronius")
		return std::nullopt;
	const auto & producers = hero.damageSpellSpecialtyProducers;
	if(!std::ranges::any_of(producers, [source](const auto & producer)
	{
		return producer.spell == source && producer.bonus && producer.supported;
	}))
		return std::nullopt;
	const auto & flag = development["damageSpellSpecialties"]["coroniusHolyWrathReplacement"];
	if(!flag.isBool() || !flag.Bool()
		|| !newHorizonsHeroes::damageSpellSpecialtyRules(development)
		|| !newHorizonsMagic::rulesActive(magic)
		|| magic["rulesetVersion"].Integer()
			< newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		return std::nullopt;
	const SpellID target(SpellID::decode("new-horizons:holyWrath"));
	if(target.hasValue() && newHorizonsMagic::spellAllowedByHeroRoster(magic, target))
		return target;
	return std::nullopt;
}

std::optional<SpellID> authoredNonDamageSpellReplacement(const CHero & hero, const JsonNode & development, const JsonNode & magic, SpellID source)
{
		const auto & key = hero.getJsonKey();
	const auto & remainingFlag = development["nonDamageSpellSpecialties"]["remainingStartReplacements"];
	const bool remaining = remainingFlag.isBool() && remainingFlag.Bool();
	// Halon's replacement is an inscription only. Do not add Guardian Spirit
	// to a specialty producer or fabricate a second specialty for this hero.
	if(remaining && key == "core:halon" && source == SpellID::STONE_SKIN
		&& newHorizonsMagic::rulesActive(magic)
		&& magic["rulesetVersion"].Integer()
			>= newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
	{
		const SpellID guardian(SpellID::decode("new-horizons:guardianSpirit"));
		if(newHorizonsMagic::spellAllowedByHeroRoster(magic, guardian))
			return guardian;
	}
	const bool inteus = remaining && key == "core:inteus" && source == SpellID::BLOODLUST;
	const auto & defensiveFlag = development["nonDamageSpellSpecialties"]["defensiveStartReplacements"];
	const bool defensive = source == SpellID::STONE_SKIN && defensiveFlag.isBool() && defensiveFlag.Bool()
		&& (key == "core:merist" || key == "core:labetha");
	const auto & offensiveFlag = development["nonDamageSpellSpecialties"]["offensiveStartReplacements"];
	const bool offensive = offensiveFlag.isBool() && offensiveFlag.Bool()
		&& ((source == SpellID::PRAYER && key == "core:loynis")
			|| (source == SpellID::PRECISION && key == "core:zubin"));
	const bool thant = key == "core:thant" && source == SpellID::ANIMATE_DEAD;
	const bool frailty = (source == SpellID::WEAKNESS
		&& (key == "core:cuthbert" || key == "core:olema" || key == "core:mirlanda"))
		|| (source == SpellID::STONE_SKIN && key == "core:xsi")
		|| (source == SpellID::DISRUPTING_RAY && key == "core:aenain"
			&& development["nonDamageSpellSpecialties"]["aenainFrailtyReplacement"].isBool()
			&& development["nonDamageSpellSpecialties"]["aenainFrailtyReplacement"].Bool());
	if(!thant && !frailty && !defensive && !offensive && !inteus)
		return std::nullopt;
	const auto rules = newHorizonsHeroes::nonDamageSpellSpecialtyRules(development);
	if(rules)
		for(const auto spell : rules->spells)
			if((thant && newHorizonsMagic::reanimateEnabled(magic, spell))
				|| (frailty && spell.toSpell()->getJsonKey() == newHorizonsMagic::SHADOW_FRAILTY_SPELL
					&& newHorizonsMagic::rulesActive(magic)
					&& magic["rulesetVersion"].Integer()
						>= newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
					&& newHorizonsMagic::spellAllowedByHeroRoster(magic, spell))
				|| (defensive && spell.toSpell()->getJsonKey()
					== (key == "core:merist" ? "new-horizons:hydrasVitality" : "new-horizons:guardianSpirit")
					&& newHorizonsMagic::rulesActive(magic)
					&& magic["rulesetVersion"].Integer()
						>= newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
					&& newHorizonsMagic::spellAllowedByHeroRoster(magic, spell))
				|| (inteus && spell.toSpell()->getJsonKey() == "new-horizons:crusade"
					&& newHorizonsMagic::rulesActive(magic)
					&& magic["rulesetVersion"].Integer()
						>= newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
					&& newHorizonsMagic::spellAllowedByHeroRoster(magic, spell))
				|| (offensive && spell.toSpell()->getJsonKey()
					== (key == "core:loynis" ? "new-horizons:crusade" : "new-horizons:focusMagic")
					&& newHorizonsMagic::rulesActive(magic)
					&& magic["rulesetVersion"].Integer()
						>= newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
					&& newHorizonsMagic::spellAllowedByHeroRoster(magic, spell)))
				return spell;
	return std::nullopt;
}


}

namespace newHorizonsHeroes
{
StartingHeroContext startingHeroContext(const IGameSettings & settings)
{
	return {settings.getValue(EGameSettings::HEROES_NEW_HORIZONS),
		settings.getValue(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES),
		settings.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS),
		settings.getValue(EGameSettings::MAGIC_NEW_HORIZONS), false,
		settings.getVector(EGameSettings::HEROES_MINIMAL_PRIMARY_SKILLS),
		settings.getVector(EGameSettings::HEROES_MAXIMAL_PRIMARY_SKILLS)};
}

StartingHeroOverrides authoredStartingHeroOverrides(const CGHeroInstance & hero)
{
	StartingHeroOverrides result;
	result.leadershipBonus = hero.valOfBonuses(BonusType::LEADERSHIP);
	if(hero.hasBonusFrom(BonusSource::HERO_BASE_SKILL))
	{
		result.primary.emplace();
		for(size_t i = 0; i < result.primary->size(); ++i)
			(*result.primary)[i] = hero.getRawBasePrimarySkillValue(static_cast<PrimarySkill>(i));
	}
	if(!(hero.secSkills.size() == 1 && hero.secSkills.front()
		== std::pair<SecondarySkill, ui8>(SecondarySkill::NONE, -1)))
		result.skills = hero.secSkills;
	const auto & spells = hero.getRawStartingSpellIds();
	if(spells.contains(SpellID::PRESET))
	{
		result.spells = spells;
		result.spells->erase(SpellID::PRESET);
		result.spells->erase(SpellID::SPELLBOOK_PRESET);
	}
	// Loader-time artifacts are durable slot IDs. Resolving their instances
	// through the hero callback requires a world whose map is not assigned
	// until loading returns (and pure preview maps have no callback at all).
	const auto * bookSlot = hero.getSlot(ArtifactPosition::SPELLBOOK);
	if(bookSlot && !bookSlot->locked && bookSlot->getID().hasValue())
		result.spellBook = true;
	else if(spells.contains(SpellID::SPELLBOOK_PRESET))
		result.spellBook = false;
	if(hero.stacksCount() > 0)
	{
		result.army.emplace();
		for(const auto & [slot, stack] : hero.Slots())
			result.army->push_back({static_cast<ui32>(stack->getCount()),
				static_cast<ui32>(stack->getCount()), stack->getCreatureID()});
	}
	return result;
}

std::optional<SpellID> startingHeroSpellReplacement(const CHero & hero,
	const JsonNode & development, const JsonNode & magic, SpellID source)
{
	if(const auto target = startingBookReplacement(development, magic, hero.getId(), source))
		return target;
	if(const auto target = authoredDamageSpellReplacement(hero, development, magic, source))
		return target;
	if(const auto target = authoredRemainingSpellReplacement(hero, development, magic, source))
		return target;
	return authoredNonDamageSpellReplacement(hero, development, magic, source);
}

int64_t clampStartingArmyCount(int64_t count, std::optional<int64_t> maximum)
{
	return maximum ? std::min(count, *maximum) : count;
}

void applyStartingLeadershipBonus(LeadershipSlotCapacity & capacity, int bonus)
{
	capacity.leadership += std::max(0, bonus);
	capacity.maximum = capacity.leadership / capacity.requirement;
}

void selectStartingHeroPerks(PerkState & state,
	const std::vector<PerkSelection> & profile, const std::vector<PerkSelection> & prototype,
	const std::function<int(const std::string &)> & rank)
{
	auto proposed = state;
	for(const auto & choice : profile)
		proposed.select(choice.skillId, choice.perkId, rank(choice.skillId));
	auto unmatched = profile;
	for(const auto & choice : prototype)
	{
		const int mastery = rank(choice.skillId);
		if(mastery <= 0)
			throw std::runtime_error("New Horizons hero starting perk requires missing skill " + choice.skillId);
		const auto overlap = std::ranges::find(unmatched, choice);
		if(overlap != unmatched.end())
			unmatched.erase(overlap);
		else
			proposed.select(choice.skillId, choice.perkId, mastery);
	}
	state = std::move(proposed);
}

StartingHeroProjection projectStartingHero(const CHero & hero,
	const StartingHeroContext & context, const StartingHeroOverrides & overrides)
{
	if(!hero.heroClass)
		throw std::runtime_error("Starting hero projection requires a class");
	const auto development = context.resolved ? context.development
		: resolveHeroRules(context.development, hero.heroClass->getId());
	const auto capabilities = context.resolved ? context.capabilities
		: resolveCapabilityRules(context.capabilities, hero.heroClass->getId());
	PerkState selections;
	selections.rules = context.perks;
	selections.validate();
	const auto profile = overrides.skills ? std::nullopt
		: startingDevelopmentProfile(development, selections, hero.getId(), hero.heroClass->getId());

	StartingHeroProjection result;
	if(overrides.primary)
	{
		for(size_t i = 0; i < result.primary.size(); ++i)
		{
			if(usesRules(development))
				result.primary[i] = std::clamp((*overrides.primary)[i], 0,
					static_cast<int>(development["maxPrimary"].Integer()));
			else
			{
				if(context.minimumPrimary.size() != result.primary.size()
					|| context.maximumPrimary.size() != result.primary.size())
					throw std::runtime_error("Authored legacy primary preview requires explicit bounds");
				result.primary[i] = std::clamp((*overrides.primary)[i], context.minimumPrimary[i],
					std::max(context.minimumPrimary[i], context.maximumPrimary[i]));
			}
		}
	}
	else if(usesRules(development))
	{
		const auto base = parsePrimaryProfile(development["profile"]).baseAtLevel(1);
		for(size_t i = 0; i < base.size(); ++i)
			result.primary[i] = static_cast<int>(base[i]);
	}
	else
		for(size_t i = 0; i < result.primary.size(); ++i)
			result.primary[i] = hero.heroClass->primarySkillInitial[i];

	result.skills = overrides.skills ? *overrides.skills : profile ? profile->skills : hero.secSkillsInit;
	if(!profile && usesRules(development))
	{
		result.skills = migrateStartingSkills(development, hero.heroClass->faction, result.skills);
		result.skills = applyStartingFactionSkill(development, hero.heroClass->isMagicHero(),
			hero.heroClass->faction, result.skills);
	}
	if(!context.magic.isNull() && !context.magic.Struct().empty())
	{
		std::vector<std::pair<SecondarySkill, ui8>> converted;
		for(const auto & [skill, rank] : result.skills)
		{
			const auto target = newHorizonsMagic::replacementSkill(context.magic, skill);
			auto existing = std::ranges::find_if(converted, [target](const auto & row){ return row.first == target; });
			if(existing == converted.end())
				converted.emplace_back(target, rank);
			else
				existing->second = std::max(existing->second, rank);
		}
		result.skills = std::move(converted);
	}
	const auto rank = [&result](const std::string & key)
	{
		const SecondarySkill id(SecondarySkill::decode(key));
		if(id.getNum() < 0 || SecondarySkill::encode(id.getNum()) != key)
			return 0;
		const auto row = std::ranges::find_if(result.skills, [id](const auto & value){ return value.first == id; });
		return row == result.skills.end() ? 0 : static_cast<int>(row->second);
	};
	if(usesPerkRules(selections.rules))
	{
		selectStartingHeroPerks(selections, profile ? profile->perks : std::vector<PerkSelection>{},
			hero.startingPerks, rank);
	}
	result.perks = selections.selected;
	result.spellBook = overrides.spellBook.value_or(hero.haveSpellBook);
	if(overrides.spells)
		result.spells = *overrides.spells;
	else
		for(const auto spell : hero.spells)
		{
			if(const auto target = startingHeroSpellReplacement(hero, development, context.magic, spell))
				result.spells.insert(*target);
			else if(!(newHorizonsMagic::isAdventureSpell(context.magic, spell)
				&& !newHorizonsMagic::spellAvailableForOrdinaryAcquisition(context.magic, spell))
				&& (!newHorizonsMagic::rulesActive(context.magic)
					|| newHorizonsMagic::spellAllowedByHeroRoster(context.magic, spell)))
				result.spells.insert(spell);
		}
	result.army = overrides.army.value_or(hero.initialArmy);
	if(!overrides.army && usesRules(capabilities) && capabilities["rulesetVersion"].Integer() >= 2)
		for(auto & row : result.army)
			if(row.creature.hasValue() && row.creature.toCreature()->warMachine == ArtifactID::NONE)
			{
				auto capacity = capabilityLeadershipSlot(capabilities, 1, row.creature);
				if(capacity)
					applyStartingLeadershipBonus(*capacity, overrides.leadershipBonus);
				const auto maximum = capacity ? std::optional<int64_t>(capacity->maximum) : std::nullopt;
				row.minAmount = clampStartingArmyCount(row.minAmount, maximum);
				row.maxAmount = clampStartingArmyCount(row.maxAmount, maximum);
			}
	return result;
}
}

