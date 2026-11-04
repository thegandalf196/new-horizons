/* PlacementPower.cpp, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "PlacementPower.h"
#include "Mechanics.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/spells/NewHorizonsPlacementPower.h"
namespace scripting::api
{
namespace
{
int64_t currentVerdantPrisonPower(const ::spells::Mechanics & m)
{
	const auto * hero = m.getHeroCaster();
	return m.adjustMetamagicMagnitude(newHorizonsMagic::verdantPrisonHitPoints(m.getEffectPower(), m.getSpellPowerCoefficientBasisPoints(), m.getEmpowerSpellBonusPercent(),
		hero && hero->hasActivePerk("new-horizons:natureMagic", "new-horizons:natureMagic.verdantWarden")), nullptr);
}
int64_t currentTransfigurePower(const ::spells::Mechanics & m, int32_t footprint)
{
	const auto * hero = m.getHeroCaster();
	return m.adjustMetamagicMagnitude(newHorizonsMagic::transfigureHitPoints(m.getEffectPower(), m.getSpellPowerCoefficientBasisPoints(), m.getEmpowerSpellBonusPercent(), footprint,
		hero && hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.matterShaper")), nullptr);
}
int64_t currentEarthquakeStructuralPower(const ::spells::Mechanics & m)
{
	const auto * hero = m.getHeroCaster();
	return m.adjustMetamagicMagnitude(newHorizonsMagic::earthquakeStructuralDamage(m.getNewHorizonsEarthquakeParameter("structuralDamage"),
		hero && hero->hasActivePerk("new-horizons:natureMagic", "new-horizons:natureMagic.geomancer")), nullptr);
}
}
void registerPlacementPowerMethods(MethodRegistrar & R)
{
	R.function<&currentVerdantPrisonPower>("getVerdantPrisonHitPoints", {}, "Shared HP pool before ring allocation.");
	R.function<&currentTransfigurePower>("getTransfigureHitPoints", {{"footprint", "Selected obstacle footprint size."}}, {},
		"Shared HP pool from the actual selected footprint; no placement or creature count is guessed.");
	R.function<&currentEarthquakeStructuralPower>("getEarthquakeStructuralDamageBase", {}, "Shared structural damage before resolved Luck.");
	R.method<&::spells::Mechanics::usesNewHorizonsEarthquake>("usesNewHorizonsEarthquake", {},
		"True only when the saved-v3 Earthquake row explicitly enables its selected-area and field rules.");
	R.method<&::spells::Mechanics::getNewHorizonsEarthquakeParameter>("getNewHorizonsEarthquakeParameter",
		{{"name", "Validated saved Earthquake parameter name."}}, {},
		"Returns a parameter from the saved Earthquake row, or zero outside the opted-in profile.");
	R.method<&::spells::Mechanics::getNewHorizonsEarthquakeSectionCount>("getNewHorizonsEarthquakeSectionCount", {},
		"Returns the saved Earthquake section count with composed Spell Power scaling and its cap.");
}
}
