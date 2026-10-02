/*
 * Effects.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "Effects.h"
#include "BattleForm.h"

#include <vcmi/spells/Caster.h>

#include "../ISpellMechanics.h"
#include "../NewHorizonsMagic.h"
#include "../NewHorizonsSpellAvailability.h"

#include "../../GameLibrary.h"
#include "../../CStack.h"
#include "../../battle/CUnitState.h"
#include "../../battle/NewHorizonsBulwark.h"
#include "../../json/JsonNode.h"
#include "../../modding/IdentifierStorage.h"
#include "../../networkPacks/PacksForClientBattle.h"
#include "../../networkPacks/SetStackEffect.h"
#include "../../scripting/ScriptService.h"
#include "../../texts/TextIdentifier.h"


namespace spells
{
namespace effects
{

namespace
{
// Ice Bolt's historic New Horizons speed effect remains part of v1/v2
// compatibility. The v3 design makes Ice Bolt damage-only, so wrap just that
// effect and resolve its availability from the saved battle profile each time
// mechanics inspect or apply it. The same gate therefore covers authoritative
// casts and hypothetical casts used by the AI.
class LegacyIceBoltSpeedEffect final : public Effect
{
	std::shared_ptr<Effect> legacyEffect;

public:
	explicit LegacyIceBoltSpeedEffect(std::shared_ptr<Effect> effect)
		: legacyEffect(std::move(effect))
	{
		indirect = legacyEffect->indirect;
		optional = legacyEffect->optional;
		name = legacyEffect->name;
		spellScope = legacyEffect->spellScope;
		spellIdentifier = legacyEffect->spellIdentifier;
	}

	bool disabledForV3(const Mechanics * mechanics) const
	{
		return mechanics && mechanics->usesNewHorizonsMagicV3();
	}

	void adjustTargetTypes(std::vector<TargetType> & types, const Mechanics * mechanics) const override
	{
		if(!disabledForV3(mechanics))
			legacyEffect->adjustTargetTypes(types, mechanics);
	}

	void adjustAffectedHexes(BattleHexArray & hexes, const Mechanics * mechanics, const Target & spellTarget) const override
	{
		if(!disabledForV3(mechanics))
			legacyEffect->adjustAffectedHexes(hexes, mechanics, spellTarget);
	}

	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		return !disabledForV3(mechanics) && legacyEffect->applicableGeneral(problem, mechanics);
	}

	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override
	{
		return !disabledForV3(mechanics) && legacyEffect->applicableTarget(problem, mechanics, target);
	}

	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		if(!disabledForV3(mechanics))
			legacyEffect->apply(server, mechanics, target);
	}

	Target filterTarget(const Mechanics * mechanics, const Target & target) const override
	{
		return disabledForV3(mechanics) ? Target{} : legacyEffect->filterTarget(mechanics, target);
	}

	Target transformTarget(const Mechanics * mechanics, const Target & aimPoint, const Target & spellTarget) const override
	{
		return disabledForV3(mechanics) ? Target{} : legacyEffect->transformTarget(mechanics, aimPoint, spellTarget);
	}

	SpellEffectValue getHealthChange(const Mechanics * mechanics, const Target & spellTarget) const override
	{
		return disabledForV3(mechanics) ? SpellEffectValue{} : legacyEffect->getHealthChange(mechanics, spellTarget);
	}

protected:
	void initImpl(JsonNode data) override
	{
		legacyEffect->init(std::move(data));
	}
};

class NewHorizonsPhysicalPoisonEffect final : public Effect
{
	std::shared_ptr<Effect> legacyEffect;

	bool enabledForHeroV3(const Mechanics * mechanics) const
	{
		if(!mechanics || !mechanics->getHeroCaster() || !mechanics->battle()
			|| !mechanics->battle()->getBattle())
			return false;
		return newHorizonsMagic::physicalPoisonEnabled(
			mechanics->battle()->getBattle()->getMagicRules(), mechanics->getSpellId());
	}

	static bool isLivingPhysicalTarget(const battle::Unit * unit)
	{
		return unit && unit->isValidTarget(false) && unit->alive()
			&& !unit->hasBonusOfType(BonusType::UNDEAD)
			&& !unit->hasBonusOfType(BonusType::NON_LIVING)
			&& !unit->hasBonusOfType(BonusType::MECHANICAL)
			&& !unit->hasBonusOfType(BonusType::SIEGE_WEAPON);
	}

public:
	explicit NewHorizonsPhysicalPoisonEffect(std::shared_ptr<Effect> effect)
		: legacyEffect(std::move(effect))
	{
		indirect = legacyEffect->indirect;
		optional = legacyEffect->optional;
		name = legacyEffect->name;
		spellScope = legacyEffect->spellScope;
		spellIdentifier = legacyEffect->spellIdentifier;
	}

	void adjustTargetTypes(std::vector<TargetType> & types, const Mechanics * mechanics) const override
	{
		legacyEffect->adjustTargetTypes(types, mechanics);
	}

	void adjustAffectedHexes(BattleHexArray & hexes, const Mechanics * mechanics, const Target & spellTarget) const override
	{
		legacyEffect->adjustAffectedHexes(hexes, mechanics, spellTarget);
	}

	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		return enabledForHeroV3(mechanics) || legacyEffect->applicableGeneral(problem, mechanics);
	}

	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override
	{
		return enabledForHeroV3(mechanics) ? !target.empty()
			: legacyEffect->applicableTarget(problem, mechanics, target);
	}

	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		if(!enabledForHeroV3(mechanics))
		{
			legacyEffect->apply(server, mechanics, target);
			return;
		}

		const int64_t baseDamage = newHorizonsMagic::poisonBaseDamageBasisPoints(
			mechanics->getEffectPower(), mechanics->getSpellPowerCoefficientBasisPoints(),
			mechanics->getEmpowerSpellBonusPercent(),
			newHorizonsMagic::poisonBaseBonusPercent(mechanics->getHeroCaster()));
		for(const auto & destination : target)
		{
			const auto * unit = destination.unitValue;
			if(!isLivingPhysicalTarget(unit))
				continue;

			auto state = unit->acquireState();
			if(!state)
				continue;
			const bool applied = newHorizonsBulwark::applyPhysicalPoison(state.get(), baseDamage, -1);
			if(applied)
			{
				BattleUnitsChanged changed;
				changed.battleID = mechanics->getBattleID();
				UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
				update.data = state->save();
				changed.changedStacks.push_back(std::move(update));
				server->apply(changed);
			}

			BattleLogMessage message;
			message.battleID = mechanics->getBattleID();
			MetaString line;
			if(applied)
			{
				line.appendRawString("Physical Poison afflicts %s for three activations, starting at ");
				line.appendNumber(baseDamage);
				line.appendRawString(" damage.");
			}
			else
				line.appendRawString("A stronger physical Poison already affects %s.");
			unit->addNameReplacement(line, unit->getCount());
			message.lines.push_back(std::move(line));
			server->apply(message);
		}
	}

	Target filterTarget(const Mechanics * mechanics, const Target & target) const override
	{
		return enabledForHeroV3(mechanics) ? target : legacyEffect->filterTarget(mechanics, target);
	}

	Target transformTarget(const Mechanics * mechanics, const Target & aimPoint, const Target & spellTarget) const override
	{
		return legacyEffect->transformTarget(mechanics, aimPoint, spellTarget);
	}

	SpellEffectValue getHealthChange(const Mechanics * mechanics, const Target & spellTarget) const override
	{
		return enabledForHeroV3(mechanics) ? SpellEffectValue{}
			: legacyEffect->getHealthChange(mechanics, spellTarget);
	}

protected:
	void initImpl(JsonNode data) override
	{
		legacyEffect->init(std::move(data));
	}
};

class NewHorizonsCurseEffect final : public Effect
{
	std::shared_ptr<Effect> legacyEffect;

	bool enabledForSavedV3(const Mechanics * mechanics) const
	{
		if(!mechanics || !mechanics->usesNewHorizonsMagicV3())
			return false;
		const auto * battle = mechanics->battle();
		return battle && battle->getBattle() && newHorizonsMagic::curseRulesEnabled(
			battle->getBattle()->getMagicRules(), mechanics->getSpellId());
	}

public:
	explicit NewHorizonsCurseEffect(std::shared_ptr<Effect> effect)
		: legacyEffect(std::move(effect))
	{
		indirect = legacyEffect->indirect;
		optional = legacyEffect->optional;
		name = legacyEffect->name;
		spellScope = legacyEffect->spellScope;
		spellIdentifier = legacyEffect->spellIdentifier;
	}

	void adjustTargetTypes(std::vector<TargetType> & types, const Mechanics * mechanics) const override
	{
		legacyEffect->adjustTargetTypes(types, mechanics);
	}

	void adjustAffectedHexes(BattleHexArray & hexes, const Mechanics * mechanics, const Target & spellTarget) const override
	{
		legacyEffect->adjustAffectedHexes(hexes, mechanics, spellTarget);
	}

	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		return legacyEffect->applicableGeneral(problem, mechanics);
	}

	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override
	{
		return legacyEffect->applicableTarget(problem, mechanics, target);
	}

	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		if(!enabledForSavedV3(mechanics))
		{
			legacyEffect->apply(server, mechanics, target);
			return;
		}

		const auto * battle = mechanics->battle();
		const auto * hero = mechanics->getHeroCaster();
		const auto family = newHorizonsMagic::spellVariantBase(battle->getBattle()->getMagicRules(), mechanics->getSpellId());
		const auto duration = newHorizonsMagic::curseDurationRounds(
			battle->getBattle()->getMagicRules(), hero, mechanics->getSpellId());
		if(!duration)
		{
			legacyEffect->apply(server, mechanics, target);
			return;
		}

		for(const auto & destination : target)
		{
			const auto * stack = destination.unitValue;
			if(!stack || !stack->alive())
				continue;

			Bonus curse(BonusDuration::N_TURNS, BonusType::ALWAYS_MINIMUM_DAMAGE, BonusSource::SPELL_EFFECT,
				0, BonusSourceID(family));
			curse.turnsRemain = *duration;
			curse.description.appendRawString("Shadow's Curse");

			SetStackEffect effects;
			effects.battleID = mechanics->getBattleID();
			const auto previousCurse = stack->getBonuses(Selector::source(
				BonusSource::SPELL_EFFECT, BonusSourceID(family))
				.And(Selector::type()(BonusType::ALWAYS_MINIMUM_DAMAGE)));
			if(previousCurse && !previousCurse->empty())
			{
				std::vector<Bonus> previousBonuses;
				previousBonuses.reserve(previousCurse->size());
				for(const auto & previous : *previousCurse)
					if(previous)
						previousBonuses.push_back(*previous);
				if(!previousBonuses.empty())
					effects.toRemove.emplace_back(stack->unitId(), std::move(previousBonuses));
			}
			effects.toAdd.emplace_back(stack->unitId(), std::vector<Bonus>{std::move(curse)});
			server->apply(effects);

			BattleLogMessage message;
			message.battleID = mechanics->getBattleID();
			MetaString line;
			line.appendRawString("Curse forces %s to roll minimum creature damage for ");
			line.appendNumber(*duration);
			line.appendRawString(*duration == 1 ? " round." : " rounds.");
			stack->addNameReplacement(line, stack->getCount());
			message.lines.push_back(std::move(line));
			server->apply(message);
		}
	}

	Target filterTarget(const Mechanics * mechanics, const Target & target) const override
	{
		return legacyEffect->filterTarget(mechanics, target);
	}

	Target transformTarget(const Mechanics * mechanics, const Target & aimPoint, const Target & spellTarget) const override
	{
		return legacyEffect->transformTarget(mechanics, aimPoint, spellTarget);
	}

	SpellEffectValue getHealthChange(const Mechanics * mechanics, const Target & spellTarget) const override
	{
		return legacyEffect->getHealthChange(mechanics, spellTarget);
	}

protected:
	void initImpl(JsonNode data) override
	{
		legacyEffect->init(std::move(data));
	}
};

class NewHorizonsSorrowEffect final : public Effect
{
	std::shared_ptr<Effect> legacyEffect;

	bool enabledForSavedV3(const Mechanics * mechanics) const
	{
		if(!mechanics || !mechanics->usesNewHorizonsMagicV3())
			return false;
		const auto * battle = mechanics->battle();
		return battle && battle->getBattle() && newHorizonsMagic::sorrowRulesEnabled(
			battle->getBattle()->getMagicRules(), mechanics->getSpellId());
	}

public:
	explicit NewHorizonsSorrowEffect(std::shared_ptr<Effect> effect)
		: legacyEffect(std::move(effect))
	{
		indirect = legacyEffect->indirect;
		optional = legacyEffect->optional;
		name = legacyEffect->name;
		spellScope = legacyEffect->spellScope;
		spellIdentifier = legacyEffect->spellIdentifier;
	}

	void adjustTargetTypes(std::vector<TargetType> & types, const Mechanics * mechanics) const override
	{
		legacyEffect->adjustTargetTypes(types, mechanics);
	}

	void adjustAffectedHexes(BattleHexArray & hexes, const Mechanics * mechanics, const Target & spellTarget) const override
	{
		legacyEffect->adjustAffectedHexes(hexes, mechanics, spellTarget);
	}

	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		return legacyEffect->applicableGeneral(problem, mechanics);
	}

	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override
	{
		return legacyEffect->applicableTarget(problem, mechanics, target);
	}

	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		if(!enabledForSavedV3(mechanics))
		{
			legacyEffect->apply(server, mechanics, target);
			return;
		}

		const auto * battle = mechanics->battle();
		const auto * hero = mechanics->getHeroCaster();
		const auto family = newHorizonsMagic::spellVariantBase(battle->getBattle()->getMagicRules(), mechanics->getSpellId());
		const auto penalty = newHorizonsMagic::sorrowMoralePenalty(
			battle->getBattle()->getMagicRules(), hero, mechanics->getSpellId(), mechanics->getEffectPower(),
			mechanics->getWarcastingBonusPercent(), mechanics->getEmpowerSpellBonusPercent(),
			mechanics->getCastSpellPowerComponentBonusPercent());
		const auto duration = newHorizonsMagic::sorrowDurationRounds(
			battle->getBattle()->getMagicRules(), hero, mechanics->getSpellId());
		if(!penalty || !duration)
			return;

		for(const auto & destination : target)
		{
			const auto * stack = destination.unitValue;
			if(!stack || !stack->alive())
				continue;

			Bonus morale(BonusDuration::N_TURNS, BonusType::MORALE, BonusSource::SPELL_EFFECT,
				-*penalty, BonusSourceID(family));
			morale.turnsRemain = *duration;
			morale.description.appendRawString("Shadow's Sorrow");

			SetStackEffect effects;
			effects.battleID = mechanics->getBattleID();
			const auto previousSorrow = stack->getBonuses(Selector::source(
				BonusSource::SPELL_EFFECT, BonusSourceID(family))
				.And(Selector::type()(BonusType::MORALE)));
			if(previousSorrow && !previousSorrow->empty())
			{
				std::vector<Bonus> previousBonuses;
				previousBonuses.reserve(previousSorrow->size());
				for(const auto & previous : *previousSorrow)
					if(previous)
						previousBonuses.push_back(*previous);
				if(!previousBonuses.empty())
					effects.toRemove.emplace_back(stack->unitId(), std::move(previousBonuses));
			}
			effects.toAdd.emplace_back(stack->unitId(), std::vector<Bonus>{std::move(morale)});
			server->apply(effects);

			BattleLogMessage message;
			message.battleID = mechanics->getBattleID();
			MetaString line;
			line.appendRawString("Sorrow reduces %s Morale by ");
			line.appendNumber(*penalty);
			line.appendRawString(" for ");
			line.appendNumber(*duration);
			line.appendRawString(*duration == 1 ? " round." : " rounds.");
			stack->addNameReplacement(line, stack->getCount());
			message.lines.push_back(std::move(line));
			server->apply(message);
		}
	}

	Target filterTarget(const Mechanics * mechanics, const Target & target) const override
	{
		return legacyEffect->filterTarget(mechanics, target);
	}

	Target transformTarget(const Mechanics * mechanics, const Target & aimPoint, const Target & spellTarget) const override
	{
		return legacyEffect->transformTarget(mechanics, aimPoint, spellTarget);
	}

	SpellEffectValue getHealthChange(const Mechanics * mechanics, const Target & spellTarget) const override
	{
		return legacyEffect->getHealthChange(mechanics, spellTarget);
	}

protected:
	void initImpl(JsonNode data) override
	{
		legacyEffect->init(std::move(data));
	}
};

bool disabledForV3(const Effect * effect, const Mechanics * mechanics)
{
	const auto * legacyIceBoltEffect = dynamic_cast<const LegacyIceBoltSpeedEffect *>(effect);
	return legacyIceBoltEffect && legacyIceBoltEffect->disabledForV3(mechanics);
}
}

bool Effects::applicable(Problem & problem, const Mechanics * m) const
{
	//stop on first problem
	//require all not optional effects to be applicable in general
	//f.e. FireWall damage effect also need to have smart target

	bool requiredEffectNotBlocked = true;
	bool oneEffectApplicable = false;

	auto callback = [&](const Effect * e, bool & stop)
	{
		if(disabledForV3(e, m))
			return;

		if(e->applicableGeneral(problem, m))
		{
			oneEffectApplicable = true;
		}
		else if(!e->optional)
		{
			requiredEffectNotBlocked = false;
			stop = true;
		}
	};

	forEachEffect(m->getEffectLevel(), callback);

	return requiredEffectNotBlocked && oneEffectApplicable;
}

bool Effects::applicable(Problem & problem, const Mechanics * m, const Target & aimPoint, const Target & spellTarget) const
{
	//stop on first problem
	//require all direct and not optional effects to be applicable at this aimPoint
	//f.e. FireWall do not need damage target here, only a place to put obstacle

	bool requiredEffectNotBlocked = true;
	bool oneEffectApplicable = false;

	auto callback = [&](const Effect * e, bool & stop)
	{
		if(disabledForV3(e, m))
			return;

		if(e->indirect)
			return;

		Target target = e->transformTarget(m, aimPoint, spellTarget);

		if(e->applicableTarget(problem, m, target))
		{
			oneEffectApplicable = true;
		}
		else if(!e->optional)
		{
			requiredEffectNotBlocked = false;
			stop = true;
		}
	};

	forEachEffect(m->getEffectLevel(), callback);

	return requiredEffectNotBlocked && oneEffectApplicable;
}

void Effects::forEachEffect(const int level, const std::function<void(const Effect *, bool &)> & callback) const
{
	bool stop = false;
	for(const auto& one : data.at(level))
	{
		callback(one.second.get(), stop);
		if(stop)
			return;
	}
}

Effects::EffectsToApply Effects::prepare(const Mechanics * m, const Target & aimPoint, const Target & spellTarget) const
{
	EffectsToApply effectsToApply;

	auto callback = [&](const Effect * e, bool & stop)
	{
		if(disabledForV3(e, m))
			return;

		bool applyThis = false;

		//todo: find a better way to handle such special cases

		if(m->getSpellIndex() == SpellID::RESURRECTION && e->name == "cure")
			applyThis = (m->caster->getHeroCaster() == nullptr);
		else
			applyThis = !e->indirect;

		if(applyThis)
		{
			Target target = e->transformTarget(m, aimPoint, spellTarget);
			effectsToApply.push_back(std::make_pair(e, target));
		}
	};

	forEachEffect(m->getEffectLevel(), callback);

	return effectsToApply;
}

Effects::EffectsMap Effects::loadJson(const JsonNode & effectMap, const std::string & spellScope, const std::string & spellIdentifier)
{
	EffectsMap result;

	for(const auto & [name, raw] : effectMap.Struct())
	{
		const auto rawType = raw["type"].String();
		JsonNode data = raw;
		std::shared_ptr<Effect> effect;
		if(rawType == "core:battleForm")
			effect = std::make_shared<BattleFormEffect>();
		else
		{
			auto identifier = LIBRARY->identifiers()->getIdentifier("script", raw["type"]);

			if(!identifier.has_value())
			{
				logMod->error("Spell '%s:%s' uses unknown script '%s' as effect '%s'!", spellScope, spellIdentifier, rawType, name);
				continue;
			}

			ScriptID effectID(*identifier);

			if(LIBRARY->scriptTypes()->getById(effectID).kind != ScriptKind::SPELL_EFFECT)
			{
				logMod->error("Spell '%s:%s' uses script '%s' as effect '%s', but that script is not a spell effect!", spellScope, spellIdentifier, rawType, name);
				continue;
			}

			LIBRARY->scriptTypes()->prepareParameters(effectID, data, TextIdentifier("spell", spellScope, spellIdentifier, name));

			effect = LIBRARY->scriptTypes()->createSpellEffect(effectID);

			if(!effect)
				continue; // reported by the handler
		}

		effect->name = name;
		effect->spellScope = spellScope;
		effect->spellIdentifier = spellIdentifier;
		effect->init(std::move(data));
		if(spellScope == "core" && spellIdentifier == "iceBolt" && name == "speedDebuff")
			effect = std::make_shared<LegacyIceBoltSpeedEffect>(std::move(effect));
		else if(spellScope == "new-horizons" && spellIdentifier == "poison" && name == "poisoning")
			effect = std::make_shared<NewHorizonsPhysicalPoisonEffect>(std::move(effect));
		else if(((spellScope == "core" && spellIdentifier == "curse")
			|| (spellScope == "new-horizons" && spellIdentifier == "massCurse"))
			&& (name == "timed" || name == "alwaysMinimumDamage"))
			effect = std::make_shared<NewHorizonsCurseEffect>(std::move(effect));
		else if(((spellScope == "core" && spellIdentifier == "sorrow")
			|| (spellScope == "new-horizons" && spellIdentifier == "massSorrow"))
			&& (name == "morale" || name == "timed"))
			effect = std::make_shared<NewHorizonsSorrowEffect>(std::move(effect));

		result.try_emplace(name, std::move(effect));
	}

	return result;
}

}
}
