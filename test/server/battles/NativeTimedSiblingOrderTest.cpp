/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/combatScripts/NativeJsonParameters.h"
#include "../../../lib/gameState/GameStatePackVisitor.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/battle/PhysicalAffliction.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include "../../../lib/battle/NewHorizonsGraveDust.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/effects/native/Timed.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/GameSettings.h"
#include "../../mock/mock_ServerCallback.h"
#include "../../spells/HistoricalCrusadeRules.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"
#include <numeric>

namespace
{
struct ObservableState
{
	JsonNode unitState;
	std::vector<std::string> localEffects;
	std::vector<std::string> afflictions;
	std::vector<int64_t> values;
	bool operator==(const ObservableState &) const = default;
};

bool fixedDiseaseMarker(const Bonus & bonus)
{
	return bonus.type == BonusType::PHYSICAL_AFFLICTION && bonus.source == BonusSource::SPELL_EFFECT
		&& bonus.sid == BonusSourceID(SpellID(SpellID::DISEASE))
		&& physicalAfflictions::markerMetadata(bonus)->kind == "disease";
}

std::string comparableBonus(const Bonus & bonus, bool diseaseContract)
{
	auto data = bonus.toJsonNode();
	// The sole exception is independently asserted below, never silently applied
	// to other producers, values, timers, source identities or marker parameters.
	if(diseaseContract && fixedDiseaseMarker(bonus)) data["addInfo"].Struct().erase("applicationOrder");
	return data.toCompactString();
}
}

