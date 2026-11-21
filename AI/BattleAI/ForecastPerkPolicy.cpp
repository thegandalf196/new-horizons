/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastPerkPolicy.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/entities/hero/CompiledPerkCatalogue.h"
#include <array>
#include <algorithm>
#include <string_view>
namespace forecast
{
namespace
{
struct NativeFamily { std::string_view skill, perk; };
// Explicit native semantics, not inference from effect descriptions. Authority
// and requirement discounts are captured through the shared Leadership kernel;
// static creature values remain shared native damage/passive calculations.
// Sorcery/Metamagic entries can act only at separately admitted cast boundaries.
// Unknown/reactive entries do not gain admission because a sibling is numeric.
constexpr NativeFamily families[] = {
	{"new-horizons:authority", "new-horizons:authority.massedRanks"},
	{"new-horizons:authority", "new-horizons:authority.veteranCadre"},
	{"new-horizons:authority", "new-horizons:authority.familiarStandards"},
	{"new-horizons:authority", "new-horizons:authority.foreignLegion"},
	{"new-horizons:authority", "new-horizons:authority.officerCorps"},
	{"new-horizons:authority", "new-horizons:authority.missileCommand"},
	{"new-horizons:authority", "new-horizons:authority.regimentalOrganization"},
	{"new-horizons:authority", "new-horizons:authority.combinedArms"},
	{"new-horizons:authority", "new-horizons:authority.grandMarshal"},
	{"new-horizons:authority", "new-horizons:authority.masterOfLegions"},
	{"new-horizons:necromancy", "new-horizons:necromancy.graveKnowledge"},
	{"new-horizons:necromancy", "new-horizons:necromancy.darkConversion"},
	{"new-horizons:necromancy", "new-horizons:necromancy.blackHarvest"},
	{"new-horizons:necromancy", "new-horizons:necromancy.corpsePreservation"},
	{"new-horizons:necromancy", "new-horizons:necromancy.masterOfBones"},
	{"new-horizons:necromancy", "new-horizons:necromancy.boneCollector"},
	{"new-horizons:necromancy", "new-horizons:necromancy.deathLord"},
	{"new-horizons:necromancy", "new-horizons:necromancy.ossuary"},
	{"new-horizons:necromancy", "new-horizons:necromancy.soulHarvester"},
	{"new-horizons:necromancy", "new-horizons:necromancy.lordOfTheDead"},
	{"new-horizons:elementalRebirth", "new-horizons:elementalRebirth.elementalAttunement"},
	{"new-horizons:elementalRebirth", "new-horizons:elementalRebirth.swiftRebirth"},
	{"new-horizons:elementalRebirth", "new-horizons:elementalRebirth.elementalConduit"},
	{"new-horizons:elementalRebirth", "new-horizons:elementalRebirth.primalBurst"},
	{"new-horizons:elementalRebirth", "new-horizons:elementalRebirth.greaterEssence"},
	{"new-horizons:elementalRebirth", "new-horizons:elementalRebirth.adaptiveElement"},
	{"new-horizons:elementalRebirth", "new-horizons:elementalRebirth.rebirthChain"},
	{"new-horizons:elementalRebirth", "new-horizons:elementalRebirth.elementalCataclysm"},
	{"new-horizons:elementalRebirth", "new-horizons:elementalRebirth.phoenixSpark"},
	{"new-horizons:elementalRebirth", "new-horizons:elementalRebirth.perfectConvergence"},
	{"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.overcharger"},
	{"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.selectiveDispel"},
	{"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.temporalist"},
	{"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.matterShaper"},
	{"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.teleporter"},
	{"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.countermage"},
	{"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.illusionist"},
	{"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.temporalField"},
	{"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.chronomancer"},
	{"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.spellbinder"},
	{"new-horizons:metamagic", "new-horizons:metamagic.conservedFormula"},
	{"new-horizons:metamagic", "new-horizons:metamagic.disruptiveCasting"},
	{"new-horizons:metamagic", "new-horizons:metamagic.spellSculpting"},
	{"new-horizons:metamagic", "new-horizons:metamagic.unravelingMagic"},
	{"new-horizons:metamagic", "new-horizons:metamagic.reconstitutingMagic"},
	{"new-horizons:metamagic", "new-horizons:metamagic.widenSpells"},
	{"new-horizons:metamagic", "new-horizons:metamagic.levitation"},
	{"new-horizons:metamagic", "new-horizons:metamagic.arcaneBallistics"},
	{"new-horizons:metamagic", "new-horizons:metamagic.arcaneCascade"},
	{"new-horizons:metamagic", "new-horizons:metamagic.empoweredSpells"},
	// Metamagic 6 (Double Cast): use counts, Crescendo and Spellstorm are native
	// reads of the battle ledger; Arcane Ward is native unit shield state. The
	// remaining completion effects run only at an authoritative double cast, which
	// a forecast never completes; the decision prices them separately.
	{"new-horizons:metamagic", "new-horizons:metamagic.arcaneWard"},
	{"new-horizons:metamagic", "new-horizons:metamagic.manaSiphon"},
	{"new-horizons:metamagic", "new-horizons:metamagic.deepReserves"},
	{"new-horizons:metamagic", "new-horizons:metamagic.formulaRecovery"},
	{"new-horizons:metamagic", "new-horizons:metamagic.arcaneCycle"},
	{"new-horizons:metamagic", "new-horizons:metamagic.afterglow"},
	{"new-horizons:metamagic", "new-horizons:metamagic.crescendo"},
	{"new-horizons:metamagic", "new-horizons:metamagic.cleansingWeave"},
	{"new-horizons:metamagic", "new-horizons:metamagic.grandWeaving"},
	{"new-horizons:metamagic", "new-horizons:metamagic.spellstorm"},
	{"new-horizons:offense", "new-horizons:offense.shockAssault"},
	{"new-horizons:offense", "new-horizons:offense.encirclement"},
	{"new-horizons:offense", "new-horizons:offense.executioner"},
	{"new-horizons:offense", "new-horizons:offense.armorPiercer"},
	{"new-horizons:offense", "new-horizons:offense.breakthrough"},
	{"new-horizons:armorer", "new-horizons:armorer.shieldMaster"},
	{"new-horizons:armorer", "new-horizons:armorer.ironDiscipline"},
	{"new-horizons:armorer", "new-horizons:armorer.countercharge"},
	{"new-horizons:armorer", "new-horizons:armorer.pavise"},
	{"new-horizons:armorer", "new-horizons:armorer.formationFighting"},
	{"new-horizons:armorer", "new-horizons:armorer.unyielding"},
	{"new-horizons:luck", "new-horizons:luck.luckyAim"},
};
}
bool ordinaryPerksSupported(const CGHeroInstance & hero)
{
	const auto * catalogue = hero.getPerkState().compiledCatalogue();
	if(!catalogue) return hero.getPerkState().getSelected().empty();
	std::array<newHorizonsHeroes::PerkId, std::size(families)> admitted;
	for(size_t index = 0; index < std::size(families); ++index)
		admitted[index] = catalogue->find(families[index].skill, families[index].perk);
	for(const auto & selected : hero.getPerkState().getSelected())
	{
		const auto id = catalogue->find(selected.skillId, selected.perkId);
		if(!id) return false;
		if(!hero.hasActivePerk(id)) continue; // Native current-rank semantics.
		if(std::ranges::find(admitted, id) == admitted.end()) return false;
	}
	return true;
}
}
