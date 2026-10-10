/* NewHorizonsCrossSchoolFormulaTest.cpp, part of VCMI; GPL v2.0 or later. */
#include "StdInc.h"
#include "NewHorizonsElementalTerrainFixture.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsCrossSchoolFormula.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#ifdef ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/callback/CBattleCallback.h"
#endif

namespace
{
namespace cross = newHorizonsCrossSchoolFormula;
constexpr auto SKILL = "new-horizons:spellcraft";
SpellID spell(const char * key) { return SpellID(SpellID::decode(key)); }

#ifdef ENABLE_BATTLE_AI
class CrossSchoolEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit CrossSchoolEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif

class NewHorizonsCrossSchoolFormulaTest : public NewHorizonsElementalTerrainFixture
{
protected:
	CStack * ally = nullptr;
	CStack * enemy = nullptr;
	void prepare(bool selected = true)
	{
		startGame();
		for(const auto * key : {"new-horizons:lightMagic", "new-horizons:sorceryMagic", "new-horizons:havocMagic",
			"new-horizons:natureMagic", "new-horizons:shadowMagic", "new-horizons:chaosMagic"})
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(key)), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(SKILL)), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({SKILL, selected ? std::string(cross::PERK) : "new-horizons:spellcraft.spellPenetration"});
		ASSERT_EQ(attackerSideHero->hasActivePerk(SKILL, std::string(cross::PERK)), selected);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 200, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		for(const auto * key : {"core:bless", "core:haste", "core:magicArrow", "core:fireball"})
			attackerSideHero->addSpellToSpellbook(spell(key));
		setTestSpellPointTotal(attackerSideHero, 1000);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		ally = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(leftHex), 1000);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10000);
		ASSERT_NE(ally, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), ally);
	}
	std::unique_ptr<spells::Mechanics> mechanics(SpellID id, spells::Mode mode = spells::Mode::HERO,
		const CBattleInfoCallback * callback = nullptr) const
	{
		spells::BattleCast cast(callback ? callback : battle(), attackerSideHero, mode, id.toSpell());
		return id.toSpell()->battleMechanics(&cast);
	}
	bool paid(SpellID id, const CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = id;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(BattleSide::ATTACKER), action);
	}
	void nextRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}
	BattleSpellCast accepted(SpellID id) const
	{
		BattleSpellCast packet;
		packet.battleID = BattleID(0);
		packet.side = BattleSide::ATTACKER;
		packet.spellID = id;
		packet.crossSchoolFormula = cross::acceptedReceipt(*battle(), packet.side, id);
		return packet;
	}
};
}

TEST_F(NewHorizonsCrossSchoolFormulaTest, PaidOwnHeroCastArmsAndDifferentSchoolScalesOnlyPower)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto arrow = spell("core:magicArrow");
	const auto ordinary = mechanics(arrow)->getEffectValue();
	ASSERT_EQ(mechanics(arrow)->getCastSpellPowerComponentBonusPercent(), 0);
	ASSERT_TRUE(paid(spell("core:bless"), ally));
	EXPECT_EQ(battle()->getCrossSchoolFormulaState(BattleSide::ATTACKER).spell, spell("core:bless"));
	nextRound();
	const auto boosted = mechanics(arrow);
	EXPECT_EQ(boosted->getCastSpellPowerComponentBonusPercent(), 10);
	EXPECT_EQ(boosted->getEffectValue(), 20 + (ordinary - 20) * 110 / 100);
	const auto damage = boosted->adjustEffectValueBeforeExecution(enemy);
	const auto hp = enemy->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto cost = battle()->battleGetSpellCost(arrow.toSpell(), attackerSideHero);
	ASSERT_TRUE(paid(arrow, enemy));
	EXPECT_EQ(hp - enemy->getAvailableHealth(), damage);
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), cost);
	EXPECT_EQ(battle()->getCrossSchoolFormulaState(BattleSide::ATTACKER).spell, arrow);
	EXPECT_EQ(mechanics(spell("core:bless"))->getCastSpellPowerComponentBonusPercent(), 10);
	EXPECT_EQ(mechanics(arrow)->getCastSpellPowerComponentBonusPercent(), 0);
}