class NativeTimedSiblingOrderTest : public BattleTestFixture
{
protected:
	ObservableState observe(CStack & unit, CStack & enemy, bool diseaseContract = false)
	{
		ObservableState result;
		result.unitState = unit.save();
		for(const auto & bonus : battle()->getLocalSpellEffects(unit.unitId())) result.localEffects.push_back(comparableBonus(bonus, diseaseContract));
		// Packet order is tested separately, without normalization. This compares
		// final membership plus queries/actions after real authoritative replay.
		std::sort(result.localEffects.begin(), result.localEffects.end());
		for(const auto & value : physicalAfflictions::enumerate(unit))
		{
			const bool disease = diseaseContract && value.kind == "disease" && value.source == BonusSource::SPELL_EFFECT
				&& value.sourceID == BonusSourceID(SpellID(SpellID::DISEASE));
			result.afflictions.push_back(value.kind + "/" + value.sourceID.toString() + "/"
				+ (disease ? "separately-asserted-disease-stamp" : std::to_string(value.applicationOrder)));
			std::vector<std::string> removal;
			for(const auto & bonus : physicalAfflictions::removalPlan(unit, value))
				removal.push_back(comparableBonus(bonus, diseaseContract));
			std::sort(removal.begin(), removal.end()); // Removal membership, with full marker contents.
			result.afflictions.insert(result.afflictions.end(), removal.begin(), removal.end());
		}
		for(const auto type : {BonusType::PRIMARY_SKILL, BonusType::STACK_HEALTH, BonusType::POISON,
			BonusType::GENERAL_ATTACK_REDUCTION, BonusType::GENERAL_DAMAGE_REDUCTION, BonusType::MORALE, BonusType::LUCK,
			BonusType::STACKS_INITIATIVE_FLAT, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
			BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS, BonusType::MINIMUM_MORALE, BonusType::SANCTIFIED,
			BonusType::STACKS_INITIATIVE, BonusType::STACKS_SPEED, BonusType::MAXIMUM_LUCK, BonusType::FORGETFULL,
			BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS, BonusType::CREATURE_ABILITY_SUPPRESSION,
			BonusType::ADDITIONAL_ATTACK, BonusType::PERCENTAGE_DAMAGE_BOOST})
			result.values.push_back(unit.valOfBonuses(type));
		result.values.push_back(unit.getMovementRange());
		result.values.push_back(unit.getInitiative(0));
		for(const auto attack : {BattleAttackInfo(&unit, &enemy, 0, false), BattleAttackInfo(&enemy, &unit, 0, false),
			BattleAttackInfo(&unit, &enemy, 0, true), BattleAttackInfo(&enemy, &unit, 0, true)})
		{
			const auto estimate = battle()->calculateDmgRange(attack);
			result.values.insert(result.values.end(), {estimate.damage.min, estimate.damage.max,
				estimate.kills.min, estimate.kills.max, estimate.damageBeforeDefense.min, estimate.damageBeforeDefense.max});
		}
		return result;
	}
	void applyPacket(SetStackEffect packet)
	{
		BattleStatePackVisitor visitor(*battle());
		packet.visit(visitor);
	}
	std::vector<SetStackEffect> captureTimed(CStack & unit, const char * key, int level, int power = 125, bool mass = false)
	{
		using namespace testing;
		const auto id = SpellID(SpellID::decode(key));
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, id.toSpell());
		cast.setSpellLevel(level); cast.setEffectPower(power); cast.setEffectDuration(4); cast.setMassSlow(mass);
		const auto mechanics = id.toSpell()->battleMechanics(&cast);
		std::vector<SetStackEffect> result;
		StrictMock<ServerCallbackMock> recording;
		EXPECT_CALL(recording, describeChanges()).Times(AnyNumber()).WillRepeatedly(Return(false));
		EXPECT_CALL(recording, apply(Matcher<SetStackEffect &>(_))).Times(AnyNumber())
			.WillRepeatedly(Invoke([&](SetStackEffect & packet) { result.push_back(packet); }));
		const auto & definition = id.toSpell()->getLevelInfo(level);
		const auto run = [&](const JsonNode & config)
		{
			spells::effects::native::Timed effect;
			effect.init(config);
			effect.apply(&recording, mechanics.get(), {spells::Destination(&unit)});
		};
		if(definition.battleEffects.isNull())
		{
			JsonNode config;
			config["bonus"] = definition.effects;
			run(config);
		}
		else for(const auto & [name, effect] : definition.battleEffects.Struct())
		{
			EXPECT_TRUE(effect["type"].String() == "timed" || effect["type"].String() == "core:timed");
			run(effect);
		}
		return result;
	}
	static std::vector<Bonus> additions(const std::vector<SetStackEffect> & packets)
	{
		std::vector<Bonus> result;
		for(const auto & packet : packets)
		{
			EXPECT_TRUE(packet.toAdd.empty());
			for(const auto & [id, bonuses] : packet.toUpdate) result.insert(result.end(), bonuses.begin(), bonuses.end());
		}
		return result;
	}
	void allPermutations(CStack & unit, CStack & enemy, const std::vector<Bonus> & bonuses,
		const std::function<std::vector<SetStackEffect>()> & recast = {})
	{
		ASSERT_GE(bonuses.size(), 1u);
		ASSERT_LE(bonuses.size(), 5u);
		const auto baseline = unit.save();
		const auto baselineMorale = unit.valOfBonuses(BonusType::MORALE);
		const auto diseaseMarker = std::find_if(bonuses.begin(), bonuses.end(), fixedDiseaseMarker);
		const bool diseaseContract = diseaseMarker != bonuses.end();
		if(diseaseContract)
		{
			ASSERT_TRUE(physicalAfflictions::enumerate(unit).empty());
		}
		std::vector<size_t> order(bonuses.size());
		std::iota(order.begin(), order.end(), 0);
		std::optional<ObservableState> expected, expectedAfterRemoval;
		const auto replay = [&](const std::vector<Bonus> & records)
		{
			ASSERT_EQ(records.size(), order.size());
			for(const auto index : order)
			{
				SetStackEffect packet;
				packet.battleID = battle()->getBattleID();
				packet.toUpdate.emplace_back(unit.unitId(), std::vector<Bonus>{records[index]});
				applyPacket(std::move(packet));
			}
		};
		do
		{
			SCOPED_TRACE(::testing::PrintToString(order));
			replay(bonuses);
			const int64_t expectedDiseaseOrder = diseaseContract && order.front() == static_cast<size_t>(diseaseMarker - bonuses.begin()) ? 1 : 0;
			const auto checkDiseaseOrder = [&]
			{
				if(!diseaseContract) return;
				const auto selected = physicalAfflictions::first(unit);
				ASSERT_TRUE(selected);
				EXPECT_EQ(selected->kind, "disease");
				EXPECT_EQ(selected->source, BonusSource::SPELL_EFFECT);
				EXPECT_EQ(selected->sourceID, BonusSourceID(SpellID(SpellID::DISEASE)));
				EXPECT_EQ(selected->applicationOrder, expectedDiseaseOrder);
				EXPECT_EQ(physicalAfflictions::removalPlan(unit, *selected).size(), bonuses.size());
			};
			checkDiseaseOrder();
			const auto state = observe(unit, enemy, diseaseContract);
			if(expected) EXPECT_EQ(state, *expected);
			else expected = state;
			if(recast)
			{
				const auto packets = recast();
				bool seenAddition = false;
				for(const auto & packet : packets)
				{
					if(!packet.toRemove.empty())
					{
						EXPECT_FALSE(seenAddition); // Keep production removal prelude in place.
						EXPECT_TRUE(packet.toUpdate.empty());
						applyPacket(packet);
					}
					seenAddition |= !packet.toUpdate.empty();
				}
				replay(additions(packets));
			}
			else replay(bonuses);
			checkDiseaseOrder(); // Refresh retains the actual old stamp, including zero.
			EXPECT_EQ(observe(unit, enemy, diseaseContract), state);
			// Check cleanup, including Keeper and physical cleanse selection.
			// Only the explicitly asserted Disease clock differs above.
			if(const auto affliction = physicalAfflictions::first(unit))
				battle()->removeUnitBonus(unit.unitId(), physicalAfflictions::removalPlan(unit, *affliction));
			std::vector<Bonus> markers;
			for(const auto & bonus : battle()->getLocalSpellEffects(unit.unitId()))
				if(bonus.type == BonusType::SANCTIFIED || bonus.type == BonusType::CREATURE_ABILITY_SUPPRESSION) markers.push_back(bonus);
			if(!markers.empty()) battle()->removeUnitBonus(unit.unitId(), markers);
			const auto removed = observe(unit, enemy);
			if(expectedAfterRemoval) EXPECT_EQ(removed, *expectedAfterRemoval);
			else expectedAfterRemoval = removed;
			if(std::any_of(bonuses.begin(), bonuses.end(), [](const Bonus & b) { return b.type == BonusType::SANCTIFIED; }))
			{
				EXPECT_EQ(unit.valOfBonuses(BonusType::MORALE), baselineMorale);
			}
			const auto local = battle()->getLocalSpellEffects(unit.unitId());
			battle()->removeUnitBonus(unit.unitId(), local);
			unit.load(baseline);
		}
		while(std::next_permutation(order.begin(), order.end()));
	}
};

