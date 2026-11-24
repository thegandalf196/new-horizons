/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "RampartCombatSpecialtyFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../SylvanTacticsHistoricalRules.h"
#include "../../../lib/CStack.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/NewHorizonsForestAmbush.h"
#include "../../../lib/battle/NewHorizonsSylvanAmbush.h"
#include "../../../lib/battle/NewHorizonsSylvanTactics.h"
#include "../../../lib/battle/NewHorizonsSylvanTacticsRevision.h"
#include "../../../lib/battle/NewHorizonsSylvanTacticsV2.h"
#include "../../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../../lib/json/JsonNode.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../client/battle/SylvanAmbushWaitButton.h"

namespace
{
using namespace rampartCombatTest;
namespace ambush = newHorizonsSylvanAmbush;
using AmbushSource = battle::SylvanAmbushState::Source;

/// Sylvan Tactics 5 on the shipped rules, driven through the authoritative
/// battle processor where a server hook exists.
class SylvanAmbushTest : public RampartCombatSpecialtyTest
{
protected:
	virtual int policy() const { return 5; }
	void mapLoaded(CMap * map) override
	{
		RampartCombatSpecialtyTest::mapLoaded(map);
		if(policy() != 5)
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, sylvanTacticsHistoricalRules(policy()));
	}
	/// One perk per Sylvan Tactics rank, in acquisition order; `rank` may exceed it.
	void sylvan(int rank, std::initializer_list<const char *> perks)
	{
		ASSERT_NO_FATAL_FAILURE(world());
		const SecondarySkill skill(SecondarySkill::decode("new-horizons:sylvanLuck"));
		attackerSideHero->setSecSkillLevel(skill, rank, ChangeValueMode::ABSOLUTE);
		for(const auto * suffix : perks)
		{
			const auto id = std::string("new-horizons:sylvanLuck.") + suffix;
			attackerSideHero->applyPerkSelection({"new-horizons:sylvanLuck", id});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:sylvanLuck", id)) << id;
		}
		for(auto * hero : {attackerSideHero, defenderSideHero})
			hero->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::LEADERSHIP, BonusSource::OTHER, 100000, BonusSourceID()));
	}
	CStack * ours(const char * creature, BattleHex hex, int count = 10, int initiative = 500)
	{
		return place(ATTACKER, creature, hex, count, initiative);
	}
	CStack * theirs(const char * creature, BattleHex hex, int count = 10, int initiative = 400)
	{
		return place(DEFENDER, creature, hex, count, initiative);
	}
	bool wait(const CStack * unit)
	{
		activate(unit);
		return submit(BattleAction::makeWait(unit), battle()->sideToPlayer(unit->unitSide()));
	}
	bool move(const CStack * unit, BattleHex hex)
	{
		activate(unit);
		return submit(BattleAction::makeMove(unit, hex), battle()->sideToPlayer(unit->unitSide()));
	}
	int64_t damage(const CStack * actor, const CStack * target, bool ranged = false, bool retaliation = false) const
	{
		BattleAttackInfo info(actor, target, 0, ranged);
		info.retaliation = retaliation;
		return battle()->calculateDmgRange(info).damage.min;
	}
	static BattleAttackInfo strike(const CStack * actor, const CStack * target, bool ranged = false)
	{
		return BattleAttackInfo(actor, target, 0, ranged);
	}
	/// A concealed stack starts its delayed activation.
	void delayedActivation(const CStack * unit)
	{
		ASSERT_TRUE(unit->waiting);
		activate(unit);
	}
	/// Mark an enemy as having walked from `from` to its current hex this round.
	void walked(CStack * unit, BattleHex from)
	{
		unit->sylvanAmbush.recordApproach(battle()->battleGetRound(), from, unit->getPosition());
	}
};
class SylvanAmbushPolicy4Test : public SylvanAmbushTest
{
protected:
	int policy() const override { return 4; }
};

