/*
 * NewHorizonsConsecratedCastingTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/CStack.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CBattleInfoEssentials.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/HeroActionAllowanceState.h"
#include "../../../lib/battle/HeroCommand.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include <vcmi/Environment.h>

#include <algorithm>
#include <memory>
#include <string>

namespace
{
constexpr auto divineMandateSkill = "new-horizons:divineMandate";
constexpr auto consecratedCastingPerk = "new-horizons:divineMandate.consecratedCasting";

bool activateConsecratedCasting(JsonNode & rules)
{
	auto & perks = rules["skills"][std::string(divineMandateSkill)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
	{
		return perk["id"].String() == consecratedCastingPerk;
	});
	if(found == perks.end())
		return false;
	(*found)["effect"]["status"].String() = "active";
	return true;
}

int appliedBlessDuration(const battle::Unit * unit)
{
	const auto bonuses = unit->getAllBonuses(Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLESS))));
	for(const auto & bonus : *bonuses)
		if(bonus->type == BonusType::ALWAYS_MAXIMUM_DAMAGE)
			return bonus->turnsRemain;
	return 0;
}

class ConsecratedCastingPredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit ConsecratedCastingPredictionEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class NewHorizonsConsecratedCastingTest : public HeroCommandFixture
{
protected:
	const CSpell * bless = nullptr;
	const CSpell * magicArrow = nullptr;
	CStack * initialTarget = nullptr;
	CStack * followupTarget = nullptr;
	CStack * enemy = nullptr;
	CStack * creatureCaster = nullptr;
	bool enableCreatureCaster = false;

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

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!activateConsecratedCasting(perkRules))
			throw std::runtime_error("Missing Consecrated Casting from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	SecondarySkill divineMandate() const
	{
		const int decoded = SecondarySkill::decode(divineMandateSkill);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void selectConsecratedCasting(CGHeroInstance * hero)
	{
		hero->setSecSkillLevel(divineMandate(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [](const auto & candidate)
			{
				return candidate.selection.skillId == divineMandateSkill
					&& candidate.selection.perkId == consecratedCastingPerk;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(divineMandateSkill, consecratedCastingPerk));
			return;
		}

		FAIL() << consecratedCastingPerk << " never appeared in a legal Basic perk offer";
	}

	void prepare(bool selectPerk, MasteryLevel::Type rank = MasteryLevel::BASIC,
		int32_t spellPower = 75)
	{
		startGame();
		ASSERT_EQ(attackerSideHero->getFactionID(), FactionID::CASTLE);
		if(selectPerk)
			selectConsecratedCasting(attackerSideHero);
		else
			attackerSideHero->setSecSkillLevel(divineMandate(), rank, ChangeValueMode::ABSOLUTE);

		const auto lightMagic = SecondarySkill::decode("new-horizons:lightMagic");
		const auto spellcraft = SecondarySkill::decode("new-horizons:spellcraft");
		ASSERT_GE(lightMagic, 0);
		ASSERT_GE(spellcraft, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(lightMagic), MasteryLevel::NONE,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(spellcraft), MasteryLevel::NONE,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);

		bless = SpellID(SpellID::BLESS).toSpell();
		magicArrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
		ASSERT_NE(bless, nullptr);
		ASSERT_NE(magicArrow, nullptr);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::BLESS));
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::MAGIC_ARROW));
		setTestSpellPointTotal(attackerSideHero, attackerSideHero->manaLimit());

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);

		initialTarget = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 100);
		followupTarget = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(6, 5), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 100);
		ASSERT_NE(initialTarget, nullptr);
		ASSERT_NE(followupTarget, nullptr);
		ASSERT_NE(enemy, nullptr);
		if(enableCreatureCaster)
		{
			creatureCaster = addStack(BattleSide::ATTACKER,
				creatureByName("core:imp"), BattleHex(9, 5), 1);
			ASSERT_NE(creatureCaster, nullptr);
			creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::SPELLCASTER, BonusSource::OTHER, MasteryLevel::EXPERT,
				BonusSourceID(), BonusSubtypeID(SpellID(SpellID::BLESS))));
			creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
			creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::CREATURE_SPELL_POWER, BonusSource::OTHER, 7500, BonusSourceID()));
			creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::CREATURE_ENCHANT_POWER, BonusSource::OTHER, 2, BonusSourceID()));
		}
		beginCombat();
		activate(initialTarget);
		ASSERT_EQ(battle()->battleActiveUnit(), initialTarget);
		ASSERT_EQ(battle()->battleGetOwner(initialTarget), PlayerColor(0));
	}

	void activate(const CStack * stack)
	{
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = stack->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
		activate(initialTarget);
	}

	bool issue(HeroCommand command)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeHeroCommand(BattleSide::ATTACKER, command));
	}

	bool cast(SpellID spell, const CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
};

TEST_F(NewHorizonsConsecratedCastingTest, OnlyTheDivineMandateLightSpellFollowupScalesAndForecastsBless)
{
	prepare(true, MasteryLevel::ADVANCED);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(divineMandateSkill, consecratedCastingPerk));
	ASSERT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 75);

	spells::BattleCast initialCast(battle(), attackerSideHero, spells::Mode::HERO, bless);
	auto initialMechanics = bless->battleMechanics(&initialCast);
	ASSERT_NE(initialMechanics, nullptr);
	EXPECT_EQ(initialMechanics->getEffectPower(), 75);
	EXPECT_EQ(initialMechanics->getCastSpellPowerComponentBonusPercent(), 0)
		<< "The ordinary first Hero Action is not a Divine Mandate follow-up";
	EXPECT_EQ(initialMechanics->getSpellPowerCoefficientBasisPoints(), 10'000);
	EXPECT_EQ(initialMechanics->getEffectDuration(), 2);
	ASSERT_TRUE(cast(SpellID(SpellID::BLESS), initialTarget));
	EXPECT_EQ(appliedBlessDuration(initialTarget), 2);
	EXPECT_EQ(initialMechanics->getCastSpellPowerComponentBonusPercent(), 0)
		<< "The already-created Mechanics snapshot is stable after its cast opens a follow-up";

	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 1);
	advanceRound();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto status = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	ASSERT_TRUE(status.pendingFollowup);
	EXPECT_EQ(status.pendingFollowup->source, HeroActionAllowanceState::GrantSource::DIVINE_MANDATE);
	EXPECT_EQ(status.pendingFollowup->allowance, HeroActionAllowanceState::AllowanceKind::SPELL);

	spells::BattleCast followupCast(battle(), attackerSideHero, spells::Mode::HERO, bless);
	const auto followupMechanics = bless->battleMechanics(&followupCast);
	ASSERT_NE(followupMechanics, nullptr);
	EXPECT_EQ(followupMechanics->getCastSpellPowerComponentBonusPercent(), 10);
	EXPECT_EQ(followupMechanics->getSpellPowerCoefficientBasisPoints(), 11'000);
	EXPECT_EQ(followupMechanics->getEffectDuration(), 3)
		<< "Bless keeps its fixed two-round base; floor(75 * 11000 / (80 * 10000)) adds one";

	const int32_t normalBeforeForecast = attackerSideHero->getNormalSpellPoints();
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	ConsecratedCastingPredictionEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);
	spells::BattleCast projectedCast(&projected, attackerSideHero, spells::Mode::HERO, bless);
	const auto projectedMechanics = bless->battleMechanics(&projectedCast);
	ASSERT_NE(projectedMechanics, nullptr);
	EXPECT_EQ(projectedMechanics->getCastSpellPowerComponentBonusPercent(), 10);
	EXPECT_EQ(projectedMechanics->getSpellPowerCoefficientBasisPoints(), 11'000);
	EXPECT_EQ(projectedMechanics->getEffectDuration(), followupMechanics->getEffectDuration());
	auto * projectedTarget = projected.battleGetUnitByID(followupTarget->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	const spells::Target projectedAim{spells::Destination(projectedTarget)};
	ASSERT_TRUE(projectedMechanics->canBeCastAt(projectedAim));
	projectedMechanics->castEval(projected.getServerCallback(), projectedAim);
	projectedTarget = projected.battleGetUnitByID(followupTarget->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	EXPECT_EQ(appliedBlessDuration(projectedTarget), 3);
	EXPECT_EQ(appliedBlessDuration(followupTarget), 0);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), normalBeforeForecast)
		<< "Detached prediction does not spend live Spell Points";
	EXPECT_TRUE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).pendingFollowup)
		<< "Detached prediction does not consume the live typed allowance";

	ASSERT_TRUE(cast(SpellID(SpellID::BLESS), followupTarget));
	EXPECT_EQ(appliedBlessDuration(followupTarget), 3);
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 2);
	EXPECT_FALSE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).pendingFollowup);
	EXPECT_EQ(followupMechanics->getCastSpellPowerComponentBonusPercent(), 10)
		<< "The cast Mechanics object retains the captured allowance snapshot after resolution";
	EXPECT_TRUE(attackerSideHero->hasActivePerk(divineMandateSkill, consecratedCastingPerk))
		<< "The saved selected perk remains after the temporary action allowance is consumed";
}

TEST_F(NewHorizonsConsecratedCastingTest, AcceptedFollowupWithoutThePerkKeepsTheOrdinaryDuration)
{
	prepare(false);
	ASSERT_FALSE(attackerSideHero->hasActivePerk(divineMandateSkill, consecratedCastingPerk));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_TRUE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).pendingFollowup);

	spells::BattleCast followupCast(battle(), attackerSideHero, spells::Mode::HERO, bless);
	const auto mechanics = bless->battleMechanics(&followupCast);
	ASSERT_NE(mechanics, nullptr);
	EXPECT_EQ(mechanics->getCastSpellPowerComponentBonusPercent(), 0);
	EXPECT_EQ(mechanics->getSpellPowerCoefficientBasisPoints(), 10'000);
	EXPECT_EQ(mechanics->getEffectDuration(), 2);
	ASSERT_TRUE(cast(SpellID(SpellID::BLESS), followupTarget));
	EXPECT_EQ(appliedBlessDuration(followupTarget), 2);
}

TEST_F(NewHorizonsConsecratedCastingTest, NonLightMetamagicAndCreatureModesDoNotUseThePerk)
{
	enableCreatureCaster = true;
	prepare(true);
	ASSERT_NE(creatureCaster, nullptr);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto status = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	ASSERT_TRUE(status.pendingFollowup);
	EXPECT_EQ(status.pendingFollowup->source, HeroActionAllowanceState::GrantSource::DIVINE_MANDATE);

	spells::BattleCast nonLightCast(battle(), attackerSideHero, spells::Mode::HERO, magicArrow);
	const auto nonLightMechanics = magicArrow->battleMechanics(&nonLightCast);
	ASSERT_NE(nonLightMechanics, nullptr);
	EXPECT_EQ(nonLightMechanics->getCastSpellPowerComponentBonusPercent(), 0);
	EXPECT_FALSE(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, SpellID(SpellID::MAGIC_ARROW)));
	EXPECT_FALSE(cast(SpellID(SpellID::MAGIC_ARROW), enemy));
	EXPECT_TRUE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).pendingFollowup)
		<< "A rejected non-Light cast leaves the real Light continuation available";

	spells::BattleCast metamagicCast(battle(), attackerSideHero, spells::Mode::HERO, bless);
	metamagicCast.setMetamagicFollowup(true);
	const auto metamagicMechanics = bless->battleMechanics(&metamagicCast);
	ASSERT_NE(metamagicMechanics, nullptr);
	EXPECT_EQ(metamagicMechanics->getCastSpellPowerComponentBonusPercent(), 0)
		<< "A Metamagic payload does not borrow a simultaneously pending Divine Mandate spell grant";

	spells::BattleCast creatureCast(battle(), creatureCaster, spells::Mode::CREATURE_ACTIVE, bless);
	const auto creatureMechanics = bless->battleMechanics(&creatureCast);
	ASSERT_NE(creatureMechanics, nullptr);
	EXPECT_EQ(creatureMechanics->getCastSpellPowerComponentBonusPercent(), 0);
	EXPECT_EQ(creatureMechanics->getEffectDuration(), 2);
	const spells::Target creatureAim{spells::Destination(followupTarget)};
	ASSERT_TRUE(creatureMechanics->canBeCastAt(creatureAim));
	creatureMechanics->cast(gameHandler->spellEnv.get(), creatureAim);
	EXPECT_EQ(appliedBlessDuration(followupTarget), 2);
	EXPECT_TRUE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).pendingFollowup)
		<< "A creature cast neither borrows nor consumes the hero's pending allowance";
}
