/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PreparedPilotFixture.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include <tuple>
namespace
{
class PreparedPilotParityTest : public PreparedPilotFixture, public ::testing::WithParamInterface<bool> {};
int rankMorale(const CGHeroInstance & hero, SecondarySkill skill)
{
	int result = 0;
	for(const auto & bonus : *hero.getBonuses(Selector::source(BonusSource::SECONDARY_SKILL, BonusSourceID(skill))
		.And(Selector::type()(BonusType::MORALE)))) result += bonus->val;
	return result;
}
struct RestoreValue
{
	Bonus & bonus;
	int value;
	explicit RestoreValue(Bonus & bonus) : bonus(bonus), value(bonus.val) {}
	~RestoreValue() { bonus.val = value; }
};
}
TEST_P(PreparedPilotParityTest, ActualRankReplacementAndFamilyAttachmentKeepOneNativeProducer)
{
	authored = GetParam();
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	const SecondarySkill skill(SecondarySkill::decode("new-horizons:discipline"));
	attackerSideHero->setSecSkillLevel(skill, 1, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(rankMorale(*attackerSideHero, skill), 1);
	attackerSideHero->setSecSkillLevel(skill, 3, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(rankMorale(*attackerSideHero, skill), 3);
	attackerSideHero->recreateSecondarySkillsBonuses();
	EXPECT_EQ(rankMorale(*attackerSideHero, skill), 3);
	EXPECT_EQ(attackerSideHero->getPreparedCreatureMechanics() != nullptr, authored);
	const auto & exported = attackerSideHero->getExportedBonusList();
	EXPECT_EQ(std::count_if(exported.begin(), exported.end(), [](const auto & bonus)
	{
		return bonus->stacking == "new-horizons:ordinary-creature-specialty:core:fiona";
	}), 1);
	startBattle();
	beginCombat();
	const auto * unit = mainUnit(BattleSide::ATTACKER);
	ASSERT_NE(unit, nullptr);
	const auto speed = unit->getBonuses(Selector::source(BonusSource::HERO_SPECIAL, BonusSourceID(attackerSideHero->getHeroTypeID()))
		.And(Selector::type()(BonusType::STACKS_SPEED)));
	ASSERT_EQ(speed->size(), 1);
	EXPECT_EQ(speed->front()->val, 5);
}
TEST_P(PreparedPilotParityTest, AcceptedSanctuaryCastRetainsParentSourceAndAuthoritativeRemoval)
{
	authored = GetParam();
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	ASSERT_NO_FATAL_FAILURE(learnPerk("new-horizons:lightMagic", "new-horizons:lightMagic.sanctuaryKeeper"));
	const SpellID spell(SpellID::decode("new-horizons:sanctuary"));
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(spell);
	setTestSpellPointTotal(attackerSideHero, 10000);
	EXPECT_EQ(attackerSideHero->getPreparedPilotPerks()->addition(nativeRules::TimedSourceSlot::Sanctuary) != nullptr, authored);
	startBattle(); beginCombat();
	const auto * unit = mainUnit(BattleSide::ATTACKER);
	ASSERT_NE(unit, nullptr);
	ASSERT_TRUE(cast(spell, unit));
	const auto morale = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)).And(Selector::type()(BonusType::MORALE)));
	ASSERT_EQ(morale->size(), 1);
	EXPECT_EQ(morale->front()->val, 2);
	const auto * parent = dynamic_cast<const HasAnotherBonusLimiter *>(morale->front()->limiter.get());
	ASSERT_NE(parent, nullptr);
	EXPECT_EQ(parent->type, BonusType::SANCTIFIED);
	EXPECT_EQ(parent->sid, BonusSourceID(spell));
	std::vector<Bonus> statuses;
	for(const auto & bonus : *unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)).And(Selector::type()(BonusType::SANCTIFIED))))
		statuses.push_back(*bonus);
	ASSERT_FALSE(statuses.empty());
	SetStackEffect remove;
	remove.battleID = BattleID(0);
	remove.toRemove.emplace_back(unit->unitId(), statuses);
	gameHandler->sendAndApply(remove);
	EXPECT_TRUE(unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)).And(Selector::type()(BonusType::MORALE)))->empty());
}
TEST_P(PreparedPilotParityTest, AcceptedForgetfulnessUsesNativeMindbreakerCategoryAndDuration)
{
	authored = GetParam();
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	ASSERT_NO_FATAL_FAILURE(learnPerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.mindbreaker"));
	const SpellID spell(SpellID::FORGETFULNESS);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(spell);
	setTestSpellPointTotal(attackerSideHero, 10000);
	EXPECT_EQ(attackerSideHero->getPreparedPilotPerks()->addition(nativeRules::TimedSourceSlot::Forgetfulness) != nullptr, authored);
	startBattle(); beginCombat();
	const auto * target = mainUnit(BattleSide::DEFENDER);
	ASSERT_NE(target, nullptr);
	ASSERT_TRUE(cast(spell, target));
	const auto effects = target->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))
		.And(Selector::type()(BonusType::CREATURE_ABILITY_SUPPRESSION)));
	ASSERT_EQ(effects->size(), 1);
	EXPECT_EQ(effects->front()->val, 2);
	EXPECT_GT(effects->front()->turnsRemain, 0);
	EXPECT_EQ(effects->front()->statusIdentity, "core:forgetfulness");
	EXPECT_FALSE(target->canShoot());
}
INSTANTIATE_TEST_SUITE_P(ExclusiveSource, PreparedPilotParityTest, ::testing::Bool());