TEST_F(NewHorizonsCrossSchoolFormulaTest, SameSchoolRefreshesAndWindowExpiresAfterNextRound)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(paid(spell("core:haste"), ally));
	EXPECT_EQ(mechanics(spell("core:magicArrow"))->getCastSpellPowerComponentBonusPercent(), 0);
	EXPECT_EQ(mechanics(spell("core:bless"))->getCastSpellPowerComponentBonusPercent(), 10);
	nextRound();
	ASSERT_TRUE(paid(spell("core:haste"), ally));
	const auto refreshed = battle()->getCrossSchoolFormulaState(BattleSide::ATTACKER);
	EXPECT_EQ(refreshed.round, battle()->getRound());
	nextRound();
	EXPECT_EQ(mechanics(spell("core:bless"))->getCastSpellPowerComponentBonusPercent(), 10);
	nextRound();
	EXPECT_EQ(mechanics(spell("core:bless"))->getCastSpellPowerComponentBonusPercent(), 0);
	EXPECT_EQ(battle()->getCrossSchoolFormulaState(BattleSide::ATTACKER), refreshed);
}

TEST(CrossSchoolMembershipTest, DifferentMeansNonemptyDisjointNotMerelyDifferentMembership)
{
	const SpellSchool nature(SpellSchool::decode("new-horizons:nature"));
	const SpellSchool havoc(SpellSchool::decode("new-horizons:havoc"));
	const SpellSchool sorcery(SpellSchool::decode("new-horizons:sorcery"));
	EXPECT_FALSE(cross::differentSchools({}, {sorcery}));
	EXPECT_FALSE(cross::differentSchools({nature}, {}));
	EXPECT_FALSE(cross::differentSchools({nature, havoc}, {havoc}));
	EXPECT_FALSE(cross::differentSchools({nature, havoc}, {nature, havoc}));
	EXPECT_TRUE(cross::differentSchools({nature, havoc}, {sorcery}));
}

TEST_F(NewHorizonsCrossSchoolFormulaTest, RejectedAndNonHeroCastsDoNotRecordOrBorrowBonus)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_FALSE(paid(spell("core:bless"), enemy));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_FALSE(battle()->getCrossSchoolFormulaState(BattleSide::ATTACKER).hasState());
	ASSERT_TRUE(paid(spell("core:bless"), ally));
	const auto history = battle()->getCrossSchoolFormulaState(BattleSide::ATTACKER);
	for(auto mode : {spells::Mode::PASSIVE, spells::Mode::MAGIC_MIRROR})
		EXPECT_EQ(mechanics(spell("core:magicArrow"), mode)->getCastSpellPowerComponentBonusPercent(), 0);
	spells::BattleCast passive(battle(), attackerSideHero, spells::Mode::PASSIVE, spell("core:magicArrow").toSpell());
	passive.cast(gameHandler->spellcastEnvironment(), {spells::Destination(enemy)});
	EXPECT_EQ(battle()->getCrossSchoolFormulaState(BattleSide::ATTACKER), history);
}

TEST_F(NewHorizonsCrossSchoolFormulaTest, AcceptedCounteredAndResistedPacketsRefreshButStaleReceiptIsAtomic)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto first = accepted(spell("core:bless"));
	first.counterspellSide = BattleSide::DEFENDER;
	first.counterspellNegated = true;
	first.resistedCres.insert(enemy->unitId());
	ASSERT_TRUE(first.crossSchoolFormula.has_value());
	gameHandler->sendAndApply(first);
	const auto history = battle()->getCrossSchoolFormulaState(BattleSide::ATTACKER);
	EXPECT_EQ(history.spell, first.spellID);
	EXPECT_EQ(mechanics(spell("core:magicArrow"))->getCastSpellPowerComponentBonusPercent(), 10);
	const auto allowance = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	EXPECT_THROW(gameHandler->sendAndApply(first), std::runtime_error);
	EXPECT_EQ(battle()->getCrossSchoolFormulaState(BattleSide::ATTACKER), history);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowance);
	auto forged = accepted(spell("core:haste"));
	forged.crossSchoolFormula->after.round += 1;
	EXPECT_THROW(gameHandler->sendAndApply(forged), std::runtime_error);
	EXPECT_EQ(battle()->getCrossSchoolFormulaState(BattleSide::ATTACKER), history);
}

