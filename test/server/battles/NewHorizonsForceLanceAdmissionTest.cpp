/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsSorcerySpells.h"

namespace
{
class NewHorizonsForceLanceAdmissionTest : public HeroCommandFixture
{
protected:
	CStack * ally = nullptr;
	CStack * enemy = nullptr;
	SpellID lance = SpellID::NONE;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		useRevisedOrders = true;
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare()
	{
		startGame();
		lance = SpellID(SpellID::decode(newHorizonsSorcerySpells::FORCE_LANCE));
		ASSERT_TRUE(lance.hasValue());
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:neela")));
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")),
			3, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(lance);
		attackerSideHero->setStackCount(SlotID(0), 1000);
		defenderSideHero->setStackCount(SlotID(0), 1000);
		startBattle();
		for(const auto & unit : battle()->stacks)
			if(unit->unitSlot() == SlotID(0))
				(unit->unitSide() == BattleSide::ATTACKER ? ally : enemy) = unit.get();
		ASSERT_NE(ally, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();
	}
	void defend(BonusType type, int value, BonusSubtypeID subtype = BonusSubtypeID())
	{
		enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER,
			value, BonusSourceID(), subtype));
	}
};
}

TEST_F(NewHorizonsForceLanceAdmissionTest, FullAuthoritativeCastBypassesImmunityResistanceReductionShieldAndMirror)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	defend(BonusType::SPELL_IMMUNITY, 1, BonusSubtypeID(lance));
	defend(BonusType::MAGIC_RESISTANCE, 100);
	defend(BonusType::MAGICAL_DAMAGE_IMMUNITY, 1);
	defend(BonusType::SPELL_DAMAGE_REDUCTION, 100, BonusSubtypeID(SpellSchool::ANY));
	defend(BonusType::MAGIC_MIRROR, 100);
	enemy->guardianSpiritHitPoints = 500;
	enemy->guardianSpiritRoundsRemaining = 3;
	enemy->health.addTemporaryHitPoints(300);
	const auto initialHealth = enemy->getAvailableHealth();
	const auto allyHealth = ally->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto casts = server.casts.size();
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, lance.toSpell());
	const auto mechanics = lance.toSpell()->battleMechanics(&event);
	const auto raw = newHorizonsSorcerySpells::forceLanceDamage(*mechanics);
	ASSERT_GT(raw, 0);
	ASSERT_TRUE(castOn(attackerSideHero, lance, enemy));
	EXPECT_EQ(enemy->getAvailableHealth(), initialHealth - raw);
	EXPECT_EQ(enemy->guardianSpiritHitPoints, 500);
	EXPECT_EQ(enemy->health.getTemporaryHitPoints(), 300);
	EXPECT_EQ(ally->getAvailableHealth(), allyHealth);
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
	ASSERT_EQ(server.casts.size(), casts + 1);
	EXPECT_TRUE(server.casts.back().announcement.resistedCres.empty());
	const auto pins = enemy->getBonusesOfType(BonusType::BIND_EFFECT);
	ASSERT_EQ(pins->size(), 1);
	EXPECT_EQ(pins->front()->turnsRemain, 1);
}

TEST_F(NewHorizonsForceLanceAdmissionTest, GuaranteedHitRetainsOrdinaryHostileLivingTargetLegality)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, lance.toSpell());
	const auto mechanics = lance.toSpell()->battleMechanics(&event);
	EXPECT_FALSE(mechanics->canBeCastAt({spells::Destination(ally)}));
	EXPECT_TRUE(mechanics->canBeCastAt({spells::Destination(enemy)}));
	EXPECT_TRUE(mechanics->isReceptive(enemy));
	const auto initialMana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(castOn(attackerSideHero, lance, ally));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), initialMana);
	EXPECT_TRUE(server.casts.empty());
}
