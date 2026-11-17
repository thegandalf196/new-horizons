/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../bonuses/Bonus.h"
#include <memory>
#include <string>

namespace newHorizonsHeroes::skillSpecialtyMarker
{
inline std::string key(HeroTypeID hero, SecondarySkill skill)
{
	return "new-horizons:skill-specialty:" + std::to_string(hero.getNum()) + ":"
		+ std::to_string(skill.getNum());
}

inline std::shared_ptr<Bonus> make(HeroTypeID hero, SecondarySkill skill)
{
	auto marker = std::make_shared<Bonus>();
	marker->type = BonusType::NONE;
	marker->duration = BonusDuration::PERMANENT;
	marker->source = BonusSource::HERO_SPECIAL;
	marker->sid = BonusSourceID(hero);
	marker->stacking = key(hero, skill);
	return marker;
}

/// Exact saved inert conversion tag, never a general NONE-bonus exemption.
inline bool matches(const Bonus & marker, HeroTypeID hero, SecondarySkill skill)
{
	return marker.type == BonusType::NONE && marker.source == BonusSource::HERO_SPECIAL
		&& marker.sid == BonusSourceID(hero) && marker.stacking == key(hero, skill)
		&& marker.duration == BonusDuration::PERMANENT && marker.val == 0 && marker.turnsRemain == 0
		&& marker.subtype == BonusSubtypeID() && marker.valType == BonusValueType::ADDITIVE_VALUE
		&& marker.targetSourceType == BonusSource::OTHER && marker.effectRange == BonusLimitEffect::NO_LIMIT
		&& !marker.parameters && !marker.limiter && !marker.propagator && !marker.updater && !marker.propagationUpdater
		&& marker.customIconPath.empty() && marker.description.empty() && !marker.hidden
		&& marker.bonusOwner == PlayerColor::CANNOT_DETERMINE && !marker.appliedByEnemy
		&& marker.spellCasterOwner == PlayerColor::CANNOT_DETERMINE
		&& !marker.hasStatusMetadata() && !marker.abilityDescriptor;
}
}
