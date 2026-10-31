/*
 * Bonus.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "Bonus.h"
#include "BonusParameters.h"
#include "Limiters.h"
#include "Updaters.h"
#include "Propagators.h"

#include "../CBonusTypeHandler.h"
#include "../CCreatureHandler.h"
#include "../CSkillHandler.h"
#include "../TerrainHandler.h"
#include "../GameLibrary.h"
#include "../callback/IGameInfoCallback.h"
#include "../mapObjects/CGObjectInstance.h"
#include "../mapObjectConstructors/CObjectClassesHandler.h"
#include "../battle/BattleInfo.h"
#include "../battle/NewHorizonsConfusionControl.h"
#include "../battle/NewHorizonsRangefinder.h"
#include "../battle/NewHorizonsMaintenanceSpecialties.h"
#include "../battle/NewHorizonsShieldAndSword.h"
#include "../battle/NewHorizonsSepulchralGrip.h"
#include "../battle/NewHorizonsInfernoStrikeDebuffs.h"
#include "../battle/NewHorizonsChargeSpecialties.h"
#include "../battle/NewHorizonsCoordinatedMarks.h"
#include "../battle/NewHorizonsReactiveDefense.h"
#include "../battle/NewHorizonsCreatureBurn.h"
#include "../battle/NewHorizonsGnollHarriers.h"
#include "../battle/NewHorizonsPrismaticVanguard.h"
#include "../battle/NewHorizonsGraveDust.h"
#include "../constants/StringConstants.h"
#include "../entities/hero/CHero.h"
#include "../modding/ModUtility.h"
#include "../texts/CGeneralTextHandler.h"

void Bonus::validateConfusionPendingMarker() const
{
	if(type == BonusType::CONFUSION_PENDING)
		newHorizonsConfusionControl::validateMarker(*this);
}

std::string Bonus::Description(const IGameInfoCallback * cb, std::optional<si32> customValue) const
{
	MetaString descriptionHelper = description;
	auto valueToShow = customValue.value_or(val);

	if(descriptionHelper.empty())
	{
		// no custom description - try to generate one based on bonus source
		switch(source)
		{
			case BonusSource::ARTIFACT:
				descriptionHelper.appendName(sid.as<ArtifactID>());
				break;
			case BonusSource::SPELL_EFFECT:
				descriptionHelper.appendName(sid.as<SpellID>());
				break;
			case BonusSource::CREATURE_ABILITY:
				descriptionHelper.appendNamePlural(sid.as<CreatureID>());
				break;
			case BonusSource::SECONDARY_SKILL:
				descriptionHelper.appendTextID(sid.as<SecondarySkill>().toEntity(LIBRARY)->getNameTextID());
				break;
			case BonusSource::HERO_SPECIAL:
				descriptionHelper.appendTextID(sid.as<HeroTypeID>().toEntity(LIBRARY)->getNameTextID());
				break;
			case BonusSource::OBJECT_INSTANCE:
				const auto * object = cb->getObj(sid.as<ObjectInstanceID>());
				if (object)
					descriptionHelper.appendTextID(object->getObjectNameTextID());
		}
	}

	if(descriptionHelper.empty())
	{
		// still no description - try to generate one based on duration
		if ((duration & BonusDuration::ONE_BATTLE) != 0)
		{
			if (val > 0)
				descriptionHelper.appendTextID("core.arraytxt.110"); //+%d Temporary until next battle"
			else
				descriptionHelper.appendTextID("core.arraytxt.109"); //-%d Temporary until next battle"

			// erase sign - already present in description string
			valueToShow = std::abs(valueToShow);
		}
	}

	if(descriptionHelper.empty())
	{
		// still no description - generate placeholder one
		descriptionHelper.appendRawString("Unknown");
	}

	if(valueToShow != 0)
	{
		descriptionHelper.replacePositiveNumber(valueToShow);
		// there is one known string that uses '%s' placeholder for bonus value:
		// "core.arraytxt.69" : "\nFountain of Fortune Visited %s",
		// So also add string replacement to handle this case
		descriptionHelper.replaceRawString(std::to_string(valueToShow));

		if(type == BonusType::CREATURE_GROWTH_PERCENT)
			descriptionHelper.appendRawString(" +" + std::to_string(valueToShow));
	}

	return descriptionHelper.toString(LIBRARY->staticTexts());
}

JsonNode Bonus::toJsonNode() const
{
	if(!hasValidStatusMetadata())
		throw std::runtime_error("Invalid bonus status metadata");
	JsonNode root;
	if(appliedByEnemy)
		root["appliedByEnemy"].Bool() = true;
	if(spellCasterOwner != PlayerColor::CANNOT_DETERMINE)
		root["spellCasterOwner"].Integer() = spellCasterOwner.getNum();
	if(!statusTags.empty())
	{
		for(const auto tag : statusTags)
		{
			switch(tag)
			{
				case BonusStatusTag::DEBUFF:
					root["statusTags"].Vector().emplace_back("DEBUFF");
					break;
				case BonusStatusTag::NON_TRANSFERABLE:
					root["statusTags"].Vector().emplace_back("NON_TRANSFERABLE");
					break;
			}
		}
	}
	if(!statusIdentity.empty())
		root["statusIdentity"].String() = statusIdentity;
	// only add values that might reasonably be found in config files
	root["type"].String() = LIBRARY->bth->bonusToString(type);
	if(subtype != BonusSubtypeID())
		root["subtype"].String() = subtype.toString();
	if(parameters)
		root["addInfo"] = parameters->toJsonNode();
	if(source != BonusSource::OTHER)
		root["sourceType"].String() = vstd::findKey(bonusSourceMap, source);
	if(targetSourceType != BonusSource::OTHER)
		root["targetSourceType"].String() = vstd::findKey(bonusSourceMap, targetSourceType);
	if(sid != BonusSourceID())
		root["sourceID"].String() = sid.toString();
	if(val != 0)
		root["val"].Integer() = val;
	if(valType != BonusValueType::ADDITIVE_VALUE)
		root["valueType"].String() = vstd::findKey(bonusValueMap, valType);
	if(!stacking.empty())
		root["stacking"].String() = stacking;
	if(!description.empty())
		root["description"].String() = description.toString(LIBRARY->staticTexts());
	if(effectRange != BonusLimitEffect::NO_LIMIT)
		root["effectRange"].String() = vstd::findKey(bonusLimitEffect, effectRange);
	if(duration != BonusDuration::PERMANENT)
		root["duration"] = BonusDuration::toJson(duration);
	if(turnsRemain)
		root["turns"].Integer() = turnsRemain;
	if(limiter)
		root["limiters"] = limiter->toJsonNode();
	if(updater)
		root["updater"] = updater->toJsonNode();
	if(propagator)
		root["propagator"].String() = vstd::findKey(bonusPropagatorMap, propagator);
	if(hidden)
		root["hidden"].Bool() = hidden;
	return root;
}

void Bonus::convertAddInfo(const std::vector<int> & oldAddInfo)
{
	if (oldAddInfo.size() > 1)
		parameters = std::make_shared<BonusParameters>(oldAddInfo);

	if (oldAddInfo.size() == 1 && oldAddInfo[0] != -1)
	{
		if (type == BonusType::SPECIAL_UPGRADE || type == BonusType::UNUSED_TRANSMUTATION)
			parameters = std::make_shared<BonusParameters>(CreatureID(oldAddInfo[0]));
		else if (type == BonusType::UNUSED_DEATH_STARE)
			parameters = std::make_shared<BonusParameters>(SpellID(oldAddInfo[0]));
		else
			parameters = std::make_shared<BonusParameters>(oldAddInfo[0]);
	}
}

Bonus::Bonus(BonusDuration::Type Duration, BonusType Type, BonusSource Src, si32 Val, BonusSourceID ID)
	: Bonus(Duration, Type, Src, Val, ID, BonusSubtypeID())
{}

Bonus::Bonus(BonusDuration::Type Duration, BonusType Type, BonusSource Src, si32 Val, BonusSourceID ID, BonusSubtypeID Subtype):
	duration(Duration),
	type(Type),
	subtype(Subtype),
	source(Src),
	val(Val),
	sid(ID)
{
	targetSourceType = BonusSource::OTHER;
}

Bonus::Bonus(BonusDuration::Type Duration, BonusType Type, BonusSource Src, si32 Val, BonusSourceID ID, BonusSubtypeID Subtype, BonusValueType ValType):
	duration(Duration),
	type(Type),
	subtype(Subtype),
	source(Src),
	val(Val),
	sid(ID),
	valType(ValType)
{
	turnsRemain = 0;
	effectRange = BonusLimitEffect::NO_LIMIT;
	targetSourceType = BonusSource::OTHER;
}

Bonus::Bonus(const Bonus & inst, const BonusSourceID & sourceId)
	: Bonus(inst)
{
	// Preserve runtime provenance when only changing the source ID of a derived bonus.
	sid = sourceId;
}

std::shared_ptr<Bonus> Bonus::addPropagator(const TPropagatorPtr & Propagator)
{
	propagator = Propagator;
	return this->shared_from_this();
}

DLL_LINKAGE std::ostream & operator<<(std::ostream &out, const Bonus &bonus)
{
	out << "\tType: " << LIBRARY->bth->bonusToString(bonus.type) << " \t";

#define printField(field) out << "\t" #field ": " << (int)bonus.field << "\n"
	printField(val);
	out << "\tSubtype: " << bonus.subtype.toString() << "\n";
	printField(duration);
	printField(source);
	out << "\tSource ID: " << bonus.sid.toString() << "\n";
	if(bonus.parameters)
		out << "\taddInfo: " << bonus.parameters->toString() << "\n";
	printField(turnsRemain);
	printField(valType);
	if(!bonus.stacking.empty())
		out << "\tstacking: \"" << bonus.stacking << "\"\n";
	printField(effectRange);
#undef printField

	if(bonus.limiter)
		out << "\tLimiter: " << bonus.limiter->toString() << "\n";
	if(bonus.updater)
		out << "\tUpdater: " << bonus.updater->toString() << "\n";

	return out;
}

std::shared_ptr<Bonus> Bonus::addLimiter(const TLimiterPtr & Limiter)
{
	if (limiter)
	{
		auto newLimiterList = std::make_shared<AllOfLimiter>();
		auto oldLimiterList = std::dynamic_pointer_cast<const AllOfLimiter>(limiter);

		if(oldLimiterList)
			newLimiterList->limiters = oldLimiterList->limiters;

		newLimiterList->add(Limiter);
		limiter = newLimiterList;
	}
	else
	{
		limiter = Limiter;
	}
	return this->shared_from_this();
}

// Updaters

static TUpdaterPtr appendToUpdaters(const TUpdaterPtr & current, const TUpdaterPtr & added)
{
	if (!current)
		return added;

	auto newList = std::make_shared<CompositeUpdater>();
	auto oldList = std::dynamic_pointer_cast<const CompositeUpdater>(current);

	if(oldList)
		newList->updaters = oldList->updaters;

	newList->updaters.push_back(added);
	return newList;
}

std::shared_ptr<Bonus> Bonus::addUpdater(const TUpdaterPtr & newUpdater)
{
	updater = appendToUpdaters(updater, newUpdater);
	return this->shared_from_this();
}

std::shared_ptr<Bonus> Bonus::addPropagationUpdater(const TUpdaterPtr & newUpdater)
{
	propagationUpdater = appendToUpdaters(propagationUpdater, newUpdater);
	return this->shared_from_this();
}

namespace
{
void appendOwnedEffectBonuses(const BonusParameters * parameters, std::vector<const Bonus *> & pending)
{
	if(!parameters) return;
	const auto * event = parameters->combatEffects();
	if(!event) return;
	for(const auto & effect : event->effects)
		if(const auto * owned = std::get_if<BonusParametersOnCombatEvent::CombatEffectBonus>(&effect))
			if(owned->bonus) pending.push_back(owned->bonus.get());
}

bool isExtendedOrdinaryEffect(const Bonus * bonus)
{
	return newHorizonsInfernoStrikeDebuffs::isEffect(bonus)
		|| newHorizonsChargeSpecialties::isLanceFormation(bonus)
		|| newHorizonsCoordinatedMarks::isMark(bonus)
		|| newHorizonsReactiveDefense::isSolidarityProtection(bonus)
		|| newHorizonsReactiveDefense::isIronHideSpent(bonus)
		|| newHorizonsCreatureBurn::isBurn(bonus)
		|| newHorizonsGnollHarriers::isMovementHistory(bonus)
		|| newHorizonsPrismaticVanguard::isEffect(bonus)
		|| (bonus && newHorizonsGraveDust::validateRecord(*bonus));
}
void validateHealingReductionType(const Bonus & bonus, bool supported)
{
	if(!supported && (bonus.type == BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS
		|| bonus.subtype.as<BonusTypeID>().toEnum() == BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS))
		throw std::runtime_error("Unsupported incoming healing reduction bonus format");
}

void validateUnsupportedOwnedBonuses(const Bonus * root, const BonusParameters * parameters, BonusSerializationContext context)
{
	std::vector<const Bonus *> pending;
	std::set<const Bonus *> visited;
	if(root) pending.push_back(root);
	appendOwnedEffectBonuses(parameters, pending);
	while(!pending.empty())
	{
		const auto * bonus = pending.back();
		pending.pop_back();
		if(!visited.insert(bonus).second) continue;
		if(!context.adjacencyFiltersSupported
			&& (bonus->type == BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION
				|| bonus->subtype.as<BonusTypeID>().toEnum() == BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION))
			throw std::runtime_error("Unsupported target MDR penetration bonus format");
		if(newHorizonsRangefinder::isEnvelope(*bonus) && !context.ordinaryRangedSpecialtiesSupported)
			throw std::runtime_error("Unsupported Rangefinder envelope bonus format");
		if(newHorizonsMaintenanceSpecialties::isDefendRetaliationBonus(bonus)
			&& !context.ordinaryMaintenanceSpecialtiesSupported)
			throw std::runtime_error("Unsupported Ogre Bulwark bonus format");
		if(newHorizonsShieldAndSword::isProtection(bonus) && !context.ordinaryCombatSpecialtiesSupported)
			throw std::runtime_error("Unsupported Shield and Sword bonus format");
		if(newHorizonsSepulchralGrip::isEffect(bonus) && !context.ordinaryCombatSpecialtiesSupported)
			throw std::runtime_error("Unsupported Sepulchral Grip bonus format");
		if(bonus->isExtendedOrdinarySpecialtyEffect() && !context.extendedOrdinarySpecialtiesSupported)
			throw std::runtime_error("Unsupported extended ordinary specialty bonus format");
		validateHealingReductionType(*bonus, context.extendedOrdinarySpecialtiesSupported);
		if(bonus->limiter) bonus->limiter->validateBonusTypeSerialization(
			context.adjacencyFiltersSupported, context.extendedOrdinarySpecialtiesSupported);
		if(context.combatEventParametersSerialized)
			appendOwnedEffectBonuses(bonus->parameters.get(), pending);
	}
}
}

bool Bonus::isExtendedOrdinarySpecialtyEffect() const
{
	return isExtendedOrdinaryEffect(this);
}

void Bonus::validateAdjacencyCapabilitySerialization(BonusSerializationContext context) const
{
	// Supported owner preflights stay constant-time; the direct codec still validates reserved provenance.
	if(context.ordinaryRangedSpecialtiesSupported)
		newHorizonsRangefinder::isEnvelope(*this);
	if(context.ordinaryMaintenanceSpecialtiesSupported)
		newHorizonsMaintenanceSpecialties::isDefendRetaliationBonus(this);
	if(context.extendedOrdinarySpecialtiesSupported)
		isExtendedOrdinarySpecialtyEffect();
	validateHealingReductionType(*this, context.extendedOrdinarySpecialtiesSupported);
	if(context.ordinaryCombatSpecialtiesSupported)
	{
		newHorizonsShieldAndSword::isProtection(this);
		newHorizonsSepulchralGrip::isEffect(this);
	}
	if(context.adjacencyFiltersSupported && context.ordinaryRangedSpecialtiesSupported
		&& context.ordinaryMaintenanceSpecialtiesSupported && context.ordinaryCombatSpecialtiesSupported
		&& context.extendedOrdinarySpecialtiesSupported)
	{
		if(limiter) limiter->validateBonusTypeSerialization(true, context.extendedOrdinarySpecialtiesSupported);
		return;
	}
	validateUnsupportedOwnedBonuses(this, nullptr, context);
}

void BonusParameters::validateAdjacencyCapabilitySerialization(BonusSerializationContext context) const
{
	if(context.adjacencyFiltersSupported && context.ordinaryRangedSpecialtiesSupported
		&& context.ordinaryMaintenanceSpecialtiesSupported && context.ordinaryCombatSpecialtiesSupported
		&& context.extendedOrdinarySpecialtiesSupported) return;
	validateUnsupportedOwnedBonuses(nullptr, this, context);
}
