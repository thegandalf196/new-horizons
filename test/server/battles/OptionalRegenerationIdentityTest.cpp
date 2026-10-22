/*
 * OptionalRegenerationIdentityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "VerdantPrisonScriptErrorCapture.h"
#include "../../SpellPointTestUtils.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/constants/StringConstants.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/Problem.h"

class OptionalRegenerationIdentityTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the core/vcmi/vcmi-test profile without New Horizons";
		startGame();
		startBattle();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		setTestSpellPointTotal(attackerSideHero, 9999);
	}

	void checkAdmission(SpellID spell, BattleSide targetSide)
	{
		attackerSideHero->addSpellToSpellbook(spell);
		auto * target = addStack(targetSide, creatureByName("core:angel"),
			BattleHex(targetSide == BattleSide::ATTACKER ? leftHex : rightHex), 10);
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		const auto mechanics = spell.toSpell()->battleMechanics(&cast);
		spells::detail::ProblemImpl problem;
		spells::Target destination;
		destination.emplace_back(target);
		// No cast, pack, round advancement or new spell declaration is performed.
		EXPECT_TRUE(attackerSideHero->hasSpellbook());
		EXPECT_TRUE(attackerSideHero->canCastThisSpell(spell.toSpell()));
		EXPECT_GE(attackerSideHero->getManaAvailable(), battle()->battleGetSpellCost(spell.toSpell(), attackerSideHero));
		EXPECT_EQ(battle()->battleCanCastSpell(attackerSideHero, spells::Mode::HERO), ESpellCastProblem::OK)
			<< "round=" << battle()->battleGetRound() << ", tactics=" << battle()->battleTacticDist();
		VerdantPrisonScriptErrorCapture scriptErrors;
		bool admitted = false;
		bool targetAdmitted = false;
		size_t effectCount = 0;
		mechanics->forEachEffect([&](const auto &)
		{
			++effectCount;
			return false;
		});
		EXPECT_GT(effectCount, 0u) << "effect level=" << mechanics->getEffectLevel();
		ASSERT_NO_THROW(admitted = mechanics->canBeCast(problem));
		ASSERT_NO_THROW(targetAdmitted = mechanics->canBeCastAt(destination, problem));
		std::vector<std::string> problems;
		problem.getAll(problems);
		EXPECT_TRUE(admitted) << testing::PrintToString(problems) << scriptErrors.errors();
		EXPECT_TRUE(targetAdmitted) << testing::PrintToString(problems) << scriptErrors.errors();
		EXPECT_TRUE(scriptErrors.errors().empty()) << scriptErrors.errors();
	}
};

TEST_F(OptionalRegenerationIdentityTest, FriendlyCoreSpellNeedsNoInstalledRegeneration)
{
	checkAdmission(SpellID::BLESS, BattleSide::ATTACKER);
}

TEST_F(OptionalRegenerationIdentityTest, HostileCoreSpellNeedsNoInstalledRegeneration)
{
	checkAdmission(SpellID::CURSE, BattleSide::DEFENDER);
}
