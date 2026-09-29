/*
 * NewHorizonsShadowGiftTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/battle/NewHorizonsShadowGift.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
constexpr std::string_view SHADOW_GIFT_KEY = "new-horizons:shadowGift";
constexpr std::string_view SHADOW_GIFT_STATUS = "core:shadowGift";
constexpr std::string_view SHADOW_MAGIC_SKILL = "new-horizons:shadowMagic";

SpellID shadowGiftSpell()
{
	return SpellID(SpellID::decode(std::string(SHADOW_GIFT_KEY)));
}

class NewHorizonsShadowGiftTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		ASSERT_NE(shadowGiftSpell(), SpellID::NONE);
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare()
	{
		startGame();
		const auto shadowMagic = SecondarySkill::decode(std::string(SHADOW_MAGIC_SKILL));
		ASSERT_GE(shadowMagic, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(shadowMagic), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(shadowGiftSpell());
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		giftBearer = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"),
			BattleHex(leftHex), 100);
		victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"),
			BattleHex(rightHex), 100);
		ASSERT_NE(giftBearer, nullptr);
		ASSERT_NE(victim, nullptr);
		beginCombat();
		ASSERT_TRUE(newHorizonsMagic::shadowGiftEnabled(battle()->getMagicRules(), shadowGiftSpell()));
	}

	bool castGift(int32_t sacrificePercent)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = shadowGiftSpell();
		action.spellShadowGiftSacrificePercent = sacrificePercent;
		action.aimToUnit(giftBearer);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	const Bonus * giftStatus(const CStack * unit) const
	{
		const auto statusId = ScriptID(ScriptID::decode(std::string(SHADOW_GIFT_STATUS)));
		const auto bonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(shadowGiftSpell())).And(Selector::typeSubtype(BonusType::COMBAT_EVENT_TRIGGER,
			BonusSubtypeID(statusId))));
		return bonuses->empty() ? nullptr : bonuses->front().get();
	}

	bool activateStack(const CStack * stack)
	{
		for(size_t index = 0; index < battle()->stacks.size() * 4 + 4; ++index)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			if(active->unitId() == stack->unitId())
				return true;
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)))
				return false;
		}
		return false;
	}

	CStack * giftBearer = nullptr;
	CStack * victim = nullptr;
};
}

TEST_F(NewHorizonsShadowGiftTest, SelectedTierPaysCurrentAndMaximumHpAndAddsARealShadowAttackPacket)
{
	prepare();
	const auto spell = shadowGiftSpell();
	const int64_t currentBefore = giftBearer->getShadowGiftCurrentHealth();
	const int64_t maximumBefore = giftBearer->getShadowGiftMaximumHealth();
	const int32_t costBasisPoints = newHorizonsShadowGift::getSacrificeCostBasisPoints(20, false);
	const int64_t expectedCurrentCost = newHorizonsShadowGift::getSacrificeHealthAmount(
		currentBefore, costBasisPoints);
	const int64_t expectedMaximumCost = newHorizonsShadowGift::getSacrificeHealthAmount(
		maximumBefore, costBasisPoints);
	const int32_t expectedDamageBonus = newHorizonsShadowGift::getDamageBonusBasisPoints(
		20, 100, newHorizonsMagic::spellPowerCoefficientBasisPoints(
			battle()->getMagicRules(), attackerSideHero, spell));

	ASSERT_TRUE(castGift(20));
	const auto * status = giftStatus(giftBearer);
	ASSERT_NE(status, nullptr);
	EXPECT_EQ(status->duration, BonusDuration::N_TURNS);
	EXPECT_EQ(status->turnsRemain, 3);
	EXPECT_EQ(status->val, expectedDamageBonus);
	EXPECT_EQ(currentBefore - giftBearer->getShadowGiftCurrentHealth(), expectedCurrentCost);
	EXPECT_EQ(maximumBefore - giftBearer->getShadowGiftMaximumHealth(), expectedMaximumCost);
	EXPECT_EQ(giftBearer->getShadowGiftMaximumHealthLost(), expectedMaximumCost);

	ASSERT_TRUE(activateStack(giftBearer));
	blockRetaliation(victim);
	forceMaximumDamage(giftBearer);
	server.attacks.clear();
	server.injuries.clear();
	ASSERT_TRUE(attack(giftBearer, victim->getPosition()));

	const auto primaryAttack = std::ranges::find_if(server.attacks, [](const BattleAttack & attack)
	{
		return !attack.counter();
	});
	ASSERT_NE(primaryAttack, server.attacks.end());
	int64_t physicalDamage = 0;
	for(const auto & hit : primaryAttack->bsa)
		if(hit.stackAttacked == victim->unitId())
			physicalDamage += hit.damageAmount;
	ASSERT_GT(physicalDamage, 0);
	const int64_t rawShadowDamage = physicalDamage * status->val / newHorizonsShadowGift::BASIS_POINTS_PER_WHOLE;
	const auto holdReduction = battle()->battleGetHoldTheLineMagicalReductionBasisPoints(victim);
	const int64_t expectedShadowDamage = spell.toSpell()->adjustRawDamage(
		attackerSideHero, victim, rawShadowDamage, 0, holdReduction);
	ASSERT_GT(expectedShadowDamage, 0);

	const BattleStackAttacked * shadowHit = nullptr;
	for(auto injury = server.injuries.rbegin(); injury != server.injuries.rend() && !shadowHit; ++injury)
		for(const auto & hit : injury->stacks)
			if(hit.stackAttacked == victim->unitId() && hit.isSpell() && hit.spellID == spell)
			{
				shadowHit = &hit;
				break;
			}
	ASSERT_NE(shadowHit, nullptr);
	EXPECT_EQ(shadowHit->attackerID, giftBearer->unitId());
	EXPECT_EQ(shadowHit->damageAmount, expectedShadowDamage);
}
