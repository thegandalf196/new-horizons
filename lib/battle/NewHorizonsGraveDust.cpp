/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsGraveDust.h"
#include "PhysicalAffliction.h"
#include "Unit.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusParameters.h"
#include <stdexcept>
namespace newHorizonsGraveDust
{
namespace
{
constexpr auto status = "graveDust";
constexpr auto markerStacking = "new-horizons:physical-affliction:graveDust:marker";
constexpr auto reductionStacking = "new-horizons:physical-affliction:graveDust:reduction";
bool claimsIdentity(const Bonus & bonus)
{
	return isReservedSource(bonus) || bonus.statusIdentity == status
		|| bonus.stacking == markerStacking || bonus.stacking == reductionStacking;
}
bool commonShape(const Bonus & bonus)
{
	return isReservedSource(bonus) && bonus.duration == BonusDuration::STACK_ACTIVATION
		&& bonus.subtype == BonusSubtypeID() && bonus.valType == BonusValueType::BASE_NUMBER
		&& bonus.targetSourceType == BonusSource::OTHER && bonus.turnsRemain == 0
		&& bonus.effectRange == BonusLimitEffect::NO_LIMIT && !bonus.appliedByEnemy
		&& bonus.bonusOwner == PlayerColor::CANNOT_DETERMINE && bonus.spellCasterOwner.isValidPlayer()
		&& bonus.statusIdentity == status && bonus.statusTags.size() == 2
		&& bonus.statusTags[0] == BonusStatusTag::DEBUFF && bonus.statusTags[1] == BonusStatusTag::NON_TRANSFERABLE
		&& !bonus.limiter && !bonus.propagator && !bonus.updater && !bonus.propagationUpdater
		&& bonus.customIconPath.empty();
}
void invalid()
{
	throw std::invalid_argument("Invalid Grave Dust provenance");
}
bool typedKindClaimsDust(const Bonus & bonus)
{
	if(bonus.type != BonusType::PHYSICAL_AFFLICTION || !bonus.parameters) return false;
	try
	{
		const auto & node = bonus.parameters->toCustom<JsonNode>();
		if(!node.isStruct()) return false;
		const auto found = node.Struct().find("kind");
		return found != node.Struct().end() && found->second.isString() && found->second.String() == status;
	}
	catch(const std::runtime_error &) { return false; } // Unrelated old records retain their old admission.
}
struct PairValidation
{
	const Bonus * marker = nullptr;
	const Bonus * reduction = nullptr;
	void append(const Bonus & bonus)
	{
		if(!validateRecord(bonus)) invalid();
		if(bonus.type == BonusType::PHYSICAL_AFFLICTION)
		{
			if(marker) invalid();
			marker = &bonus;
		}
		else
		{
			if(reduction) invalid();
			reduction = &bonus;
		}
	}
	void finish() const
	{
		if(!marker || !reduction || marker->spellCasterOwner != reduction->spellCasterOwner) invalid();
	}
};
}
bool isReservedSource(const Bonus & bonus)
{
	return bonus.source == BonusSource::OTHER && bonus.sid == BonusSourceID(BonusCustomSource::newHorizonsGraveDust);
}
void validateParsedMarker(const Bonus & bonus, const physicalAfflictions::MarkerMetadata & metadata)
{
	if(metadata.kind != status && !claimsIdentity(bonus)) return;
	if(metadata.kind != status || bonus.type != BonusType::PHYSICAL_AFFLICTION || bonus.val != 0
		|| bonus.stacking != markerStacking || !commonShape(bonus)) invalid();
}
bool validateRecord(const Bonus & bonus)
{
	if(bonus.type == BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS
		&& (bonus.val < 0 || bonus.val > 10'000 || bonus.valType != BonusValueType::BASE_NUMBER))
		throw std::invalid_argument("Invalid incoming healing reduction bonus");
	if(!claimsIdentity(bonus) && !typedKindClaimsDust(bonus)) return false;
	if(bonus.type == BonusType::PHYSICAL_AFFLICTION)
	{
		physicalAfflictions::markerMetadata(bonus); // Parsed hook validates bidirectionally, without recursion.
		return true;
	}
	if(bonus.type != BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS || bonus.val <= 0
		|| bonus.stacking != reductionStacking || bonus.parameters || !commonShape(bonus)) invalid();
	return true;
}
void validateBatchRecords(const std::vector<Bonus> & removed,
	const std::vector<std::vector<Bonus> *> & incoming)
{
	for(const auto & bonus : removed) validateRecord(bonus);
	for(const auto * records : incoming)
		if(records) // The existing caller retains null/duplicate-vector topology rejection.
			for(const auto & bonus : *records) validateRecord(bonus);
}
bool isEffect(const Bonus * bonus)
{
	return bonus && validateRecord(*bonus);
}
void validateSourceGroup(const std::vector<Bonus> & bonuses)
{
	PairValidation pair;
	for(const auto & bonus : bonuses) pair.append(bonus);
	pair.finish();
}
void validateBearerGroup(const IBonusBearer & bearer)
{
	const auto actual = bearer.getUnstackedBonuses(CSelector([](const Bonus * bonus)
	{
		return bonus && isReservedSource(*bonus);
	}));
	if(!actual) invalid();
	PairValidation pair;
	for(const auto & bonus : *actual)
	{
		if(!bonus) invalid();
		pair.append(*bonus);
	}
	pair.finish();
}
void validateUnitGroup(const battle::Unit & unit)
{
	validateBearerGroup(unit);
}
std::vector<Bonus> records(const int32_t reductionBasisPoints, const PlayerColor sourcePlayer)
{
	if(reductionBasisPoints < 0 || reductionBasisPoints > 10'000)
		throw std::invalid_argument("Invalid incoming healing receipt inputs");
	if(reductionBasisPoints == 0) return {};
	if(!sourcePlayer.isValidPlayer()) invalid();
	const auto source = BonusSourceID(BonusCustomSource::newHorizonsGraveDust);
	Bonus marker(BonusDuration::STACK_ACTIVATION, BonusType::PHYSICAL_AFFLICTION,
		BonusSource::OTHER, 0, source, BonusSubtypeID(), BonusValueType::BASE_NUMBER);
	marker.stacking = markerStacking;
	marker.statusIdentity = status;
	marker.statusTags = {BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE};
	marker.spellCasterOwner = sourcePlayer;
	JsonNode metadata;
	metadata["kind"].String() = status;
	metadata["applicationOrder"].Integer() = 0;
	marker.parameters = std::make_shared<BonusParameters>(metadata);
	Bonus reduction = marker;
	reduction.type = BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS;
	reduction.val = reductionBasisPoints;
	reduction.stacking = reductionStacking;
	reduction.parameters.reset();
	return {std::move(marker), std::move(reduction)};
}
}
