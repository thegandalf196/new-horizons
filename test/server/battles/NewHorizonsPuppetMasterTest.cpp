/*
 * NewHorizonsPuppetMasterTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/json/JsonBonus.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/Problem.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr auto puppetMasterKey = "new-horizons:puppetMaster";

SpellID puppetMasterSpell()
{
	return SpellID(SpellID::decode(std::string(puppetMasterKey)));
}

SpellID sanctuarySpell()
{
	return SpellID(SpellID::decode("new-horizons:sanctuary"));
}

JsonNode certainMorale()
{
	JsonNode result;
	for(int index = 0; index < 10; ++index)
		result.Vector().emplace_back(100);
	return result;
}

class NewHorizonsPuppetMasterTest : public HeroCommandFixture
{
protected:
	CStack * controlled = nullptr;
	CStack * originalSideTarget = nullptr;
	CStack * controllerAlly = nullptr;
	CStack * protectedOriginalSideTarget = nullptr;
	CStack * normalShooter = nullptr;

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
		loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_MORALE_CHANCE, certainMorale());
		loaded->overrideGameSetting(EGameSettings::COMBAT_MORALE_DICE_SIZE, JsonNode(100));
	}

	void configureHero(CGHeroInstance * hero)
	{
		giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : hero->getSpellsInSpellbook())
			hero->removeSpellFromSpellbook(known);
		for(const auto spell : {puppetMasterSpell(), SpellID(SpellID::BERSERK),
			SpellID(SpellID::SLOW), SpellID(SpellID::HASTE)})
			hero->addSpellToSpellbook(spell);

		const int chaosMagic = SecondarySkill::decode("new-horizons:chaosMagic");
		ASSERT_GE(chaosMagic, 0);
		hero->setSecSkillLevel(SecondarySkill(chaosMagic), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(hero, 1000);
	}

	void prepare(bool rangedControlledStack = false, bool positiveMorale = false)
	{
		startGame();
		configureHero(attackerSideHero);
		configureHero(defenderSideHero);
		startBattle();

		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		controllerAlly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"),
			BattleHex(3, 5), 20);
		normalShooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"),
			BattleHex(3, 7), 20);
		controlled = addStack(BattleSide::DEFENDER,
			creatureByName(rangedControlledStack ? "core:archer" : "core:pikeman"),
			BattleHex(12, 5), 30);
		originalSideTarget = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"),
			BattleHex(rangedControlledStack ? 8 : 13, 5), 100);
		protectedOriginalSideTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"),
			BattleHex(9, 7), 20);
		ASSERT_NE(controllerAlly, nullptr);
		ASSERT_NE(normalShooter, nullptr);
		ASSERT_NE(controlled, nullptr);
		ASSERT_NE(originalSideTarget, nullptr);
		ASSERT_NE(protectedOriginalSideTarget, nullptr);
		if(positiveMorale)
			controlled->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::MORALE, BonusSource::OTHER, 1, BonusSourceID()));

		beginCombat();
	}

	bool castPuppetMaster()
	{
		if(!activate(controllerAlly))
			return false;
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = puppetMasterSpell();
		// The live UI submits a selected hex for Creature-target spells; the
		// action processor must resolve it back to the current unit.
		action.aimToHex(controlled->getPosition());
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool submitUnitAction(PlayerColor player, const battle::Unit * actor, const BattleAction & action)
	{
		if(!actor)
			return false;
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), player, action);
	}

	bool submitControlledAction(const battle::Unit * actor, const BattleAction & action)
	{
		return actor && submitUnitAction(battle()->battleGetActionController(actor), actor, action);
	}

	bool activate(const battle::Unit * wanted)
	{
		for(size_t attempt = 0; attempt < 128; ++attempt)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			if(active->unitId() == wanted->unitId())
				return true;
			const auto action = BattleAction::makeDefend(active);
			if(!submitControlledAction(active, action))
				return false;
		}
		return false;
	}

	void addSanctuary(CStack * target)
	{
		Bonus marker(BonusDuration::ONE_BATTLE, BonusType::SANCTIFIED,
			BonusSource::SPELL_EFFECT, 1, BonusSourceID(sanctuarySpell()));
		target->addNewBonus(std::make_shared<Bonus>(marker));
	}

	void addBerserkSpellEffectBundle(CStack * target)
	{
		const BonusSourceID berserkSource(SpellID(SpellID::BERSERK));
		target->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_OWN_ATTACK,
			BonusType::ATTACKS_NEAREST_CREATURE, BonusSource::SPELL_EFFECT, 0, berserkSource));
		// A second component with the same spell source proves Puppet Master removes the
		// complete Berserk source bundle rather than only its control-driving bonus.
		target->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_OWN_ATTACK,
			BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, 1, berserkSource));
	}

	void addInnateNearestCreatureBonus(CStack * target)
	{
		target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::ATTACKS_NEAREST_CREATURE, BonusSource::CREATURE_ABILITY, 0,
			BonusSourceID(target->creatureId())));
	}

	bool heroSpellIsReceptive(const SpellID spellId, const CStack * target) const
	{
		const auto * spell = spellId.toSpell();
		if(!spell || !target)
			return false;
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
		const auto mechanics = spell->battleMechanics(&cast);
		if(!mechanics)
			return false;
		return mechanics->isReceptive(target);
	}

	auto spellEffects(const CStack * target, SpellID spell) const
	{
		return target->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	}
};
}

TEST_F(NewHorizonsPuppetMasterTest, AcceptedHeroCastRecordsTheControllerWithoutChangingAllegiance)
{
	prepare();
	ASSERT_NE(puppetMasterSpell(), SpellID::NONE);
	const auto * spell = puppetMasterSpell().toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_NE(mechanics, nullptr);
	spells::Target hexAim{battle::Destination(controlled->getPosition())};
	EXPECT_TRUE(mechanics->canBeCastAt(hexAim));
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto castCost = battle()->battleGetSpellCost(spell, attackerSideHero);

	ASSERT_TRUE(castPuppetMaster());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - castCost);
	EXPECT_EQ(server.castsOf(puppetMasterSpell()).size(), 1u);
	EXPECT_TRUE(server.startedActions.back().ba.isSpellAction());
	EXPECT_EQ(server.startedActions.back().ba.spell, puppetMasterSpell());

	const auto * live = battle()->battleGetUnitByID(controlled->unitId());
	ASSERT_NE(live, nullptr);
	const auto markers = live->getBonuses(Selector::type()(BonusType::PUPPET_MASTER_CONTROL));
	ASSERT_EQ(markers->size(), 1u);
	const auto * marker = markers->front().get();
	EXPECT_EQ(marker->source, BonusSource::SPELL_EFFECT);
	EXPECT_EQ(marker->duration, BonusDuration::ONE_BATTLE);
	EXPECT_EQ(marker->sid.as<SpellID>(), puppetMasterSpell());
	EXPECT_EQ(marker->spellCasterOwner, PlayerColor(0));
	EXPECT_TRUE(newHorizonsPuppetMaster::isValidControlMarker(*battle(), live, marker));
	EXPECT_EQ(battle()->battleGetActionController(live), PlayerColor(0));
	EXPECT_EQ(battle()->battleGetOwner(live), PlayerColor(1));
	EXPECT_EQ(live->unitSide(), BattleSide::DEFENDER);
	EXPECT_TRUE(battle()->battleMatchOwner(live, originalSideTarget, true));
	EXPECT_FALSE(battle()->battleMatchActionController(live, originalSideTarget, true));
}

TEST_F(NewHorizonsPuppetMasterTest, WaitRetainsControlThenMeleeEndsItBeforeMoraleAndGrantsLucidity)
{
	prepare(false, true);
	ASSERT_TRUE(castPuppetMaster());
	ASSERT_TRUE(newHorizonsPuppetMaster::hasControlMarker(controlled));
	ASSERT_TRUE(activate(controlled));
	EXPECT_EQ(battle()->battleGetActionController(controlled), PlayerColor(0));

	const auto actionsBeforeWrongOwner = server.startedActions.size();
	EXPECT_FALSE(submitUnitAction(PlayerColor(1), controlled, BattleAction::makeDefend(controlled)));
	EXPECT_EQ(server.startedActions.size(), actionsBeforeWrongOwner);
	EXPECT_EQ(battle()->battleActiveUnit()->unitId(), controlled->unitId());

	ASSERT_TRUE(submitControlledAction(controlled, BattleAction::makeWait(controlled)));
	EXPECT_TRUE(newHorizonsPuppetMaster::hasControlMarker(controlled));
	EXPECT_FALSE(newHorizonsPuppetMaster::hasLucidity(controlled));
	ASSERT_TRUE(activate(controlled));
	EXPECT_EQ(battle()->battleGetActionController(controlled), PlayerColor(0));
	EXPECT_TRUE(battle()->battleMatchActionController(controlled, originalSideTarget));
	EXPECT_FALSE(battle()->battleMatchOwner(controlled, originalSideTarget));
	EXPECT_TRUE(battle()->battleCanAttackUnitAction(controlled, originalSideTarget));
	EXPECT_FALSE(battle()->battleCanAttackUnit(controlled, originalSideTarget));

	const auto beforeAttack = originalSideTarget->getAvailableHealth();
	const auto melee = BattleAction::makeMeleeAttack(controlled, originalSideTarget->getPosition(),
		controlled->getPosition(), false);
	ASSERT_TRUE(submitControlledAction(controlled, melee));
	EXPECT_LT(originalSideTarget->getAvailableHealth(), beforeAttack);
	EXPECT_EQ(controlled->unitSide(), BattleSide::DEFENDER);
	EXPECT_EQ(battle()->battleGetOwner(controlled), PlayerColor(1));
	EXPECT_EQ(battle()->battleGetActionController(controlled), PlayerColor(1));
	EXPECT_FALSE(newHorizonsPuppetMaster::hasControlMarker(controlled));
	EXPECT_TRUE(newHorizonsPuppetMaster::hasLucidity(controlled));
	const auto lucidity = spellEffects(controlled,
		SpellID(SpellID::decode(std::string(newHorizonsPuppetMaster::LUCIDITY_SPELL_ID))));
	ASSERT_EQ(lucidity->size(), 1u);
	EXPECT_EQ(lucidity->front()->type, BonusType::LUCIDITY);
	EXPECT_EQ(lucidity->front()->duration, BonusDuration::N_TURNS);
	EXPECT_EQ(lucidity->front()->turnsRemain, 2);
	EXPECT_TRUE(vstd::contains_if(server.stackActivations, [this](const BattleSetActiveStack & activation)
	{
		return activation.stack == controlled->unitId()
			&& activation.reason == BattleUnitTurnReason::MORALE;
	}));

	EXPECT_FALSE(heroSpellIsReceptive(puppetMasterSpell(), controlled));
	EXPECT_FALSE(heroSpellIsReceptive(SpellID(SpellID::BERSERK), controlled));
	const auto * slow = SpellID(SpellID::SLOW).toSpell();
	ASSERT_NE(slow, nullptr);
	spells::BattleCast slowCast(battle(), attackerSideHero, spells::Mode::HERO, slow);
	const auto slowMechanics = slow->battleMechanics(&slowCast);
	ASSERT_NE(slowMechanics, nullptr);
	spells::Target slowAim{battle::Destination(controlled)};
	spells::detail::ProblemImpl slowTargetProblem;
	const bool slowCanBeCastAt = slowMechanics->canBeCastAt(slowAim, slowTargetProblem);
	std::vector<std::string> slowTargetReasons;
	slowTargetProblem.getAll(slowTargetReasons);
	spells::detail::ProblemImpl slowAvailabilityProblem;
	const bool slowCanBeCast = slowMechanics->canBeCast(slowAvailabilityProblem);
	std::vector<std::string> slowAvailabilityReasons;
	slowAvailabilityProblem.getAll(slowAvailabilityReasons);
	EXPECT_TRUE(slowMechanics->isReceptive(controlled))
		<< "Lucidity should not make an ordinary debuff unreceptive; round=" << battle()->getRound()
		<< ", heroMana=" << attackerSideHero->getManaAvailable()
		<< ", canBeCast=" << slowCanBeCast << " " << ::testing::PrintToString(slowAvailabilityReasons)
		<< ", canBeCastAt=" << slowCanBeCastAt << " " << ::testing::PrintToString(slowTargetReasons);

	// Lucidity is a fixed two-round status, not a one-activation marker.
	const auto lucidityStartRound = battle()->getRound();
	endRound();
	const auto afterOneRoundNumber = battle()->getRound();
	EXPECT_EQ(afterOneRoundNumber, lucidityStartRound + 1)
		<< "endRound must advance exactly one round, from " << lucidityStartRound
		<< " to " << afterOneRoundNumber;
	const auto afterOneRound = spellEffects(controlled,
		SpellID(SpellID::decode(std::string(newHorizonsPuppetMaster::LUCIDITY_SPELL_ID))));
	ASSERT_EQ(afterOneRound->size(), 1u)
		<< "Lucidity should persist after one round; round before/after="
		<< lucidityStartRound << "/" << afterOneRoundNumber;
	EXPECT_EQ(afterOneRound->front()->turnsRemain, 1);
	const auto secondRoundStart = battle()->getRound();
	endRound();
	EXPECT_EQ(battle()->getRound(), secondRoundStart + 1);
	EXPECT_FALSE(newHorizonsPuppetMaster::hasLucidity(controlled))
		<< "Lucidity should expire after the second round; round before/after="
		<< secondRoundStart << "/" << battle()->getRound();
}

TEST_F(NewHorizonsPuppetMasterTest, SelectedShotUsesControllerAndSanctuaryStillBlocksBothSides)
{
	prepare(true);
	addSanctuary(protectedOriginalSideTarget);
	EXPECT_FALSE(battle()->battleCanAttackUnitAction(controllerAlly, protectedOriginalSideTarget));
	EXPECT_FALSE(battle()->battleCanAttackUnit(controllerAlly, protectedOriginalSideTarget));
	EXPECT_FALSE(battle()->battleCanShootAction(normalShooter,
		protectedOriginalSideTarget->getPosition()));
	EXPECT_FALSE(battle()->battleCanShoot(normalShooter, protectedOriginalSideTarget->getPosition()));

	ASSERT_TRUE(castPuppetMaster());
	ASSERT_TRUE(activate(controlled));
	EXPECT_EQ(battle()->battleGetActionController(controlled), PlayerColor(0));
	EXPECT_EQ(controlled->unitSide(), BattleSide::DEFENDER);
	EXPECT_FALSE(battle()->battleCanShootAction(controlled,
		protectedOriginalSideTarget->getPosition()));
	EXPECT_FALSE(battle()->battleCanAttackUnitAction(controlled, protectedOriginalSideTarget));
	EXPECT_TRUE(battle()->battleCanShootAction(controlled, originalSideTarget->getPosition()));
	EXPECT_FALSE(battle()->battleCanShoot(controlled, originalSideTarget->getPosition()));

	const auto healthBefore = originalSideTarget->getAvailableHealth();
	ASSERT_TRUE(submitControlledAction(controlled,
		BattleAction::makeShotAttack(controlled, originalSideTarget)));
	EXPECT_LT(originalSideTarget->getAvailableHealth(), healthBefore);
	EXPECT_FALSE(newHorizonsPuppetMaster::hasControlMarker(controlled));
	EXPECT_TRUE(newHorizonsPuppetMaster::hasLucidity(controlled));
	EXPECT_EQ(battle()->battleGetOwner(controlled), PlayerColor(1));
	EXPECT_EQ(controlled->unitSide(), BattleSide::DEFENDER);
	EXPECT_TRUE(vstd::contains_if(server.attacks, [this](const BattleAttack & attack)
	{
		return attack.stackAttacking == controlled->unitId() && attack.shot()
			&& vstd::contains_if(attack.bsa, [this](const BattleStackAttacked & hit)
			{
				return hit.stackAttacked == originalSideTarget->unitId();
			});
	}));
}

TEST_F(NewHorizonsPuppetMasterTest, SuccessfulControlRemovesOnlyTheBerserkSpellEffectBundle)
{
	prepare();
	ASSERT_TRUE(activate(controllerAlly));
	addBerserkSpellEffectBundle(controlled);
	addInnateNearestCreatureBonus(controlled);
	ASSERT_EQ(spellEffects(controlled, SpellID(SpellID::BERSERK))->size(), 2u);
	ASSERT_TRUE(controlled->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));

	ASSERT_TRUE(castPuppetMaster());
	EXPECT_TRUE(newHorizonsPuppetMaster::hasControlMarker(controlled));
	EXPECT_TRUE(spellEffects(controlled, SpellID(SpellID::BERSERK))->empty());

	const BonusSourceID innateSource(controlled->creatureId());
	const auto innateNearestCreature = controlled->getBonuses(
		Selector::source(BonusSource::CREATURE_ABILITY, innateSource)
			.And(Selector::type()(BonusType::ATTACKS_NEAREST_CREATURE)));
	ASSERT_NE(innateNearestCreature, nullptr);
	ASSERT_EQ(innateNearestCreature->size(), 1u);
	EXPECT_EQ(innateNearestCreature->front()->source, BonusSource::CREATURE_ABILITY);
	EXPECT_EQ(innateNearestCreature->front()->sid, innateSource);
	EXPECT_EQ(innateNearestCreature->front()->duration, BonusDuration::PERMANENT);
	EXPECT_TRUE(controlled->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
}

TEST_F(NewHorizonsPuppetMasterTest, ResistedControlLeavesTheExistingBerserkSpellEffectBundle)
{
	prepare();
	ASSERT_TRUE(activate(controllerAlly));
	addBerserkSpellEffectBundle(controlled);
	controlled->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 100, BonusSourceID()));
	ASSERT_EQ(spellEffects(controlled, SpellID(SpellID::BERSERK))->size(), 2u);

	const auto * spell = puppetMasterSpell().toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_NE(mechanics, nullptr);
	ASSERT_EQ(controlled->magicResistance(), 75)
		<< "The New Horizons resistance cap clips the fixture's 100% source to 75%";
	EXPECT_TRUE(mechanics->isReceptive(controlled))
		<< "Magic Resistance is rolled during accepted cast resolution, not target receptivity";
	spells::detail::ProblemImpl problem;
	spells::Target aim{battle::Destination(controlled->getPosition())};
	EXPECT_TRUE(mechanics->canBeCastAt(aim, problem));

	ASSERT_TRUE(castPuppetMaster()) << "The legal cast must reach its authoritative resistance roll";
	const auto casts = server.castsOf(puppetMasterSpell());
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.resistedCres.contains(controlled->unitId()))
		<< "The seeded 75% resistance roll must actually reject Puppet Master's effect";
	EXPECT_FALSE(newHorizonsPuppetMaster::hasControlMarker(controlled));
	EXPECT_EQ(spellEffects(controlled, SpellID(SpellID::BERSERK))->size(), 2u)
		<< "A resisted Puppet Master cast must not clean up the prior spell source";
}

TEST_F(NewHorizonsPuppetMasterTest, ControlledCreatureActiveSpellUsesControllerTargetingAndProvenance)
{
	prepare();
	controlled->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELLCASTER, BonusSource::CREATURE_ABILITY, 1, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::HASTE))));
	ASSERT_TRUE(castPuppetMaster());
	ASSERT_TRUE(activate(controlled));
	EXPECT_EQ(battle()->battleGetActionController(controlled), PlayerColor(0));
	EXPECT_EQ(controlled->unitSide(), BattleSide::DEFENDER);

	spells::Target target{battle::Destination(controllerAlly)};
	const auto manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(submitControlledAction(controlled,
		BattleAction::makeCreatureSpellcast(controlled, target, SpellID(SpellID::HASTE))));
	const auto effects = spellEffects(controllerAlly, SpellID(SpellID::HASTE));
	ASSERT_FALSE(effects->empty());
	EXPECT_TRUE(vstd::contains_if(*effects, [](const auto & effect)
	{
		return effect && effect->spellCasterOwner == PlayerColor(0);
	}));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore)
		<< "The real active creature spell uses no Hero spell-point budget";
	EXPECT_EQ(battle()->battleGetOwner(controlled), PlayerColor(1));
	EXPECT_EQ(battle()->battleGetActionController(controlled), PlayerColor(1));
	EXPECT_TRUE(newHorizonsPuppetMaster::hasLucidity(controlled));
	EXPECT_FALSE(newHorizonsPuppetMaster::hasControlMarker(controlled));
}

TEST_F(NewHorizonsPuppetMasterTest, ControlMarkerRoundTripsJsonAndWireAndRejectsOldWriter)
{
	auto marker = newHorizonsPuppetMaster::controlMarker(puppetMasterSpell(), PlayerColor(0));
	EXPECT_EQ(marker.type, BonusType::PUPPET_MASTER_CONTROL);
	EXPECT_EQ(marker.source, BonusSource::SPELL_EFFECT);
	EXPECT_EQ(marker.duration, BonusDuration::ONE_BATTLE);
	EXPECT_EQ(marker.sid.as<SpellID>(), puppetMasterSpell());
	EXPECT_EQ(marker.spellCasterOwner, PlayerColor(0));

	auto json = marker.toJsonNode();
	json.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	const auto fromJson = JsonUtils::parseBonus(json);
	ASSERT_NE(fromJson, nullptr);
	EXPECT_EQ(fromJson->type, marker.type);
	EXPECT_EQ(fromJson->source, marker.source);
	EXPECT_EQ(fromJson->duration, marker.duration);
	EXPECT_EQ(fromJson->sid.as<SpellID>(), puppetMasterSpell());
	EXPECT_EQ(fromJson->spellCasterOwner, PlayerColor(0));

	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	wire.oser & marker;
	Bonus restored;
	wire.iser & restored;
	EXPECT_EQ(restored.type, marker.type);
	EXPECT_EQ(restored.source, marker.source);
	EXPECT_EQ(restored.duration, marker.duration);
	EXPECT_EQ(restored.sid.as<SpellID>(), puppetMasterSpell());
	EXPECT_EQ(restored.spellCasterOwner, PlayerColor(0));

	CMemorySerializer oldWriter;
	oldWriter.oser.version = ESerializationVersion::BONUS_SPELL_CASTER_OWNER;
	EXPECT_THROW(oldWriter.oser & marker, std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty()) << "Refuse lossy old-protocol writes before emitting Bonus fields";
}
