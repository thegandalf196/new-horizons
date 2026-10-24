/*
 * NewHorizonsRetainedSpecialtyArtwork.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsRetainedSpecialtyArtwork.h"
#include "NewHorizonsSpecialtyArtwork.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../spells/NewHorizonsMagic.h"
#include "../../spells/NewHorizonsSpellAvailability.h"
namespace newHorizonsHeroes
{
namespace
{
std::optional<std::string> resolveResourceSpecialtyArtwork(const CHero & hero,
	const CGHeroInstance * capturedHero)
{
	// A live hero's retained specialty is authoritative; installed prototype
	// bonuses are only the source for a detached starting preview.
	const auto & bonuses = capturedHero ? capturedHero->getExportedBonusList() : hero.specialty;
	for(const auto & bonus : bonuses)
	{
		if(!bonus || bonus->type != BonusType::GENERATE_RESOURCE
			|| bonus->source != BonusSource::HERO_SPECIAL || bonus->sid != BonusSourceID(hero.getId()))
			continue;
		const auto resource = bonus->subtype.as<GameResID>();
		if(!resource.hasValue())
			continue;
		if(const auto artwork = detail::resolvedSpecialtyArtwork(hero.getJsonKey(),
			detail::SpecialtyArtworkFamily::Resource, GameResID::encode(resource.getNum())))
			return artwork;
	}
	return std::nullopt;
}

std::optional<std::string> resolveMetamagicSpecialtyArtwork(const CHero & hero,
	const CGHeroInstance * capturedHero)
{
	if(hero.getJsonKey() != "core:halon")
		return std::nullopt;
	const auto & bonuses = capturedHero ? capturedHero->getExportedBonusList() : hero.specialty;
	for(const auto & bonus : bonuses)
		if(bonus && bonus->type == BonusType::METAMAGIC_USES_PER_COMBAT
			&& bonus->source == BonusSource::HERO_SPECIAL && bonus->sid == BonusSourceID(hero.getId())
			&& bonus->valType == BonusValueType::BASE_NUMBER && bonus->val == 1)
			return detail::resolvedSpecialtyArtwork(hero.getJsonKey(), detail::SpecialtyArtworkFamily::Bonus,
				BonusTypeID::encode(BonusTypeID(bonus->type).getNum()));
	return std::nullopt;
}

std::optional<std::string> resolveAuthoredMasterChainArtwork(const CHero & hero,
	const JsonNode & magic, const CGHeroInstance * capturedHero)
{
	if(hero.getJsonKey() != "core:solmyr" || !newHorizonsMagic::rulesActive(magic))
		return std::nullopt;
	const SpellID master(SpellID::decode("new-horizons:masterChainLightning"));
	if(!master.hasValue() || !hero.haveSpellBook || !hero.spells.contains(master)
		|| hero.excludedSpells.contains(master) || !hero.excludedSpells.contains(SpellID::CHAIN_LIGHTNING)
		|| !newHorizonsMagic::spellAllowedByHeroRoster(magic, master))
		return std::nullopt;
	// Authored access is independent of ordinary-acquisition eligibility. A live
	// hero must retain the actual inscription and physical book, not a prototype repair.
	if(capturedHero && (!capturedHero->hasSpellbook() || !capturedHero->spellbookContainsSpell(master)))
		return std::nullopt;
	return detail::resolvedSpecialtyArtwork(hero.getJsonKey(), detail::SpecialtyArtworkFamily::Spell,
		SpellID::encode(master.getNum()));
}
}
namespace detail
{
std::optional<std::string> resolveRetainedSpecialtyArtwork(const CHero & hero,
	const JsonNode & magic, const CGHeroInstance * capturedHero)
{
	if(const auto artwork = resolveMetamagicSpecialtyArtwork(hero, capturedHero))
		return artwork;
	if(const auto artwork = resolveAuthoredMasterChainArtwork(hero, magic, capturedHero))
		return artwork;
	return resolveResourceSpecialtyArtwork(hero, capturedHero);
}
}
}
