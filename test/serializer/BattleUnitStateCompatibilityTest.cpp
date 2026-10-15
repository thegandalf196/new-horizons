/*
 * BattleUnitStateCompatibilityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "../../lib/battle/Unit.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
using Version = ESerializationVersion;
enum class Carrier { UNIT, UNITS, HIT, ATTACKER, TARGET, INJURY };
enum class Shape { ABSENT, DEFAULT_VALUE, POPULATED, MALFORMED };
constexpr std::array CARRIERS{Carrier::UNIT, Carrier::UNITS, Carrier::HIT,
	Carrier::ATTACKER, Carrier::TARGET, Carrier::INJURY};
constexpr std::array SHAPES{Shape::ABSENT, Shape::DEFAULT_VALUE, Shape::POPULATED, Shape::MALFORMED};

struct Feature
{
	const char * name;
	Version boundary;
	const char * unsupported;
	const char * malformed; // nullptr means this wire guard does not reject that shape.
};

const std::array<Feature, 13> FEATURES{{
	{"heroicSpirit", Version::NEW_HORIZONS_HEROIC_SPIRIT,
		"Cannot discard Heroic Spirit receipt in an older unit update", "Heroic Spirit receipt must be a boolean"},
	{"overwatch", Version::NEW_HORIZONS_OVERWATCH,
		"Cannot discard Overwatch state in an older unit update format", "Invalid serialized Overwatch round marker"},
	{"veteranCohesion", Version::NEW_HORIZONS_VETERAN_COHESION,
		"Cannot discard Veteran Cohesion receipt in an older unit update format", "Invalid Veteran Cohesion receipt"},
	{"mercenary", Version::NEW_HORIZONS_DIPLOMACY_COHORTS,
		"Cannot discard Mercenary Captain unit bonus", nullptr},
	{"training", Version::NEW_HORIZONS_RECRUITMENT_TRAINING,
		"Cannot discard training bonus in an older unit update", nullptr},
	{"plague", Version::NEW_HORIZONS_PLAGUEBEARER,
		"Cannot discard captured Plague unit propagation limit", "Invalid captured Plague propagation limit"},
	{"frozen", Version::NEW_HORIZONS_FROZEN,
		"Cannot discard Frozen application receipt in an older unit update format", "Invalid Frozen application receipt in unit update"},
	{"phantom", Version::NEW_HORIZONS_SPELLCRAFT_TARGET_DURATION,
		"Cannot discard extended Phantom lifetime in an older unit update", nullptr},
	{"battleForm", Version::NEW_HORIZONS_SAFE_BATTLE_FORMS,
		"Cannot discard battle-form state in an older unit update format", nullptr},
	{"confusionMarker", Version::NEW_HORIZONS_CONFUSION_MARKER,
		"Cannot discard source-bound Confusion pending state in an older format", "Invalid Confusion boolean metadata"},
	{"confusionHistory", Version::NEW_HORIZONS_CONFUSION_STATE,
		"Cannot discard Confusion pending state or history in an older format", "Invalid Confusion caster or behavior identifier"},
	{"morale", Version::NEW_HORIZONS_MORALE_EXTRA_DAMAGE,
		"Cannot discard current Morale activation provenance", "Morale activation provenance must be a boolean"},
	{"opportunist", Version::NEW_HORIZONS_OPPORTUNIST,
		"Cannot discard an earned own lucky attack sequence", "Own lucky attack sequence must be a boolean"}
}};

Version previous(Version boundary)
{
	return static_cast<Version>(static_cast<int>(boundary) - 1);
}

const char * carrierName(Carrier carrier)
{
	constexpr std::array names{"unit", "units", "hit", "attackAttacker", "attackTarget", "injury"};
	return names.at(static_cast<size_t>(carrier));
}

std::string hex(const std::vector<std::byte> & bytes)
{
	constexpr auto digits = "0123456789abcdef";
	std::string result;
	result.reserve(bytes.size() * 2);
	for(const auto value : bytes)
	{
		const auto byte = std::to_integer<unsigned char>(value);
		result += digits[byte >> 4];
		result += digits[byte & 15];
	}
	return result;
}

JsonNode featureData(size_t feature, Shape shape)
{
	JsonNode result;
	if(shape == Shape::ABSENT)
		return result;
	const bool populated = shape == Shape::POPULATED;
	const bool malformed = shape == Shape::MALFORMED;
	auto & state = result["state"];
	state.Struct();
	const auto boolean = [&](const char * name)
	{
		state[name] = malformed ? JsonNode(7) : JsonNode(populated);
	};
	const auto integer = [&](const char * name, int empty, int active)
	{
		state[name] = malformed ? JsonNode("wrong-type") : JsonNode(populated ? active : empty);
	};
	switch(feature)
	{
		case 0: boolean("heroicSpiritRetaliation"); break;
		case 1: integer("battlecraftOverwatchReadyRound", -1, 2); break;
		case 2: boolean("veteranCohesionEarned"); break;
		case 3:
		case 4:
		{
			JsonNode bonus;
			bonus["stacking"] = malformed ? JsonNode(7) : JsonNode(populated
				? (feature == 3 ? "new-horizons:mercenaryCaptain" : "new-horizons:fieldInstructor") : "");
			result["bonuses"].Vector().push_back(bonus);
			break;
		}
		case 5:
		{
			JsonNode marker;
			marker["type"] = JsonNode("COMBAT_EVENT_TRIGGER");
			marker["sourceType"] = JsonNode("SPELL_EFFECT");
			marker["sourceID"] = JsonNode("new-horizons:plague");
			marker["addInfo"]["propagationLimit"] = JsonNode(malformed ? 0 : populated ? 2 : 1);
			result["bonuses"].Vector().push_back(marker);
			break;
		}
		case 6: integer("frozenAppliedRound", -1, 2); break;
		case 7: integer("phantomRoundsRemaining", 3, 4); break;
		case 8: integer("battleFormRoundsRemaining", 0, 2); break;
		case 9:
		case 10:
		{
			battle::ConfusionState confusion;
			if(populated && feature == 9)
				confusion.applyPending(PlayerColor(0), true);
			if(populated && feature == 10)
				confusion.recordResolved(battle::ConfusionBehavior::ATTACK);
			state["confusion"] = confusion.toJson();
			if(malformed && feature == 9)
				state["confusion"]["pending"] = JsonNode("wrong-type");
			if(malformed && feature == 10)
				state["confusion"]["previousResolved"] = JsonNode(99);
			break;
		}
		case 11: boolean("moraleExtraActivation"); state["hadMorale"] = JsonNode(populated); break;
		case 12: boolean("luckyOwnAttackSequence"); break;
		default: throw std::logic_error("Unknown feature fixture");
	}
	return result;
}

UnitChanges change(const JsonNode & data)
{
	UnitChanges result(17, UnitChanges::EOperation::UPDATE);
	result.healthDelta = -5;
	result.data = data;
	return result;
}

BattleStackAttacked hit(const JsonNode & data)
{
	BattleStackAttacked result;
	result.stackAttacked = 17;
	result.attackerID = 23;
	result.damageAmount = 5;
	result.newState = change(data);
	return result;
}

template<typename Function>
auto withCarrier(Carrier carrier, const JsonNode & data, Function function)
{
	switch(carrier)
	{
		case Carrier::UNIT: { auto packet = change(data); return function(packet); }
		case Carrier::UNITS:
		{
			BattleUnitsChanged packet;
			packet.battleID = BattleID(4); packet.changedStacks.push_back(change(data));
			return function(packet);
		}
		case Carrier::HIT: { auto packet = hit(data); return function(packet); }
		case Carrier::ATTACKER:
		case Carrier::TARGET:
		{
			BattleAttack packet;
			packet.battleID = packet.attackerChanges.battleID = BattleID(4);
			if(carrier == Carrier::ATTACKER) packet.attackerChanges.changedStacks.push_back(change(data));
			else packet.bsa.push_back(hit(data));
			return function(packet);
		}
		case Carrier::INJURY:
		{
			StacksInjured packet;
			packet.battleID = BattleID(4); packet.stacks.push_back(hit(data));
			return function(packet);
		}
	}
	throw std::logic_error("Unknown carrier fixture");
}

template<typename Packet>
UnitChanges & selected(Packet & packet, Carrier carrier)
{
	if constexpr(std::is_same_v<Packet, UnitChanges>) return packet;
	else if constexpr(std::is_same_v<Packet, BattleUnitsChanged>) return packet.changedStacks.at(0);
	else if constexpr(std::is_same_v<Packet, BattleStackAttacked>) return packet.newState;
	else if constexpr(std::is_same_v<Packet, StacksInjured>) return packet.stacks.at(0).newState;
	else return carrier == Carrier::ATTACKER ? packet.attackerChanges.changedStacks.at(0) : packet.bsa.at(0).newState;
}

struct Outcome
{
	std::vector<std::byte> bytes;
	std::string error;
};

template<typename Packet>
Outcome encode(Packet & packet, Version version, const std::vector<std::byte> & prefix = {})
{
	CMemorySerializer memory(prefix);
	memory.oser.version = version;
	Outcome result;
	try { memory.oser & packet; }
	catch(const std::runtime_error & error) { result.error = error.what(); }
	result.bytes = memory.extractBuffer();
	return result;
}

template<typename Packet>
void seedReused(Packet & packet)
{
	if constexpr(std::is_same_v<Packet, BattleStackAttacked>)
	{
		packet.armorerLastStandSide = BattleSide::DEFENDER;
		packet.armorerLastStandEndsActivation = true;
	}
	else if constexpr(std::is_same_v<Packet, BattleAttack>)
	{
		packet.fortuneSide = BattleSide::ATTACKER; packet.fortuneState.emplace();
		packet.relentlessAssaultSide = BattleSide::ATTACKER; packet.relentlessAssaultState.emplace();
		packet.perfectFortuneSide = BattleSide::ATTACKER; packet.perfectFortuneState.emplace();
		packet.luckSerendipitySide = BattleSide::ATTACKER; packet.luckSerendipityState.emplace();
		packet.chainGateTriggered = true;
	}
	else if constexpr(std::is_same_v<Packet, BattleUnitsChanged>)
	{
		packet.rebirthChainConsumption.emplace(); packet.phoenixSparkConsumption.emplace();
	}
}

template<typename Packet>
void expectCleared(const Packet & packet)
{
	if constexpr(std::is_same_v<Packet, BattleStackAttacked>)
	{
		EXPECT_EQ(packet.armorerLastStandSide, BattleSide::NONE);
		EXPECT_FALSE(packet.armorerLastStandEndsActivation);
	}
	else if constexpr(std::is_same_v<Packet, BattleAttack>)
	{
		EXPECT_EQ(packet.fortuneSide, BattleSide::NONE); EXPECT_FALSE(packet.fortuneState);
		EXPECT_EQ(packet.relentlessAssaultSide, BattleSide::NONE); EXPECT_FALSE(packet.relentlessAssaultState);
		EXPECT_EQ(packet.perfectFortuneSide, BattleSide::NONE); EXPECT_FALSE(packet.perfectFortuneState);
		EXPECT_EQ(packet.luckSerendipitySide, BattleSide::NONE); EXPECT_FALSE(packet.luckSerendipityState);
		EXPECT_FALSE(packet.chainGateTriggered);
	}
	else if constexpr(std::is_same_v<Packet, BattleUnitsChanged>)
	{
		EXPECT_FALSE(packet.rebirthChainConsumption); EXPECT_FALSE(packet.phoenixSparkConsumption);
	}
}

template<typename Packet>
void roundTrip(Packet & packet, Carrier carrier, Version version, const std::vector<std::byte> & bytes)
{
	Packet restored;
	seedReused(restored);
	CMemorySerializer memory(bytes);
	memory.iser.version = version;
	ASSERT_NO_THROW(memory.iser & restored);
	EXPECT_EQ(selected(restored, carrier).data, selected(packet, carrier).data);
	EXPECT_EQ(selected(restored, carrier).operation, selected(packet, carrier).operation);
	EXPECT_EQ(selected(restored, carrier).id, 17u);
	EXPECT_EQ(selected(restored, carrier).healthDelta, -5);
	expectCleared(restored);
	const auto encoded = encode(restored, version);
	EXPECT_TRUE(encoded.error.empty()); EXPECT_EQ(encoded.bytes, bytes);
}

JsonNode row(const std::string & label, const Outcome & result)
{
	JsonNode value;
	value["label"] = JsonNode(label);
	value["exception"] = JsonNode(result.error.empty() ? "none" : "std::runtime_error");
	value["message"] = JsonNode(result.error);
	value["bytesHex"] = JsonNode(hex(result.bytes));
	return value;
}

void publish(const JsonNode & rows)
{
	const auto text = rows.toCompactString();
	ASSERT_LE(text.size(), 4u * 1024u * 1024u);
	::testing::Test::RecordProperty("wireRows", text);
}

std::string label(Carrier carrier, const Feature & feature, Version version, Shape shape)
{
	return std::string(carrierName(carrier)) + "/" + feature.name + "/"
		+ std::to_string(static_cast<int>(version)) + "/" + std::to_string(static_cast<int>(shape));
}

// Observation only. It executes actual packet serialize bodies and nested common
// carriers, but never reimplements a validator or emits substitute wire bytes.
template<bool Saving>
struct TraceHandler
{
	using Version = ESerializationVersion;
	static constexpr bool saving = Saving;
	Version version = Version::CURRENT;
	std::vector<int> queries;
	std::vector<size_t> unitStarts;
	std::optional<size_t> firstField;

	bool hasFeature(Version value)
	{
		queries.push_back(static_cast<int>(value));
		return version >= value;
	}

	template<typename Value>
	void visit(Value & value)
	{
		if constexpr(std::is_same_v<Value, UnitChanges>) unitStarts.push_back(queries.size());
		value.serialize(*this);
	}

	template<typename Value>
	TraceHandler & operator&(Value & value)
	{
		if constexpr(std::is_same_v<Value, UnitChanges> || std::is_same_v<Value, BattleStackAttacked>
			|| std::is_same_v<Value, BattleUnitsChanged>) visit(value);
		else if constexpr(std::is_same_v<Value, std::vector<UnitChanges>>
			|| std::is_same_v<Value, std::vector<BattleStackAttacked>>)
			for(auto & element : value) visit(element);
		else if(!firstField) firstField = queries.size();
		return *this;
	}
};

JsonNode traceRow(const char * name, const std::vector<int> & queries)
{
	JsonNode result;
	result["label"] = JsonNode(name);
	for(int query : queries) result["features"].Vector().emplace_back(query);
	return result;
}

JsonNode allActiveData()
{
	JsonNode data;
	for(size_t i = 0; i < FEATURES.size(); ++i)
	{
		const auto part = featureData(i, Shape::POPULATED);
		for(const auto & [key, value] : part["state"].Struct()) data["state"][key] = value;
		for(const auto & bonus : part["bonuses"].Vector()) data["bonuses"].Vector().push_back(bonus);
	}
	data["state"]["confusion"]["pending"] = JsonNode(true);
	data["state"]["confusion"]["pendingCaster"] = JsonNode(0);
	return data;
}

template<typename Packet>
BattleStackAttacked & affectedHit(Packet & packet)
{
	if constexpr(std::is_same_v<Packet, BattleStackAttacked>) return packet;
	else if constexpr(std::is_same_v<Packet, BattleAttack>) return packet.bsa.at(0);
	else return packet.stacks.at(0);
}

template<typename Packet>
void expectPreflight(const char * name, Packet & packet, Version version, const char * message, JsonNode & rows)
{
	SCOPED_TRACE(name);
	const std::vector<std::byte> prefix{std::byte{0xab}, std::byte{0x45}};
	const auto empty = encode(packet, version);
	const auto seeded = encode(packet, version, prefix);
	EXPECT_EQ(empty.error, message); EXPECT_EQ(seeded.error, message);
	EXPECT_TRUE(empty.bytes.empty()); EXPECT_EQ(seeded.bytes, prefix);
	rows.Vector().push_back(row(name, empty));
	rows.Vector().push_back(row(std::string(name) + "/prefilled", seeded));
}

struct Tail
{
	const char * name;
	Version boundary;
	unsigned preflightCarriers;
	unsigned acceptedCarriers;
};
constexpr unsigned bit(Carrier carrier) { return 1u << static_cast<unsigned>(carrier); }
constexpr unsigned ALL = 63;
constexpr unsigned UNIT = bit(Carrier::UNIT);
constexpr unsigned UNITS = bit(Carrier::UNITS);
constexpr unsigned HIT = bit(Carrier::HIT);
constexpr unsigned ATTACKS = bit(Carrier::ATTACKER) | bit(Carrier::TARGET);
const std::array<Tail, 10> TAILS{{
	{"reserve", Version::NEW_HORIZONS_RESERVE, UNIT, 0},
	{"veteranDamage", Version::NEW_HORIZONS_ARMORER_VETERAN, UNIT, 0},
	{"rebirthHP", Version::NEW_HORIZONS_REBIRTH_OUTPUT_ORIGINAL_HP, UNIT | UNITS, 0},
	{"mastery", Version::NEW_HORIZONS_BATTLEFIELD_MASTERY, UNIT | UNITS | ATTACKS, 0},
	{"lastStand", Version::NEW_HORIZONS_ARMORER_LAST_STAND, UNIT | UNITS | HIT | ATTACKS, 0},
	{"rage", Version::NEW_HORIZONS_RAGE_THROUGH_PAIN, ALL, 0},
	{"preemptive", Version::NEW_HORIZONS_BATTLECRAFT_PREEMPTIVE_STRIKE, ALL, 0},
	{"casualty", Version::BATTLE_CASUALTY_PROVENANCE, ALL, 0},
	{"noQuarter", Version::NEW_HORIZONS_NO_QUARTER, UNITS | ATTACKS, UNIT | HIT | bit(Carrier::INJURY)},
	{"rangedFollowUp", Version::NEW_HORIZONS_RANGED_FOLLOW_UP, UNITS | HIT, UNIT}
}};

JsonNode tailData(size_t index)
{
	JsonNode result;
	constexpr std::array fields{"activationMovementBonus", "veteranPhysicalDamageSinceActivation", "",
		"battlecraftWaitMasteryDoubled", "armorerLastStandDefending", "personalBloodrageIncrement",
		"battlecraftPreemptiveStrikeRound", "", "noQuarterMoraleActivationsRemaining", "rangedFollowUpDamagePercent"};
	if(index == 2) result["rebirthOriginalAggregateHP"] = JsonNode(100);
	else if(index == 7)
	{
		JsonNode cohort;
		cohort["count"] = JsonNode(1);
		cohort["provenance"] = JsonNode(static_cast<int>(battle::DamageProvenance::SPELL));
		cohort["temporarilyRestored"] = JsonNode(0);
		result["state"]["health"]["casualtyProvenanceInitialized"] = JsonNode(true);
		result["state"]["health"]["casualtyProvenance"].Vector().push_back(cohort);
	}
	else result["state"][fields.at(index)] = index == 3 || index == 4 ? JsonNode(true) : JsonNode(1);
	return result;
}
}

TEST(BattleUnitStateCompatibilityTest, ThirteenFeaturesAcrossSixRealCarriersAndVersionBoundaries)
{
	JsonNode rows;
	const std::vector<std::byte> prefix{std::byte{0xa5}, std::byte{0x6b}, std::byte{0x91}};
	for(const auto carrier : CARRIERS)
		for(size_t index = 0; index < FEATURES.size(); ++index)
			for(const auto version : {previous(FEATURES[index].boundary), FEATURES[index].boundary, Version::CURRENT})
				for(const auto shape : SHAPES)
				{
					const auto & feature = FEATURES[index];
					const auto name = label(carrier, feature, version, shape);
					SCOPED_TRACE(name);
					withCarrier(carrier, featureData(index, shape), [&](auto & packet)
					{
						const auto result = encode(packet, version);
						const auto seeded = encode(packet, version, prefix);
						const bool malformed = shape == Shape::MALFORMED && feature.malformed;
						const bool unsupported = shape == Shape::POPULATED && version < feature.boundary;
						const auto expected = malformed ? feature.malformed : unsupported ? feature.unsupported : "";
						EXPECT_EQ(result.error, expected); EXPECT_EQ(seeded.error, result.error);
						if(malformed || unsupported)
						{
							EXPECT_TRUE(result.bytes.empty()); EXPECT_EQ(seeded.bytes, prefix);
						}
						else if(result.error.empty())
						{
							auto expectedBytes = prefix; expectedBytes.insert(expectedBytes.end(), result.bytes.begin(), result.bytes.end());
							EXPECT_EQ(seeded.bytes, expectedBytes);
							EXPECT_EQ(encode(packet, version).bytes, result.bytes);
							roundTrip(packet, carrier, version, result.bytes);
						}
						rows.Vector().push_back(row(name, result));
						rows.Vector().push_back(row(name + "/prefilled", seeded));
						return 0;
					});
				}
	ASSERT_EQ(rows.Vector().size(), 1872u);
	publish(rows);
}

TEST(BattleUnitStateCompatibilityTest, ActualNestedQueriesKeepEnvelopeAndDirectOrderAndDuplicateChecks)
{
	const std::vector<int> envelope{static_cast<int>(Version::NEW_HORIZONS_HEROIC_SPIRIT),
		static_cast<int>(Version::NEW_HORIZONS_OVERWATCH), static_cast<int>(Version::NEW_HORIZONS_VETERAN_COHESION),
		static_cast<int>(Version::NEW_HORIZONS_DIPLOMACY_COHORTS), static_cast<int>(Version::NEW_HORIZONS_RECRUITMENT_TRAINING),
		static_cast<int>(Version::NEW_HORIZONS_PLAGUEBEARER), static_cast<int>(Version::NEW_HORIZONS_FROZEN),
		static_cast<int>(Version::NEW_HORIZONS_SPELLCRAFT_TARGET_DURATION), static_cast<int>(Version::NEW_HORIZONS_SAFE_BATTLE_FORMS),
		static_cast<int>(Version::NEW_HORIZONS_CONFUSION_MARKER), static_cast<int>(Version::NEW_HORIZONS_CONFUSION_STATE),
		static_cast<int>(Version::NEW_HORIZONS_MORALE_EXTRA_DAMAGE), static_cast<int>(Version::NEW_HORIZONS_OPPORTUNIST)};
	auto direct = envelope;
	direct.erase(direct.begin(), direct.begin() + 2);
	direct.erase(direct.begin() + 9);
	direct.insert(direct.begin(), static_cast<int>(Version::NEW_HORIZONS_MORALE_EXTRA_DAMAGE));
	direct.insert(direct.begin() + 6, envelope.begin(), envelope.begin() + 2);
	JsonNode rows;
	for(const auto carrier : CARRIERS)
		withCarrier(carrier, allActiveData(), [&](auto & packet)
		{
			TraceHandler<true> trace;
			ASSERT_NO_THROW(trace.visit(packet));
			const auto & prefix = carrier == Carrier::UNIT ? direct : envelope;
			ASSERT_GE(trace.queries.size(), prefix.size());
			EXPECT_TRUE(std::equal(prefix.begin(), prefix.end(), trace.queries.begin()));
			ASSERT_EQ(trace.unitStarts.size(), 1u);
			const auto start = trace.unitStarts.front();
			ASSERT_GE(trace.queries.size(), start + direct.size());
			EXPECT_TRUE(std::equal(direct.begin(), direct.end(), trace.queries.begin() + start));
			const auto expectedCount = carrier == Carrier::UNIT ? 1 : carrier == Carrier::UNITS || carrier == Carrier::HIT ? 2 : 3;
			EXPECT_EQ(std::count(trace.queries.begin(), trace.queries.end(), static_cast<int>(Version::NEW_HORIZONS_HEROIC_SPIRIT)), expectedCount);
			rows.Vector().push_back(traceRow(carrierName(carrier), trace.queries));
		});
	publish(rows);
}

TEST(BattleUnitStateCompatibilityTest, FirstFailureIsLazyAndDirectMoralePrecedesEnvelopeHeroicSpirit)
{
	JsonNode data = featureData(0, Shape::POPULATED);
	data["state"]["moraleExtraActivation"] = JsonNode(true);
	data["state"]["hadMorale"] = JsonNode(true);
	JsonNode rows;
	for(const auto carrier : CARRIERS)
		withCarrier(carrier, data, [&](auto & packet)
		{
			TraceHandler<true> trace;
			trace.version = previous(Version::NEW_HORIZONS_HEROIC_SPIRIT);
			EXPECT_THROW(trace.visit(packet), std::runtime_error);
			ASSERT_EQ(trace.queries.size(), 1u);
			EXPECT_EQ(trace.queries.front(), static_cast<int>(carrier == Carrier::UNIT
				? Version::NEW_HORIZONS_MORALE_EXTRA_DAMAGE : Version::NEW_HORIZONS_HEROIC_SPIRIT));
			const auto result = encode(packet, trace.version);
			EXPECT_EQ(result.error, carrier == Carrier::UNIT ? FEATURES[11].unsupported : FEATURES[0].unsupported);
			EXPECT_TRUE(result.bytes.empty());
			rows.Vector().push_back(row(carrierName(carrier), result));
			rows.Vector().push_back(traceRow(carrierName(carrier), trace.queries));
		});
	publish(rows);
}

TEST(BattleUnitStateCompatibilityTest, ReadingDoesNotRunEnvelopeFrozenFeedback)
{
	JsonNode rows;
	for(const auto carrier : CARRIERS)
		withCarrier(carrier, JsonNode(), [&](auto & packet)
		{
			if constexpr(std::is_same_v<std::remove_reference_t<decltype(packet)>, BattleStackAttacked>) packet.flags = BattleStackAttacked::SHATTER;
			if constexpr(std::is_same_v<std::remove_reference_t<decltype(packet)>, BattleAttack>)
				for(auto & value : packet.bsa) value.flags = BattleStackAttacked::SHATTER;
			if constexpr(std::is_same_v<std::remove_reference_t<decltype(packet)>, StacksInjured>)
				for(auto & value : packet.stacks) value.flags = BattleStackAttacked::SHATTER;
			TraceHandler<false> trace;
			trace.version = previous(Version::NEW_HORIZONS_FROZEN);
			ASSERT_NO_THROW(trace.visit(packet));
			EXPECT_EQ(std::count(trace.queries.begin(), trace.queries.end(), static_cast<int>(Version::NEW_HORIZONS_FROZEN)), 0);
			rows.Vector().push_back(traceRow(carrierName(carrier), trace.queries));
		});
	publish(rows);
}

TEST(BattleUnitStateCompatibilityTest, FrozenShatterAndLastStandErrorsKeepTheirCarrierSpecificSeams)
{
	JsonNode rows;
	for(const auto carrier : {Carrier::HIT, Carrier::TARGET, Carrier::INJURY})
		for(int scenario = 0; scenario < 4; ++scenario)
		{
			const auto data = scenario == 0 ? featureData(6, Shape::POPULATED)
				: scenario == 1 ? featureData(8, Shape::POPULATED) : JsonNode();
			const auto version = previous(scenario == 1 ? Version::NEW_HORIZONS_SAFE_BATTLE_FORMS : Version::NEW_HORIZONS_FROZEN);
			withCarrier(carrier, data, [&](auto & packet)
			{
				using Packet = std::remove_reference_t<decltype(packet)>;
				if constexpr(std::is_same_v<Packet, BattleStackAttacked> || std::is_same_v<Packet, BattleAttack>
					|| std::is_same_v<Packet, StacksInjured>)
				{
					auto & selectedHit = affectedHit(packet);
					selectedHit.flags = BattleStackAttacked::SHATTER;
					if(scenario == 2) selectedHit.armorerLastStandSide = static_cast<BattleSide>(99);
					const char * expected = carrier == Carrier::HIT ? "Cannot discard Frozen Shatter attack feedback"
						: carrier == Carrier::TARGET ? "Cannot discard Frozen Shatter in an older battle attack format"
						: "Cannot discard Frozen Shatter in an older injury format";
					if(scenario == 0) expected = FEATURES[6].unsupported;
					if(scenario == 1 && carrier == Carrier::HIT) expected = FEATURES[8].unsupported;
					if(scenario == 2 && carrier == Carrier::HIT) expected = "Invalid Last Stand attack side";
					const auto name = std::string(carrierName(carrier)) + "/seam/" + std::to_string(scenario);
					expectPreflight(name.c_str(), packet, version, expected, rows);
				}
			});
		}
	publish(rows);
}

TEST(BattleUnitStateCompatibilityTest, NonShatteredHitsPreserveLazyOuterFrozenFeatureQueries)
{
	JsonNode rows;
	for(const auto carrier : {Carrier::HIT, Carrier::TARGET, Carrier::INJURY})
		withCarrier(carrier, JsonNode(), [&](auto & packet)
		{
			TraceHandler<true> trace;
			trace.version = previous(Version::NEW_HORIZONS_FROZEN);
			ASSERT_NO_THROW(trace.visit(packet));
			ASSERT_TRUE(trace.firstField);
			const auto count = std::count(trace.queries.begin(), trace.queries.begin() + *trace.firstField,
				static_cast<int>(Version::NEW_HORIZONS_FROZEN));
			EXPECT_EQ(count, carrier == Carrier::HIT ? 1 : 0);
			rows.Vector().push_back(traceRow(carrierName(carrier), trace.queries));
		});
	publish(rows);
}

TEST(BattleUnitStateCompatibilityTest, MultipleRowsAndAttackContainersKeepFirstFailureOrder)
{
	const auto version = previous(Version::NEW_HORIZONS_HEROIC_SPIRIT);
	const auto heroic = featureData(0, Shape::POPULATED);
	const auto frozen = featureData(6, Shape::MALFORMED);
	JsonNode rows;
	BattleUnitsChanged units;
	units.battleID = BattleID(4);
	units.changedStacks = {change(heroic), change(frozen)};
	expectPreflight("units/heroic-first", units, version, FEATURES[0].unsupported, rows);
	std::reverse(units.changedStacks.begin(), units.changedStacks.end());
	expectPreflight("units/frozen-first", units, version, FEATURES[6].malformed, rows);
	units.changedStacks = {change(JsonNode()), change(frozen)};
	expectPreflight("units/second-row", units, version, FEATURES[6].malformed, rows);
	BattleAttack attack;
	attack.battleID = attack.attackerChanges.battleID = BattleID(4);
	attack.attackerChanges.changedStacks = {change(frozen)};
	attack.bsa = {hit(heroic)};
	expectPreflight("attack/attacker-before-target", attack, version, FEATURES[6].malformed, rows);
	attack.attackerChanges.changedStacks.clear();
	attack.bsa = {hit(JsonNode()), hit(frozen)};
	expectPreflight("attack/second-target", attack, version, FEATURES[6].malformed, rows);
	StacksInjured injury;
	injury.battleID = BattleID(4); injury.stacks = attack.bsa;
	expectPreflight("injury/second-hit", injury, version, FEATURES[6].malformed, rows);
	publish(rows);
}

TEST(BattleUnitStateCompatibilityTest, ExistingTailGuardsKeepExplicitPreflightLateAndUnguardedCategories)
{
	JsonNode rows;
	const std::vector<std::byte> prefix{std::byte{0xa3}};
	for(const auto carrier : CARRIERS)
		for(size_t i = 0; i < TAILS.size(); ++i)
			withCarrier(carrier, tailData(i), [&](auto & packet)
			{
				const auto & tail = TAILS[i];
				const auto name = std::string(carrierName(carrier)) + "/tail/" + tail.name;
				SCOPED_TRACE(name);
				const auto result = encode(packet, previous(tail.boundary));
				const auto seeded = encode(packet, previous(tail.boundary), prefix);
				const bool accepted = (tail.acceptedCarriers & bit(carrier)) != 0;
				EXPECT_EQ(result.error.empty(), accepted); EXPECT_EQ(result.error, seeded.error);
				if(tail.preflightCarriers & bit(carrier))
				{
					EXPECT_TRUE(result.bytes.empty()); EXPECT_EQ(seeded.bytes, prefix);
				}
				else
				{
					EXPECT_FALSE(result.bytes.empty());
					auto expected = prefix; expected.insert(expected.end(), result.bytes.begin(), result.bytes.end());
					EXPECT_EQ(seeded.bytes, expected);
				}
				auto value = row(name, result);
				value["category"] = JsonNode(accepted ? "legacy-unchecked" : (tail.preflightCarriers & bit(carrier)) ? "preflight-rejection" : "late-rejection");
				rows.Vector().push_back(value); rows.Vector().push_back(row(name + "/prefilled", seeded));
				return 0;
			});
	publish(rows);
}

TEST(BattleUnitStateCompatibilityTest, UnchangedSylvanAndRelentlessChecksRetainNonzeroPrefixRejection)
{
	JsonNode rows;
	BattleAttack attack;
	attack.battleID = attack.attackerChanges.battleID = BattleID(4);
	const std::vector<std::byte> prefix{std::byte{0x74}, std::byte{0x19}};
	for(const bool relentless : {false, true})
	{
		attack.fortuneState.reset(); attack.fortuneSide = BattleSide::NONE;
		attack.relentlessAssaultState.reset(); attack.relentlessAssaultSide = BattleSide::NONE;
		if(relentless) { attack.relentlessAssaultState.emplace(); attack.relentlessAssaultSide = BattleSide::ATTACKER; }
		else { attack.fortuneState.emplace(); attack.fortuneSide = BattleSide::ATTACKER; }
		const auto version = previous(relentless ? Version::NEW_HORIZONS_RELENTLESS_ASSAULT : Version::NEW_HORIZONS_SYLVAN_LUCK);
		const auto result = encode(attack, version);
		const auto seeded = encode(attack, version, prefix);
		EXPECT_EQ(result.error, relentless ? "Cannot discard Relentless Assault attack state" : "Cannot discard Sylvan Luck strike state");
		EXPECT_EQ(seeded.error, result.error); EXPECT_FALSE(result.bytes.empty());
		auto expected = prefix; expected.insert(expected.end(), result.bytes.begin(), result.bytes.end());
		EXPECT_EQ(seeded.bytes, expected);
		rows.Vector().push_back(row(relentless ? "relentless-late" : "sylvan-late", result));
		rows.Vector().push_back(row(relentless ? "relentless-late/prefilled" : "sylvan-late/prefilled", seeded));
	}
	publish(rows);
}

TEST(BattleUnitStateCompatibilityTest, ConfusionAndMoraleRetainDifferentDirectAndEnvelopePriority)
{
	auto data = featureData(9, Shape::POPULATED);
	data["state"]["moraleExtraActivation"] = JsonNode(true);
	data["state"]["hadMorale"] = JsonNode(true);
	JsonNode rows;
	for(const auto carrier : CARRIERS)
		withCarrier(carrier, data, [&](auto & packet)
		{
			expectPreflight(carrierName(carrier), packet, previous(Version::NEW_HORIZONS_CONFUSION_MARKER),
				carrier == Carrier::UNIT ? FEATURES[11].unsupported : FEATURES[9].unsupported, rows);
		});
	publish(rows);
}

TEST(BattleUnitStateCompatibilityTest, RangeEnumIdentityAndProvenanceNegativesUseExistingStrictParsers)
{
	struct Invalid { std::string name; JsonNode data; const char * message; };
	std::vector<Invalid> invalid;
	for(const auto feature : {size_t(1), size_t(6)})
		for(const auto value : {int64_t(-2), int64_t(std::numeric_limits<int32_t>::max()) + 1})
		{
			auto data = featureData(feature, Shape::DEFAULT_VALUE);
			data["state"][feature == 1 ? "battlecraftOverwatchReadyRound" : "frozenAppliedRound"] = JsonNode(value);
			invalid.push_back({std::string(FEATURES[feature].name) + "/" + std::to_string(value), data, FEATURES[feature].malformed});
		}
	auto data = featureData(0, Shape::DEFAULT_VALUE);
	data["state"]["heroicSpiritMoralePending"] = JsonNode(true);
	invalid.push_back({"heroic/unearned-pending", data, "Pending Heroic Spirit Morale requires its retaliation receipt"});
	data = featureData(11, Shape::POPULATED);
	data["state"]["hadMorale"] = JsonNode(false);
	invalid.push_back({"morale/unearned", data, "Morale activation provenance requires its earned receipt"});
	data = featureData(9, Shape::POPULATED);
	data["state"]["confusion"]["pendingCaster"] = JsonNode(PlayerColor::CANNOT_DETERMINE.getNum());
	invalid.push_back({"confusion/caster", data, "Invalid Confusion pending caster provenance"});
	data = featureData(10, Shape::DEFAULT_VALUE);
	data["state"]["confusion"]["unexpected"] = JsonNode(true);
	invalid.push_back({"confusion/unknown-key", data, "Unknown Confusion state metadata"});
	data = featureData(5, Shape::POPULATED);
	data["bonuses"].Vector().at(0)["addInfo"]["propagationLimit"] = JsonNode(1.5);
	invalid.push_back({"plague/fraction", data, "Invalid captured Plague propagation limit"});
	JsonNode rows;
	for(const auto carrier : CARRIERS)
		for(const auto & item : invalid)
			for(const auto version : {previous(Version::NEW_HORIZONS_CONFUSION_STATE), Version::CURRENT})
				withCarrier(carrier, item.data, [&](auto & packet)
				{
					const auto name = std::string(carrierName(carrier)) + "/" + item.name + "/" + std::to_string(static_cast<int>(version));
					expectPreflight(name.c_str(), packet, version, item.message, rows);
				});
	// Unrecognized training identities are intentionally not a new validation rule.
	for(const auto carrier : CARRIERS)
		withCarrier(carrier, featureData(4, Shape::DEFAULT_VALUE), [&](auto & packet)
		{
			selected(packet, carrier).data["bonuses"].Vector().at(0)["stacking"] = JsonNode("not-a-training-marker");
			const auto result = encode(packet, previous(Version::NEW_HORIZONS_RECRUITMENT_TRAINING));
			EXPECT_TRUE(result.error.empty()); rows.Vector().push_back(row(carrierName(carrier), result));
			return 0;
		});
	publish(rows);
}

TEST(BattleUnitStateCompatibilityTest, DirectOldReaderKeepsMidReadFencesAndUncheckedTrainingMetadata)
{
	JsonNode rows;
	for(size_t i = 0; i < FEATURES.size(); ++i)
	{
		SCOPED_TRACE(FEATURES[i].name);
		auto packet = change(featureData(i, Shape::POPULATED));
		const auto wire = encode(packet, Version::CURRENT);
		ASSERT_TRUE(wire.error.empty());
		CMemorySerializer memory(wire.bytes);
		memory.iser.version = previous(FEATURES[i].boundary);
		UnitChanges restored(99, UnitChanges::EOperation::REMOVE);
		std::string error;
		try { memory.iser & restored; }
		catch(const std::runtime_error & exception) { error = exception.what(); }
		const bool unchecked = i == 3 || i == 4; // Existing direct reader does not run training validation.
		EXPECT_EQ(error, unchecked ? "" : FEATURES[i].unsupported);
		EXPECT_EQ(restored.id, packet.id); EXPECT_EQ(restored.healthDelta, packet.healthDelta);
		EXPECT_EQ(restored.data, packet.data);
		EXPECT_EQ(restored.operation, i == 5 || i == 11 ? UnitChanges::EOperation::REMOVE : UnitChanges::EOperation::UPDATE);
		JsonNode result;
		result["feature"] = JsonNode(FEATURES[i].name);
		result["exception"] = JsonNode(error.empty() ? "none" : "std::runtime_error");
		result["message"] = JsonNode(error);
		result["operation"] = JsonNode(static_cast<int>(restored.operation));
		result["payloadHex"] = JsonNode(hex(wire.bytes));
		result["category"] = JsonNode(unchecked ? "legacy-reader-unchecked" : "existing-reader-rejection");
		rows.Vector().push_back(result);
	}
	publish(rows);
}

TEST(BattleUnitStateCompatibilityTest, SupportedTailBoundariesAndCurrentRoundTripAcrossAllCarriers)
{
	JsonNode rows;
	for(const auto carrier : CARRIERS)
		for(size_t i = 0; i < TAILS.size(); ++i)
			for(const auto version : {TAILS[i].boundary, Version::CURRENT})
				withCarrier(carrier, tailData(i), [&](auto & packet)
				{
					const auto name = std::string(carrierName(carrier)) + "/supported/" + TAILS[i].name
						+ "/" + std::to_string(static_cast<int>(version));
					SCOPED_TRACE(name);
					const auto result = encode(packet, version);
					EXPECT_TRUE(result.error.empty()); EXPECT_FALSE(result.bytes.empty());
					if(result.error.empty()) roundTrip(packet, carrier, version, result.bytes);
					rows.Vector().push_back(row(name, result));
					return 0;
				});
	ASSERT_EQ(rows.Vector().size(), 120u);
	publish(rows);
}
