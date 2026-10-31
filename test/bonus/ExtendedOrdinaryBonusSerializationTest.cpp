/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/gameState/ReplayLog.h"
#include "../../lib/battle/NewHorizonsCreatureBurn.h"
#include "../../lib/battle/NewHorizonsGraveDust.h"
#include "../../lib/battle/BattleEffectExchange.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <array>
namespace
{
using Version = ESerializationVersion;
constexpr auto admitted = Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES;
constexpr std::array oldVersions{Version::NEW_HORIZONS_ORDINARY_MAINTENANCE_SPECIALTIES,
	Version::NEW_HORIZONS_ORDINARY_MACHINE_SPECIALTIES, Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES};
Bonus ordinary()
{
	return Bonus(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 2, BonusSourceID());
}
Bonus status(const char * hero, const char * name, BonusDuration::Type duration, int value)
{
	Bonus result(duration, BonusType::NONE, BonusSource::HERO_SPECIAL, value,
		BonusSourceID(HeroTypeID(HeroTypeID::decode(hero))));
	result.stacking = std::string("new-horizons:ordinary-creature-specialty:") + hero + ":" + name;
	result.statusIdentity = name; result.valType = BonusValueType::BASE_NUMBER;
	result.statusTags = {BonusStatusTag::NON_TRANSFERABLE};
	return result;
}
std::vector<Bonus> markers()
{
	std::vector<Bonus> result;
	for(const bool nymus : {false, true})
	{
		auto b = status(nymus ? "core:nymus" : "core:calh", nymus ? "cruelOverseer" : "scorchingImpact",
			BonusDuration::STACK_ACTIVATION, nymus ? -3 : 20);
		b.type = nymus ? BonusType::PRIMARY_SKILL : BonusType::GENERAL_ATTACK_REDUCTION;
		if(nymus) { b.subtype = PrimarySkill::DEFENSE; b.valType = BonusValueType::ADDITIVE_VALUE; }
		b.statusTags = {BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE};
		result.push_back(b);
	}
	auto lance = status("core:tyris", "lanceFormation", BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, 1);
	lance.type = BonusType::PRIMARY_SKILL; lance.subtype = PrimarySkill::DEFENSE;
	lance.valType = BonusValueType::ADDITIVE_VALUE; result.push_back(lance);
	for(const bool feather : {false, true})
	{
		auto b = status(feather ? "core:lorelei" : "core:ingham", feather ? "featheredFeint" : "consecratedTarget",
			BonusDuration::N_TURNS, 20);
		b.turnsRemain = 1; b.statusTags = {BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE};
		b.parameters = std::make_shared<BonusParameters>(std::vector<int32_t>{0, 7}); result.push_back(b);
	}
	auto solidarity = status("core:marius", "demonicSolidarity", BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, 20);
	solidarity.type = BonusType::GENERAL_DAMAGE_REDUCTION; solidarity.subtype = BonusCustomSubtype::damageTypeAll;
	result.push_back(solidarity);
	auto spent = status("core:alkin", "ironHideSpent", BonusDuration::N_TURNS, 1);
	spent.turnsRemain = 1; result.push_back(spent);
	for(const auto * hero : {"core:rashka", "core:ignissa"})
		result.push_back(newHorizonsCreatureBurn::marker(HeroTypeID(HeroTypeID::decode(hero)), 7, 1));
	auto moved = ordinary(); moved.duration = BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION;
	moved.type = BonusType::NONE; moved.val = 1; moved.valType = BonusValueType::BASE_NUMBER; moved.stacking = "new-horizons:latest-voluntary-movement";
	moved.statusIdentity = "latestVoluntaryMovement"; moved.statusTags = {BonusStatusTag::NON_TRANSFERABLE};
	result.push_back(moved);
	for(const bool active : {false, true})
	{
		auto b = status("core:monere", "prismaticVanguard", active ? BonusDuration::STACK_ACTIVATION : BonusDuration::ONE_BATTLE, 20);
		if(!active) b.statusIdentity = "prismaticVanguardPending";
		result.push_back(b);
	}
	for(const auto & b : newHorizonsGraveDust::records(5000, PlayerColor(0))) result.push_back(b);
	return result;
}
std::shared_ptr<BonusParameters> owned(std::shared_ptr<const Bonus> child)
{
	BonusParametersOnCombatEvent value;
	value.effects.push_back(BonusParametersOnCombatEvent::CombatEffectBonus{std::move(child), false});
	return std::make_shared<BonusParameters>(value);
}
template<class T> std::vector<std::byte> encode(T & source, Version version)
{
	CMemorySerializer wire; wire.oser.version = version; wire.oser & source; return wire.extractBuffer();
}
template<class T> void rejectPrefix(T & source, Version version)
{
	CMemorySerializer wire; wire.oser.version = version;
	EXPECT_THROW(wire.oser & source, std::exception);
	EXPECT_TRUE(wire.extractBuffer().empty());
}
class ExtendedOrdinaryBonusSerializationTest : public BattleTestFixture {};
TEST_F(ExtendedOrdinaryBonusSerializationTest, EveryFrozenMarkerBareAndDeepOwnedRejectsBeforeLegacyPrefixes)
{
	startGame();
	for(auto source : markers())
	{
		SCOPED_TRACE(source.stacking);
		EXPECT_TRUE(source.isExtendedOrdinarySpecialtyEffect());
		auto root = ordinary(); root.parameters = owned(std::make_shared<Bonus>(source));
		auto outer = ordinary(); outer.parameters = owned(std::make_shared<Bonus>(root));
		BonusList list; list.push_back(std::make_shared<Bonus>(outer));
		for(const auto version : oldVersions)
		{
			rejectPrefix(source, version); rejectPrefix(outer, version); rejectPrefix(list, version);
		}
		const auto bytes = encode(source, admitted);
		CMemorySerializer read(bytes); read.iser.version = admitted; Bonus restored;
		ASSERT_NO_THROW(read.iser & restored);
		EXPECT_EQ(encode(restored, admitted), bytes);
		CMemorySerializer legacyRead(bytes); legacyRead.iser.version = oldVersions.back();
		EXPECT_THROW(legacyRead.iser & restored, std::runtime_error);
	}
}
TEST_F(ExtendedOrdinaryBonusSerializationTest, DirectCombatEffectAndEventProtectTheirPointerAndVectorPrefixes)
{
	startGame();
	for(const auto & marker : markers())
	{
		BonusParametersOnCombatEvent::CombatEffectBonus effect{std::make_shared<Bonus>(marker), false};
		BonusParametersOnCombatEvent event; event.effects.push_back(effect);
		for(const auto version : oldVersions) { rejectPrefix(effect, version); rejectPrefix(event, version); }
		EXPECT_NO_THROW(encode(effect, admitted));
		EXPECT_NO_THROW(encode(event, admitted));
	}
}
TEST_F(ExtendedOrdinaryBonusSerializationTest, MalformedReservedShapesRejectBeforeBarePrefixesAtBothEpochs)
{
	startGame();
	for(auto source : markers())
	{
		SCOPED_TRACE(source.stacking); source.turnsRemain = 5;
		for(const auto version : {oldVersions.back(), admitted}) rejectPrefix(source, version);
	}
}
TEST_F(ExtendedOrdinaryBonusSerializationTest, CommanderAndAllThreeChangeListsProtectOuterPrefixes)
{
	startGame();
	for(const auto & marker : markers())
	{
		auto root = ordinary(); root.parameters = owned(std::make_shared<Bonus>(marker));
		for(const auto & payload : {marker, root})
		{
			SetCommanderProperty commander; commander.which = SetCommanderProperty::BONUS;
			commander.accumulatedBonus = payload;
			for(const auto version : oldVersions) rejectPrefix(commander, version);
			EXPECT_NO_THROW(encode(commander, admitted));
			for(int list = 0; list < 3; ++list)
			{
				SetStackEffect change; change.battleID = BattleID(0);
				auto * target = list == 0 ? &change.toAdd : list == 1 ? &change.toUpdate : &change.toRemove;
				target->emplace_back(7, std::vector<Bonus>{payload});
				for(const auto version : oldVersions) rejectPrefix(change, version);
				EXPECT_NO_THROW(encode(change, admitted));
			}
		}
	}
}
TEST_F(ExtendedOrdinaryBonusSerializationTest, SnapshotBothEndpointHalvesProtectTheirFirstPrefix)
{
	startGame();
	for(const auto & marker : markers())
		for(const bool replacement : {false, true})
		{
			auto root = ordinary(); root.source = BonusSource::SPELL_EFFECT;
			root.sid = BonusSourceID(SpellID(SpellID::SLOW)); root.parameters = owned(std::make_shared<Bonus>(marker));
			battle::BattleEffectExchange exchange;
			auto & endpoint = exchange.endpoints.back();
			(replacement ? endpoint.replacement : endpoint.expected).effects.push_back(root);
			for(const auto version : oldVersions) rejectPrefix(exchange, version);
			EXPECT_NO_THROW(encode(exchange, admitted));
		}
}
TEST_F(ExtendedOrdinaryBonusSerializationTest, ReplayReaderUsesPayloadVersionAndRefusesNewMarkersStampedLegacy)
{
	startGame();
	for(const auto & marker : markers())
	{
		SetStackEffect packet; packet.battleID = BattleID(0);
		packet.toAdd.emplace_back(7, std::vector<Bonus>{marker});
		const CPackForClient * base = &packet;
		ReplayPayload payload(encode(base, admitted), oldVersions.back());
		EXPECT_THROW(ReplayPackSerializer::read(payload, gameState().get()), std::runtime_error);
	}
	SetStackEffect legacy; legacy.battleID = BattleID(0);
	legacy.toAdd.emplace_back(7, std::vector<Bonus>{ordinary()});
	const auto payload = ReplayPackSerializer::write(legacy);
	EXPECT_NO_THROW(ReplayPackSerializer::read(payload, gameState().get()));
}
TEST(ExtendedOrdinaryBonusSerialization, GenericHealingTypeAndTypedReferencesGateIndependentlyOfSourceAndZero)
{
	for(const auto source : {BonusSource::OTHER, BonusSource::SPELL_EFFECT})
		for(const int value : {0, 10000})
		{
			auto bonus = ordinary(); bonus.source = source; bonus.val = value;
			bonus.type = BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS; bonus.valType = BonusValueType::BASE_NUMBER;
			for(const auto version : oldVersions) rejectPrefix(bonus, version);
			EXPECT_NO_THROW(encode(bonus, admitted));
			CMemorySerializer rawReader(encode(bonus, admitted)); rawReader.iser.version = oldVersions.back();
			Bonus restored; EXPECT_THROW(rawReader.iser & restored, std::runtime_error);
			auto outer = ordinary(); outer.parameters = owned(std::make_shared<Bonus>(bonus));
			for(const auto version : oldVersions) rejectPrefix(outer, version);
			bonus.type = BonusType::HATES_TRAIT;
			bonus.subtype = BonusTypeID(BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS);
			for(const auto version : oldVersions) rejectPrefix(bonus, version);
			EXPECT_NO_THROW(encode(bonus, admitted));
			CMemorySerializer typedReader(encode(bonus, admitted)); typedReader.iser.version = oldVersions.back();
			EXPECT_THROW(typedReader.iser & restored, std::runtime_error);
			bonus.subtype = BonusCustomSubtype(static_cast<int32_t>(BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS));
			EXPECT_EQ(encode(bonus, admitted), encode(bonus, oldVersions.back()));
		}
	for(const int value : {-1, 10001})
	{
		auto bonus = ordinary(); bonus.type = BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS; bonus.valType = BonusValueType::BASE_NUMBER; bonus.val = value;
		rejectPrefix(bonus, admitted);
	}
}
TEST(ExtendedOrdinaryBonusSerialization, DirectAndAggregateLimitersGateNewTypeAndTypedSubtypeBeforePrefix)
{
	for(const bool subtype : {false, true})
	{
		HasAnotherBonusLimiter source(subtype ? BonusType::HATES_TRAIT : BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS);
		if(subtype) source.subtype = BonusTypeID(BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS);
		AllOfLimiter aggregate({std::make_shared<HasAnotherBonusLimiter>(source)});
		auto root = ordinary(); root.limiter = std::make_shared<AllOfLimiter>(aggregate);
		for(const auto version : oldVersions)
		{ rejectPrefix(source, version); rejectPrefix(aggregate, version); rejectPrefix(root, version); }
		EXPECT_NO_THROW(encode(root, admitted));
		const auto bytes = encode(source, admitted); CMemorySerializer reader(bytes);
		reader.iser.version = oldVersions.back(); HasAnotherBonusLimiter restored;
		EXPECT_THROW(reader.iser & restored, std::runtime_error);
	}
}
TEST(ExtendedOrdinaryBonusSerialization, TriggerEnumFieldRefusesLegacyBeforeAnyPacketBytes)
{
	BattleTriggerEffect source; source.effect = BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS;
	for(const auto version : oldVersions) rejectPrefix(source, version);
	const auto bytes = encode(source, admitted); CMemorySerializer reader(bytes); reader.iser.version = oldVersions.back();
	BattleTriggerEffect restored; EXPECT_THROW(reader.iser & restored, std::runtime_error);
	source.effect = BonusType::MORALE;
	EXPECT_EQ(encode(source, admitted), encode(source, oldVersions.back()));
}
TEST(ExtendedOrdinaryBonusSerialization, LegacyBytesAndPreTriggerReusedReadKeepTheirOldRepresentation)
{
	auto source = ordinary(); const auto clean = encode(source, oldVersions.back());
	EXPECT_EQ(encode(source, admitted), clean);
	const auto preTrigger = static_cast<Version>(899);
	const auto oldBytes = encode(source, preTrigger);
	auto child = ordinary(); child.type = BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS; child.val = 0;
	source.parameters = owned(std::make_shared<Bonus>(child));
	const auto retained = source.parameters;
	EXPECT_EQ(encode(source, preTrigger), oldBytes);
	EXPECT_EQ(source.parameters, retained);
	CMemorySerializer reader(oldBytes); reader.iser.version = preTrigger;
	ASSERT_NO_THROW(reader.iser & source);
	// Pre-trigger oldAddInfo does not own or replace unsent destination parameters.
	EXPECT_EQ(source.parameters, retained);
	EXPECT_EQ(encode(source, preTrigger), oldBytes);

	const auto trigger = Version::BONUS_TRIGGER;
	ASSERT_EQ(static_cast<int>(trigger), 901);
	auto plain = ordinary(); const auto triggerBytes = encode(plain, trigger);
	CMemorySerializer triggerReader(triggerBytes); triggerReader.iser.version = trigger;
	ASSERT_NO_THROW(triggerReader.iser & source);
	EXPECT_FALSE(source.parameters); // Decoded absence owns and clears this field from901 onward.
	EXPECT_EQ(encode(source, trigger), triggerBytes);
}
}