TEST_F(NewHorizonsCrossSchoolFormulaTest, UnselectedHeroHasNoReceiptAndInvalidSavedStateRejects)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	EXPECT_FALSE(cross::acceptedReceipt(*battle(), BattleSide::ATTACKER, spell("core:bless")));
	ASSERT_TRUE(paid(spell("core:bless"), ally));
	EXPECT_FALSE(battle()->getCrossSchoolFormulaState(BattleSide::ATTACKER).hasState());
	EXPECT_EQ(mechanics(spell("core:magicArrow"))->getCastSpellPowerComponentBonusPercent(), 0);
	EXPECT_THROW(battle()->setCrossSchoolFormulaState(BattleSide::ATTACKER, {spell("core:bless"), battle()->getRound() + 1}), std::runtime_error);
	EXPECT_THROW(battle()->setCrossSchoolFormulaState(BattleSide::ATTACKER, {SpellID::NONE, 0}), std::runtime_error);
}

TEST_F(NewHorizonsCrossSchoolFormulaTest, PacketStateAndWorldLobbyOldWritersRejectBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(paid(spell("core:bless"), ally));
	auto packet = accepted(spell("core:magicArrow"));
	ASSERT_TRUE(packet.crossSchoolFormula);
	ASSERT_TRUE(packet.crossSchoolFormula->before.hasState());
	ASSERT_TRUE(packet.crossSchoolFormula->after.hasState());
	packet.extendedSpell = true;
	CMemorySerializer current;
	current.oser & packet;
	BattleSpellCast restored;
	current.iser & restored;
	ASSERT_TRUE(restored.crossSchoolFormula);
	EXPECT_EQ(restored.crossSchoolFormula->before, packet.crossSchoolFormula->before);
	EXPECT_EQ(restored.crossSchoolFormula->after, packet.crossSchoolFormula->after);
	EXPECT_EQ(restored.crossSchoolFormula, packet.crossSchoolFormula);
	EXPECT_TRUE(restored.extendedSpell);
	CMemorySerializer oldPacket;
	oldPacket.oser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	oldPacket.iser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	auto crossOnly = packet;
	crossOnly.extendedSpell = false;
	EXPECT_THROW(oldPacket.oser & crossOnly, std::runtime_error);
	EXPECT_TRUE(oldPacket.extractBuffer().empty());
	BattleSpellCast plain = packet;
	plain.crossSchoolFormula.reset();
	plain.extendedSpell = false;
	CMemorySerializer oldPlain;
	oldPlain.oser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	oldPlain.iser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	ASSERT_NO_THROW(oldPlain.oser & plain);
	ASSERT_TRUE(restored.crossSchoolFormula);
	ASSERT_NO_THROW(oldPlain.iser & restored);
	EXPECT_FALSE(restored.crossSchoolFormula);
	EXPECT_FALSE(restored.extendedSpell);
	CMemorySerializer oldEmpty;
	oldEmpty.oser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	oldEmpty.iser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	cross::State empty;
	oldEmpty.oser & empty;
	cross::State legacy{spell("core:bless"), 0};
	oldEmpty.iser & legacy;
	EXPECT_EQ(legacy, cross::State{});
	for(auto kind : {0, 1})
	{
		CMemorySerializer serializer;
		serializer.oser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
		serializer.iser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
		if(kind == 0)
			EXPECT_THROW(serializer.oser & *gameState(), std::runtime_error);
		else
		{
			LobbyStartGame lobby;
			lobby.initializedGameState = gameState();
			EXPECT_THROW(serializer.oser & lobby, std::runtime_error);
		}
		EXPECT_TRUE(serializer.extractBuffer().empty());
	}
}

TEST_F(NewHorizonsCrossSchoolFormulaTest, LegalCompositionAbove100PercentIsPreservedWithoutClamping)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto id = spell("core:magicArrow");
	const auto & rules = battle()->getMagicRules();
	const auto baseline = newHorizonsMagic::spellPowerCoefficientBasisPoints(rules, attackerSideHero, id);
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(rules, attackerSideHero, id, 117), baseline * 217 / 100);
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(rules, attackerSideHero, id, 200), baseline * 3);
	EXPECT_THROW(newHorizonsMagic::spellPowerCoefficientBasisPoints(rules, attackerSideHero, id, 201), std::invalid_argument);
}

