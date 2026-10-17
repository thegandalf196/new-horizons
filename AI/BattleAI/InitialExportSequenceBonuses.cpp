/*
 * InitialExportSequenceBonuses.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "InitialExportBattleSubject.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/NewHorizonsSwiftRebirth.h"
#include <stdexcept>

void InitialExportBattleSubject::addSequenceBonus(const HypotheticBattle & branch, uint32_t id,
	const std::vector<Bonus> & prepared, bool forceAdd)
{
	captureBranchValues(branch); // Source-count/placement inputs precede this ADD in replay.
	const auto index = occurrence(id);
	const auto & recipient = owned->unitAt(index);
	// This slice routes the actual generic Last Stand/Fortune originating calls,
	// NOT arbitrary spell/status effect specifications. Existing sequence admission
	// already excludes spell effects and these lifecycle/control mechanisms.
	// Ordinary callback/selector paths do not opt into this private subject.
	for(const auto & bonus : prepared)
	{
		if(bonus.source == BonusSource::SPELL_EFFECT || bonus.type == BonusType::PHYSICAL_AFFLICTION
			|| bonus.type == BonusType::CONFUSION_PENDING || bonus.type == BonusType::GUARDIAN_SPIRIT
			|| newHorizonsSwiftRebirth::isLifecycleMarker(bonus)
			|| (!forceAdd && (bonus.hasStatusMetadata() || recipient.hasBonusFrom(BonusSource::SPELL_EFFECT))))
			throw std::invalid_argument("Originating effect requires shared native refresh/control projection");
	}
	for(const auto & bonus : prepared)
		owned->add(owned->sourceAt(index), bonus);
	// Native generic UPDATE with no explicit status metadata and no effective
	// SPELL_EFFECT refresh candidate follows addNewBonus, including OTHER stance
	// entries. No refresh predicate or duration/value policy is duplicated here.
}
