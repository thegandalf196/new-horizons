/*
 * NewHorizonsPuppetMasterAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"

#include <algorithm>
#include <memory>
#include <string>

namespace
{
SpellID puppetMasterSpell()
{
	return SpellID(SpellID::decode("new-horizons:puppetMaster"));
}

class PuppetMasterEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit PuppetMasterEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsPuppetMasterAITest : public HeroCommandFixture
{
protected:
	CStack * controlled = nullptr;
	CStack * originalSideAlly = nullptr;
	CStack * controllerAlly = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
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
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		attackerSideHero->addSpellToSpellbook(puppetMasterSpell());
		const int chaosMagic = SecondarySkill::decode("new-horizons:chaosMagic");
		ASSERT_GE(chaosMagic, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(chaosMagic), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		controllerAlly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 20);
		controlled = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(12, 5), 50);
		originalSideAlly = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(8, 5), 100);
		ASSERT_NE(controllerAlly, nullptr);
		ASSERT_NE(controlled, nullptr);
		ASSERT_NE(originalSideAlly, nullptr);
		beginCombat();

		// This fixture bonus keeps the existing Berserk suppression path visible
		// while the independent hypothetical Puppet cast is evaluated.
		controlled->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::ATTACKS_NEAREST_CREATURE, BonusSource::SPELL_EFFECT, 1,
			BonusSourceID(SpellID(SpellID::BERSERK))));
	}
};
}

TEST_F(NewHorizonsPuppetMasterAITest, DetachedCastKeepsPhysicalSideButScoresFormerAlliesAsHostileActions)
{
	prepare();
	auto environment = std::make_shared<PuppetMasterEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto projected = std::make_shared<HypotheticBattle>(environment.get(), callback);
	const auto * projectedControlled = projected->battleGetUnitByID(controlled->unitId());
	ASSERT_NE(projectedControlled, nullptr);
	const auto * liveAlly = projected->battleGetUnitByID(originalSideAlly->unitId());
	ASSERT_NE(liveAlly, nullptr);
	const auto liveAllyHealth = originalSideAlly->getAvailableHealth();

	const auto * spell = puppetMasterSpell().toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(projected.get(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_NE(mechanics, nullptr);
	spells::Target aim{battle::Destination(projectedControlled)};
	ASSERT_TRUE(mechanics->canBeCastAt(aim));
	mechanics->castEval(projected->getServerCallback(), aim);

	const auto * controlledAfterCast = projected->battleGetUnitByID(controlled->unitId());
	ASSERT_NE(controlledAfterCast, nullptr);
	EXPECT_TRUE(newHorizonsPuppetMaster::hasValidControlMarker(*projected, controlledAfterCast));
	EXPECT_EQ(projected->battleGetActionController(controlledAfterCast), PlayerColor(0));
	EXPECT_EQ(projected->battleGetOwner(controlledAfterCast), PlayerColor(1));
	EXPECT_EQ(controlledAfterCast->unitSide(), BattleSide::DEFENDER);
	EXPECT_TRUE(projected->battleMatchOwner(controlledAfterCast, liveAlly, true));
	EXPECT_TRUE(projected->battleMatchActionController(controlledAfterCast, liveAlly));
	EXPECT_TRUE(projected->battleCanShootAction(controlledAfterCast, liveAlly->getPosition()));
	EXPECT_FALSE(newHorizonsPuppetMaster::hasControlMarker(controlled))
		<< "The cast evaluation is detached and cannot mutate the live battle";

	DamageCache damage;
	PotentialTargets actions(controlledAfterCast, damage, projected);
	ASSERT_FALSE(actions.possibleAttacks.empty());
	const auto formerAllyShot = std::ranges::find_if(actions.possibleAttacks,
		[this](const AttackPossibility & possibility)
		{
			return possibility.attack.shooting
				&& possibility.attack.defender->unitId() == originalSideAlly->unitId();
		});
	ASSERT_NE(formerAllyShot, actions.possibleAttacks.end());
	EXPECT_GT(formerAllyShot->damageDiff(), 0.0f);
	EXPECT_GT(actions.bestActionValue(), 0)
		<< "The detached AI values a direct former-ally shot as hostile to the current controller";
	EXPECT_FALSE(actions.berserk)
		<< "Puppet-controlled action planning is not replaced by Berserk's forced target chooser";

	EXPECT_EQ(originalSideAlly->getAvailableHealth(),
		liveAllyHealth);
}