TEST_F(SylvanAmbushTest, PolicyFiveCapturesTheAmbushRosterAndRetiresReusedMeanings)
{
	ASSERT_NO_FATAL_FAILURE(sylvan(2, {"luckyRecovery", "forestCover"}));
	const auto & rules = attackerSideHero->getPerkState().getRules();
	EXPECT_TRUE(newHorizonsHeroes::usesSylvanTacticsV5(rules));
	EXPECT_TRUE(newHorizonsHeroes::usesSylvanTacticsV3(rules));
	EXPECT_FALSE(newHorizonsHeroes::usesSylvanTacticsV4(rules));
	EXPECT_TRUE(newHorizonsHeroes::requiresSylvanAmbushFormat(rules));
	SylvanLuckState fortune;
	newHorizonsSylvanTactics::configure(*attackerSideHero, fortune);
	EXPECT_TRUE(fortune.tactics.dappledCover);
	EXPECT_FALSE(fortune.tactics.quickRedeployment);
	EXPECT_FALSE(newHorizonsSylvanTacticsRevision::active(attackerSideHero, "luckyRecovery"));
	for(const auto * retired : {"wildChance", "cascadingFortune", "natureSProvidence"})
		EXPECT_FALSE(newHorizonsSylvanTacticsV2::hasPerk(attackerSideHero, retired)) << retired;
	EXPECT_TRUE(ambush::hasPerk(attackerSideHero, "forestCover"));
	EXPECT_EQ(ambush::rankSlots(attackerSideHero), 2);
	EXPECT_EQ(ambush::rankSlots(defenderSideHero), 0);
}

TEST_F(SylvanAmbushTest, RankSlotsAreCountedFirstComeAndResetEachRound)
{
	ASSERT_NO_FATAL_FAILURE(sylvan(2, {"serendipity"}));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * first = ours("core:dwarf", BattleHex(3, 1), 10, 700);
	auto * second = ours("core:dwarf", BattleHex(3, 5), 10, 600);
	auto * third = ours("core:dwarf", BattleHex(3, 9), 10, 500);
	ASSERT_TRUE(theirs("core:pikeman", BattleHex(14, 5)));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	for(auto * unit : {first, second, third})
		ASSERT_TRUE(wait(unit));
	EXPECT_TRUE(first->sylvanAmbush.concealed);
	EXPECT_EQ(first->sylvanAmbush.source, AmbushSource::RANK);
	EXPECT_TRUE(second->sylvanAmbush.concealed);
	EXPECT_FALSE(third->sylvanAmbush.concealed);
	// A revealed stack keeps its slot for the rest of the round.
	battle()->moveUnit(first->unitId(), BattleHex(2, 1));
	EXPECT_FALSE(first->sylvanAmbush.concealed);
	EXPECT_EQ(ambush::concealmentOffer(*battle(), *third).rankUsed, 2);
	battle()->nextRound();
	EXPECT_FALSE(second->sylvanAmbush.concealed);
	EXPECT_EQ(ambush::concealmentOffer(*battle(), *third).rankUsed, 0);
	ASSERT_TRUE(wait(third));
	EXPECT_TRUE(third->sylvanAmbush.concealed);
}

TEST_F(SylvanAmbushTest, ExpertRankHidesThreeStacks)
{
	ASSERT_NO_FATAL_FAILURE(sylvan(3, {"serendipity", "startle", "apexPredator"}));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	std::vector<CStack *> units;
	for(int row = 1; row <= 9; row += 2)
		units.push_back(ours("core:dwarf", BattleHex(3, row), 10, 800 - row));
	ASSERT_TRUE(theirs("core:pikeman", BattleHex(14, 5)));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	int hidden = 0;
	for(auto * unit : units)
	{
		ASSERT_TRUE(wait(unit));
		hidden += unit->sylvanAmbush.concealed ? 1 : 0;
	}
	EXPECT_EQ(hidden, 3);
}

TEST_F(SylvanAmbushTest, ForestCoverAddsOneSlotOnlyBesideAnObstacle)
{
	ASSERT_NO_FATAL_FAILURE(sylvan(2, {"serendipity", "forestCover"}));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * first = ours("core:dwarf", BattleHex(3, 1), 10, 700);
	auto * second = ours("core:dwarf", BattleHex(3, 3), 10, 650);
	auto * open = ours("core:dwarf", BattleHex(3, 7), 10, 600);
	auto * covered = ours("core:dwarf", BattleHex(5, 5), 10, 550);
	auto * coveredToo = ours("core:dwarf", BattleHex(5, 9), 10, 500);
	ASSERT_TRUE(theirs("core:pikeman", BattleHex(14, 5)));
	ASSERT_NO_FATAL_FAILURE(forceField(BattleHex(4, 5)));
	ASSERT_NO_FATAL_FAILURE(forceField(BattleHex(4, 9)));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_TRUE(wait(first));
	ASSERT_TRUE(wait(second));
	EXPECT_FALSE(ambush::concealmentSlot(*battle(), *open));
	ASSERT_TRUE(wait(open));
	EXPECT_FALSE(open->sylvanAmbush.concealed);
	ASSERT_EQ(ambush::concealmentSlot(*battle(), *covered), AmbushSource::FOREST_COVER);
	ASSERT_TRUE(wait(covered));
	EXPECT_EQ(covered->sylvanAmbush.source, AmbushSource::FOREST_COVER);
	ASSERT_TRUE(wait(coveredToo));
	EXPECT_FALSE(coveredToo->sylvanAmbush.concealed);
}