TEST_F(NativeTimedSiblingOrderTest, CoreAbilityBundlesCommuteThroughAuthoritativeReplayAndDamageQueries)
{
	startGame();
	startBattle();
	auto * recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 15);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 15);
	for(const auto * key : {"core:stoneGaze", "core:paralyze", "core:disease", "core:poison"})
	{
		SCOPED_TRACE(key);
		const auto capture = [&] { return captureTimed(*recipient, key, 0); };
		const auto bonuses = additions(capture());
		ASSERT_GE(bonuses.size(), 2u);
		allPermutations(*recipient, *enemy, bonuses, capture);
	}
}

TEST_F(NativeTimedSiblingOrderTest, CuratedCrusadeAndChaosBundlesCommuteThroughAuthoritativeReplayAndDamageQueries)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires native curated preset";
	startGame();
	startBattle();
	auto * recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 15);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 15);
	for(const auto * key : {"new-horizons:crusade", "new-horizons:shieldOfChaos"})
	{
		SCOPED_TRACE(key);
		const auto capture = [&] { return captureTimed(*recipient, key, 0); };
		const auto bonuses = additions(capture());
		ASSERT_GE(bonuses.size(), 2u);
		allPermutations(*recipient, *enemy, bonuses, capture);
	}
}

TEST_F(NativeTimedSiblingOrderTest, LoadedPrayerAndBlindLevelsCommuteAfterRealConversion)
{
	startGame();
	startBattle();
	auto * recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 15);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 15);
	for(const auto * key : {"core:prayer", "core:blind"})
		for(const int level : {0, 1, 2, 3})
		{
			SCOPED_TRACE(key);
			SCOPED_TRACE(level);
			const auto capture = [&] { return captureTimed(*recipient, key, level); };
			const auto bonuses = additions(capture());
			ASSERT_GE(bonuses.size(), 2u);
			allPermutations(*recipient, *enemy, bonuses, capture);
		}
}

