/*
 * HeroCommandState.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCommand.h"

void HeroOrderState::validateShape() const
{
	if(revision)
	{
		revision->validate(command, issuedRound);
		if(revision->crown.has_value() != !crownAndAltarRecipientUnitIds.empty())
			throw std::runtime_error("Crown and Altar revised potency has no matching recipient provenance");
		if(command == HeroCommand::VANGUARD
			&& !std::includes(revision->recipientUnitIds.begin(), revision->recipientUnitIds.end(),
				crownAndAltarRecipientUnitIds.begin(), crownAndAltarRecipientUnitIds.end()))
			throw std::runtime_error("Crown and Altar Vanguard recipient is outside the original snapshot");
		if(primaryTargetUnitId != INVALID_UNIT_ID || secondaryTargetUnitId != INVALID_UNIT_ID
			|| !anchors.empty() || !flankTargets.empty())
			throw std::runtime_error("Targetless revised Order contains targeted state");
	}
	if((command == HeroCommand::FEINT || command == HeroCommand::VANGUARD) != revision.has_value())
		throw std::runtime_error("Missing or unexpected revised Order snapshot");
	if(command == HeroCommand::VANGUARD && (ironWillLifetime || !divineDisciplineRecipientUnitIds.empty()))
		throw std::runtime_error("Vanguard cannot carry round-only Order lifetime extensions");
	if(ironWillLifetime && (command == HeroCommand::SECOND_WIND || divineDisciplineRecipientUnitIds.empty()))
		throw std::runtime_error("Invalid Iron Will Order carry");
	const auto validIds = [](const auto & ids)
	{
		return std::is_sorted(ids.begin(), ids.end())
			&& std::adjacent_find(ids.begin(), ids.end()) == ids.end()
			&& std::all_of(ids.begin(), ids.end(), [](uint32_t id)
				{ return id <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max()); });
	};
	if(!validIds(crownAndAltarRecipientUnitIds) || crownAndAltarFocusFirePercent < 0
		|| crownAndAltarFocusFirePercent > 200
		|| crownAndAltarHoldReductionBasisPoints > MAX_HOLD_MAGICAL_REDUCTION_BASIS_POINTS
		|| crownAndAltarHoldReductionBasisPoints % BASIS_POINTS_PER_PHYSICAL_PERCENT != 0
		|| (crownAndAltarFocusFirePercent != 0 && command != HeroCommand::FOCUS_FIRE)
		|| (crownAndAltarHoldReductionBasisPoints != 0 && command != HeroCommand::HOLD_THE_LINE)
		|| (crownAndAltarRecipientUnitIds.empty()
			&& (crownAndAltarFocusFirePercent != 0 || crownAndAltarHoldReductionBasisPoints != 0)))
		throw std::runtime_error("Invalid Crown and Altar recipient components");
	if(!validIds(divineDisciplineRecipientUnitIds) || !validIds(divineDisciplineCompletedUnitIds)
		|| !std::includes(divineDisciplineRecipientUnitIds.begin(), divineDisciplineRecipientUnitIds.end(),
			divineDisciplineCompletedUnitIds.begin(), divineDisciplineCompletedUnitIds.end()))
		throw std::runtime_error("Invalid Divine Discipline recipient lifetime");
	const auto maxWireId = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
	if(!std::is_sorted(royalStandardRecipientUnitIds.begin(), royalStandardRecipientUnitIds.end())
		|| std::adjacent_find(royalStandardRecipientUnitIds.begin(), royalStandardRecipientUnitIds.end())
			!= royalStandardRecipientUnitIds.end()
		|| std::any_of(royalStandardRecipientUnitIds.begin(), royalStandardRecipientUnitIds.end(),
			[maxWireId](uint32_t id) { return id > maxWireId; }))
		throw std::runtime_error("Invalid Royal Standard recipient identities");
	if(command == HeroCommand::NONE || issuedRound < 1
		|| warcastingBonusPercent < 0 || warcastingBonusPercent > 100
		|| (sacredCommandEfficiencyBonusPercent != 0 && sacredCommandEfficiencyBonusPercent != 10)
		|| (knightlySequenceEfficiencyBonusPercent != 0 && knightlySequenceEfficiencyBonusPercent != 5)
		|| holdMagicalReductionBasisPoints > MAX_HOLD_MAGICAL_REDUCTION_BASIS_POINTS
		|| holdMagicalReductionBasisPoints % BASIS_POINTS_PER_PHYSICAL_PERCENT != 0
		|| protectInterceptionLimit < 1 || protectInterceptionLimit > 2
		|| protectInterceptionsConsumed > protectInterceptionLimit
		|| (primaryTargetUnitId != INVALID_UNIT_ID && primaryTargetUnitId > maxWireId)
		|| (secondaryTargetUnitId != INVALID_UNIT_ID && secondaryTargetUnitId > maxWireId)
		|| !std::is_sorted(consumedUnitIds.begin(), consumedUnitIds.end())
		|| std::adjacent_find(consumedUnitIds.begin(), consumedUnitIds.end()) != consumedUnitIds.end()
		|| !std::is_sorted(braceTriggeredUnitIds.begin(), braceTriggeredUnitIds.end())
		|| std::adjacent_find(braceTriggeredUnitIds.begin(), braceTriggeredUnitIds.end()) != braceTriggeredUnitIds.end()
		|| !std::is_sorted(holdBrokenUnitIds.begin(), holdBrokenUnitIds.end())
		|| std::adjacent_find(holdBrokenUnitIds.begin(), holdBrokenUnitIds.end()) != holdBrokenUnitIds.end())
		throw std::runtime_error("Invalid New Horizons Hero Order state shape");
	if(command != HeroCommand::PROTECT
		&& (protectInterceptionsConsumed != 0 || protectInterceptionLimit != 1))
		throw std::runtime_error("Non-Protect Hero Order contains Protect interception state");
	if(command != HeroCommand::HOLD_THE_LINE && holdMagicalReductionBasisPoints != 0)
		throw std::runtime_error("Non-Hold Hero Order contains Iron Discipline state");
	for(const auto & id : consumedUnitIds)
		if(id > maxWireId)
			throw std::runtime_error("Invalid consumed Hero Order unit identity");
	for(const auto & id : braceTriggeredUnitIds)
		if(id > maxWireId)
			throw std::runtime_error("Invalid Brace Hero Order unit identity");
	for(const auto & id : holdBrokenUnitIds)
		if(id > maxWireId)
			throw std::runtime_error("Invalid Hold the Line Hero Order unit identity");
	for(const auto & anchor : anchors)
		if(anchor.unitId > maxWireId || anchor.position < 0 || anchor.position >= 2047)
			throw std::runtime_error("Invalid Hold the Line anchor");
	uint32_t previous = 0;
	bool firstFlankTarget = true;
	for(const auto & target : flankTargets)
	{
		if(target.unitId > maxWireId || target.sideMask > 0x3f || (!firstFlankTarget && target.unitId <= previous))
			throw std::runtime_error("Invalid Flank target state");
		previous = target.unitId;
		firstFlankTarget = false;
	}
}

bool HeroOrderState::completeDisciplineActivation(uint32_t unitId, int32_t currentRound)
{
	// Native multi-round Orders can be scheduled without any Divine carry.
	// Only an explicitly captured carry recipient may enter its completion ledger.
	if(currentRound <= issuedRound
		|| !std::binary_search(divineDisciplineRecipientUnitIds.begin(), divineDisciplineRecipientUnitIds.end(), unitId)
		|| !scheduledFor(unitId, currentRound))
		return false;
	divineDisciplineCompletedUnitIds.insert(std::lower_bound(divineDisciplineCompletedUnitIds.begin(),
		divineDisciplineCompletedUnitIds.end(), unitId), unitId);
	return true;
}