TEST_F(SylvanAmbushTest, NoConcealmentAfterHarmOrTwoRoundsInARowButAgainTwoRoundsLater)
{
	ASSERT_NO_FATAL_FAILURE(sylvan(3, {"serendipity"}));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * hider = ours("core:dwarf", BattleHex(3, 1), 10, 700);
	auto * struck = ours("core:dwarf", BattleHex(6, 5), 10, 650);
	auto * enemy = theirs("core:pikeman", BattleHex(7, 5), 10, 900);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_TRUE(attack(enemy, struck->getPosition()));
	EXPECT_TRUE(struck->sylvanAmbush.harmedThisRound);
	ASSERT_TRUE(wait(struck));
	EXPECT_FALSE(struck->sylvanAmbush.concealed);
	ASSERT_TRUE(wait(hider));
	EXPECT_TRUE(hider->sylvanAmbush.concealed);
	battle()->nextRound();
	ASSERT_TRUE(wait(hider));
	EXPECT_FALSE(hider->sylvanAmbush.concealed);
	battle()->nextRound();
	ASSERT_TRUE(wait(hider));
	EXPECT_TRUE(hider->sylvanAmbush.concealed);
	// Any damage, not just an attack, reveals and spends the round.
	int64_t amount = 1;
	auto state = hider->acquireState();
	state->damage(amount);
	EXPECT_FALSE(state->sylvanAmbush.concealed);
	EXPECT_TRUE(state->sylvanAmbush.harmedThisRound);
}

