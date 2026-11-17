/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CompiledMetamagicRules.h"
#include "CapturedPerkRules.h"
#include "CapturedRuleSet.h"
#include "CapturedRuleExecutionAudit.h"
#include "../GameLibrary.h"
#include "../spells/CSpell.h"
#include "../spells/CSpellHandler.h"
#include "../spells/NewHorizonsMagic.h"
#include "../spells/NewHorizonsMetamagicCompletion.h"
#include "../spells/NewHorizonsSpellAvailability.h"
#include "../battle/IBattleState.h"
#include "../mapObjects/CGHeroInstance.h"
#include <stdexcept>
#include <cmath>

namespace nativeRules
{
namespace
{
constexpr std::array<const char *, static_cast<size_t>(MetamagicPerk::Count)> names{
	"conservedFormula", "manaRecycling", "disruptiveCasting", "purgingFormula",
	"spellSculpting", "hexWeaving", "widenSpell", "matterbreaker", "magicalMemory", "arcaneRupture",
	"unravelingMagic", "reconstitutingMagic", "widenSpells", "levitation", "arcaneBallistics", "arcaneCascade", "empoweredSpells"};

MetamagicTargetShape cardinality(const JsonNode & node)
{
	MetamagicTargetShape result = MetamagicTargetShape::Single;
	if(node.isStruct())
		for(const auto & [key, child] : node.Struct())
		{
			if(key == "chainLength" && !child.isNull())
			{
				if(!child.isNumber() || !std::isfinite(child.Float()) || child.Float() < 0
					|| child.Float() > std::numeric_limits<int32_t>::max() || std::floor(child.Float()) != child.Float())
					return MetamagicTargetShape::Unsupported;
				if(child.Float() > 1) result = MetamagicTargetShape::Multiple;
			}
			if(key == "handOfFate" && !child.isNull())
			{
				if(!child.isBool()) return MetamagicTargetShape::Unsupported;
				if(child.Bool()) result = MetamagicTargetShape::Multiple;
			}
			const auto nested = cardinality(child);
			if(nested == MetamagicTargetShape::Unsupported) return nested;
			if(nested == MetamagicTargetShape::Multiple) result = nested;
		}
	if(node.isVector())
		for(const auto & child : node.Vector())
		{
			const auto nested = cardinality(child);
			if(nested == MetamagicTargetShape::Unsupported) return nested;
			if(nested == MetamagicTargetShape::Multiple) result = nested;
		}
	return result;
}
bool debuffPayload(const JsonNode & node)
{
	if(node.isStruct())
	{
		const auto & type = node["type"];
		if(type.isString() && (type.String() == "core:timed" || type.String() == "timed"
			|| type.String() == "newHorizonsConfusion" || type.String() == "newHorizonsPuppetMaster")) return true;
		for(const auto & [key, child] : node.Struct()) if(debuffPayload(child)) return true;
	}
	if(node.isVector()) for(const auto & child : node.Vector()) if(debuffPayload(child)) return true;
	return false;
}

bool debuffPayload(const CSpell & spell, int level)
{
	const auto & definition = spell.getLevelInfo(level);
	if(spell.hasBattleEffects()) return debuffPayload(definition.battleEffects);
	// Match ISpellMechanicsFactory's legacy fallback: nonempty effects, or
	// cumulativeEffects only when effects is absent, become core:timed.
	return !definition.effects.Struct().empty() || !definition.cumulativeEffects.Struct().empty();
}

size_t index(MetamagicPerk key)
{
	const auto value = static_cast<size_t>(key);
	if(value >= names.size())
		throw std::out_of_range("Invalid compiled Metamagic perk key");
	return value;
}
}

CompiledMetamagicRules::CompiledMetamagicRules(const CapturedPerkRules & owner)
	: capturedPolicy(newHorizonsMetamagic::resolutionPolicy(owner.document()))
	, capturedSkill(owner.catalogue().skill("new-horizons:metamagic"))
{
	NH_CAPTURE_RULE_WORK(MetamagicAdmission);
	if(!capturedPolicy)
		return;
	// Numeric registry identities are resolved once, after capture admission.
	// An unavailable external registry cannot confer a specialty by default.
	try { halon = HeroTypeID(HeroTypeID::decode("core:halon")); }
	catch(const std::exception &) { halon = HeroTypeID::NONE; }
	for(size_t entry = 0; entry < names.size(); ++entry)
	{
		perks[entry] = owner.catalogue().find("new-horizons:metamagic",
			std::string("new-horizons:metamagic.") + names[entry]);
		const auto * definition = owner.catalogue().definition(perks[entry]);
		available[entry] = definition && definition->active;
	}
}

const newHorizonsMetamagic::ResolutionPolicy * CompiledMetamagicRules::policy() const
{
	return capturedPolicy ? &*capturedPolicy : nullptr;
}

int CompiledMetamagicRules::removalChance(int liveRank, HeroTypeID casterType) const
{
	return capturedPolicy ? newHorizonsMetamagic::removalChancePercent(*capturedPolicy, liveRank,
		isHalon(casterType)) : 0;
}

int CompiledMetamagicRules::overflowPercent(int metamagicRank) const
{
	return capturedPolicy ? capturedPolicy->overflowPercentByRank[std::clamp(metamagicRank, 0, 3)] : 0;
}

int CompiledMetamagicRules::friendlyReductionPercent(int rank) const
{
	return capturedPolicy && rank >= 1 && rank <= 3 ? capturedPolicy->friendlyDamageReductionPercentByRank[rank] : 0;
}

int CompiledMetamagicRules::debuffRetryPercent(int rank) const
{
	return capturedPolicy && rank >= 1 && rank <= 3 ? capturedPolicy->debuffRetryChancePercentByRank[rank] : 0;
}

const newHorizonsHeroes::PerkId & CompiledMetamagicRules::perk(MetamagicPerk key) const
{
	return perks[index(key)];
}

bool CompiledMetamagicRules::supports(MetamagicPerk key) const
{
	return capturedPolicy && available[index(key)];
}

int64_t CompiledMetamagicRules::discharge(int64_t actuallyRemovedMana) const
{
	return capturedPolicy ? newHorizonsMetamagic::energyHitPoints(actuallyRemovedMana, capturedPolicy->damagePerMana) : 0;
}

int64_t CompiledMetamagicRules::restoration(int64_t actuallyRemovedMana) const
{
	return capturedPolicy ? newHorizonsMetamagic::energyHitPoints(actuallyRemovedMana, capturedPolicy->restorationPerMana) : 0;
}

int32_t CompiledMetamagicRules::recyclingRefund(bool currentlySelected, int64_t actuallyRemovedMana,
	int32_t actualPaid, int32_t alreadyRefunded, int32_t capacityRoom) const
{
	return currentlySelected && supports(MetamagicPerk::ManaRecycling)
		? newHorizonsMetamagic::recyclingRefund(actuallyRemovedMana, actualPaid, alreadyRefunded, capacityRoom) : 0;
}

int32_t CompiledMetamagicRules::conservedRefund(bool currentlySelected, int32_t actualPaid,
	int32_t alreadyRefunded, int32_t capacityRoom, bool validSingleTarget, bool completelyResisted) const
{
	return currentlySelected && supports(MetamagicPerk::ConservedFormula)
		? newHorizonsMetamagic::conservedFormulaRefund(actualPaid, alreadyRefunded, capacityRoom,
			validSingleTarget, completelyResisted) : 0;
}

CompiledMetamagicEnergy::CompiledMetamagicEnergy(const JsonNode & admittedMagic)
{
	NH_CAPTURE_RULE_WORK(MetamagicEnergyAdmission);
	if(!LIBRARY || !LIBRARY->spellh)
		return;
	definitions.resize(LIBRARY->spellh->objects.size());
	identities.resize(definitions.size());
	try { markerSkill = SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")); }
	catch(const std::exception &) { markerSkill = std::nullopt; }
	for(size_t entry = 0; entry < definitions.size(); ++entry)
	{
		if(!LIBRARY->spellh->objects[entry])
			continue;
		++sourceResolutions;
		const SpellID spell(static_cast<int32_t>(entry));
		try
		{
			const auto family = newHorizonsMagic::spellVariantBase(admittedMagic, spell);
			if(family.hasValue() && static_cast<size_t>(family.getNum()) < definitions.size()
				&& LIBRARY->spellh->objects[family.getNum()])
				identities[entry] = MetamagicSpellIdentity{family,
					LIBRARY->spellh->objects[entry]->isPositive(), LIBRARY->spellh->objects[entry]->isNegative()};
		}
		catch(const std::exception &) { /* Unknown identities cannot admit an enchantment marker. */ }
		if(!identities[entry])
			continue;
		const auto & actualSpell = *LIBRARY->spellh->objects[entry];
		for(int level = 0; level < 4; ++level)
		{
			identities[entry]->effectCardinalityByLevel[level] = cardinality(actualSpell.getLevelInfo(level).battleEffects);
			identities[entry]->singleTargetByLevel[level] = actualSpell.getTargetType() == spells::AimType::CREATURE
				&& actualSpell.getLevelInfo(level).range == std::vector<int>{0}
				&& identities[entry]->effectCardinalityByLevel[level] == MetamagicTargetShape::Single;
			identities[entry]->debuffByLevel[level] = actualSpell.isNegative() && !actualSpell.isDamage()
				&& debuffPayload(actualSpell, level);
		}
		const auto family = identities[entry]->family;
		const auto & definition = LIBRARY->spellh->objects[family.getNum()];
		const auto & costs = admittedMagic["spells"][definition->getJsonKey()]["costs"];
		if(!costs.isVector() || costs.Vector().size() != 4)
			continue;
		const auto & listedMana = costs.Vector().front();
		if(listedMana.getType() != JsonNode::JsonType::DATA_INTEGER || listedMana.Integer() <= 0
			|| listedMana.Integer() > std::numeric_limits<int32_t>::max())
			continue;
		definitions[entry] = newHorizonsMetamagic::EnergyDefinition{family,
			static_cast<int32_t>(listedMana.Integer()), definition->isMagical(),
			definition->isPositive(), definition->isNegative(), definition->isCommonHeroSpell()};
	}
}

std::shared_ptr<const CompiledMetamagicEnergy> CompiledMetamagicEnergy::compileRawBoundary(JsonNode capturedMagic)
{
	newHorizonsMagic::validateRules(capturedMagic);
	return std::make_shared<const CompiledMetamagicEnergy>(capturedMagic);
}

const newHorizonsMetamagic::EnergyDefinition * CompiledMetamagicEnergy::definition(SpellID spell) const
{
	if(!spell.hasValue() || static_cast<size_t>(spell.getNum()) >= definitions.size())
		return nullptr;
	const auto & entry = definitions[spell.getNum()];
	return entry ? &*entry : nullptr;
}

const MetamagicSpellIdentity * CompiledMetamagicEnergy::identity(SpellID spell) const
{
	if(!spell.hasValue() || static_cast<size_t>(spell.getNum()) >= identities.size())
		return nullptr;
	const auto & entry = identities[spell.getNum()];
	return entry ? &*entry : nullptr;
}

const newHorizonsMetamagic::EnergyDefinition * CompiledMetamagicEnergy::fuelDefinition(SpellID spell) const
{
	const auto * entry = definition(spell);
	return entry && entry->family.hasValue() && entry->listedMana > 0 && entry->magical && entry->commonHeroSpell
		? entry : nullptr;
}

const CompiledMetamagicRules * metamagicRules(const CGHeroInstance * hero)
{
	const auto owner = hero ? hero->getPerkState().capturedOwner() : nullptr;
	return owner ? &owner->metamagic() : nullptr;
}

int metamagicSkillRank(const CGHeroInstance * hero)
{
	const auto * compiled = metamagicRules(hero);
	const auto skill = compiled ? compiled->skill() : std::nullopt;
	return skill ? hero->getSecSkillLevel(*skill) : 0;
}

bool hasMetamagicPerk(const CGHeroInstance * hero, MetamagicPerk perk)
{
	const auto * compiled = metamagicRules(hero);
	return compiled && compiled->supports(perk) && hero->hasActivePerk(compiled->perk(perk));
}

std::shared_ptr<const CompiledMetamagicEnergy> captureMetamagicEnergy(const IBattleInfo & battle)
{
	const auto owner = battle.getCapturedMagicRules();
	const auto & source = battle.getMagicRules();
	if(owner && owner->kind() == CapturedRuleKind::WorldMagic && &owner->document() == &source
		&& owner->metamagicEnergy())
		return owner->metamagicEnergy();
	return CompiledMetamagicEnergy::compileRawBoundary(source);
}
}