TEST_F(NativeTimedSiblingOrderTest, PoisonOrderPreservesWoundedCapacityTrackedHealth)
{
	startGame();
	startBattle();
	auto * recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 15);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 15);
	recipient->preserveCreatureHealthOnCapacityIncrease();
	int64_t injury = 3;
	recipient->damage(injury);
	const auto capture = [&] { return captureTimed(*recipient, "core:poison", 0); };
	const auto bonuses = additions(capture());
	ASSERT_EQ(bonuses.size(), 2u);
	allPermutations(*recipient, *enemy, bonuses, capture);
}

TEST_F(NativeTimedSiblingOrderTest, DiseaseStampOffsetsPreserveSavedClocksAndLaterAfflictionSelection)
{
	startGame(); startBattle();
	auto * recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 15);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 15);
	const auto baseline = recipient->save();
	const auto disease = additions(captureTimed(*recipient, "core:disease", 0));
	const auto marker = std::find_if(disease.begin(), disease.end(), fixedDiseaseMarker);
	ASSERT_NE(marker, disease.end());
	const auto frozen = newHorizonsFrozen::marker(BonusSourceID(creatureByName("core:iceElemental")), 1);
	const auto grave = newHorizonsGraveDust::records(2500, PlayerColor(0));
	const auto install = [&](std::vector<Bonus> values)
	{
		SetStackEffect packet;
		packet.battleID = battle()->getBattleID();
		packet.toUpdate.emplace_back(recipient->unitId(), std::move(values));
		applyPacket(std::move(packet));
	};
	const auto kindOrder = [&](const std::string & kind)
	{
		for(const auto & value : physicalAfflictions::enumerate(*recipient)) if(value.kind == kind) return value.applicationOrder;
		ADD_FAILURE() << "Missing affliction " << kind;
		return int64_t{-1};
	};
	for(const bool saved : {false, true})
	{
		SCOPED_TRACE(saved);
		std::optional<std::vector<std::string>> expectedRecords, expectedSelections;
		std::optional<ObservableState> expectedDuring, expectedClean;
		for(const bool markerFirst : {false, true})
		{
			SCOPED_TRACE(markerFirst);
			if(saved)
			{
				auto retained = grave;
				auto parameters = retained.front().parameters->toJsonNode();
				parameters["applicationOrder"].Integer() = 23;
				retained.front().parameters = std::make_shared<BonusParameters>(parameters);
				install(std::move(retained));
				EXPECT_EQ(kindOrder("graveDust"), 23); // Preexisting captured positive stamp remains exact.
			}
			if(markerFirst) install({*marker});
			for(const auto & value : disease) if(!fixedDiseaseMarker(value)) install({value});
			if(!markerFirst) install({*marker});
			const int64_t diseaseStamp = markerFirst ? (saved ? 24 : 1) : 0;
			EXPECT_EQ(kindOrder("disease"), diseaseStamp);
			install({frozen});
			const int64_t frozenStamp = std::max<int64_t>(saved ? 23 : 0, diseaseStamp) + 1;
			EXPECT_EQ(kindOrder("frozen"), frozenStamp);
			install(grave);
			const int64_t graveStamp = saved ? 23 : frozenStamp + 1;
			EXPECT_EQ(kindOrder("graveDust"), graveStamp);
			install(grave); // This existing group's refresh retains its exact stamp.
			EXPECT_EQ(kindOrder("frozen"), frozenStamp);
			EXPECT_EQ(kindOrder("graveDust"), graveStamp);
			std::vector<std::string> records;
			// All source groups, including non-spell Frozen and Grave Dust, must
			// retain every field except the three specifically asserted fresh clocks.
			for(const auto & affliction : physicalAfflictions::enumerate(*recipient))
				for(const auto & value : physicalAfflictions::removalPlan(*recipient, affliction))
				{
					auto json = value.toJsonNode();
					if(value.type == BonusType::PHYSICAL_AFFLICTION)
					{
						if(fixedDiseaseMarker(value)) json["addInfo"]["applicationOrder"].Integer() = 100;
						else if(value.source == frozen.source && value.sid == frozen.sid && affliction.kind == "frozen")
							json["addInfo"]["applicationOrder"].Integer() = 101;
						else if(!saved && newHorizonsGraveDust::isReservedSource(value) && affliction.kind == "graveDust")
							json["addInfo"]["applicationOrder"].Integer() = 102;
					}
					records.push_back(json.toCompactString());
				}
			std::sort(records.begin(), records.end());
			if(expectedRecords) EXPECT_EQ(records, *expectedRecords); else expectedRecords = records;
			auto during = observe(*recipient, *enemy);
			during.localEffects.clear(); during.afflictions.clear(); // Fully compared above with the bounded stamp exception.
			if(expectedDuring) EXPECT_EQ(during, *expectedDuring); else expectedDuring = during;
			battle()->removeUnitBonus(recipient->unitId(), newHorizonsFrozen::removalPlan(*recipient));
			install({newHorizonsFrozen::marker(frozen.sid, 2)}); // Eligible later-round application after removal.
			EXPECT_EQ(kindOrder("frozen"), std::max(diseaseStamp, graveStamp) + 1);
			EXPECT_LT(kindOrder("graveDust"), kindOrder("frozen"));
			std::vector<std::string> selected;
			while(const auto next = physicalAfflictions::first(*recipient))
			{
				selected.push_back(next->kind + "/" + next->sourceID.toString());
				const auto removed = physicalAfflictions::removalPlan(*recipient, *next);
				ASSERT_FALSE(removed.empty());
				battle()->removeUnitBonus(recipient->unitId(), removed);
			}
			ASSERT_EQ(selected.size(), 3u);
			EXPECT_TRUE(selected.front().starts_with("disease/"));
			if(expectedSelections) EXPECT_EQ(selected, *expectedSelections); else expectedSelections = selected;
			const auto clean = observe(*recipient, *enemy);
			if(expectedClean) EXPECT_EQ(clean, *expectedClean); else expectedClean = clean;
			recipient->load(baseline);
		}
	}
}

