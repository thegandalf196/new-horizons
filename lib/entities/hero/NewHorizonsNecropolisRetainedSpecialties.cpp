/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsNecropolisRetainedSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../json/JsonNode.h"

namespace newHorizonsHeroes
{
namespace
{
const JsonNode * retainedNecropolisRow(const CGHeroInstance * hero, const char * identity, const char * name)
{
	if(!hero || !hero->getHeroType() || hero->getHeroType()->getJsonKey() != identity)
		return nullptr;
	const auto & development = hero->getPrimaryGrowthRules();
	if(!usesRules(development))
		return nullptr;
	const auto & rows = development["revisedSpecialties"];
	return rows.isStruct() && rows.Struct().contains(name) ? &rows[name] : nullptr;
}
int retainedNecropolisPercent(const JsonNode & row, const CGHeroInstance & hero,
	const char * base, const char * increment, const char * maximum)
{
	return static_cast<int>(std::min<int64_t>(row[maximum].Integer(), row[base].Integer()
		+ std::max(0, static_cast<int>(hero.level) - 1) * row[increment].Integer()));
}
}
bool bloodScentUsesPermille(const CGHeroInstance & hero)
{
	return retainedNecropolisRow(&hero, "core:vokial", "bloodScent") != nullptr;
}
int bloodScentCapPermille(const CGHeroInstance & hero)
{
	const auto * row = retainedNecropolisRow(&hero, "core:vokial", "bloodScent");
	return row ? retainedNecropolisPercent(*row, hero, "baseCapPermille", "capPermillePerLevel", "maximumCapPermille") : 1000;
}
int bloodScentCapPercent(const CGHeroInstance & hero)
{
	return bloodScentCapPermille(hero) / 10;
}
int soulHarvestPercent(const CGHeroInstance * hero)
{
	const auto * row = retainedNecropolisRow(hero, "core:vidomina", "soulHarvest");
	return row ? 100 + std::max(0, static_cast<int>(hero->level) - 1) * static_cast<int>((*row)["percentPerLevel"].Integer()) : 100;
}
int ancientOathsDiscountPercent(const CGHeroInstance * hero)
{
	const auto * row = retainedNecropolisRow(hero, "core:nimbus", "ancientOathsDiscount");
	return row ? retainedNecropolisPercent(*row, *hero, "basePercent", "percentPerLevel", "maximumPercent") : 0;
}
}
