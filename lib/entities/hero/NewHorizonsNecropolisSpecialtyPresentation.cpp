/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsNecropolisSpecialtyPresentation.h"
#include "NewHorizonsNecropolisCombatSpecialties.h"
#include "NewHorizonsHoardedPower.h"
#include "NewHorizonsGhostSpecialty.h"
#include "NewHorizonsHeroRules.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../json/JsonNode.h"

namespace newHorizonsHeroes
{
namespace
{
std::string necropolisPermilleText(int value)
{
	return std::to_string(value / 10) + (value % 10 ? "." + std::to_string(value % 10) : "") + "%";
}
bool necropolisCombatPresentation(const std::string & key, const NecropolisCombatSpecialtyRules & rules,
	int level, SpecialtyPresentation & result)
{
	if(key == "core:septienna" && rules.residualLife)
	{
		const auto & row = *rules.residualLife;
		const int threshold = std::max(row.minimumDamagePerBuffer,
			row.damagePerBuffer - row.damagePerBufferReductionPerLevel * level);
		result.name = "Residual Life";
		result.description = "Septienna gains 1 Buffer Spell Point per " + std::to_string(threshold)
			+ " HP of damage dealt by her spells, up to " + std::to_string(row.maximumPerCombat)
			+ " Buffer Spell Points per combat. The damage required falls by "
			+ std::to_string(row.damagePerBufferReductionPerLevel) + " per hero level, to a minimum of "
			+ std::to_string(row.minimumDamagePerBuffer) + " HP.";
	}
	else if(key == "core:aislinn" && rules.earthboundDrain)
	{
		result.name = "Earthbound Drain";
		result.spell = SpellID(SpellID::decode("new-horizons:lifeDrain"));
		result.description = "Aislinn's Life Drain gains " + std::to_string(rules.earthboundDrain->at(level))
			+ "% more damage and healing. The bonus grows by "
			+ std::to_string(rules.earthboundDrain->perLevel) + "% per hero level, up to "
			+ std::to_string(rules.earthboundDrain->maximum) + "%. She starts with Life Drain.";
	}
	else if(key == "core:thant" && rules.eternalServitude)
	{
		const auto & row = *rules.eternalServitude;
		result.name = "Eternal Servitude";
		result.spell = SpellID(SpellID::decode("new-horizons:reanimate"));
		result.description = std::to_string(row.at(level)) + "% of the creatures Thant's Re-animate restores "
			"remain permanently if their stack survives combat. Starts at " + std::to_string(row.base)
			+ "% at hero level 1, plus " + std::to_string(row.perLevel) + "% per further level, up to "
			+ std::to_string(row.maximum) + "%.";
	}
	else if(key == "core:xsi" && rules.unyieldingBones)
	{
		const auto & row = *rules.unyieldingBones;
		result.name = "Unyielding Bones";
		result.description = "Xsi's army takes " + necropolisPermilleText(row.at(level))
			+ " less physical damage, combining with Armorer. Starts at " + necropolisPermilleText(row.base)
			+ " at hero level 1, plus " + necropolisPermilleText(row.perLevel) + " per further level, up to "
			+ necropolisPermilleText(row.maximum) + ".";
	}
	else
		return false;
	return true;
}

bool necropolisRetainedPresentation(const CHero & hero, const JsonNode & development,
	int level, SpecialtyPresentation & result)
{
	const auto key = hero.getJsonKey();
	const auto & rows = development["revisedSpecialties"];
	if(!rows.isStruct())
		return false;
	if(key == "core:vokial" && rows.Struct().contains("bloodScent"))
	{
		const auto & row = rows["bloodScent"];
		const int cap = static_cast<int>(std::min<int64_t>(row["maximumCapPermille"].Integer(),
			row["baseCapPermille"].Integer() + (level - 1) * row["capPermillePerLevel"].Integer()));
		result.name = "Blood Scent";
		result.creature = CreatureID::VAMPIRE;
		result.description = "Vampires and Vampire Lords gain primary melee damage equal to a living target's "
			"percentage of creatures lost from its battle-start native count, capped at +" + necropolisPermilleText(cap)
			+ ". Wounds alone do not count, restoration lowers the deficit, and native life drain is unchanged. "
			"The cap grows by " + necropolisPermilleText(static_cast<int>(row["capPermillePerLevel"].Integer()))
			+ " per further hero level, up to +" + necropolisPermilleText(static_cast<int>(row["maximumCapPermille"].Integer())) + ".";
	}
	else if(key == "core:vidomina" && rows.Struct().contains("soulHarvest")
		&& resolveGhostSpecialtyPresentation(hero, development, result))
	{
		const int growth = static_cast<int>(rows["soulHarvest"]["percentPerLevel"].Integer());
		result.description += " The total extra Ghosts from each attack grow by " + std::to_string(growth)
			+ "% per further hero level, currently +" + std::to_string(growth * (level - 1))
			+ "%. Fractions round down after counting the creatures killed by that attack.";
	}
	else if(key == "core:nimbus" && rows.Struct().contains("ancientOathsDiscount"))
	{
		const auto & row = rows["ancientOathsDiscount"];
		const int discount = static_cast<int>(std::min<int64_t>(row["maximumPercent"].Integer(),
			row["basePercent"].Integer() + (level - 1) * row["percentPerLevel"].Integer()));
		result.name = "Ancient Oaths";
		result.description = "While Seth visits or garrisons an owned Necropolis with an upgraded Tomb of Souls, "
			"Royal Mummies share its Wraith recruitment stock. The upgraded Tomb can also upgrade Mummies to Royal Mummies. "
			"Royal Mummies recruited by Seth cost " + std::to_string(discount)
			+ "% less. The discount grows by " + std::to_string(row["percentPerLevel"].Integer())
			+ "% per further hero level, up to " + std::to_string(row["maximumPercent"].Integer()) + "%.";
	}
	else
		return false;
	return true;
}
}

bool resolveNecropolisSpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * captured, SpecialtyPresentation & result)
{
	if(!usesRules(development))
		return false;
	const int level = captured ? std::max(1, static_cast<int>(captured->level)) : 1;
	if(hero.getJsonKey() == "core:nagash")
	{
		if(const auto row = hoardedPowerRules(development))
		{
			const int cap = std::min(row->maximumCapPercent, row->baseCapPercent + (level - 1) * row->capPercentPerLevel);
			result.name = "Hoarded Power";
			result.description = "Fixed Mana recovery that would overflow Nagash's maximum becomes Buffer Spell Points. "
				"Includes daily recovery, Mysticism, Soul Drinker and other fixed recovery after victory. "
				"Refilling to maximum grants no Buffer. Magic Spring and Arcane Reservoir keep their own grants. "
				"His total Buffer from every source is capped at " + std::to_string(cap)
				+ "% of maximum Mana, growing by " + std::to_string(row->capPercentPerLevel)
				+ "% per further hero level to at most " + std::to_string(row->maximumCapPercent) + "%.";
			result.summary = result.description;
			return true;
		}
	}
	if(!necropolisCombatPresentation(hero.getJsonKey(), necropolisCombatSpecialtyRules(development), level, result)
		&& !necropolisRetainedPresentation(hero, development, level, result))
		return false;
	result.summary = result.description;
	return true;
}
}
