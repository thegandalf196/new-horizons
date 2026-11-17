/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/ForecastSideState.h"
#include "../../lib/nativeRules/CapturedRuleSet.h"
#include "../../lib/nativeRules/CapturedPerkRules.h"
#include "../../lib/networkPacks/PacksForClient.h"
namespace forecastSideTest
{
using namespace forecast;
inline constexpr auto SIDE = BattleSide::ATTACKER;
class ForecastSideStateTest : public ProjectedTargetHitFixture
{
protected:
	void prepareSide(bool elemental = false)
	{
		startGame();
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:aenain")));
		ASSERT_TRUE(attackerSideHero->getPerkState().getSelected().empty());
		if(elemental)
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsElementalAttunement::SKILL_ID)),
				MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->initializeSpellPoints(50, 5);
		Bonus combatMana; combatMana.type = BonusType::COMBAT_MANA_BONUS; combatMana.val = 4;
		GiveBonus temporary(GiveBonus::ETarget::OBJECT, attackerSideHero->id, combatMana);
		gameHandler->sendAndApply(temporary);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
		startBattle(); beginCombat();
		environment = std::make_unique<projectedHitTest::Environment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	}
	std::shared_ptr<const SideState> side()
	{
		auto captured = SideState::capture(*live, SIDE);
		if(!captured) throw std::runtime_error("Native fixture side was not admitted");
		return std::make_shared<const SideState>(std::move(*captured));
	}
	void expectNativeMana(const SideState & state)
	{
		ASSERT_TRUE(state.mana);
		EXPECT_EQ(state.mana->pools.getNormal(), attackerSideHero->getNormalSpellPoints());
		EXPECT_EQ(state.mana->pools.getBuffer(), attackerSideHero->getBufferSpellPoints());
		EXPECT_EQ(state.mana->temporaryBufferRemaining, battle()->getTemporaryBufferRemaining(SIDE));
	}
};

}