TEST_F(NewHorizonsCrossSchoolFormulaTest, PlainCurrentBattleStartReadsAndRoundTripsWithOrWithoutInfo)
{
	startGame();
	BattleStart empty;
	empty.battleID = BattleID(0);
	CMemorySerializer nullInfo;
	nullInfo.oser & empty;
	BattleStart restoredEmpty;
	nullInfo.iser & restoredEmpty;
	EXPECT_EQ(restoredEmpty.battleID, empty.battleID);
	EXPECT_EQ(restoredEmpty.info, nullptr);

	const int3 tile(4, 4, 0);
	const auto terrain = gameState()->getMap().getTile(tile).terrainType;
	const BattleField battlefield(*LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "battlefield", "core:grass_hills"));
	BattleSideArray<const CGHeroInstance *> heroes = {attackerSideHero, defenderSideHero};
	BattleSideArray<const CArmedInstance *> armies = {attackerSideHero, defenderSideHero};
	const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, defenderSideHero);
	BattleStart populated;
	populated.battleID = BattleID(0);
	populated.info = BattleInfo::setupBattle(gameState().get(), tile, terrain, battlefield, armies, heroes, layout, nullptr);
	ASSERT_NE(populated.info, nullptr);
	ASSERT_FALSE(populated.info->getCrossSchoolFormulaState(BattleSide::ATTACKER).hasState());
	CMemorySerializer current;
	current.oser & populated;
	BattleStart restored;
	current.iser.cb = gameState().get();
	current.iser & restored;
	ASSERT_NE(restored.info, nullptr);
	EXPECT_EQ(restored.battleID, populated.battleID);
	EXPECT_EQ(restored.info->getCrossSchoolFormulaState(BattleSide::ATTACKER), cross::State{});
	EXPECT_EQ(restored.info->getCrossSchoolFormulaState(BattleSide::DEFENDER), cross::State{});
	EXPECT_EQ(restored.info->getRound(), populated.info->getRound());
}

#ifdef ENABLE_BATTLE_AI
TEST_F(NewHorizonsCrossSchoolFormulaTest, DetachedSuccessCopiesAndRefreshesOnlyItsOwnHistory)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(paid(spell("core:bless"), ally));
	nextRound();
	auto callback = std::make_shared<CBattleCallback>(battle()->sideToPlayer(BattleSide::ATTACKER), nullptr);
	callback->onBattleStarted(battle());
	CrossSchoolEnvironment environment(gameState());
	auto projected = std::make_shared<HypotheticBattle>(&environment, callback->getBattle(BattleID(0)));
	const auto history = battle()->getCrossSchoolFormulaState(BattleSide::ATTACKER);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto hp = enemy->getAvailableHealth();
	const auto id = spell("core:magicArrow");
	const auto focused = mechanics(id, spells::Mode::HERO, projected.get());
	EXPECT_EQ(focused->getCastSpellPowerComponentBonusPercent(), 10);
	const auto expected = focused->adjustEffectValueBeforeExecution(projected->battleGetUnitByID(enemy->unitId()));
	spells::BattleCast cast(projected.get(), attackerSideHero, spells::Mode::HERO, id.toSpell());
	const spells::Target target{spells::Destination(projected->battleGetUnitByID(enemy->unitId()))};
	ASSERT_TRUE(cast.mechanicsForTarget(target)->canBeCastAt(target));
	cast.castEval(projected->getServerCallback(), target);
	EXPECT_EQ(hp - projected->battleGetUnitByID(enemy->unitId())->getAvailableHealth(), expected);
	EXPECT_EQ(projected->getCrossSchoolFormulaState(BattleSide::ATTACKER).spell, id);
	HypotheticBattle nested(&environment, projected);
	EXPECT_EQ(nested.getCrossSchoolFormulaState(BattleSide::ATTACKER), projected->getCrossSchoolFormulaState(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->getCrossSchoolFormulaState(BattleSide::ATTACKER), history);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(enemy->getAvailableHealth(), hp);
}
#endif
