/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsHeroRulesFixture.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/entities/hero/NewHorizonsForestAmbush.h"
#include "../../lib/entities/hero/NewHorizonsAerialSupremacy.h"
#include "../../lib/entities/hero/NewHorizonsLivingInferno.h"
#include "../../lib/entities/hero/NewHorizonsDreadCavalry.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../lib/serializer/CMemorySerializer.h"
using namespace newHorizonsHeroes;
namespace
{
class NewHorizonsCreatureSpecialtyDefaultsTest : public TinyMapGameTest
{
protected:
    void SetUp() override
    {
        TinyMapGameTest::SetUp();
        ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
    }
};
void expectFiveDefaults(const JsonNode & rules)
{
    ASSERT_TRUE(forestAmbushPolicy(rules).has_value());
    EXPECT_TRUE(*forestAmbushPolicy(rules));
    ASSERT_TRUE(aerialSupremacyPolicy(rules).has_value());
    EXPECT_TRUE(*aerialSupremacyPolicy(rules));
    ASSERT_TRUE(rules.Struct().contains("petrifyingBloodSpecialty"));
    EXPECT_EQ(rules["petrifyingBloodSpecialty"]["version"].Integer(), 1);
    ASSERT_TRUE(livingInfernoRules(rules).has_value());
    EXPECT_EQ(livingInfernoRules(rules)->damagePerCreature, 1);
    EXPECT_EQ(livingInfernoRules(rules)->durationRounds, 1);
    ASSERT_TRUE(dreadCavalryDamagePercent(rules).has_value());
    EXPECT_EQ(*dreadCavalryDamagePercent(rules), 50);
}
}
TEST_F(NewHorizonsCreatureSpecialtyDefaultsTest, ActualNewGameCapturesAllNineAndPreservesReinitAndSave)
{
    // No rules override: this must consume the generated curated module defaults.
    TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
    builder.size(36).playerActive(PlayerColor(0));
    const std::vector<std::string> heroes = {"ignatius", "fafner", "vidomina", "ajit", "ivor", "bron", "fiur", "edric", "tamika"};
    for(size_t i = 0; i < heroes.size(); ++i)
        builder.hero({5 + static_cast<int>(i) * 3, 5, 0}, HeroTypeID(HeroTypeID::decode("core:" + heroes[i])), PlayerColor(0)).heroExperience(0);
    startWithMap(std::move(builder));
    GameRandomizer randomizer(*gameState());
    randomizer.setSeed(19);
    for(size_t i = 0; i < heroes.size(); ++i)
    {
        SCOPED_TRACE(heroes[i]);
        auto * hero = findHeroAt({5 + static_cast<int>(i) * 3, 5, 0});
        ASSERT_NE(hero, nullptr);
        const auto captured = hero->getPrimaryGrowthRules();
        expectFiveDefaults(captured);
        EXPECT_EQ(captured["additionalSpecialties"]["manaLeeches"]["manaPerDamagingAttackSequence"].Integer(), 1);
        EXPECT_TRUE(captured["additionalSpecialties"]["serpentsEmbrace"]["enabled"].Bool());
        EXPECT_TRUE(captured["additionalSpecialties"]["paralyzingGaze"]["enabled"].Bool());
        EXPECT_EQ(captured["ghostSpecialty"]["version"].Integer(), 1);
        ASSERT_NO_THROW(hero->initHero(randomizer));
        EXPECT_EQ(hero->getPrimaryGrowthRules(), captured);
    }
    CGameState restored;
    restored.preInit(LIBRARY);
    restored.loadFromMemory(gameState()->saveToMemory());
    for(size_t i = 0; i < heroes.size(); ++i)
    {
        const auto * original = findHeroAt({5 + static_cast<int>(i) * 3, 5, 0});
        ASSERT_NE(restored.getHero(original->id), nullptr);
        EXPECT_EQ(restored.getHero(original->id)->getPrimaryGrowthRules(), original->getPrimaryGrowthRules());
    }
}
TEST(NewHorizonsCreatureSpecialtyDefaults, CapturedHistoricalAbsenceNeverReadsInstalledDefaults)
{
    // Deliberately construct only the five-feature-predecessor snapshot. This is
    // not a shared fixture rewrite and preserves every other captured field.
    auto configured = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
    expectFiveDefaults(resolveHeroRules(configured, HeroClassID(0)));
    configured["additionalSpecialties"].Struct().erase("forestAmbush");
    configured["additionalSpecialties"].Struct().erase("aerialSupremacy");
    for(const auto * key : {"petrifyingBloodSpecialty", "livingInfernoSpecialty", "dreadCavalrySpecialty"})
        configured.Struct().erase(key);
    const auto historical = resolveHeroRules(configured, HeroClassID(0));
    ASSERT_NO_THROW(validateResolvedHeroRules(historical));
    EXPECT_FALSE(forestAmbushPolicy(historical).has_value());
    EXPECT_FALSE(aerialSupremacyPolicy(historical).has_value());
    EXPECT_FALSE(livingInfernoRules(historical).has_value());
    EXPECT_FALSE(dreadCavalryDamagePercent(historical).has_value());
    EXPECT_FALSE(historical.Struct().contains("petrifyingBloodSpecialty"));
    CMemorySerializer codec;
    auto writable = historical;
    codec.oser & writable;
    JsonNode decoded;
    codec.iser & decoded;
    EXPECT_EQ(decoded, historical);
    EXPECT_NO_THROW(validateResolvedHeroRules(decoded));
}
