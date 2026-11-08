/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PassiveFactionSkillPresentation.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/PassiveDivineMandate.h"
#include "../../lib/battle/NewHorizonsElementalAttunement.h"
#include <array>
#include <bit>
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsMetamagicMarks.h"

namespace passiveFactionSkillPresentation
{
std::optional<SkillStatusText> metamagic(const CGHeroInstance & hero,
	const CBattleInfoCallback & battle, BattleSide side)
{
	if(newHorizonsMetamagic::usesPoints(hero.getPerkState().rules))
	{
		const auto facts = newHorizonsMetamagic::profile(&hero);
		if(facts.rank <= 0 || !battle.getBattle())
			return std::nullopt;
		const auto & state = battle.getBattle()->getMetamagicPointState(side);
		return metamagicPoints(facts, state);
	}
	if(newHorizonsMetamagic::usesArcaneMarks(hero.getPerkState().rules))
	{
		const auto facts = newHorizonsMetamagic::profile(&hero);
		if(facts.rank <= 0)
			return std::nullopt;
		return SkillStatusText{"Marks",
			"Repeated spells on the same stack strengthen this hero's later spells."
			"\n\nEach stack can carry up to 5 of this hero's Arcane Marks."
			" Each mark currently adds +" + std::to_string(newHorizonsMetamagic::effectivenessBonusPercent(facts, 1))
			+ "% spell effectiveness; at 5 marks the total bonus is +"
			+ std::to_string(newHorizonsMetamagic::effectivenessBonusPercent(facts, 5))
			+ "%. Marks apply after resolution, last for combat and cannot be dispelled."
			" Inspect a stack to see its marks. Metamagic grants no additional spell action.", true};
	}
	const int total = newHorizonsMagic::metamagicCapacity(&hero);
	if(total <= 0)
		return std::nullopt;
	const int consumed = std::clamp(battle.battleMetamagicUsesConsumed(side), 0, total);
	return SkillStatusText{std::to_string(total - consumed) + " / " + std::to_string(total), {}, false};
}

std::optional<SkillStatusText> elementalAttunement(const CGHeroInstance & hero,
	const CBattleInfoCallback & battle, BattleSide side)
{
	if(!newHorizonsElementalAttunement::usesRules(&hero) || !battle.getBattle())
		return std::nullopt;
	const auto profile = newHorizonsElementalAttunement::captureProfile(&hero);
	if(profile.rank <= 0)
		return std::nullopt;
	const auto mask = battle.getBattle()->getElementalAttunementMask(side);
	std::string active;
	for(const auto & [bit, name] : std::array<std::pair<uint8_t, const char *>, 4>{{
		{1, "Air"}, {2, "Fire"}, {4, "Water"}, {8, "Earth"}}})
	{
		if((mask & bit) == 0)
			continue;
		if(!active.empty())
			active += ", ";
		active += name;
	}
	std::string details = "Your elemental spells Attune the corresponding friendly Elementals for the rest of combat."
		"\n\nActive elements: " + (active.empty() ? std::string("none") : active)
		+ ".\nAttuned creatures receive the skill rank's attack damage bonus."
		" Firebirds and Phoenixes benefit from any active element, once."
		"\nRepeated casts do not stack Attunement.";
	return SkillStatusText{std::to_string(std::popcount(mask)) + "/4", std::move(details), true};
}

std::optional<SkillStatusText> divineMandate(const CGHeroInstance & hero,
	const CBattleInfoCallback & battle, BattleSide side)
{
	if(newHorizonsPassiveDivineMandate::enabled(hero.getPerkState().rules))
	{
		const auto policy = newHorizonsPassiveDivineMandate::capturePolicy(&hero);
		const auto total = newHorizonsPassiveDivineMandate::activationLimit(policy);
		if(total == 0 || !battle.getBattle())
			return std::nullopt;
		const newHorizonsPassiveDivineMandate::Usage used{
			battle.getBattle()->getPassiveDivineMandateUses(side)};
		const auto remaining = newHorizonsPassiveDivineMandate::remainingActivations(policy, used);
		return SkillStatusText{std::to_string(remaining) + " / " + std::to_string(total),
			"Issuing an Order can automatically cast one useful known Light spell."
			"\n\nFree activations remaining: " + std::to_string(remaining) + " of " + std::to_string(total)
			+ ". The spell costs no Mana or additional Hero Action."
			" No activation is spent when no useful spell is available."
			"\nOrder efficiency: +" + std::to_string(newHorizonsPassiveDivineMandate::orderEfficiencyPercent(policy))
			+ " percentage points on attribute-derived components.", true};
	}
	const auto mandate = battle.battleGetDivineMandateStatus(side);
	if(!mandate.active || mandate.maximumPairs == 0)
		return std::nullopt;
	const auto completed = static_cast<unsigned>(mandate.completedPairs);
	const auto remaining = completed < mandate.maximumPairs
		? static_cast<unsigned>(mandate.maximumPairs) - completed : 0u;
	std::string details = "Completed Spell/Order pairs this combat: "
		+ std::to_string(completed) + " of " + std::to_string(mandate.maximumPairs)
		+ "; remaining pairs: " + std::to_string(remaining) + ".";
	if(mandate.pendingFollowup
		&& mandate.pendingFollowup->source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE)
	{
		const auto followupName = mandate.pendingFollowup->allowance == HeroActionAllowanceState::AllowanceKind::SPELL
			? "Light Spell" : "Order";
		details += "\nSource: Divine Mandate. Pending " + std::string(followupName)
			+ " follow-up through the end of round "
			+ std::to_string(mandate.pendingFollowup->expiryRound) + ".";
	}
	return SkillStatusText{std::to_string(remaining) + " / " + std::to_string(mandate.maximumPairs), details, false};
}
}