TEST_F(SylvanAmbushTest, SanctuaryAndIvorConcealmentUseNoSlot)
{
	ASSERT_NO_FATAL_FAILURE(sylvan(1, {"serendipity"}));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * sanctified = ours("core:dwarf", BattleHex(3, 1), 10, 700);
	auto * hidden = ours("core:dwarf", BattleHex(3, 5), 10, 650);
	auto * free = ours("core:dwarf", BattleHex(3, 9), 10, 600);
	ASSERT_TRUE(theirs("core:pikeman", BattleHex(14, 5)));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	effect(sanctified, Bonus(BonusDuration::ONE_BATTLE, BonusType::SANCTIFIED, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(wait(sanctified));
	EXPECT_FALSE(sanctified->sylvanAmbush.concealed);
	auto ivor = hidden->acquireState();
	ivor->forestAmbush.waited = true;
	ivor->forestAmbush.concealed = true;
	EXPECT_FALSE(ambush::concealmentSlot(*battle(), *ivor));
	ASSERT_TRUE(wait(free));
	EXPECT_TRUE(free->sylvanAmbush.concealed);
}

TEST_F(SylvanAmbushTest, ConcealmentBlocksShotsAndSingleTargetSpellsButNotMeleeOrAreaSpells)
{
	ASSERT_NO_FATAL_FAILURE(sylvan(1, {"serendipity"}));
	giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	for(const auto spell : {SpellID::MAGIC_ARROW, SpellID::FIREBALL})
		defenderSideHero->addSpellToSpellbook(spell);
	setTestSpellPointTotal(defenderSideHero, 1000);
	ASSERT_NO_FATAL_FAILURE(emptyField());
	// Centaurs have no Magic Resistance, so the area spell below always lands.
	auto * hider = ours("core:centaur", BattleHex(6, 5), 20, 700);
	auto * archers = theirs("core:archer", BattleHex(14, 5), 10, 900);
	auto * pikes = theirs("core:pikeman", BattleHex(7, 5), 10, 800);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_TRUE(wait(hider));
	ASSERT_TRUE(hider->sylvanAmbush.concealed);
	EXPECT_TRUE(newHorizonsForestAmbush::protectsFromRangedOrSpell(hider));
	EXPECT_FALSE(newHorizonsForestAmbush::protectsFromMelee(hider));
	EXPECT_FALSE(battle()->battleCanShoot(archers, hider->getPosition()));
	EXPECT_FALSE(battle()->battleCanShootAction(archers, hider->getPosition()));
	EXPECT_FALSE(castOn(defenderSideHero, SpellID::MAGIC_ARROW, hider));
	EXPECT_TRUE(battle()->battleCanAttackUnitAction(pikes, hider));
	activate(archers);
	EXPECT_FALSE(submit(BattleAction::makeShotAttack(archers, hider), PlayerColor(1)));
	// An area spell aimed at a hex is not a single-target selection, as with
	// Sanctuary: it still hits the Concealed stack and reveals it.
	const auto health = hider->getAvailableHealth();
	activate(pikes);
	ASSERT_TRUE(submit(spellAtHex(DEFENDER, SpellID::FIREBALL, pikes->getPosition()), PlayerColor(1)));
	EXPECT_LT(hider->getAvailableHealth(), health);
	EXPECT_FALSE(hider->sylvanAmbush.concealed);
}

TEST_F(SylvanAmbushTest, MeleeIsLegalAndRevealsWhileRetaliationResolves)
{
	ASSERT_NO_FATAL_FAILURE(sylvan(1, {"serendipity"}));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * hider = ours("core:dwarf", BattleHex(6, 5), 20, 700);
	auto * pikes = theirs("core:pikeman", BattleHex(7, 5), 10, 800);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_TRUE(wait(hider));
	ASSERT_TRUE(hider->sylvanAmbush.concealed);
	const auto pikeHealth = pikes->getAvailableHealth();
	ASSERT_TRUE(attack(pikes, hider->getPosition()));
	EXPECT_FALSE(hider->sylvanAmbush.concealed);
	EXPECT_LT(pikes->getAvailableHealth(), pikeHealth); // It retaliated.
}

TEST_F(SylvanAmbushTest, ActivationAttackMoveAndRoundEndReveal)
{
	battle::SylvanAmbushState state;
	state.conceal(AmbushSource::RANK, 3);
	state.beginActivation(false, 3, BattleHex(20), false);
	EXPECT_FALSE(state.concealed);
	EXPECT_FALSE(state.strikePending); // Morale, Second Wind and other activations.
	state.conceal(AmbushSource::RANK, 3);
	state.beginActivation(true, 3, BattleHex(20), false);
	EXPECT_FALSE(state.concealed);
	EXPECT_TRUE(state.strikePending);
	EXPECT_EQ(state.strikeOrigin, BattleHex(20));
	state.finishAttack(false);
	EXPECT_FALSE(state.strikePending);
	state.conceal(AmbushSource::RANK, 3);
	state.finishAttack(false); // An Overwatch shot from hiding.
	EXPECT_FALSE(state.concealed);
	state.conceal(AmbushSource::RANK, 3);
	state.newRound();
	EXPECT_FALSE(state.concealed);
	EXPECT_EQ(state.concealedRound, 3);
	EXPECT_EQ(state.source, AmbushSource::RANK);
}

TEST_F(SylvanAmbushTest, ApproachRecordKeepsTheLatestUnbrokenVoluntaryChain)
{
	ASSERT_NO_FATAL_FAILURE(sylvan(1, {"serendipity"}));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * hider = ours("core:dwarf", BattleHex(3, 5), 20, 300);
	auto * pikes = theirs("core:pikeman", BattleHex(12, 5), 10, 900);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_TRUE(move(pikes, BattleHex(10, 5)));
	EXPECT_EQ(pikes->sylvanAmbush.approachRound, battle()->battleGetRound());
	EXPECT_EQ(pikes->sylvanAmbush.approachStart, BattleHex(12, 5));
	EXPECT_EQ(pikes->sylvanAmbush.approachEnd, BattleHex(10, 5));
	EXPECT_TRUE(ambush::prey(*battle(), hider, pikes));
	// A forced move breaks the chain; the next voluntary move restarts it.
	battle()->moveUnit(pikes->unitId(), BattleHex(11, 3));
	EXPECT_EQ(pikes->sylvanAmbush.approachEnd, BattleHex(10, 5));
	ambush::recordApproach(*battle(), *pikes, BattleHex(11, 3), BattleHex(12, 3));
	EXPECT_EQ(pikes->sylvanAmbush.approachStart, BattleHex(11, 3));
	EXPECT_FALSE(ambush::prey(*battle(), hider, pikes)); // Farther.
	battle()->nextRound();
	EXPECT_FALSE(ambush::prey(*battle(), hider, pikes)); // A previous-round record.
}

TEST_F(SylvanAmbushTest, PerStepAndPerSegmentRecordsAgree)
{
	battle::SylvanAmbushState steps;
	battle::SylvanAmbushState segment;
	const BattleHex path[] = {BattleHex(12, 5), BattleHex(11, 5), BattleHex(10, 5), BattleHex(9, 5)};
	for(size_t index = 1; index < std::size(path); ++index)
		steps.recordApproach(2, path[index - 1], path[index]);
	segment.recordApproach(2, path[0], path[std::size(path) - 1]);
	EXPECT_EQ(steps, segment);
	// A chain continues across two orders in the same round.
	segment.recordApproach(2, BattleHex(9, 5), BattleHex(8, 5));
	EXPECT_EQ(segment.approachStart, BattleHex(12, 5));
	// A new round restarts it.
	segment.recordApproach(3, BattleHex(8, 5), BattleHex(7, 5));
	EXPECT_EQ(segment.approachStart, BattleHex(8, 5));
}

TEST_F(SylvanAmbushTest, PreyMeasuresCloserFartherEqualAndTwoHexFootprints)
{
	ASSERT_NO_FATAL_FAILURE(sylvan(1, {"serendipity"}));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * hider = ours("core:centaur", BattleHex(3, 5), 20, 300);
	auto * closer = theirs("core:pikeman", BattleHex(8, 5));
	auto * farther = theirs("core:pikeman", BattleHex(12, 3));
	auto * equal = theirs("core:pikeman", BattleHex(9, 7));
	auto * wide = theirs("core:cavalier", BattleHex(9, 9));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_TRUE(hider->doubleWide() && wide->doubleWide());
	walked(closer, BattleHex(12, 5));
	walked(farther, BattleHex(10, 3));
	walked(equal, BattleHex(9, 3));
	walked(wide, BattleHex(13, 9));
	const auto distance = [&](const CStack * unit, BattleHex at)
	{
		return ambush::footprintDistance(*unit, at, *hider, hider->getPosition());
	};
	ASSERT_EQ(distance(equal, BattleHex(9, 3)), distance(equal, BattleHex(9, 7)));
	EXPECT_TRUE(ambush::prey(*battle(), hider, closer));
	EXPECT_FALSE(ambush::prey(*battle(), hider, farther));
	EXPECT_FALSE(ambush::prey(*battle(), hider, equal));
	EXPECT_TRUE(ambush::prey(*battle(), hider, wide));
	// Both footprints count: the two-hex mover's nearer hex and the ambusher's.
	EXPECT_EQ(ambush::footprintDistance(*wide, wide->getPosition(), *hider, hider->getPosition()),
		std::min(BattleHex::getDistance(wide->getPosition(), BattleHex(3, 5)),
			BattleHex::getDistance(wide->getPosition(), BattleHex(2, 5))));
}

TEST_F(SylvanAmbushTest, AmbushStrikeNeedsPreyAndIsSpentByTheFirstAttack)
{
	ASSERT_NO_FATAL_FAILURE(sylvan(3, {"serendipity", "startle", "apexPredator"}));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * hider = ours("core:dwarf", BattleHex(6, 5), 20, 300);
	auto * prey = theirs("core:pikeman", BattleHex(7, 5), 20, 900);
	auto * idle = theirs("core:pikeman", BattleHex(7, 4), 20, 800);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_TRUE(wait(hider));
	ASSERT_TRUE(hider->sylvanAmbush.concealed);
	walked(prey, BattleHex(12, 5));
	ASSERT_NO_FATAL_FAILURE(delayedActivation(hider));
	ASSERT_TRUE(hider->sylvanAmbush.strikePending);
	EXPECT_TRUE(ambush::ambushStrike(*battle(), strike(hider, prey)));
	EXPECT_FALSE(ambush::ambushStrike(*battle(), strike(hider, idle)));
	// Patient Hunter 30% plus Apex Predator 25%, added; 15% against other targets.
	EXPECT_EQ(ambush::waitStrikePercent(*battle(), strike(hider, prey), true, 15), 55);
	EXPECT_EQ(ambush::waitStrikePercent(*battle(), strike(hider, idle), true, 15), 15);
	EXPECT_EQ(ambush::waitStrikePercent(*battle(), strike(hider, prey), false, 15), 25);
	auto retaliationInfo = strike(hider, prey);
	retaliationInfo.retaliation = true;
	EXPECT_FALSE(ambush::ambushStrike(*battle(), retaliationInfo));
	auto collateral = strike(hider, prey);
	collateral.secondaryAttack = true;
	EXPECT_FALSE(ambush::ambushStrike(*battle(), collateral));
	const auto ambushed = damage(hider, prey);
	auto plain = hider->acquireState();
	plain->sylvanAmbush.finishActivation();
	BattleAttackInfo plainInfo(plain.get(), prey, 0, false);
	const auto ordinary = battle()->calculateDmgRange(plainInfo).damage.min;
	EXPECT_GE(ambushed, ordinary * 155 / 115 - 1);
	EXPECT_LE(ambushed, ordinary * 155 / 115 + 1);
	// A non-prey first attack spends the strike too.
	auto spent = hider->acquireState();
	spent->afterAttack(false, false);
	EXPECT_FALSE(spent->sylvanAmbush.strikePending);
}

TEST_F(SylvanAmbushTest, StartleHalvesOnlyTheRetaliationToThatStrikeAndHuntingPackLastsTwoRounds)
{
	ASSERT_NO_FATAL_FAILURE(sylvan(3, {"elvenPrecision", "startle", "apexPredator"}));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * hider = ours("core:dwarf", BattleHex(6, 5), 20, 300);
	auto * pack = ours("core:dwarf", BattleHex(6, 6), 20, 200);
	auto * human = ours("core:pikeman", BattleHex(6, 4), 20, 100);
	auto * prey = theirs("core:dwarf", BattleHex(7, 5), 30, 900);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_TRUE(wait(hider));
	walked(prey, BattleHex(12, 5));
	ASSERT_NO_FATAL_FAILURE(delayedActivation(hider));
	const auto full = damage(prey, pack, false, true);
	// Forecasts read the pending strike before it marks its victim.
	const auto forecast = damage(prey, hider, false, true);
	EXPECT_EQ(ambush::huntingPackPercent(*battle(), pack, prey), 0);
	ASSERT_TRUE(attack(hider, prey->getPosition()));
	EXPECT_FALSE(hider->sylvanAmbush.strikePending);
	EXPECT_EQ(prey->sylvanAmbush.huntingPackRound, battle()->battleGetRound());
	EXPECT_GE(forecast, full / 2 - 1);
	EXPECT_LE(forecast, full / 2 + 1);
	// The retaliation answered the strike, so later ones are full again.
	EXPECT_EQ(prey->sylvanAmbush.startledBy, -1);
	EXPECT_EQ(damage(prey, hider, false, true), damage(prey, pack, false, true));
	EXPECT_EQ(ambush::huntingPackPercent(*battle(), pack, prey), 15);
	EXPECT_EQ(ambush::huntingPackPercent(*battle(), human, prey), 0); // Not Rampart.
	battle()->nextRound();
	EXPECT_EQ(ambush::huntingPackPercent(*battle(), pack, prey), 15);
	battle()->nextRound();
	EXPECT_EQ(ambush::huntingPackPercent(*battle(), pack, prey), 0);
}

TEST_F(SylvanAmbushTest, IvorElvesStrikeOnlyEveryOtherRoundAndKeepMeleeProtection)
{
	ASSERT_NO_FATAL_FAILURE(sylvan(1, {"serendipity"}));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * elves = ours("core:woodElf", BattleHex(3, 5), 20, 300);
	ASSERT_TRUE(theirs("core:pikeman", BattleHex(12, 5)));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto state = elves->acquireState();
	const auto hideAsIvor = [&]()
	{
		state->waiting = state->waitedThisTurn = true;
		state->forestAmbush = {};
		state->forestAmbush.acceptWait(true);
	};
	hideAsIvor();
	EXPECT_TRUE(newHorizonsForestAmbush::protectsFromMelee(state.get()));
	ambush::beginActivation(*battle(), *state, true);
	EXPECT_TRUE(state->sylvanAmbush.strikePending);
	EXPECT_EQ(state->sylvanAmbush.concealedRound, battle()->battleGetRound());
	EXPECT_EQ(state->sylvanAmbush.source, AmbushSource::NONE);
	const int first = battle()->battleGetRound();
	battle()->nextRound();
	state->sylvanAmbush.newRound();
	hideAsIvor();
	ambush::beginActivation(*battle(), *state, true);
	EXPECT_FALSE(state->sylvanAmbush.strikePending);
	EXPECT_EQ(state->sylvanAmbush.concealedRound, first);
	battle()->nextRound();
	state->sylvanAmbush.newRound();
	hideAsIvor();
	ambush::beginActivation(*battle(), *state, true);
	EXPECT_TRUE(state->sylvanAmbush.strikePending);
}

TEST_F(SylvanAmbushTest, DappledCoverProtectsAWaitingStackThatDidNotHide)
{
	ASSERT_NO_FATAL_FAILURE(sylvan(1, {"luckyRecovery"}));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * hider = ours("core:dwarf", BattleHex(3, 1), 20, 700);
	auto * exposed = ours("core:dwarf", BattleHex(3, 7), 20, 600);
	auto * archers = theirs("core:archer", BattleHex(14, 5), 10, 100);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	const auto ordinary = damage(archers, exposed, true);
	ASSERT_TRUE(wait(hider));
	ASSERT_TRUE(wait(exposed));
	EXPECT_TRUE(hider->sylvanAmbush.concealed);
	EXPECT_FALSE(exposed->sylvanAmbush.concealed);
	EXPECT_TRUE(newHorizonsSylvanTactics::rangedProtection(*battle(), hider).empty());
	EXPECT_EQ(newHorizonsSylvanTactics::rangedProtection(*battle(), exposed), std::vector<int>{25});
	EXPECT_NEAR(static_cast<double>(damage(archers, exposed, true)), ordinary * 0.75, 1.0);
}

TEST_F(SylvanAmbushPolicy4Test, CapturedPolicyFourNeverConcealsOrStrikes)
{
	ASSERT_NO_FATAL_FAILURE(sylvan(3, {"luckyRecovery", "natureSProvidence", "perfectMoment"}));
	EXPECT_TRUE(newHorizonsHeroes::usesSylvanTacticsV4(attackerSideHero->getPerkState().getRules()));
	EXPECT_FALSE(ambush::captured(attackerSideHero));
	EXPECT_TRUE(newHorizonsSylvanTacticsRevision::active(attackerSideHero, "natureSProvidence"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * waiter = ours("core:dwarf", BattleHex(3, 5), 20, 700);
	ASSERT_TRUE(theirs("core:pikeman", BattleHex(12, 5)));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	EXPECT_FALSE(ambush::hasBattlePolicy(*battle()));
	ASSERT_TRUE(wait(waiter));
	EXPECT_EQ(waiter->sylvanAmbush, battle::SylvanAmbushState{});
}

TEST_F(SylvanAmbushTest, StateRoundTripsAndRejectsInvalidOrDowngradedState)
{
	battle::SylvanAmbushState state;
	state.conceal(AmbushSource::FOREST_COVER, 4);
	state.recordApproach(4, BattleHex(30), BattleHex(31));
	state.huntingPackRound = 3;
	state.startledBy = 7;
	EXPECT_EQ(battle::SylvanAmbushState::fromJson(state.toJson()), state);
	EXPECT_NO_THROW(state.validateAt(4));
	EXPECT_THROW(state.validateAt(5), std::runtime_error);
	auto noSource = state.toJson();
	noSource["source"] = JsonNode(static_cast<int64_t>(0));
	EXPECT_THROW(battle::SylvanAmbushState::fromJson(noSource), std::runtime_error);
	auto oneHex = state.toJson();
	oneHex["approachEnd"] = JsonNode(static_cast<int64_t>(-1));
	EXPECT_THROW(battle::SylvanAmbushState::fromJson(oneHex), std::runtime_error);
	auto pendingWithoutRound = battle::SylvanAmbushState{};
	pendingWithoutRound.strikePending = true;
	pendingWithoutRound.strikeOrigin = BattleHex(20);
	EXPECT_THROW(pendingWithoutRound.validate(), std::runtime_error);
	auto unknown = state.toJson();
	unknown["extra"] = JsonNode(true);
	EXPECT_THROW(battle::SylvanAmbushState::fromJson(unknown), std::runtime_error);

	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & state);
	battle::SylvanAmbushState loaded;
	ASSERT_NO_THROW(current.iser & loaded);
	EXPECT_EQ(loaded, state);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_REVISED_SPECIALTIES;
	EXPECT_THROW(old.oser & state, std::runtime_error);
	battle::SylvanAmbushState inert;
	CMemorySerializer oldInert;
	oldInert.oser.version = ESerializationVersion::NEW_HORIZONS_REVISED_SPECIALTIES;
	EXPECT_NO_THROW(oldInert.oser & inert);
}

TEST_F(SylvanAmbushTest, UnitPacketsRoundTripAndOldFormatsRejectLiveState)
{
	ASSERT_NO_FATAL_FAILURE(sylvan(1, {"serendipity"}));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * hider = ours("core:dwarf", BattleHex(3, 5), 20, 700);
	ASSERT_TRUE(theirs("core:pikeman", BattleHex(12, 5)));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_TRUE(wait(hider));
	ASSERT_TRUE(hider->sylvanAmbush.concealed);
	UnitChanges update(hider->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = hider->save();
	CMemorySerializer bytes;
	ASSERT_NO_THROW(bytes.oser & update);
	UnitChanges restored;
	ASSERT_NO_THROW(bytes.iser & restored);
	auto detached = hider->acquireState();
	detached->sylvanAmbush = {};
	detached->load(restored.data);
	EXPECT_EQ(detached->sylvanAmbush, hider->sylvanAmbush);
	auto legacy = restored.data;
	legacy["state"].Struct().erase("sylvanAmbush");
	detached->load(legacy);
	EXPECT_EQ(detached->sylvanAmbush, battle::SylvanAmbushState{});
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	packet.changedStacks.push_back(update);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_REVISED_SPECIALTIES;
	EXPECT_THROW(old.oser & packet, std::runtime_error);
	// A concealed record without the accepted Wait queue state is corrupt.
	auto forged = restored.data;
	forged["state"]["waiting"] = JsonNode(false);
	EXPECT_THROW(battle::sylvanAmbushStateFromUnitJson(forged), std::runtime_error);
}

TEST_F(SylvanAmbushTest, WaitButtonOffersExactlyWhatTheServerGrants)
{
	ASSERT_NO_FATAL_FAILURE(sylvan(2, {"serendipity", "forestCover"}));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * first = ours("core:dwarf", BattleHex(3, 1), 10, 700);
	auto * second = ours("core:dwarf", BattleHex(3, 3), 10, 650);
	auto * covered = ours("core:dwarf", BattleHex(5, 5), 10, 600);
	auto * open = ours("core:dwarf", BattleHex(3, 9), 10, 550);
	ASSERT_TRUE(theirs("core:pikeman", BattleHex(14, 5)));
	ASSERT_NO_FATAL_FAILURE(forceField(BattleHex(4, 5)));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	const auto translate = [](const std::string & key) { return "<" + key + ">"; };
	const auto shown = [&](const CStack * unit)
	{
		return sylvanAmbushWaitButton::present(ambush::concealmentOffer(*battle(), *unit), translate);
	};
	const auto check = [&](CStack * unit)
	{
		const bool promised = shown(unit).ambush;
		EXPECT_TRUE(wait(unit));
		EXPECT_EQ(unit->sylvanAmbush.concealed, promised);
	};
	EXPECT_EQ(shown(first).help, "<new-horizons.combat.sylvanAmbush.wait.rank>");
	auto rankText = sylvanAmbushWaitButton::present(ambush::concealmentOffer(*battle(), *first),
		[](const std::string &) { return std::string("%LEFT% of %TOTAL%"); });
	EXPECT_EQ(rankText.help, "2 of 2");
	ASSERT_NO_FATAL_FAILURE(check(first));
	rankText = sylvanAmbushWaitButton::present(ambush::concealmentOffer(*battle(), *second),
		[](const std::string &) { return std::string("%LEFT% of %TOTAL%"); });
	EXPECT_EQ(rankText.help, "1 of 2");
	ASSERT_NO_FATAL_FAILURE(check(second));
	EXPECT_EQ(shown(covered).help, "<new-horizons.combat.sylvanAmbush.wait.forestCover>");
	EXPECT_FALSE(shown(open).ambush);
	ASSERT_NO_FATAL_FAILURE(check(covered));
	ASSERT_NO_FATAL_FAILURE(check(open));
	// Harmed, last-round, Sanctified and policy-absent cases all fall back.
	battle()->nextRound();
	EXPECT_FALSE(shown(first).ambush); // Concealed last round.
	effect(open, Bonus(BonusDuration::ONE_BATTLE, BonusType::SANCTIFIED, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_FALSE(shown(open).ambush);
	auto harmed = open->acquireState();
	harmed->sylvanAmbush.harm();
	EXPECT_FALSE(sylvanAmbushWaitButton::present(ambush::concealmentOffer(*battle(), *harmed), translate).ambush);
	EXPECT_FALSE(sylvanAmbushWaitButton::present({}, translate).ambush);
}
}

TEST_F(SylvanAmbushTest, DamageIsDynamicOnlyWhileAnAmbushRiderCanApply)
{
	ASSERT_NO_FATAL_FAILURE(sylvan(3, {"elvenPrecision", "startle", "apexPredator"}));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * hider = ours("core:centaur", BattleHex(6, 5), 20, 700);
	auto * target = theirs("core:pikeman", BattleHex(8, 5), 10, 800);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	// No pending strike, Startle mark or Hunting Pack mark: both directions stay cacheable.
	EXPECT_FALSE(ambush::dynamicDamage(*battle(), hider, target));
	EXPECT_FALSE(ambush::dynamicDamage(*battle(), target, hider));
	// A pending strike can carry Apex Predator, and Startle on its retaliation.
	hider->sylvanAmbush.strikePending = true;
	EXPECT_TRUE(ambush::dynamicDamage(*battle(), hider, target));
	EXPECT_TRUE(ambush::dynamicDamage(*battle(), target, hider));
	hider->sylvanAmbush.strikePending = false;
	target->sylvanAmbush.startledBy = static_cast<int32_t>(hider->unitId());
	EXPECT_TRUE(ambush::dynamicDamage(*battle(), target, hider));
	target->sylvanAmbush.startledBy = -1;
	// Hunting Pack reads a mark from this round or the previous one.
	target->sylvanAmbush.huntingPackRound = battle()->battleGetRound();
	EXPECT_TRUE(ambush::dynamicDamage(*battle(), hider, target));
	EXPECT_FALSE(ambush::dynamicDamage(*battle(), target, hider));
	target->sylvanAmbush.huntingPackRound = -1;
	EXPECT_FALSE(ambush::dynamicDamage(*battle(), hider, target));
}
