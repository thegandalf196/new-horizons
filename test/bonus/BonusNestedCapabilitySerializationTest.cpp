/*
 * BonusNestedCapabilitySerializationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
constexpr auto previous = static_cast<ESerializationVersion>(1144);
Bonus ordinary()
{
	return {BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 7, BonusSourceID()};
}
std::shared_ptr<Bonus> marker(int value)
{
	auto bonus = std::make_shared<Bonus>(ordinary());
	bonus->type = BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION;
	bonus->val = value;
	return bonus;
}
std::shared_ptr<BonusParameters> effects(std::shared_ptr<const Bonus> child)
{
	BonusParametersOnCombatEvent event;
	event.effects.push_back(BonusParametersOnCombatEvent::CombatEffectBonus{std::make_shared<Bonus>(ordinary()), false});
	event.effects.push_back(BonusParametersOnCombatEvent::CombatEffectBonus{child, true});
	return std::make_shared<BonusParameters>(event);
}
template<typename T> void rejectBeforePrefix(T & source)
{
	CMemorySerializer old;
	old.oser.version = previous;
	EXPECT_THROW(old.oser & source, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}
template<typename T> void roundtrip(T & source, bool supported)
{
	CMemorySerializer wire;
	if(!supported) wire.oser.version = wire.iser.version = previous;
	ASSERT_NO_THROW(wire.oser & source);
	T restored;
	ASSERT_NO_THROW(wire.iser & restored);
	EXPECT_FALSE(wire.extractBuffer().empty());
}
template<typename T> void rejectCurrentWireAs1144(T & source)
{
	CMemorySerializer wire;
	ASSERT_NO_THROW(wire.oser & source);
	wire.iser.version = previous;
	T restored;
	EXPECT_THROW(wire.iser & restored, std::runtime_error);
}

TEST(BonusNestedCapabilitySerialization, ParametersAndOuterBonusRejectNestedEnumIncludingZero)
{
	for(const int value : {0, 20})
	{
		auto source = ordinary();
		auto parameters = effects(marker(value));
		source.parameters = parameters;
		rejectBeforePrefix(source);
		rejectBeforePrefix(*parameters);
		CMemorySerializer current;
		ASSERT_NO_THROW(current.oser & source);
		Bonus restored;
		ASSERT_NO_THROW(current.iser & restored);
		ASSERT_TRUE(restored.parameters);
		const auto * event = restored.parameters->combatEffects();
		ASSERT_NE(event, nullptr);
		ASSERT_EQ(event->effects.size(), 2u);
		const auto & nested = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(event->effects[1]);
		ASSERT_TRUE(nested.bonus);
		EXPECT_EQ(nested.bonus->type, BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION);
		EXPECT_EQ(nested.bonus->val, value);
		EXPECT_TRUE(nested.targetEnemy);
		roundtrip(*parameters, true);
		rejectCurrentWireAs1144(source);
		rejectCurrentWireAs1144(*parameters);
	}
}

TEST(BonusNestedCapabilitySerialization, NestedFilterOnlyBonusProtectsParametersListAndNode)
{
	auto child = std::make_shared<Bonus>(ordinary());
	auto filter = std::make_shared<UnitAdjacentLimiter>();
	filter->targetUnit = CreatureID(0);
	filter->filter.relationship = AdjacentUnitFilter::Relationship::SAME_ARMY;
	child->limiter = filter;
	auto outer = std::make_shared<Bonus>(ordinary());
	outer->parameters = effects(child);
	BonusList list;
	list.push_back(outer);
	CBonusSystemNode node(BonusNodeType::ARMY);
	node.addNewBonus(outer);
	rejectBeforePrefix(*outer);
	rejectBeforePrefix(list);
	rejectBeforePrefix(node);
	roundtrip(*outer, true);
}

TEST(BonusNestedCapabilitySerialization, OrdinaryNestedPayloadSupportsBothVersionsAndNullSpellJsonAlternatives)
{
	auto source = ordinary();
	auto parameters = effects(std::make_shared<Bonus>(ordinary()));
	auto & event = parameters->toCustom<BonusParametersOnCombatEvent>();
	event.effects.push_back(BonusParametersOnCombatEvent::CombatEffectBonus{});
	event.effects.push_back(BonusParametersOnCombatEvent::CombatEffectSpell{SpellID(0), 1, false});
	source.parameters = parameters;
	roundtrip(source, false);
	roundtrip(source, true);
	CMemorySerializer current, old;
	old.oser.version = previous;
	ASSERT_NO_THROW(current.oser & source);
	ASSERT_NO_THROW(old.oser & source);
	EXPECT_EQ(current.extractBuffer(), old.extractBuffer());
	JsonNode text;
	text["uninterpreted"].String() = "TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION";
	source.parameters = std::make_shared<BonusParameters>(text);
	roundtrip(source, false);
	roundtrip(source, true);
}

TEST(BonusNestedCapabilitySerialization, SharedAliasesAndCyclesTerminateAndRetainIdentity)
{
	auto root = std::make_shared<Bonus>(ordinary());
	auto child = std::make_shared<Bonus>(ordinary());
	root->parameters = effects(child);
	child->parameters = effects(root);
	// Repeated child alias and a cycle use pointer identity, never inherited graph links.
	auto parameters = std::const_pointer_cast<BonusParameters>(root->parameters);
	parameters->toCustom<BonusParametersOnCombatEvent>().effects.push_back(
		BonusParametersOnCombatEvent::CombatEffectBonus{child, false});
	for(const bool supported : {false, true})
	{
		EXPECT_NO_THROW(root->validateAdjacencyCapabilitySerialization(BonusSerializationContext{supported, true}));
		CMemorySerializer wire;
		if(!supported) wire.oser.version = wire.iser.version = previous;
		ASSERT_NO_THROW(wire.oser & root);
		std::shared_ptr<Bonus> restored;
		ASSERT_NO_THROW(wire.iser & restored);
		const auto * event = restored->parameters->combatEffects();
		ASSERT_NE(event, nullptr);
		const auto a = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(event->effects[1]).bonus;
		const auto b = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(event->effects[2]).bonus;
		EXPECT_EQ(a.get(), b.get());
		const auto back = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(a->parameters->combatEffects()->effects[1]).bonus;
		EXPECT_EQ(back.get(), restored.get());
		std::const_pointer_cast<Bonus>(a)->parameters.reset(); // break restored ownership cycle
	}
	child->type = BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION;
	rejectBeforePrefix(*root);
	child->parameters.reset(); // break source ownership cycle
}

TEST(BonusNestedCapabilitySerialization, DependencyLimiterDirectAggregateAndOuterRejectNewTypeAndTypedSubtype)
{
	for(const bool subtypeReference : {false, true})
	{
		HasAnotherBonusLimiter source(BonusType::STACKS_SPEED);
		if(subtypeReference)
		{
			source.type = BonusType::HATES_TRAIT;
			source.subtype = BonusTypeID(BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION);
			source.isSubtypeRelevant = true;
		}
		else source.type = BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION;
		rejectBeforePrefix(source);
		roundtrip(source, true);
		rejectCurrentWireAs1144(source);
		AllOfLimiter aggregate({std::make_shared<HasAnotherBonusLimiter>(source)});
		rejectBeforePrefix(aggregate);
		roundtrip(aggregate, true);
		rejectCurrentWireAs1144(aggregate);
		auto outer = ordinary();
		outer.limiter = std::make_shared<AllOfLimiter>(aggregate);
		rejectBeforePrefix(outer);
		roundtrip(outer, true);
		rejectCurrentWireAs1144(outer);
		BonusList list;
		list.push_back(std::make_shared<Bonus>(outer));
		rejectBeforePrefix(list);
	}
}

TEST(BonusNestedCapabilitySerialization, TypedHatesTraitSubtypeRejectsWhileSameCustomNumberRemainsSupported)
{
	auto source = ordinary();
	source.type = BonusType::HATES_TRAIT;
	source.subtype = BonusTypeID(BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION);
	rejectBeforePrefix(source);
	roundtrip(source, true);
	rejectCurrentWireAs1144(source);
	source.subtype = BonusTypeID(BonusType::UNDEAD);
	roundtrip(source, false);
	roundtrip(source, true);
	// Same integer in a different variant alternative is not a BonusType enum reference.
	source.type = BonusType::STACKS_SPEED;
	source.subtype = BonusCustomSubtype(static_cast<int32_t>(BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION));
	roundtrip(source, false);
	roundtrip(source, true);
	HasAnotherBonusLimiter limiter(BonusType::HATES_TRAIT, BonusTypeID(BonusType::UNDEAD));
	roundtrip(limiter, false);
	roundtrip(limiter, true);
	limiter.subtype = source.subtype;
	roundtrip(limiter, false);
}

TEST(BonusNestedCapabilitySerialization, PreTriggerBonusBytesAndReusedReadIgnoreUnserializedParameters)
{
	for(const auto version : {ESerializationVersion::MINIMAL, static_cast<ESerializationVersion>(899),
		static_cast<ESerializationVersion>(900)})
	{
		ASSERT_LT(version, ESerializationVersion::BONUS_TRIGGER);
		auto source = ordinary();
		auto retained = effects(marker(20));
		source.parameters = retained;
		CMemorySerializer wire, control;
		wire.oser.version = control.oser.version = version;
		ASSERT_NO_THROW(wire.oser & source);
		EXPECT_EQ(source.parameters.get(), retained.get());
		auto plain = ordinary();
		ASSERT_NO_THROW(control.oser & plain);
		auto bytes = wire.extractBuffer();
		EXPECT_EQ(bytes, control.extractBuffer());
		// Historical empty oldAddInfo preserves an existing destination pointer.
		// Capability admission must not interpret this unsent destination state.
		CMemorySerializer input(std::move(bytes));
		input.iser.version = version;
		auto restored = ordinary();
		auto stale = effects(marker(0));
		restored.parameters = stale;
		ASSERT_NO_THROW(input.iser & restored);
		EXPECT_EQ(restored.parameters.get(), stale.get());
		EXPECT_EQ(restored.type, BonusType::STACKS_SPEED);
		EXPECT_EQ(restored.val, plain.val);
	}
}

TEST(BonusNestedCapabilitySerialization, TriggerBoundaryOwnsParametersAndClearsDecodedAbsence)
{
	ASSERT_EQ(static_cast<int>(ESerializationVersion::BONUS_TRIGGER), 901);
	auto source = ordinary();
	source.parameters = effects(marker(20));
	CMemorySerializer rejected;
	rejected.oser.version = ESerializationVersion::BONUS_TRIGGER;
	EXPECT_THROW(rejected.oser & source, std::runtime_error);
	EXPECT_TRUE(rejected.extractBuffer().empty());
	auto plain = ordinary();
	CMemorySerializer wire;
	wire.oser.version = wire.iser.version = ESerializationVersion::BONUS_TRIGGER;
	ASSERT_NO_THROW(wire.oser & plain);
	auto reused = ordinary();
	reused.parameters = effects(marker(0));
	ASSERT_NO_THROW(wire.iser & reused);
	EXPECT_FALSE(reused.parameters);
	roundtrip(source, true);
}

TEST(BonusNestedCapabilitySerialization, NodeAndStoredListPropagateActualTargetParameterOwnership)
{
	auto outer = std::make_shared<Bonus>(ordinary());
	outer->parameters = effects(marker(20));
	BonusList list;
	list.push_back(outer);
	CBonusSystemNode node(BonusNodeType::ARMY);
	node.addNewBonus(outer);
	for(const auto version : {ESerializationVersion::MINIMAL, static_cast<ESerializationVersion>(899),
		static_cast<ESerializationVersion>(900)})
	{
		CMemorySerializer listWire, nodeWire;
		listWire.oser.version = nodeWire.oser.version = version;
		ASSERT_NO_THROW(listWire.oser & list);
		ASSERT_NO_THROW(nodeWire.oser & node);
	}
	CMemorySerializer listWire, nodeWire;
	listWire.oser.version = nodeWire.oser.version = ESerializationVersion::BONUS_TRIGGER;
	EXPECT_THROW(listWire.oser & list, std::runtime_error);
	EXPECT_THROW(nodeWire.oser & node, std::runtime_error);
	EXPECT_TRUE(listWire.extractBuffer().empty());
	EXPECT_TRUE(nodeWire.extractBuffer().empty());
}

TEST(BonusNestedCapabilitySerialization, DirectParametersAlwaysOwnTheirRootButChildBonusesHonorTargetCodec)
{
	auto root = effects(marker(20));
	for(const auto version : {ESerializationVersion::MINIMAL, static_cast<ESerializationVersion>(900)})
	{
		CMemorySerializer wire;
		wire.oser.version = version;
		EXPECT_THROW(wire.oser & *root, std::runtime_error);
		EXPECT_TRUE(wire.extractBuffer().empty());
	}
	auto child = std::make_shared<Bonus>(ordinary());
	child->parameters = effects(marker(20));
	auto direct = effects(child);
	CMemorySerializer old;
	old.oser.version = old.iser.version = static_cast<ESerializationVersion>(900);
	ASSERT_NO_THROW(old.oser & *direct);
	BonusParameters restored;
	ASSERT_NO_THROW(old.iser & restored);
	const auto * event = restored.combatEffects();
	ASSERT_NE(event, nullptr);
	ASSERT_EQ(event->effects.size(), 2u);
	const auto & decoded = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(event->effects[1]);
	ASSERT_TRUE(decoded.bonus);
	EXPECT_FALSE(decoded.bonus->parameters);
	CMemorySerializer boundary;
	boundary.oser.version = ESerializationVersion::BONUS_TRIGGER;
	EXPECT_THROW(boundary.oser & *direct, std::runtime_error);
	EXPECT_TRUE(boundary.extractBuffer().empty());
}
}
