/*
 * ProjectedUnitPacketSidecarsTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/battle/ProjectedBattleWorldArena.h"
#include "../../lib/battle/NewHorizonsCrisisCommand.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../lib/battle/NewHorizonsBloodrage.h"
#include "../../lib/battle/CrisisCommandReferences.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <stdexcept>

namespace
{
using Arena = battle::ProjectedBattleWorldArena;
using Operation = BattleChanges::EOperation;
template<typename T> std::vector<std::byte> encoded(T value)
{
	CMemorySerializer writer;
	writer.oser & value;
	return writer.extractBuffer();
}

class ProjectedUnitPacketSidecarsTest : public HeroCommandFixture
{
protected:
	std::unique_ptr<Arena> arena;
	CStack * first = nullptr;
	CStack * second = nullptr;
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		ASSERT_TRUE(newHorizonsCrisisCommand::activeProfile(perks));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}
	void select(CGHeroInstance * hero)
	{
		const SecondarySkill command(SecondarySkill::decode(newHorizonsCrisisCommand::SKILL));
		hero->setSecSkillLevel(command, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({newHorizonsCrisisCommand::SKILL, "new-horizons:command.aggressiveCommander"});
		hero->applyPerkSelection({newHorizonsCrisisCommand::SKILL, newHorizonsCrisisCommand::PERK});
		ASSERT_TRUE(newHorizonsCrisisCommand::eligible(hero));
		const SecondarySkill rage(SecondarySkill::decode("new-horizons:bloodrage"));
		hero->setSecSkillLevel(rage, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({"new-horizons:bloodrage", "new-horizons:bloodrage.firstBlood"});
		ASSERT_TRUE(newHorizonsBloodrage::hasFirstBlood(hero));
	}
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		startGame();
		ASSERT_NO_FATAL_FAILURE(select(attackerSideHero));
		ASSERT_NO_FATAL_FAILURE(select(defenderSideHero));
		startBattle();
		first = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 4);
		second = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(rightHex), 4);
	}
	void TearDown() override
	{
		arena.reset();
		HeroCommandFixture::TearDown();
	}
	void capture() { arena = std::make_unique<Arena>(*battle()); }
	UnitChanges update(CStack & unit, const JsonNode & state, int64_t delta)
	{
		UnitChanges result(unit.unitId(), Operation::UPDATE);
		result.data = state;
		result.healthDelta = delta;
		return result;
	}
	UnitChanges death(CStack & unit)
	{
		const auto initial = unit.save();
		const auto amount = unit.getAvailableHealth();
		int64_t damage = amount;
		unit.damage(damage);
		const auto requested = unit.save();
		unit.load(initial);
		EXPECT_EQ(unit.save(), initial);
		return update(unit, requested, -amount);
	}
	UnitChanges addition(uint32_t id)
	{
		battle::UnitInfo info;
		info.type = CreatureID(0);
		info.count = 1;
		info.side = BattleSide::ATTACKER;
		info.position = BattleHex(leftHex - 3);
		info.summoned = false;
		UnitChanges result(id, Operation::ADD);
		info.save(result.data);
		return result;
	}
	void scalarParity(const std::vector<UnitChanges> & changes)
	{
		const auto originalFirst = first->save();
		const auto originalCrisis = encoded(battle()->getCrisisCommandState());
		capture();
		const auto generation = PreparedBonusGraphStorage::currentGeneration();
		arena->applyUnitChanges(changes);
		std::vector<JsonNode> expected;
		for(size_t i = 0; i < arena->size(); ++i)
			expected.push_back(arena->snapshotAt(i));
		const auto crisis = encoded(arena->crisisState());
		const auto deaths = arena->bloodrageDeaths();
		const auto spent = arena->firstBloodUsed();
		const std::array<int, 2> rage{arena->bloodrageDamage(BattleSide::ATTACKER), arena->bloodrageDamage(BattleSide::DEFENDER)};
		EXPECT_EQ(first->save(), originalFirst);
		EXPECT_EQ(encoded(battle()->getCrisisCommandState()), originalCrisis);
		EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
		arena.reset(); // retire all borrowed views BEFORE scalar replay/application
		BattleUnitsChanged pack;
		pack.battleID = BattleID(0);
		pack.changedStacks = changes;
		gameState()->apply(pack); // actual shared GameState visitor, not RecordingServer
		ASSERT_EQ(battle()->stacks.size(), expected.size());
		for(size_t i = 0; i < expected.size(); ++i)
		{
			EXPECT_EQ(battle()->stacks[i]->save(), expected[i]);
		}
		EXPECT_EQ(encoded(battle()->getCrisisCommandState()), crisis);
		EXPECT_EQ(battle()->bloodrageDestroyedUnits, deaths);
		EXPECT_EQ(battle()->bloodrageFirstBloodUsed, spent);
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			EXPECT_EQ(battle()->getBloodrageDamagePercent(side), rage.at(static_cast<size_t>(side)));
		}
	}
};

TEST_F(ProjectedUnitPacketSidecarsTest, EmptyEntryListStillUsesOriginalCrisisValidation)
{
	scalarParity({});
}

TEST_F(ProjectedUnitPacketSidecarsTest, NegativeUpdateDeathCapturesCrisisButNotRemoveOnlyBloodrage)
{
	const auto killed = death(*first);
	capture();
	arena->applyUnitChanges({killed});
	ASSERT_EQ(arena->crisisState().pending.size(), 1);
	EXPECT_EQ(arena->crisisState().pending.front(), (newHorizonsCrisisCommand::DeathReceipt{BattleSide::ATTACKER, first->unitId()}));
	EXPECT_TRUE(arena->bloodrageDeaths().empty());
	EXPECT_FALSE(arena->firstBloodUsed());
}

TEST_F(ProjectedUnitPacketSidecarsTest, LethalUpdateSidecarsAndWholeValuesMatchActualGameStateVisitor)
{
	scalarParity({death(*first)});
}

TEST_F(ProjectedUnitPacketSidecarsTest, AllCandidatesComeFromPrepacketStateBeforeEarlierDeath)
{
	const auto killed = death(*first);
	capture();
	arena->applyUnitChanges({killed, UnitChanges(first->unitId(), Operation::REMOVE)});
	EXPECT_TRUE(arena->bloodrageDeaths().contains(first->unitId()));
	EXPECT_TRUE(arena->firstBloodUsed());
	EXPECT_EQ(arena->bloodrageDamage(BattleSide::ATTACKER), 2 * newHorizonsBloodrage::BASIC_INCREMENT);
	ASSERT_EQ(arena->crisisState().pending.size(), 1);
	EXPECT_EQ(arena->crisisState().pending.front().unit, first->unitId());
}

TEST_F(ProjectedUnitPacketSidecarsTest, UpdateThenRemoveMatchesScalarPacketRatherThanPerEntryCapture)
{
	scalarParity({death(*first), UnitChanges(first->unitId(), Operation::REMOVE)});
}

TEST_F(ProjectedUnitPacketSidecarsTest, AddedThenRemovedIdentityWasAbsentAtPrepacketCapture)
{
	const auto id = battle()->nextUnitId();
	capture();
	arena->applyUnitChanges({addition(id), UnitChanges(id, Operation::REMOVE)});
	ASSERT_NE(arena->findUnit(id), nullptr);
	EXPECT_TRUE(arena->findUnit(id)->isGhost());
	EXPECT_FALSE(arena->bloodrageDeaths().contains(id));
	EXPECT_FALSE(arena->firstBloodUsed());
	EXPECT_TRUE(arena->crisisState().pending.empty());
}

TEST_F(ProjectedUnitPacketSidecarsTest, RemoveThenResurrectSpendsGrantBeforeLivingReceiptClear)
{
	const auto living = first->save();
	capture();
	arena->applyUnitChanges({UnitChanges(first->unitId(), Operation::REMOVE), update(*first, living, 1)});
	ASSERT_NE(arena->findUnit(first->unitId()), nullptr);
	EXPECT_TRUE(arena->findUnit(first->unitId())->alive());
	EXPECT_FALSE(arena->bloodrageDeaths().contains(first->unitId()));
	EXPECT_TRUE(arena->firstBloodUsed());
	EXPECT_EQ(arena->bloodrageDamage(BattleSide::ATTACKER), 2 * newHorizonsBloodrage::BASIC_INCREMENT);
}

TEST_F(ProjectedUnitPacketSidecarsTest, RemoveThenResurrectWholePacketMatchesScalar)
{
	scalarParity({UnitChanges(first->unitId(), Operation::REMOVE), update(*first, first->save(), 1)});
}

TEST_F(ProjectedUnitPacketSidecarsTest, FinalLivingStateSuppressesPrecapturedCrisisDestruction)
{
	const auto living = first->save();
	const auto killed = death(*first);
	capture();
	arena->applyUnitChanges({killed, update(*first, living, 1)});
	ASSERT_NE(arena->findUnit(first->unitId()), nullptr);
	EXPECT_TRUE(arena->findUnit(first->unitId())->alive());
	EXPECT_TRUE(arena->crisisState().pending.empty());
	EXPECT_FALSE(arena->crisisState().used.at(static_cast<size_t>(BattleSide::ATTACKER)));
}

TEST_F(ProjectedUnitPacketSidecarsTest, CrisisReceiptOrderingFollowsEntriesNotIdSorting)
{
	const auto one = death(*first), two = death(*second);
	capture();
	arena->applyUnitChanges({two, one, two});
	ASSERT_EQ(arena->crisisState().pending.size(), 2);
	EXPECT_EQ(arena->crisisState().pending[0].unit, second->unitId());
	EXPECT_EQ(arena->crisisState().pending[1].unit, first->unitId());
}

TEST_F(ProjectedUnitPacketSidecarsTest, PuppetControllerIsCapturedBeforeLethalSpellCleanup)
{
	const SpellID spell(SpellID::decode(std::string(newHorizonsPuppetMaster::SPELL_ID)));
	ASSERT_TRUE(spell.hasValue());
	first->addNewBonus(std::make_shared<Bonus>(newHorizonsPuppetMaster::controlMarker(spell, PlayerColor(1))));
	ASSERT_EQ(battle()->battleGetActionController(first), PlayerColor(1));
	const auto killed = death(*first);
	capture();
	arena->applyUnitChanges({killed});
	ASSERT_EQ(arena->crisisState().pending.size(), 1);
	EXPECT_EQ(arena->crisisState().pending.front().side, BattleSide::DEFENDER);
}

TEST_F(ProjectedUnitPacketSidecarsTest, ControlledDeathMatchesScalarControllerNotRawSide)
{
	const SpellID spell(SpellID::decode(std::string(newHorizonsPuppetMaster::SPELL_ID)));
	ASSERT_TRUE(spell.hasValue());
	first->addNewBonus(std::make_shared<Bonus>(newHorizonsPuppetMaster::controlMarker(spell, PlayerColor(1))));
	scalarParity({death(*first)});
}

TEST_F(ProjectedUnitPacketSidecarsTest, SummonedRemoveDoesNotGrantBloodrageAndDoesNotInventCrisisCandidate)
{
	first->summoned = true;
	capture();
	arena->applyUnitChanges({UnitChanges(first->unitId(), Operation::REMOVE)});
	EXPECT_FALSE(arena->bloodrageDeaths().contains(first->unitId()));
	EXPECT_FALSE(arena->firstBloodUsed());
	EXPECT_TRUE(arena->crisisState().pending.empty());
}

TEST_F(ProjectedUnitPacketSidecarsTest, CloneRemoveDoesNotGrantBloodrage)
{
	first->cloned = true;
	capture();
	arena->applyUnitChanges({UnitChanges(first->unitId(), Operation::REMOVE)});
	EXPECT_FALSE(arena->bloodrageDeaths().contains(first->unitId()));
	EXPECT_FALSE(arena->firstBloodUsed());
}

TEST_F(ProjectedUnitPacketSidecarsTest, IgnoredLivingUpdateStillClearsExistingDeathReceipt)
{
	battle()->bloodrageDestroyedUnits.insert(first->unitId());
	first->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID()));
	capture();
	arena->applyUnitChanges({update(*first, JsonNode("ignored malformed state"), -1)});
	EXPECT_FALSE(arena->bloodrageDeaths().contains(first->unitId()));
	EXPECT_TRUE(battle()->bloodrageDestroyedUnits.contains(first->unitId()));
}

TEST_F(ProjectedUnitPacketSidecarsTest, RepeatedRemoveIdsGrantOnlyOnce)
{
	capture();
	arena->applyUnitChanges({UnitChanges(first->unitId(), Operation::REMOVE), UnitChanges(first->unitId(), Operation::REMOVE)});
	EXPECT_EQ(arena->bloodrageDeaths().size(), 1);
	EXPECT_EQ(arena->bloodrageDamage(BattleSide::ATTACKER), 2 * newHorizonsBloodrage::BASIC_INCREMENT);
}

TEST_F(ProjectedUnitPacketSidecarsTest, LaterMalformedEntryAbandonsAllValuesAndDependentSidecars)
{
	const auto before = first->save();
	const auto killed = death(*first);
	auto malformed = second->save();
	malformed["state"]["guardianSpiritHitPoints"].Integer() = -1;
	capture();
	EXPECT_THROW(arena->applyUnitChanges({killed, update(*second, malformed, 0)}), std::runtime_error);
	EXPECT_THROW(arena->crisisState(), std::logic_error);
	EXPECT_EQ(first->save(), before);
	EXPECT_TRUE(battle()->getCrisisCommandState().pending.empty());
	EXPECT_TRUE(battle()->bloodrageDestroyedUnits.empty());
}

TEST_F(ProjectedUnitPacketSidecarsTest, MissingRemoveRemainsNonexceptionAndEmptySidecarPass)
{
	scalarParity({UnitChanges(99999, Operation::REMOVE)});
}

TEST_F(ProjectedUnitPacketSidecarsTest, UnknownOperationUsesExistingErrorAndPoisonsPrivateOwner)
{
	UnitChanges invalid(first->unitId(), static_cast<Operation>(127));
	capture();
	EXPECT_THROW(arena->applyUnitChanges({invalid}), std::runtime_error);
	EXPECT_THROW(arena->size(), std::logic_error);
}

TEST_F(ProjectedUnitPacketSidecarsTest, SubsequentPacketUsesUpdatedSidecarsWithoutRestoringFirstBlood)
{
	const auto living = first->save();
	capture();
	arena->applyUnitChanges({UnitChanges(first->unitId(), Operation::REMOVE)});
	arena->applyUnitChanges({update(*first, living, 1)});
	arena->applyUnitChanges({UnitChanges(first->unitId(), Operation::REMOVE)});
	EXPECT_TRUE(arena->firstBloodUsed());
	EXPECT_EQ(arena->bloodrageDamage(BattleSide::ATTACKER), 3 * newHorizonsBloodrage::BASIC_INCREMENT);
}
}