// Zero is ordinary core policy; 1/2 are saved NH versions, 3 is the captured
// pre-Sorcery v3 policy, and 4 is the current shipped v3 capture.
class NativeTimedDynamicOrderTest : public NativeTimedSiblingOrderTest, public testing::WithParamInterface<int>
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		NativeTimedSiblingOrderTest::mapLoaded(loaded);
		JsonNode rules;
		if(GetParam() == 1)
		{
			rules = historicalMagicV1Rules();
			// Captured v1 predates NH common spells; installed v2/v3 rows are
			// not historical saved policy and must not enter this fixture.
			std::erase_if(rules["spells"].Struct(), [](const auto & row)
			{
				return row.first.starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ':');
			});
		}
		else if(GetParam() > 1)
		{
			rules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
			if(GetParam() < 4) rules = historicalCrusadeRules(std::move(rules), GetParam());
		}
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, std::move(rules));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		// testHeroRules() is legacy development, without modern offer weights.
		captureLegacyPerkOfferPolicy(*loaded);
	}
};

TEST_P(NativeTimedDynamicOrderTest, ShippedDynamicFamiliesAndKeeperCommuteThroughApplicationRecastAndCleanup)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires native curated preset";
	startGame();
	for(const auto * skill : {"new-horizons:lightMagic", "new-horizons:chaosMagic", "new-horizons:sorceryMagic"})
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * recipient = addStack(BattleSide::ATTACKER, CreatureID::ARCHER, BattleHex(leftHex), 15);
	auto * enemy = addStack(BattleSide::DEFENDER, CreatureID::ARCHER, BattleHex(rightHex), 15);
	for(const auto type : {BonusType::ADDITIONAL_ATTACK, BonusType::PERCENTAGE_DAMAGE_BOOST})
		recipient->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, type,
			BonusSource::CREATURE_ABILITY, 1, BonusSourceID(recipient->creatureId())));
	for(const bool perks : {false, true})
	{
		SCOPED_TRACE(perks);
		if(perks)
		{
			attackerSideHero->applyPerkSelection({"new-horizons:lightMagic", "new-horizons:lightMagic.sanctuaryKeeper"});
			// Mindbreaker is Advanced; Blinkmaster fills its Basic prerequisite
			// without changing any of the four Timed spell families below.
			attackerSideHero->applyPerkSelection({"new-horizons:chaosMagic", "new-horizons:chaosMagic.blinkmaster"});
			attackerSideHero->applyPerkSelection({"new-horizons:chaosMagic", "new-horizons:chaosMagic.mindbreaker"});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.sanctuaryKeeper"));
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.blinkmaster"));
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.mindbreaker"));
		}
		for(const auto * key : {"core:slow", "core:misfortune", "core:forgetfulness", "new-horizons:sanctuary"})
			for(const int level : {0, 3})
				for(const int power : {10, 200})
					for(const bool mass : {false, true})
					{
						if(mass && (std::string(key) != "core:slow" || GetParam() < 3)) continue;
						SCOPED_TRACE(key);
						SCOPED_TRACE(level);
						SCOPED_TRACE(power);
						SCOPED_TRACE(mass);
						const auto capture = [&] { return captureTimed(*recipient, key, level, power, mass); };
						const auto bonuses = additions(capture());
						const bool pair = std::string(key) == "core:slow" ? GetParam() == 4
							: std::string(key) == "new-horizons:sanctuary" ? GetParam() >= 3 && perks : GetParam() >= 3;
						ASSERT_EQ(bonuses.size(), pair ? 2u : 1u);
						if(std::string(key) == "core:forgetfulness" && GetParam() >= 3)
						{
							for(const auto & bonus : bonuses)
								if(bonus.type == BonusType::CREATURE_ABILITY_SUPPRESSION)
								{
									EXPECT_EQ(bonus.val, perks ? 2 : 1);
								}
						}
						allPermutations(*recipient, *enemy, bonuses, capture);
					}
	}
	if(GetParam() >= 3)
		for(const bool reverse : {false, true})
		{
			const auto baselineMorale = recipient->valOfBonuses(BonusType::MORALE);
			auto keeper = captureTimed(*recipient, "new-horizons:sanctuary", 0);
			ASSERT_EQ(additions(keeper).size(), 2u);
			if(reverse) std::reverse(keeper.begin(), keeper.end());
			for(const auto & packet : keeper) applyPacket(packet);
			EXPECT_EQ(recipient->valOfBonuses(BonusType::MORALE), baselineMorale + 2);
			const auto light = SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic"));
			attackerSideHero->setSecSkillLevel(light, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
			const auto ordinary = captureTimed(*recipient, "new-horizons:sanctuary", 0);
			ASSERT_EQ(additions(ordinary).size(), 1u);
			for(const auto & packet : ordinary) applyPacket(packet);
			attackerSideHero->setSecSkillLevel(light, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			EXPECT_TRUE(recipient->hasBonusOfType(BonusType::SANCTIFIED));
			EXPECT_EQ(recipient->valOfBonuses(BonusType::MORALE), baselineMorale);
			battle()->removeUnitBonus(recipient->unitId(), battle()->getLocalSpellEffects(recipient->unitId()));
		}
}

INSTANTIATE_TEST_SUITE_P(SavedPolicies, NativeTimedDynamicOrderTest, testing::Values(0, 1, 2, 3, 4));
