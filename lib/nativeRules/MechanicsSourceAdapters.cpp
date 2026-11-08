/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "MechanicsSourceAdapters.h"
#include "NativeRulePilots.h"
#include <stdexcept>
namespace nativeRules
{
std::vector<std::shared_ptr<Bonus>> rankEffectsOrLegacy(const JsonNode & capturedRank,
	SecondarySkill source, const LegacyPassiveProducer & legacy)
{
	if(!source.hasValue()) throw std::invalid_argument("Unresolved mechanics skill source");
	const auto authored = prepareSourceReplacement(capturedRank,
		{SourceKind::SkillRank, BonusSourceID(source), "", SpellID()}, {});
	if(authored) return authored->persistentReplacement();
	// Never clone these pointers: pending native identifier callbacks own their identity.
	auto effects = legacy();
	for(auto & bonus : effects) bonus = prepareSkillRankBonus(std::move(bonus));
	return effects;
}
std::vector<std::shared_ptr<Bonus>> familyEffectsOrLegacy(const JsonNode & capturedRow,
	HeroTypeID source, std::string stacking, const FamilyResolver & resolveFamily,
	const LegacyPassiveProducer & legacy)
{
	if(!source.hasValue()) throw std::invalid_argument("Unresolved mechanics specialty source");
	const auto authored = prepareSourceReplacement(capturedRow,
		{SourceKind::HeroSpecialty, BonusSourceID(source), std::move(stacking), SpellID()}, resolveFamily);
	return authored ? authored->persistentReplacement() : legacy();
}
std::optional<PreparedMechanics> prepareTimedSource(const JsonNode & capturedRow, SpellID source, TimedSourceSlot slot)
{
	if(!source.hasValue() || (slot != TimedSourceSlot::Sanctuary && slot != TimedSourceSlot::Forgetfulness))
		throw std::invalid_argument("Unresolved mechanics timed source");
	const SpellID expected = slot == TimedSourceSlot::Sanctuary
		? SpellID(SpellID::decode("new-horizons:sanctuary")) : SpellID(SpellID::FORGETFULNESS);
	if(source != expected)
		throw std::invalid_argument("Mechanics timed source does not own this native addition");
	return prepareSourceReplacement(capturedRow,
		{SourceKind::SpellAddition, BonusSourceID(source), "", source, slot}, {});
}
}