class PreparedPilotFamilyTest : public PreparedPilotFixture,
	public ::testing::WithParamInterface<std::tuple<bool, const char *, const char *, BonusType, int>> {};
TEST_P(PreparedPilotFamilyTest, NativeAuraOwnerUsesExactPreparedFamilyAndRecipientRules)
{
	authored = std::get<0>(GetParam());
	heroName = std::get<1>(GetParam());
	creatureName = std::get<2>(GetParam());
	const auto type = std::get<3>(GetParam());
	const auto value = std::get<4>(GetParam());
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	EXPECT_EQ(attackerSideHero->getPreparedCreatureMechanics() != nullptr, authored);
	startBattle();
	beginCombat();
	const auto * source = mainUnit(BattleSide::ATTACKER);
	ASSERT_NE(source, nullptr);
	const battle::Unit * recipient = source;
	if(type == BonusType::SPELL_DAMAGE_REDUCTION)
	{
		for(const auto hex : source->getPosition().getNeighbouringTiles())
			if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex))
			{
				recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), hex, 10);
				break;
			}
		ASSERT_NE(recipient, source);
	}
	const auto bonuses = recipient->getBonuses(Selector::source(BonusSource::HERO_SPECIAL, BonusSourceID(attackerSideHero->getHeroTypeID()))
		.And(Selector::type()(type)));
	ASSERT_EQ(bonuses->size(), 1);
	EXPECT_EQ(bonuses->front()->val, value);
}
INSTANTIATE_TEST_SUITE_P(ExclusiveSource, PreparedPilotFamilyTest, ::testing::Values(
	std::tuple{false, "core:clancy", "core:unicorn", BonusType::SPELL_RESISTANCE_AURA, 10},
	std::tuple{true, "core:clancy", "core:unicorn", BonusType::SPELL_RESISTANCE_AURA, 10},
	std::tuple{false, "core:pasis", "new-horizons:wisp", BonusType::SPELL_DAMAGE_REDUCTION, 15},
	std::tuple{true, "core:pasis", "new-horizons:wisp", BonusType::SPELL_DAMAGE_REDUCTION, 15}));

TEST_F(PreparedPilotFixture, SavedAndCampaignRowsRebuildWithoutInstalledRankDefaults)
{
	customValues = true;
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	const SecondarySkill skill(SecondarySkill::decode("new-horizons:discipline"));
	attackerSideHero->setSecSkillLevel(skill, 2, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(rankMorale(*attackerSideHero, skill), 11);
	ASSERT_NE(attackerSideHero->getPreparedCreatureMechanics(), nullptr);
	EXPECT_EQ(attackerSideHero->getPreparedCreatureMechanics()->bonus().val, 13);
	RestoreValue restore(*skill.toSkill()->at(3).effects.front());
	restore.bonus.val = 77;
	CGameState loaded;
	loaded.preInit(LIBRARY);
	loaded.loadFromMemory(gameState()->saveToMemory());
	auto * savedHero = loaded.getMap().getHero(attackerSideHero->getHeroTypeID());
	ASSERT_NE(savedHero, nullptr);
	EXPECT_EQ(savedHero->getPerkState().rules, attackerSideHero->getPerkState().rules);
	savedHero->setSecSkillLevel(skill, 3, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(rankMorale(*savedHero, skill), 13);
	ASSERT_NE(savedHero->getPreparedCreatureMechanics(), nullptr);
	EXPECT_EQ(savedHero->getPreparedCreatureMechanics()->bonus().val, 13);
	CampaignState campaign;
	const auto snapshot = campaign.crossoverSerialize(attackerSideHero);
	auto crossover = campaign.crossoverDeserialize(snapshot, &gameState()->getMap());
	ASSERT_NE(crossover, nullptr);
	crossover->setSecSkillLevel(skill, 3, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(rankMorale(*crossover, skill), 13);
	ASSERT_NE(crossover->getPreparedCreatureMechanics(), nullptr);
	EXPECT_EQ(crossover->getPreparedCreatureMechanics()->bonus().val, 13);
	GameRandomizer randomizer(*gameState());
	ASSERT_NO_THROW(crossover->initHero(randomizer));
	EXPECT_EQ(rankMorale(*crossover, skill), 13);
	EXPECT_NE(crossover->getSpecialtyDescriptionTranslated().find("13"), std::string::npos);
}
