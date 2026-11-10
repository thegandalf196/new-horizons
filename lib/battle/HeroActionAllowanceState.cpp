/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroActionAllowanceState.h"

void HeroActionAllowanceState::resetForRound(int32_t round)
{
	validateShape();
	if(round < 0 || round < currentRound)
		throw std::invalid_argument("Invalid Hero Action allowance round");
	if(round == currentRound)
		return;

	auto next = *this;
	std::erase_if(next.grants, [round](const Grant & grant)
	{
		return grant.expiryRound < round;
	});
	next.currentRound = round;
	next.heroicSpiritProcessedReceipts.clear();
	next.appendGrant(AllowanceKind::HERO, GrantSource::ROUND, round);
	next.validateShape();
	*this = std::move(next);
}

void HeroActionAllowanceState::validateShape() const
{
	if(currentRound < -1 || nextGrantId == 0 || (currentRound == -1 && (!grants.empty() || orderIssuedThisCombat)))
		throw std::runtime_error("Invalid Hero Action allowance ledger shape");
	if(divineMandateCompletedPairs > MAX_DIVINE_MANDATE_COMPLETED_PAIRS
		|| (currentRound == -1 && divineMandateCompletedPairs != 0))
		throw std::runtime_error("Invalid Divine Mandate completed-pair count");
	if((currentRound == -1 && !heroicSpiritProcessedReceipts.empty())
		|| !std::is_sorted(heroicSpiritProcessedReceipts.begin(), heroicSpiritProcessedReceipts.end())
		|| std::adjacent_find(heroicSpiritProcessedReceipts.begin(), heroicSpiritProcessedReceipts.end()) != heroicSpiritProcessedReceipts.end()
		|| std::any_of(heroicSpiritProcessedReceipts.begin(), heroicSpiritProcessedReceipts.end(), [this](uint32_t id)
		{
			return id == 0 || id >= nextGrantId || std::any_of(grants.begin(), grants.end(), [id](const Grant & grant)
			{
				return grant.id == id;
			});
		}))
		throw std::runtime_error("Invalid Heroic Spirit processed Order receipts");

	uint32_t previousId = 0;
	uint32_t currentRoundBaseGrants = 0;
	uint32_t divineMandateGrants = 0;
	uint32_t heroicSpiritGrants = 0;
	for(const auto & grant : grants)
	{
		grant.validateShape();
		if(grant.id <= previousId || grant.id >= nextGrantId
			|| grant.grantedRound > currentRound || grant.expiryRound < currentRound)
			throw std::runtime_error("Invalid Hero Action allowance grant ordering or lifetime");
		previousId = grant.id;
		if(grant.source == GrantSource::ROUND && grant.grantedRound == currentRound)
			++currentRoundBaseGrants;
		if(grant.source == GrantSource::DIVINE_MANDATE)
			++divineMandateGrants;
		if(grant.source == GrantSource::HEROIC_SPIRIT)
			++heroicSpiritGrants;
	}
	if(currentRoundBaseGrants > 1)
		throw std::runtime_error("Duplicate base Hero Action allowance");
	if(divineMandateGrants > 1)
		throw std::runtime_error("Duplicate Divine Mandate follow-up allowance");
	if(heroicSpiritGrants > heroicSpiritProcessedReceipts.size())
		throw std::runtime_error("Heroic Spirit allowance lacks a processed Order receipt");
}
