/*
 * NewHorizonsPhantomArmyTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../SpellPointTestUtils.h"

#include "BattleTestFixture.h"
#include "../../../server/CGameHandler.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"

namespace
{
SpellID phantomArmySpell()
{
	return SpellID(SpellID::decode(newHorizonsSorcery::PHANTOM_ARMY_SPELL));
}

std::shared_ptr<Bonus> timeStopMarker(BattleSide side)
{
	auto marker = std::make_shared<Bonus>(
		BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	marker->parameters = std::make_shared<BonusParameters>(static_cast<int32_t>(side));
	return marker;
}

int64_t sourceDamageToLeaveHealth(int32_t sourceCount, int64_t availableHealth)
{
	const auto maxHealth = BattleTestFixture::creatureByName("core:pikeman").toCreature()->getMaxHealth();
	return static_cast<int64_t>(sourceCount) * maxHealth - availableHealth;
}

class NewHorizonsPhantomArmyTest : public BattleTestFixture
{
protected:
	bool useIllusionistPerkRules = false;
	bool startCombatBeforeCast = false;

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		if(useIllusionistPerkRules)
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
				JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	bool startPhantomBattle(CStack *& source, CStack *& phantom, int32_t sourceCount = 1000,
		bool sourceHasExtraHealth = false, bool selectIllusionist = false,
		int32_t spellPower = 100, int64_t sourceDamageBeforeCast = 0)
	{
		const SpellID spell = phantomArmySpell();
		if(spell == SpellID::NONE)
			return false;

		useIllusionistPerkRules = selectIllusionist;
		startGame();
		if(selectIllusionist)
		{
			const auto sorcery = SecondarySkill::decode("new-horizons:sorceryMagic");
			if(sorcery < 0)
				return false;
			attackerSideHero->setSecSkillLevel(SecondarySkill(sorcery), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({
				"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.illusionist"});
			if(!attackerSideHero->hasActivePerk(
				"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.illusionist"))
				return false;
		}
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(spell);
		setTestSpellPointTotal(attackerSideHero, 9999);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		startBattle();

		source = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), sourceCount);
		if(!source)
			return false;
		if(sourceHasExtraHealth)
			source->health.addTemporaryHitPoints(source->getTotalHealth() * 3);
		if(sourceDamageBeforeCast > 0)
		{
			int64_t damage = sourceDamageBeforeCast;
			source->health.damage(damage);
			if(damage != sourceDamageBeforeCast)
				return false;
		}
		// Hero Actions are granted when the first playable round begins, not
		// during the round-zero battle construction phase.
		if(startCombatBeforeCast)
			beginCombat();
		else
			battle()->nextRound();
		if(!castOn(attackerSideHero, spell, source))
			return false;

		const auto phantoms = battle()->battleGetStacksIf([](const CStack * unit)
		{
			return unit->getPhantomInitialIntegrity() > 0;
		});
		if(phantoms.size() != 1)
			return false;
		phantom = const_cast<CStack *>(phantoms.front());
		return true;
	}
};
}

TEST_F(NewHorizonsPhantomArmyTest, RealCastPreservesCountAndIntegrityAcrossUnitAndStateJson)
{
	CStack * source = nullptr;
	CStack * phantom = nullptr;
	ASSERT_TRUE(startPhantomBattle(source, phantom));
	ASSERT_NE(source, nullptr);
	ASSERT_NE(phantom, nullptr);

	const auto spell = phantomArmySpell();
	const auto expectedIntegrity = newHorizonsSorcery::phantomArmyIntegrity(
		source->getAvailableHealth(), attackerSideHero->getEffectPower(spell.toSpell()));
	EXPECT_EQ(phantom->getCount(), source->getCount());
	EXPECT_EQ(phantom->getTotalHealth(), source->getTotalHealth());
	EXPECT_EQ(phantom->getAvailableHealth(), expectedIntegrity);
	EXPECT_EQ(phantom->getPhantomIntegrity(), expectedIntegrity);
	EXPECT_EQ(phantom->getPhantomInitialIntegrity(), expectedIntegrity);
	EXPECT_TRUE(phantom->summoned);
	EXPECT_EQ(phantom->getUnusableRemains(), 0);

	// The spawn profile is carried by the authoritative UnitInfo packet.
	battle::UnitInfo outgoing;
	outgoing.id = phantom->unitId();
	outgoing.count = phantom->getCount();
	outgoing.type = phantom->creatureId();
	outgoing.side = phantom->unitSide();
	outgoing.position = phantom->getPosition();
	outgoing.summoned = true;
	outgoing.phantomIntegrity = expectedIntegrity;
	outgoing.phantomDuration = newHorizonsSorcery::PHANTOM_ARMY_DURATION_ROUNDS;
	JsonNode packetData;
	outgoing.save(packetData);
	battle::UnitInfo incoming;
	incoming.load(outgoing.id, packetData);
	EXPECT_EQ(incoming.phantomIntegrity, outgoing.phantomIntegrity);
	EXPECT_EQ(incoming.phantomDuration, outgoing.phantomDuration);
	EXPECT_EQ(incoming.count, outgoing.count);

	// Runtime damage changes only Integrity; CUnitState persistence must preserve that
	// separate ledger while the copied stack still reports its complete creature count.
	auto damaged = phantom->acquireState();
	int64_t damage = std::max<int64_t>(1, expectedIntegrity / 4);
	damaged->damage(damage);
	const auto stateData = damaged->save();
	auto restored = phantom->acquireState();
	restored->load(stateData);
	EXPECT_EQ(restored->getPhantomInitialIntegrity(), expectedIntegrity);
	EXPECT_EQ(restored->getPhantomIntegrity(), expectedIntegrity - damage);
	EXPECT_EQ(restored->getCount(), source->getCount());
	EXPECT_EQ(restored->getTotalHealth(), source->getTotalHealth());
}

TEST_F(NewHorizonsPhantomArmyTest, SourceHealthIntegrityMayExceedNominalHealthAndInvalidProfileIsAtomic)
{
	CStack * source = nullptr;
	CStack * phantom = nullptr;
	ASSERT_TRUE(startPhantomBattle(source, phantom, 1000, true));
	ASSERT_GT(source->getAvailableHealth(), source->getTotalHealth());
	ASSERT_GT(phantom->getPhantomIntegrity(), phantom->getTotalHealth());
	auto savedState = phantom->acquireState()->save();
	auto restoredState = phantom->acquireState();
	restoredState->load(savedState);
	EXPECT_EQ(restoredState->getPhantomIntegrity(), phantom->getPhantomIntegrity());

	const auto initialStacks = battle()->battleGetStacksIf([](const CStack *) { return true; }).size();
	battle::UnitInfo invalid;
	invalid.id = battle()->nextUnitId();
	invalid.count = source->getCount();
	invalid.type = source->creatureId();
	invalid.side = BattleSide::ATTACKER;
	invalid.position = BattleHex(leftHex + 10);
	invalid.summoned = true;
	invalid.phantomDuration = newHorizonsSorcery::PHANTOM_ARMY_DURATION_ROUNDS;
	JsonNode packetData;
	invalid.save(packetData);

	// A malformed profile is rejected before the stack is attached to the battle.
	EXPECT_THROW(battle()->addUnit(invalid.id, packetData), std::runtime_error);
	EXPECT_EQ(battle()->battleGetStacksIf([](const CStack *) { return true; }).size(), initialStacks);
	EXPECT_EQ(battle()->getStack(invalid.id), nullptr);
}

TEST_F(NewHorizonsPhantomArmyTest, IllusionistBoostsCappedIntegrityByTwentyFivePercent)
{
	CStack * source = nullptr;
	CStack * phantom = nullptr;
	ASSERT_TRUE(startPhantomBattle(source, phantom, 309, false, true, 200,
		sourceDamageToLeaveHealth(309, 1234)));
	ASSERT_NE(source, nullptr);
	ASSERT_NE(phantom, nullptr);
	ASSERT_EQ(source->getAvailableHealth(), 1234);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(
		"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.illusionist"));

	const auto spell = phantomArmySpell();
	const auto spellPower = attackerSideHero->getEffectPower(spell.toSpell());
	ASSERT_EQ(newHorizonsSorcery::phantomArmyIntegrityBasisPoints(spellPower), 4000);
	ASSERT_EQ(newHorizonsSorcery::phantomArmyIntegrityBasisPoints(spellPower, true), 5000);
	const auto baseIntegrity = newHorizonsSorcery::phantomArmyIntegrity(source->getAvailableHealth(), spellPower);
	const auto expectedIntegrity = newHorizonsSorcery::phantomArmyIntegrity(
		source->getAvailableHealth(), spellPower, true);
	EXPECT_EQ(baseIntegrity, 493);
	EXPECT_EQ(expectedIntegrity, 617);
	EXPECT_EQ(expectedIntegrity, source->getAvailableHealth() * 5000 / 10000);
	EXPECT_EQ(phantom->getPhantomIntegrity(), expectedIntegrity);
}

TEST_F(NewHorizonsPhantomArmyTest, IllusionistIntegrityUsesOneExactFloorForNonRoundHealth)
{
	CStack * source = nullptr;
	CStack * phantom = nullptr;
	ASSERT_TRUE(startPhantomBattle(source, phantom, 323, false, true, 0,
		sourceDamageToLeaveHealth(323, 1292)));
	ASSERT_NE(source, nullptr);
	ASSERT_NE(phantom, nullptr);
	ASSERT_EQ(source->getAvailableHealth(), 1292);

	const auto expectedIntegrity = newHorizonsSorcery::phantomArmyIntegrity(1292, 0, true);
	EXPECT_EQ(expectedIntegrity, 323);
	EXPECT_EQ(phantom->getPhantomIntegrity(), expectedIntegrity);
}

TEST_F(NewHorizonsPhantomArmyTest, TimeStopPausesPhantomRemainingDuration)
{
	CStack * source = nullptr;
	CStack * phantom = nullptr;
	ASSERT_TRUE(startPhantomBattle(source, phantom));
	const auto integrity = phantom->getPhantomIntegrity();

	// Cast in the first playable round; Time Stop holds the full two-round
	// duration across later transitions.
	ASSERT_EQ(battle()->getRound(), 1);
	battle()->addOrUpdateUnitBonus(phantom, *timeStopMarker(BattleSide::DEFENDER), true);
	ASSERT_TRUE(phantom->isTimeStopped());
	for(int transition = 0; transition < 3; ++transition)
	{
		battle()->nextRound();
		EXPECT_TRUE(phantom->alive());
		EXPECT_EQ(phantom->getPhantomIntegrity(), integrity);
	}

	battle()->expireTimeStops(BattleSide::DEFENDER);
	EXPECT_FALSE(phantom->isTimeStopped());
	battle()->nextRound();
	EXPECT_TRUE(phantom->alive());
	EXPECT_EQ(phantom->getPhantomIntegrity(), integrity);
	battle()->nextRound();
	EXPECT_FALSE(phantom->alive());
	EXPECT_TRUE(phantom->ghostPending);
	EXPECT_EQ(phantom->getPhantomIntegrity(), 0);
}

TEST_F(NewHorizonsPhantomArmyTest, AttackPreviewAndAppliedHitUseThePhysicalDamageSplitOnce)
{
	CStack * source = nullptr;
	CStack * phantom = nullptr;
	ASSERT_TRUE(startPhantomBattle(source, phantom));
	auto * attacker = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex + 4), 100);
	ASSERT_NE(attacker, nullptr);

	BattleAttackInfo physical(attacker, phantom, 0, false);
	BattleAttackInfo magical = physical;
	magical.physicalDamage = false;
	const auto physicalRange = battle()->calculateDmgRange(physical).damage;
	const auto magicalRange = battle()->calculateDmgRange(magical).damage;
	ASSERT_GT(physicalRange.max, 0);
	EXPECT_GT(magicalRange.max, physicalRange.max * 7);
	EXPECT_LT(magicalRange.max, physicalRange.max * 9);

	// A lethal forecast must count the complete copied stack as casualties; ordinary
	// per-creature HP prediction would otherwise report no kills for Integrity damage.
	auto * overwhelmingAttacker = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(rightHex + 6), 100000);
	BattleAttackInfo lethalPreview(overwhelmingAttacker, phantom, 0, false);
	const auto lethalEstimate = battle()->calculateDmgRange(lethalPreview);
	ASSERT_GE(lethalEstimate.damage.min, phantom->getPhantomIntegrity());
	EXPECT_EQ(lethalEstimate.kills.min, source->getCount());
	EXPECT_EQ(lethalEstimate.kills.max, source->getCount());

	const auto integrityBefore = phantom->getPhantomIntegrity();
	BattleStackAttacked hit;
	hit.stackAttacked = phantom->unitId();
	hit.damageAmount = physicalRange.max;
	phantom->prepareAttacked(hit, gameHandler->getRandomGenerator());
	ASSERT_EQ(hit.killedAmount, 0);
	EXPECT_EQ(hit.damageAmount, physicalRange.max);

	StacksInjured injury;
	injury.battleID = BattleID(0);
	injury.stacks.push_back(std::move(hit));
	gameHandler->sendAndApply(injury);
	EXPECT_EQ(phantom->getPhantomIntegrity(), integrityBefore - physicalRange.max);
	EXPECT_EQ(phantom->getCount(), source->getCount());
}

TEST_F(NewHorizonsPhantomArmyTest, MagicalSpellAndPositiveFireShieldDamageDoubleIntegrityLoss)
{
	CStack * source = nullptr;
	CStack * phantom = nullptr;
	ASSERT_TRUE(startPhantomBattle(source, phantom));

	giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	defenderSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
	setTestSpellPointTotal(defenderSideHero, 9999);
	const auto copiedCount = source->getCount();
	const auto sourceHealthBefore = source->getAvailableHealth();
	const auto integrityBefore = phantom->getPhantomIntegrity();
	// Compare two real casts against the original copied stack and its phantom.
	// This deliberately uses the live saved Magic Arrow formula instead of the
	// legacy CSpell calculation, then verifies the Phantom-only damage multiplier.
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::MAGIC_ARROW, source));
	const auto normalCasts = server.castsOf(SpellID::MAGIC_ARROW);
	ASSERT_EQ(normalCasts.size(), 1u);
	ASSERT_GT(normalCasts.back().damage, 0);
	EXPECT_EQ(source->getAvailableHealth(), sourceHealthBefore - normalCasts.back().damage);

	// The ordinary Hero Action is one per round. The next transition leaves
	// one round of Phantom Army duration and permits the matching second cast.
	battle()->nextRound();
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::MAGIC_ARROW, phantom));
	const auto casts = server.castsOf(SpellID::MAGIC_ARROW);
	ASSERT_EQ(casts.size(), 2u);
	EXPECT_EQ(casts.back().damage, normalCasts.back().damage * 2);
	EXPECT_EQ(phantom->getPhantomIntegrity(), integrityBefore - casts.back().damage);
	EXPECT_EQ(phantom->getCount(), copiedCount);

	// Fire Shield is a positive timed spell (isDamage() is false) but its
	// adjustDamage path still applies the magical Phantom multiplier.
	const CSpell * fireShield = SpellID(SpellID::FIRE_SHIELD).toSpell();
	const auto reflectedToPhantom = fireShield->adjustRawDamage(source, phantom, 100);
	const auto reflectedToNormalStack = fireShield->adjustRawDamage(source, source, 100);
	EXPECT_EQ(reflectedToPhantom, reflectedToNormalStack * 2);
}

TEST_F(NewHorizonsPhantomArmyTest, PhantomExpiresAfterTwoRoundsAndLethalHitHasNoRebirthOrRemains)
{
	CStack * source = nullptr;
	CStack * phantom = nullptr;
	ASSERT_TRUE(startPhantomBattle(source, phantom));
	const uint32_t phantomId = phantom->unitId();
	const auto initialCount = phantom->getCount();
	const auto integrityBefore = phantom->getPhantomIntegrity();

	// Give the copied creature Rebirth so the lethal path proves the phantom
	// profile blocks conversion back into a permanent stack.
	phantom->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::REBIRTH, BonusSource::CREATURE_ABILITY, 100, BonusSourceID()));

	BattleStackAttacked lethal;
	lethal.stackAttacked = phantomId;
	lethal.damageAmount = integrityBefore;
	phantom->prepareAttacked(lethal, gameHandler->getRandomGenerator());
	EXPECT_EQ(lethal.damageAmount, integrityBefore);
	EXPECT_EQ(lethal.killedAmount, initialCount);
	EXPECT_TRUE(lethal.killed());
	EXPECT_FALSE(lethal.willRebirth());

	StacksInjured injury;
	injury.battleID = BattleID(0);
	injury.stacks.push_back(std::move(lethal));
	gameHandler->sendAndApply(injury);
	EXPECT_EQ(phantom->getCount(), 0);
	EXPECT_EQ(phantom->getPhantomIntegrity(), 0);
	EXPECT_EQ(phantom->getAvailableHealth(), 0);
	EXPECT_TRUE(phantom->summoned);
}


TEST_F(NewHorizonsPhantomArmyTest, ExpiryRemovesPhantomAfterTwoFullBattleRounds)
{
	startCombatBeforeCast = true;
	CStack * source = nullptr;
	CStack * expiring = nullptr;
	ASSERT_TRUE(startPhantomBattle(source, expiring));
	const uint32_t expiringId = expiring->unitId();
	ASSERT_EQ(battle()->getRound(), 1);
	ASSERT_TRUE(battle()->getStack(expiringId)->alive());
	endRound();
	ASSERT_NE(battle()->getStack(expiringId), nullptr);
	EXPECT_TRUE(battle()->getStack(expiringId)->alive());
	endRound();
	EXPECT_EQ(battle()->getStack(expiringId), nullptr);
}
