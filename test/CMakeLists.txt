include(GoogleTest)
include(CheckCXXCompilerFlag)
find_package(Threads REQUIRED)

option(ENABLE_NEW_HORIZONS_MANAGED_TESTS
	"Compile opt-in tests requiring an isolated registered70 profile and NH_REQUIRE_MANAGED_MISSILE_PROFILE=1" OFF)

# Pure saved-view navigation state; no renderer or gameplay fixture implied.
add_executable(nhHeroDevelopmentNavigationTest ../client/tests/HeroDevelopmentNavigationTest.cpp)
add_test(NAME nhHeroDevelopmentNavigationTest COMMAND nhHeroDevelopmentNavigationTest)
add_executable(nhHeroMasteryDialogStateTest ../client/tests/HeroMasteryDialogStateTest.cpp)
add_test(NAME nhHeroMasteryDialogStateTest COMMAND nhHeroMasteryDialogStateTest)
add_executable(nhCureAfflictionSessionTest ../client/tests/CureAfflictionSessionTest.cpp)
add_test(NAME nhCureAfflictionSessionTest COMMAND nhCureAfflictionSessionTest)
if(ENABLE_NH_CURE_CLIENT_INTEGRATION)
	add_executable(nhCureIntegrationBootstrapStateTest ../client/tests/CureIntegrationBootstrapStateTest.cpp)
	add_test(NAME nhCureIntegrationBootstrapStateTest COMMAND nhCureIntegrationBootstrapStateTest)
	set_tests_properties(nhCureIntegrationBootstrapStateTest PROPERTIES LABELS "nh-integration;state" TIMEOUT 10)
	add_executable(nhCureFixtureStateTest ../client/tests/CureFixtureStateTest.cpp)
	add_test(NAME nhCureFixtureStateTest COMMAND nhCureFixtureStateTest)
	set_tests_properties(nhCureFixtureStateTest PROPERTIES LABELS "nh-integration;state" TIMEOUT 10)
endif()
add_executable(nhExchangeSecondarySkillSlotsTest ../client/tests/ExchangeSecondarySkillSlotsTest.cpp)
add_test(NAME nhExchangeSecondarySkillSlotsTest COMMAND nhExchangeSecondarySkillSlotsTest)

# Pure UI-state test for the stack hover-to-panel travel grace period.
add_executable(nhStackInfoPanelHoverStateTest ../client/tests/StackInfoPanelHoverStateTest.cpp)
add_test(NAME nhStackInfoPanelHoverStateTest COMMAND nhStackInfoPanelHoverStateTest)
add_executable(nhFrozenApplicationFeedbackTest ../client/tests/FrozenApplicationFeedbackTest.cpp)
add_test(NAME nhFrozenApplicationFeedbackTest COMMAND nhFrozenApplicationFeedbackTest)

# Reproduce the Academy built-town-icon load path with every scaling factor. The
# SDL dummy backend keeps the renderer bootstrap isolated from the host display.
if(TARGET vcmisdl2 AND UNIX AND NOT APPLE AND NOT ENABLE_SDL3)
	add_executable(nhAcademyBuiltIconRuntimeTest
		../client/tests/AcademyBuiltIconRuntimeTest.cpp
		../client/tests/MagiPaletteInspection.h
	)
	target_link_libraries(nhAcademyBuiltIconRuntimeTest PRIVATE vcmisdl2)
	vcmi_set_output_dir(nhAcademyBuiltIconRuntimeTest "")
	add_test(NAME nhAcademyBuiltIconRuntimeTest COMMAND nhAcademyBuiltIconRuntimeTest)
	set_tests_properties(nhAcademyBuiltIconRuntimeTest PROPERTIES
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
		TIMEOUT 60
		SKIP_RETURN_CODE 77
		ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software"
	)
endif()

set(VCMI_GOOGLETEST_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/googletest" CACHE PATH
	"GoogleTest source directory (bundled submodule or installed local sources)")
set(googleTest_Dir "${VCMI_GOOGLETEST_SOURCE_DIR}")
if(EXISTS "${googleTest_Dir}/CMakeLists.txt"
	AND EXISTS "${googleTest_Dir}/googletest/include/gtest/gtest.h"
	AND EXISTS "${googleTest_Dir}/googlemock/include/gmock/gmock.h")
	set(GTestSrc "${googleTest_Dir}/googletest")
	set(GMockSrc "${googleTest_Dir}/googlemock")
else()
	message(FATAL_ERROR "No complete GoogleTest sources at ${googleTest_Dir}; set VCMI_GOOGLETEST_SOURCE_DIR to a local source directory.")
endif()

set(test_SRCS
		# Pure queue/replay storage only; native Engine session/factories stay in client.
		../client/netlag/NetworkLagReplyState.cpp
		../client/netlag/NetworkLagRollbackCopy.cpp
		../client/netlag/PreparedNetworkLagRetirement.cpp
		../client/replay/ClientReceiveAccess.cpp
		../client/replay/PreparedClientBattleReplay.cpp
		../client/replay/PreparedClientWorldReplay.cpp
		../client/replay/PreparedConsumedBattleReply.cpp
		rmg/NewHorizonsRandomScrollTest.cpp
 		StdInc.cpp
 		main.cpp
 		CMemoryBufferTest.cpp
		serializer/JsonUpdaterTest.cpp
		serializer/SaveParityTest.cpp
		../tools/native/SaveParityInput.cpp
		../tools/native/SaveParityEvidence.cpp
		../tools/native/SaveParityCoverage.cpp
		serializer/BattleUnitStateCompatibilityTest.cpp
		serializer/BattlePacketReceiptCompatibilityTest.cpp
		serializer/QuestLegacyBorderSerializationTest.cpp
 		CVcmiTestConfig.cpp
 		JsonComparer.cpp
		json/GeneratedImageRecipesTest.cpp
		json/GrayscalePngPaletteTest.cpp
		json/NewHorizonsSaveDirectoryTest.cpp
		server/battles/NewHorizonsGuildSpellHelpTest.cpp

		battle/AutocombatPreferencesTest.cpp
		battle/AlternatingHeroActionStateTest.cpp
		battle/SpellResponseStateTest.cpp
		battle/NewHorizonsCounterBatteryAITest.cpp
		battle/BattleDeploymentOrderTest.cpp
		battle/CasualtyProvenanceTest.cpp
		battle/HeroActionAllowanceStateTest.cpp
		battle/HeroSpellAllowanceTransitionTest.cpp
		battle/OrderTransitionCharacterizationTest.cpp
		battle/BattleHexTest.cpp
		battle/BattleResultReplyTest.cpp
		battle/BattleFieldReachabilityTest.cpp
		battle/ReachabilityLifetimeTest.cpp
		battle/BattleFormAccessibilityTest.cpp
		battle/BattleEffectExchangeTest.cpp
		battle/NewHorizonsPassThroughRangeTest.cpp
		battle/NewHorizonsBattleFormHealthTest.cpp
 		battle/CBattleInfoCallbackTest.cpp
 		battle/CHealthTest.cpp
		battle/CUnitStateTest.cpp
		battle/CUnitStateLifecycleTest.cpp
		battle/UnitStateValueExchangeTest.cpp
		battle/PreparedStackStateTransferTest.cpp
		battle/PreparedBattleUnitRootsTest.cpp
		battle/PreparedBattleWorldCommitTest.cpp
		battle/BattleRootLifetimeTest.cpp
		battle/BattleRootCommitAdmissionTest.cpp
		battle/PreparedReplayAppendTest.cpp
		serializer/ReplayPayloadVersionTest.cpp
		serializer/ReplaySnapshotVersionTest.cpp
		client/ReplayPlannerPayloadTest.cpp
		../client/replay/ReplayPlanner.cpp
		battle/CUnitStateMagicTest.cpp
		battle/PhysicalAfflictionTest.cpp
		server/battles/PhysicalAfflictionStampingTest.cpp
		battle/NewHorizonsDebuffStatusesTest.cpp
		battle/NewHorizonsFrozenTest.cpp
		battle/FocusFireStateTest.cpp
		battle/RelentlessAssaultStateTest.cpp
		battle/HeroCommandFocusFireRulesTest.cpp
		battle/battle_UnitTest.cpp

		bonus/BonusSystemTest.cpp
		bonus/BonusEvaluationContextTest.cpp
		bonus/ProjectedBonusLimitationTest.cpp
		bonus/TerrainAuthorityTest.cpp
		bonus/TerrainLimiterSerializationTest.cpp
		bonus/ProjectedUnitMagicResistanceTest.cpp
		bonus/ProjectedUnitEnvironmentTest.cpp
	bonus/ProjectedBonusCleanupTest.cpp
	battle/ProjectedBattleAccessibilityTest.cpp
	bonus/ProjectedBonusRelationsTest.cpp
	bonus/ProjectedUnitInitializationTest.cpp
	bonus/ColdBonusContextTest.cpp
	bonus/ProjectedSummonHealthQueryTest.cpp
	bonus/SummonQueryTerrainTest.cpp
	battle/ProjectedUnitRemovalTest.cpp
	battle/ProjectedBattleWorldArenaTest.cpp
	battle/PlacementUpdateCacheTest.cpp
	battle/UnitUpdateAdmissionTest.cpp
	battle/ProjectedUnitTransitionTest.cpp
	battle/ProjectedUnitPacketSidecarsTest.cpp
	battle/PreparedBattleUnitSidecarsTest.cpp
	battle/ProjectedRemoveSnapshotTest.cpp
	battle/ProjectedUnitPacketWireTest.cpp
		bonus/ProjectedUnitEnvironmentHeroInputsTest.cpp
		bonus/ProjectedUnitEnvironmentFixture.h
		bonus/ProjectedBonusGraphTest.cpp
		bonus/ProjectedDurationHistoryTest.cpp
		bonus/ProjectedFormTransitionTest.cpp
		bonus/PreparedBonusGraphFinalizationTest.cpp
		bonus/PreparedBonusGraphStorageTest.cpp
		bonus/NativeBonusGraphAccessTest.cpp
		battle/DamageCalculationTimingTest.cpp
		bonus/NativeBonusGraphAdmissionTest.cpp
		bonus/BonusStatusTagsTest.cpp
		bonus/NewHorizonsUnstackedBonusTest.cpp
		bonus/NewHorizonsCreatureProbabilityTest.cpp

		combatScripts/CombatScriptDescriptionTest.cpp
		spells/NewHorizonsMagicDescriptionTest.cpp

		entity/CArtifactTest.cpp
		entity/CCreatureTest.cpp
		entity/CFactionTest.cpp
		entity/CHeroClassTest.cpp
		entity/CHeroTest.cpp
		entity/CSkillTest.cpp

		events/ApplyDamageTest.cpp
		events/EventBusTest.cpp

		filesystem/FilesystemTest.cpp
		filesystem/CNhArtLoaderTest.cpp
		filesystem/SavegamePathTest.cpp

		game/BattleSpellCastTest.cpp
		game/GameSettingsTest.cpp
		game/NewHorizonsCombatScalarCaptureTest.cpp
		game/HeroRecruitmentTest.cpp
		game/HeroSecondarySkillsTest.cpp
		creatures/NewHorizonsCreatureCategoryRulesTest.cpp
		creatures/NewHorizonsCreatureCategorySchemaTest.cpp
		hero/NewHorizonsPrimaryProfileTest.cpp
		hero/NewHorizonsStartingEquipmentProjectionTest.cpp
		hero/NewHorizonsPrimaryGrowthTest.cpp
		hero/NewHorizonsPrimaryScaleTest.cpp
		hero/SpellPointStateTest.cpp
		hero/SpellPointCapacityTest.cpp
		hero/SpellPointRewardTest.cpp
		hero/SpellPointPresentationTest.cpp
		client/NewHorizonsCrusadeStatusTest.cpp
		client/NewHorizonsConfusionStatusTest.cpp
		client/NewHorizonsEntangleStatusTest.cpp
		client/NewHorizonsBattleFormStatusTest.cpp
		client/NewHorizonsRealityWarpPreviewTest.cpp
		client/NewHorizonsFlankReadbackTest.cpp
		client/NewHorizonsHeroMoraleLuckReadbackTest.cpp
		hero/NewHorizonsLeadershipTest.cpp
		hero/NewHorizonsMovementRulesTest.cpp
		hero/NewHorizonsCapabilityRulesTest.cpp
		hero/NewHorizonsMasteryRulesTest.cpp
		hero/NewHorizonsMasteryStateTest.cpp
		hero/NewHorizonsPerkRulesTest.cpp
		hero/NewHorizonsPerkStateTest.cpp
		hero/NewHorizonsPerkAvailabilityTest.cpp
		hero/HeroGrowthPerkPresentationTest.cpp
		hero/NewHorizonsPerkOfferWireTest.cpp
		hero/NewHorizonsLogisticsMasteryStateTest.cpp
		hero/NewHorizonsLogisticsLevelWireTest.cpp
		hero/NewHorizonsHeroRulesTest.cpp
		hero/NewHorizonsIndividualSpecialtiesTest.cpp
	hero/NewHorizonsSchoolSpellcraftRulesTest.cpp
	hero/NewHorizonsAdditionalSpecialtiesTest.cpp
	battle/NewHorizonsManaFurnacePersistenceTest.cpp
	battle/NewHorizonsDeepTrancePersistenceTest.cpp
	hero/NewHorizonsManaFurnaceRulesTest.cpp
	hero/NewHorizonsLearningSpecialtyRulesTest.cpp
	hero/NewHorizonsRecoverySpecialtyRulesTest.cpp
	hero/NewHorizonsHerbalSurgeonRulesTest.cpp
		hero/NewHorizonsSkillSpecialtyQueryTest.cpp
		hero/NewHorizonsHalonInitializationTest.cpp
		hero/NewHorizonsHeroBiographyTest.cpp
		hero/NewHorizonsHeroRulesFixture.h
		game/MovementCostTest.cpp
		game/NewHorizonsLogisticsMovementPerksTest.cpp
		game/NewHorizonsProtectedMobilityTest.cpp

		json/JsonTests.cpp
		modding/ContentManifestTest.cpp

		map/CMapEditManagerTest.cpp
		map/CMapFormatTest.cpp
		map/CGArtifactRegressionTest.cpp
		map/MapComparer.cpp
		map/MapScriptTest.cpp
		map/TinyH3MBuilderTest.cpp
		map/NewHorizonsStartingDevelopmentTest.cpp
		map/NewHorizonsNavigationSuccessorTest.cpp
		server/NewHorizonsStartingBookReplacementTest.cpp
		server/NewHorizonsStartingEquipmentTest.cpp
		server/battles/NewHorizonsRemainingSpellSpecialtyTest.cpp
		NewHorizonsHistoricalAdventurePolicyTestUtils.h
		game/NewHorizonsLighthouseDepartureFixture.h
		game/NewHorizonsLighthouseDepartureTest.cpp
		server/queries/NewHorizonsWaterWalkDayEndTest.cpp

		quest/QuestTest.cpp
		quest/QuestAssertions.cpp
		quest/QuestScenarios.cpp
		quest/CQuestLoaderTest.cpp
		quest/CQuestSeerTest.cpp
		quest/CQuestSeerMultiTest.cpp
		quest/CQuestVisibilityTest.cpp
		quest/CQuestMarkerTest.cpp
		quest/CQuestGuardTest.cpp
		quest/CQuestBorderTest.cpp
		quest/CQuestGateTest.cpp

		netpacks/NetPackFixture.cpp

		spells/AbilityCasterTest.cpp
		spells/AdventureSpellEffectTest.cpp
		spells/CSpellTest.cpp
		spells/CSpellHandlerEffectFormatTest.cpp
		modding/ScopedIdentifierTest.cpp
		spells/NewHorizonsDirectDamageTest.cpp
		spells/NewHorizonsAdventureMagicTest.cpp
		spells/NewHorizonsMagicTest.cpp
		spells/NewHorizonsVinesProjectionReceiptTest.cpp
		spells/OrientedSpellPatternTest.cpp
		spells/MagicalDamageReductionTest.cpp
		spells/NewHorizonsMagicArrowTest.cpp
		spells/NewHorizonsMagicSchemaTest.cpp
		spells/NewHorizonsMagicV2RulesTest.cpp
		spells/WarcastingSpellComponentTest.cpp
		spells/NewHorizonsMagicV2SchemaTest.cpp
		spells/NewHorizonsSpellAvailabilityTest.cpp
		spells/NewHorizonsModCompatibilityTest.cpp
		spells/NewHorizonsSorceryTest.cpp
		spells/NewHorizonsSpellLockIdentityTest.cpp
		spells/NewHorizonsRegenerationTest.cpp
		spells/NewHorizonsRealityWarpPlannerTest.cpp
		spells/NewHorizonsRealityWarpCollectionTest.cpp
 		spells/TargetConditionTest.cpp
		spells/TargetConditionTransferTest.cpp

		spells/effects/EffectFixture.cpp
		spells/effects/BattleFormTest.cpp
 		spells/effects/CatapultTest.cpp
 		spells/effects/CloneTest.cpp
 		spells/effects/DamageTest.cpp
 		spells/effects/DispelTest.cpp
 		spells/effects/HealTest.cpp
 		spells/effects/DemonSummonTest.cpp
		spells/effects/MoatTest.cpp
		spells/effects/ObstacleTest.cpp
		spells/effects/RemoveObstacleTest.cpp
		spells/effects/SacrificeTest.cpp
 		spells/effects/SummonTest.cpp
 		spells/effects/TeleportTest.cpp
 		spells/effects/TimedTest.cpp

 		spells/targetConditions/AbsoluteSpellConditionTest.cpp
 		spells/targetConditions/AbsoluteLevelConditionTest.cpp
 		spells/targetConditions/BonusConditionTest.cpp
 		spells/targetConditions/CreatureConditionTest.cpp
 		spells/targetConditions/ElementalConditionTest.cpp
 		spells/targetConditions/HealthValueConditionTest.cpp
 		spells/targetConditions/ImmunityNegationConditionTest.cpp
 		spells/targetConditions/NormalLevelConditionTest.cpp
 		spells/targetConditions/NormalSpellConditionTest.cpp
 		spells/targetConditions/ReceptiveFeatureConditionTest.cpp
		spells/targetConditions/ResistanceConditionTest.cpp
 		spells/targetConditions/SpellEffectConditionTest.cpp
 		spells/targetConditions/TargetConditionItemFixture.cpp

		server/battles/AcidBreathTest.cpp
		server/battles/BattleTestFixture.cpp
		server/battles/PacketUpdateAtomicityTest.cpp
		network/PacketUpdatePublicationTest.cpp
		battle/PreparedReplayBatchTest.cpp
		bonus/ReadOnlyBonusGraphParticipantTest.cpp
		client/PreparedClientBattleReplayTest.cpp
		client/PreparedClientReplaySnapshotTest.cpp
		client/PreparedClientWorldReplayTest.cpp
		client/PreparedConsumedBattleReplyTest.cpp
		client/PreparedNetworkLagRetirementTest.cpp
		network/BattleUnitsChangedInputTest.cpp
		network/PreparedRecipientPublicationTest.cpp
		network/PreparedWriteFailureTest.cpp
		server/battles/CombatEventTriggerTest.cpp
		server/battles/DamageCalculatorTest.cpp
		server/battles/OptionalRegenerationIdentityTest.cpp
		server/battles/OptionalHydraIdentityTest.cpp
		server/battles/NewHorizonsMoraleActivationTest.cpp
		server/battles/NewHorizonsMoraleActivationProtocolTest.cpp
		server/battles/NewHorizonsMoraleCreatureSpellTest.cpp
		server/battles/NewHorizonsSecondWindCreatureSpellTest.cpp
		server/battles/NewHorizonsRetainedTownMechanicsTest.cpp
		server/battles/NewHorizonsHoldAnchorUITest.cpp
		server/battles/DeathStareTest.cpp
		server/battles/DestructionTest.cpp
		server/battles/EnchantedTest.cpp
		server/battles/FireShieldTest.cpp
		server/battles/FocusMagicSpellTest.cpp
		server/battles/NewHorizonsHexOfPainTest.cpp
		server/battles/NewHorizonsShadowGiftTest.cpp
		server/battles/NewHorizonsPlagueTest.cpp
		server/battles/NewHorizonsPlagueControlSpellLockTest.cpp
		server/battles/NewHorizonsSpellLockControlTest.cpp
		server/battles/NewHorizonsPuppetMasterTest.cpp
		server/battles/NewHorizonsForcedMarchTest.cpp
		server/battles/NewHorizonsForgetfulnessTest.cpp
		server/battles/NewHorizonsSoulChainTest.cpp
		server/battles/NewHorizonsScrollCastingTest.cpp
		server/battles/NewHorizonsNaturesWrathTest.cpp
		server/battles/NewHorizonsPandemoniumTest.cpp
		server/battles/NewHorizonsPolymorphLifecycleTest.cpp
		server/battles/NewHorizonsRealityWarpTest.cpp
		server/battles/NewHorizonsPursuitMarchTest.cpp
		server/battles/NewHorizonsElementalRebirthTest.cpp
		server/battles/NewHorizonsElementalConvergenceTest.cpp
		server/battles/NewHorizonsRebirthTerrainTest.cpp
		server/battles/NewHorizonsRebirthChainTest.cpp
		server/battles/NewHorizonsChaplainReserveTest.cpp
		server/battles/NewHorizonsConsecratedCastingTest.cpp
		server/battles/NewHorizonsSacredCommandTest.cpp
		server/battles/NewHorizonsSacredCommandPacketsTest.cpp
		server/battles/NewHorizonsKnightlySequenceTest.cpp
		server/battles/NewHorizonsKnightlySequencePacketsTest.cpp
		server/battles/NewHorizonsMandateOfHeavenTest.cpp
		server/battles/NewHorizonsMandateOfHeavenPacketsTest.cpp
		server/battles/NewHorizonsFrailtyTest.cpp
		server/battles/LifeDrainTest.cpp
		server/battles/SoulStealTest.cpp
		server/battles/SummonGuardiansTest.cpp
		server/battles/TransmutationTest.cpp
		server/battles/WarMachineControlTest.cpp
		server/battles/HeroCommandTest.cpp
		server/battles/NewHorizonsProtectLinkTest.cpp
		server/battles/NewHorizonsMultipleOrderPacketsTest.cpp
		server/battles/NewHorizonsMultipleOrdersTest.cpp
		server/battles/NewHorizonsDoubleCommandTest.cpp
		server/battles/NewHorizonsBattlePlanTest.cpp
		server/battles/NewHorizonsOffenseRankTest.cpp
		server/battles/NewHorizonsBreakthroughDefendReductionTest.cpp
		server/battles/NewHorizonsVengeanceTest.cpp
		server/battles/NewHorizonsCleaveTest.cpp
		server/battles/NewHorizonsNoQuarterTest.cpp
		server/battles/NewHorizonsDefiantTest.cpp
		server/battles/NewHorizonsRelentlessAssaultTest.cpp
		server/battles/NewHorizonsPursuitTest.cpp
		server/battles/NewHorizonsDisciplineTest.cpp
		server/battles/NewHorizonsEspritDeCorpsTest.cpp
		server/battles/HeroCommandPersistenceTest.cpp
		server/battles/HeroCommandFixtureExportTest.cpp
		server/battles/NewHorizonsDemonicGatingFixtureExportTest.cpp
		server/battles/HeroCommandRejectionTest.cpp
		server/battles/HeroCommandRejectionAtomicityTest.cpp
		server/battles/HeroCommandEligibilityTest.cpp
		server/battles/HeroCommandCloneTest.cpp
		server/battles/FocusFireLifecycleTest.cpp
		server/battles/FocusFirePersistenceTest.cpp
		server/battles/FocusFireCompatibilityTest.cpp
		server/battles/FocusFireServerTest.cpp
		server/battles/HeroCommandTurnLifecycleTest.cpp
		server/battles/TeleporterPerkLifecycleTest.cpp
		server/battles/SelectiveDispelPerkTest.cpp
		server/battles/TemporalFieldPerkTest.cpp
		server/battles/CounterspellPerkTest.cpp
		server/battles/TransfigureMatterPerkTest.cpp
		server/battles/NewHorizonsMagicStateTest.cpp
		server/battles/NewHorizonsWarMachinesPerksTest.cpp
		server/battles/NewHorizonsSpellcraftTargetDurationTest.cpp
		server/battles/NewHorizonsCrossSchoolFormulaTest.cpp
		server/battles/NewHorizonsDefensiveSpecialtyTest.cpp
		server/battles/NewHorizonsOffensiveSpecialtyTest.cpp
		server/battles/NewHorizonsRemainingStartTest.cpp
		server/battles/NewHorizonsCoroniusHolyWrathTest.cpp
		server/battles/NewHorizonsMagicFixtureExportTest.cpp
		server/battles/NewHorizonsMetamagicUIFixtureExportTest.cpp
		server/battles/NewHorizonsBloodrageUIFixtureExportTest.cpp
		server/battles/NewHorizonsMetamagicTest.cpp
		server/battles/NewHorizonsSkillRewardTest.cpp
		server/battles/NewHorizonsRewardSkillFilterTest.cpp
		server/battles/NewHorizonsSkillFeedbackTest.cpp
		server/battles/NewHorizonsSpellRewardFilterTest.cpp
		server/battles/NewHorizonsDirectDamageMechanicsTest.cpp
		server/battles/NewHorizonsChainLightningFalloffTest.cpp
		battle/BattlecraftStatusPresentationTest.cpp
		battle/BattleMoraleReadbackTest.cpp
		battle/NewHorizonsQueueActivationStatusTest.cpp
		battle/NewHorizonsOrderBadgeHelpTest.cpp
		battle/BattleLuckReadbackTest.cpp
		battle/LeadershipProposalReadbackTest.cpp
		battle/SylvanLuckStatusPresentationTest.cpp
		battle/LuckSerendipityStateTest.cpp
		server/battles/NewHorizonsElementalOrbsTest.cpp
		server/battles/NewHorizonsCabirRepairTest.cpp
		spells/NewHorizonsIncomingElementDamageTest.cpp
		server/battles/NewHorizonsMagicResistanceTest.cpp
		server/battles/NewHorizonsCreatureSpecialtyTest.cpp
		server/battles/NewHorizonsSpecialtyArtBindingTest.cpp
		server/battles/NewHorizonsSpecialtyCapturedPresentationTest.cpp
		server/battles/NewHorizonsCastleCreatureStatsTest.cpp
		server/battles/NewHorizonsCanonicalCreatureGrowthTest.cpp
		server/battles/NewHorizonsGlyphsSiegeDefenseTest.cpp
		server/battles/NewHorizonsWarMachineHitPointsTest.cpp
		server/battles/NewHorizonsUnmannedTowerTest.cpp
		server/battles/NewHorizonsBrimstoneSiegeTest.cpp
		server/battles/NewHorizonsDamageSpecialtyTest.cpp
		server/battles/NewHorizonsHealingSpecialtyTest.cpp
		server/battles/NewHorizonsResurrectionFoundationTest.cpp
		server/battles/NewHorizonsSkillSpecialtyTest.cpp
		server/battles/NewHorizonsArmorerSpecialtyTest.cpp
		server/battles/NewHorizonsOffenseSpecialtyTest.cpp
		server/battles/NewHorizonsArcherySpecialtyTest.cpp
		server/battles/NewHorizonsHeroSpellAccessTest.cpp
		battle/NewHorizonsMagicArrowActionTest.cpp
		battle/NewHorizonsBerserkForcedActionTest.cpp
		battle/NewHorizonsConfusionChoicesTest.cpp
		battle/NewHorizonsConfusionResolutionTest.cpp
		battle/NewHorizonsConfusionStateTest.cpp
		server/battles/NewHorizonsConfusionApplicationTest.cpp
		battle/NewHorizonsCounterspellActionTest.cpp
		server/battles/NewHorizonsSpellRosterContextTest.cpp
		server/battles/NewHorizonsSpellRosterConsumerTest.cpp
		server/battles/NewHorizonsSpellArraySafetyTest.cpp
		server/battles/SpellTargetIdentityTest.cpp
		server/battles/NewHorizonsSkillRewardApplicationTest.cpp
		server/battles/NewHorizonsHeroGrowthTest.cpp
		server/battles/NewHorizonsStartingSkillMigrationTest.cpp
		server/battles/NewHorizonsCreatureCategoryStateTest.cpp
		server/battles/NewHorizonsCreaturePopupTextTest.cpp
		server/battles/NewHorizonsCreatureCategoryWireTest.cpp
		server/battles/NewHorizonsCreatureCategoryFixtureExportTest.cpp
		server/battles/NewHorizonsMasteryTest.cpp
		server/battles/NewHorizonsLogisticsMasteryTest.cpp
		server/battles/NewHorizonsNecromancyTest.cpp
		server/battles/NewHorizonsBloodrageTest.cpp
		server/battles/NewHorizonsBloodragePerksTest.cpp
		server/battles/NewHorizonsBloodrageThresholdTest.cpp
		server/battles/NewHorizonsDemonicGatingTest.cpp
		server/battles/NewHorizonsBulwarkRulesTest.cpp
		server/battles/NewHorizonsBulwarkRuntimeTest.cpp
		server/battles/NewHorizonsBattlecraftTest.cpp
		server/battles/NewHorizonsBattlecraftPreemptiveStrikeTest.cpp
		server/battles/NewHorizonsOverwatchTest.cpp
		server/battles/NewHorizonsRapidResponseTest.cpp
		server/battles/NewHorizonsSeizeInitiativeTest.cpp
	server/battles/NewHorizonsCrisisCommandTest.cpp
		server/battles/NewHorizonsBattlecraftDefendLifetimeTest.cpp
		server/battles/NewHorizonsBattlefieldMasteryTest.cpp
		server/battles/NewHorizonsLastStandTest.cpp
		server/battles/NewHorizonsGuardianSpiritTest.cpp
		server/battles/NewHorizonsCommandTest.cpp
		server/battles/HeroOrderTargetRejectionTest.cpp
		server/battles/NewHorizonsCombatSkillsTest.cpp
		server/battles/NewHorizonsFormationFightingTest.cpp
		server/battles/NewHorizonsArmorerVeteranTest.cpp
		server/battles/NewHorizonsVeteranHistorySerializationTest.cpp
		server/battles/NewHorizonsBastionTest.cpp
		server/battles/NewHorizonsPhantomArmyTest.cpp
		server/battles/NewHorizonsCureTest.cpp
		server/battles/NewHorizonsCureTargetTest.cpp
		server/battles/NewHorizonsDruidConsumerTest.cpp
		server/battles/NewHorizonsIndividualSpecialtyConsumerTest.cpp
	server/battles/NewHorizonsSchoolSpellcraftConsumerTest.cpp
	server/battles/NewHorizonsFieldPhysicianTest.cpp
	server/battles/NewHorizonsDeepTranceTest.cpp
	server/battles/NewHorizonsManaFurnaceTest.cpp
	server/battles/NewHorizonsMysticismRecoveryTest.cpp
		server/battles/NewHorizonsCanonicalSpellClausesSerializationTest.cpp
		server/battles/NewHorizonsImplosionTest.cpp
		server/battles/NewHorizonsTemporaryMagicDispelTest.cpp
		server/battles/NewHorizonsSorrowTest.cpp
		server/battles/NewHorizonsMassShadowTest.cpp
		server/battles/NewHorizonsMassBlessTest.cpp
		server/battles/NewHorizonsMassRegenerationTest.cpp
		battleAI/PurifyRuleFixtures.cpp
		server/battles/NewHorizonsMassSlowTest.cpp
		server/battles/NewHorizonsLifeDrainTest.cpp
		server/battles/NewHorizonsVampirismTest.cpp
		server/battles/NewHorizonsReanimateTest.cpp
		server/battles/NewHorizonsSoulReaperTest.cpp
		server/battles/NewHorizonsDoomTest.cpp
		server/battles/NewHorizonsHolyWrathTest.cpp
		server/battles/NewHorizonsHandOfFateTest.cpp
		server/battles/NewHorizonsMisfortuneTest.cpp
		server/battles/NewHorizonsShieldOfChaosTest.cpp
		server/battles/NewHorizonsArcaneBallisticsTest.cpp
		server/battles/NewHorizonsArcaneFocusTest.cpp
		server/battles/NewHorizonsGrandFormulaTest.cpp
		server/battles/NewHorizonsHolyArmorTest.cpp
		server/battles/NewHorizonsHeavenlyGaleTest.cpp
		server/battles/NewHorizonsDivineRetributionTest.cpp
		server/battles/NewHorizonsPurifyTest.cpp
		server/battles/NewHorizonsCrusadeTest.cpp
		server/battles/NewHorizonsEntangleTest.cpp
		server/battles/NewHorizonsVengefulVinesTest.cpp
		server/battles/NewHorizonsSummonTrollsTest.cpp
		server/battles/NewHorizonsVerdantPrisonTest.cpp
		server/battles/SummonedCreatureHealthTerrainTest.cpp
		server/battles/NewHorizonsHydrasVitalityTest.cpp
		server/battles/NewHorizonsChaosBlinkTest.cpp
		server/battles/NewHorizonsCommandEfficiencyTest.cpp
		server/battles/NewHorizonsCombinedArmsTest.cpp
		server/battles/NewHorizonsPreparedCasterTest.cpp
		server/battles/NewHorizonsManaConservationTest.cpp
		server/battles/NewHorizonsMagicalDamageReductionTest.cpp
		server/battles/NewHorizonsSpellwardTest.cpp
		server/battles/NewHorizonsCounterpressureTest.cpp
		server/battles/NewHorizonsPreciseCastingTest.cpp
		server/battles/NewHorizonsForcedDisplacementTest.cpp
		battleAI/NewHorizonsPreciseCastingAITest.cpp
		server/battles/NewHorizonsStormOfDaggersTest.cpp
		server/battles/NewHorizonsSanctuaryTest.cpp
		server/battles/NewHorizonsSanctuaryKeeperTest.cpp
		server/battles/NewHorizonsObstacleMovementCostTest.cpp
		server/battles/NewHorizonsPassingLinesTest.cpp
		server/battles/NewHorizonsPassThroughLongReachTest.cpp
		server/battles/NewHorizonsTacticsDeploymentTest.cpp
		server/battles/NewHorizonsDeploymentOrderRuntimeTest.cpp
		server/battles/NewHorizonsCorpsePreservationTest.cpp
		server/battles/NewHorizonsRedeploymentTest.cpp
		server/battles/NewHorizonsEarthquakeTest.cpp
		battleAI/NewHorizonsEarthquakeAITest.cpp
		battleAI/NewHorizonsCreatureSiegeAITest.cpp
		server/battles/NewHorizonsHavocStructuresTest.cpp
		server/battles/NewHorizonsFriendlyFireTest.cpp
		battleAI/NewHorizonsHavocStructuresAITest.cpp
		server/battles/NewHorizonsVenomancerTest.cpp
		server/battles/NewHorizonsRegenerationTest.cpp
		server/battles/NewHorizonsBerserkProfileTest.cpp
		server/battles/NewHorizonsBerserkActivationTest.cpp
		server/battles/NewHorizonsDispelProfileTest.cpp
		server/battles/NewHorizonsChainLightningProfileTest.cpp
		server/battles/NewHorizonsBlessTest.cpp
		server/battles/NewHorizonsWarcastingTest.cpp
		server/NewHorizonsSkeletonTransformerTest.cpp
		battle/NewHorizonsEnchantedCommandTest.cpp
		battle/OverwhelmingFormulaStateTest.cpp
		battle/OverwhelmingFormulaProtocolTest.cpp
		spells/OverwhelmingFormulaDamageTest.cpp
		server/battles/OverwhelmingFormulaTest.cpp
		server/battles/WarcastingScriptSpellTest.cpp
		server/battles/NewHorizonsShroudTest.cpp
		server/battles/NewHorizonsShroudNoEscapeTest.cpp
		server/battles/NewHorizonsEvasiveShroudTest.cpp
		server/battles/NewHorizonsAmbusherTest.cpp
		server/battles/NewHorizonsShadowAssaultTest.cpp
		server/battles/NewHorizonsNightProwlerTest.cpp
		server/battles/NewHorizonsSylvanLuckTest.cpp
		server/battles/NewHorizonsHeroLuckTest.cpp
		server/battles/NewHorizonsLuckSerendipityTest.cpp
		server/battles/NewHorizonsFortunesFavorTest.cpp
		server/NewHorizonsPerfectFortuneTest.cpp
		server/NewHorizonsCounterBatteryTest.cpp
		server/NewHorizonsBattlefieldMedicTest.cpp
		server/battles/NewHorizonsLuckyAimTest.cpp
		server/battles/NewHorizonsSecondChanceTest.cpp
		server/battles/NewHorizonsGamblerTest.cpp
		server/battles/NewHorizonsChainOfFortuneTest.cpp
		server/battles/NewHorizonsAdverseCombatRerollTest.cpp
		server/battles/NewHorizonsTwistRuntimeTest.cpp
		server/battles/NewHorizonsRallyTest.cpp
		server/battles/NewHorizonsUnbreakableTest.cpp
		server/battles/NewHorizonsReserveTest.cpp
		server/battles/NewHorizonsStandardBearerTest.cpp
		server/battles/NewHorizonsMoraleRulesTest.cpp
		server/battles/NewHorizonsSteadfastTest.cpp
		server/battles/NewHorizonsCommandingPresenceTest.cpp
		server/battles/NewHorizonsBloodScentTest.cpp
		server/battles/NewHorizonsElementalCounterfireTest.cpp
		server/battles/NewHorizonsInspirationalCreatureSpellTest.cpp
		server/battles/NewHorizonsRageThroughPainTest.cpp
		server/battles/NewHorizonsHoldFastTest.cpp
		server/battles/NewHorizonsFearlessTest.cpp
		server/battles/NewHorizonsSurgeonTest.cpp
		server/battles/NewHorizonsPiercingBoltsTest.cpp
		server/battles/NewHorizonsFortificationEngineerTest.cpp
		server/battles/NewHorizonsMasterGunnerTest.cpp
		server/battles/NewHorizonsQuartermasterTest.cpp
		server/battles/NewHorizonsTwistScriptedTest.cpp
		server/battles/NewHorizonsLandMineTest.cpp
		server/battles/NewHorizonsMineLayerTest.cpp
		server/battles/NewHorizonsQuicksandTest.cpp
		server/battles/NewHorizonsFireWallTest.cpp
		server/battles/NewHorizonsTimeStopTest.cpp
		server/battles/NewHorizonsFrozenTest.cpp
		server/battles/FrozenAttackCommitCharacterizationTest.cpp
		server/battles/FrozenAttackResolutionTest.cpp
		server/battles/NewHorizonsMasteryFixtureExportTest.cpp
		server/battles/NewHorizonsConvenienceFixtureExportTest.cpp
		server/battles/NewHorizonsHeroFixtureExportTest.cpp
		server/battles/NewHorizonsSkirmisherPreviewFixtureExportTest.cpp
		server/battles/NewHorizonsHeroPositiveCueFixtureExportTest.cpp
		server/battles/NewHorizonsSpecialtyPresentationFixtureExportTest.cpp
		server/queries/QueriesProcessorTest.cpp
		server/queries/NewHorizonsPerkVerticalSliceTest.cpp
		server/queries/NewHorizonsPerkRewardContextTest.cpp
		server/queries/NewHorizonsQuickStudyTest.cpp
		server/queries/NewHorizonsUniqueBuildingTrainingTest.cpp
		server/queries/NewHorizonsNecromancyAmplifierTest.cpp
		server/queries/NewHorizonsBattleScholarAcademyTest.cpp
		server/queries/NewHorizonsFountainOfFortuneTest.cpp
		server/queries/NewHorizonsCastleStablesTest.cpp
		server/queries/NewHorizonsBloodObeliskTest.cpp
		server/queries/NewHorizonsArtifactScaleTest.cpp
		server/queries/NewHorizonsMasterLogisticianTest.cpp
		server/queries/NewHorizonsSpeedArtifactTest.cpp
		server/queries/NewHorizonsConfluxGrowthTest.cpp
		server/queries/NewHorizonsMagicUniversityTest.cpp
		server/queries/NewHorizonsAdventureSpellUnlockTest.cpp
		server/queries/NewHorizonsTownPortalMovementTest.cpp
		server/queries/NewHorizonsManaRegenerationArtifactTest.cpp
		server/queries/NewHorizonsGlyphsOfFearTest.cpp
		server/NewHorizonsEconomyTest.cpp
		server/NewHorizonsEstatesFinancierTest.cpp
		server/NewHorizonsEstatesInvestorTest.cpp
		server/NewHorizonsEstatesLandSurveyorTest.cpp
		server/NewHorizonsEstatesProspectorTest.cpp
		server/NewHorizonsEstatesStewardTest.cpp
		server/NewHorizonsEstatesMagnateTest.cpp
		server/NewHorizonsAcademicStudyTest.cpp
		server/queries/NewHorizonsScholarTest.cpp
		server/queries/NewHorizonsSageTest.cpp
		server/queries/NewHorizonsRecruitersContactsTest.cpp
		server/NewHorizonsResourceBrokerTest.cpp
		server/NewHorizonsLearningMentorTest.cpp
		server/NewHorizonsLearningMasterTeacherTest.cpp
		server/NewHorizonsLearningFieldStudyTest.cpp
		server/battles/NewHorizonsEagleEyeTest.cpp
		server/NewHorizonsAstrologyTest.cpp
		server/NewHorizonsCastleGateTest.cpp
		server/NewHorizonsLeadershipAdmissionTest.cpp
		server/NewHorizonsUpgradeLeadershipParityTest.cpp
		server/NewHorizonsBattleFormResultTest.cpp
		server/NewHorizonsRecruitmentMusterTest.cpp
		server/NewHorizonsDiplomacyTest.cpp
		server/NewHorizonsLegendaryReputationTest.cpp
		server/NewHorizonsMercenaryAdmissionTest.cpp
		server/NewHorizonsPortalSummoningTest.cpp
		server/NewHorizonsDemonicReserveTest.cpp
		server/RandomArtifactPoolExclusionsTest.cpp
		server/RandomArtifactPoolConsumersTest.cpp

		texts/TextOperationsTest.cpp

		mock/BattleFake.cpp
		mock/GameHandlerTestServer.cpp
		mock/mock_IGameEventCallback.cpp
 		mock/mock_MapService.cpp
 		mock/mock_MapServiceTinyH3M.cpp
 		mock/mock_BonusBearer.cpp
		mock/mock_CPSICallback.cpp
		mock/TinyMapGameTest.cpp
		mock/TinyH3MWriter.cpp
		mock/TinyH3MBuilder.cpp
)

set(test_HEADERS
		battleAI/HeroActionTimingCapture.h
		battleAI/HeroQueueSelectorFixture.h
		battleAI/HeroExpectedSpellFixture.h
		battleAI/PurifyRuleFixtures.h
		battleAI/HeroQueueBonusSnapshot.h
 		StdInc.h

 		CVcmiTestConfig.h
		JsonComparer.h
		SpellPointTestUtils.h
		battleAI/InitialExportWorldFixture.h
		battleAI/OriginalMeleeSequenceFixture.h
		battleAI/BattlecraftPreemptiveAIFixture.h
		battleAI/OwnedExportLifecycleFixture.h
		battleAI/LegacyOrderOracleChecks.h
		battleAI/SelectedHeroUtilityCapture.h
		battleAI/HeroActionSelectionObserver.h
		battleAI/HeroActionSelectionState.h
		battleAI/BattlecraftFutureBoundaryFixture.h
		battleAI/OwnedExportMeleeFixture.h
		battleAI/ProjectedTargetHitFixture.h

		map/MapComparer.h

 		netpacks/NetPackFixture.h

		spells/effects/EffectFixture.h

		spells/targetConditions/TargetConditionItemFixture.h

		server/battles/BattleTestFixture.h
		server/battles/VerdantPrisonScriptErrorCapture.h
		server/battles/FrozenAttackStageFixture.h
		server/battles/HeroCommandFixture.h
		server/battles/NewHorizonsElementalTerrainFixture.h
		server/battles/FocusFireFixture.h
		server/battles/BattleStartSnapshotFixture.h

		mock/BattleFake.h
		mock/GameHandlerTestServer.h
		mock/mock_BonusBearer.h
		mock/mock_CStack.h
		mock/mock_IGameEventCallback.h
 		mock/mock_MapService.h
 		mock/mock_MapServiceTinyH3M.h
		mock/mock_BonusBearer.h
		mock/TownFake.h
		mock/TinyMapGameTest.h
		mock/TinyH3MWriter.h
		mock/TinyH3MBuilder.h

		quest/QuestTest.h
		quest/QuestAssertions.h
		quest/QuestScenarios.h
)

if(ENABLE_BATTLE_AI)
	file(GLOB_RECURSE BATTLE_AI_TEST_SRCS "../AI/BattleAI/*.cpp")
	list(FILTER BATTLE_AI_TEST_SRCS EXCLUDE REGEX ".*main\\.cpp$")

	list(APPEND test_SRCS
		${BATTLE_AI_TEST_SRCS}
		server/battles/NewHorizonsAvatarOfRageTest.cpp
		server/battles/NewHorizonsBloodrageDeathPerksTest.cpp
		server/battles/NewHorizonsSwiftRebirthTest.cpp
		server/battles/NewHorizonsElementalMemoryTest.cpp
		server/battles/NewHorizonsPhoenixSparkTest.cpp
		server/battles/NewHorizonsVeteranCohesionTest.cpp
		server/battles/NewHorizonsRoyalStandardTest.cpp
		server/battles/NewHorizonsDeepFlankTest.cpp
		server/battles/NewHorizonsEncircledDoomTest.cpp
		server/battles/NewHorizonsVanishTest.cpp
		server/battles/NewHorizonsHasteSpecialtyTest.cpp
		server/battles/NewHorizonsThantReanimateSpecialtyTest.cpp
		server/battles/NewHorizonsSharedPurposeTest.cpp
		server/battles/NewHorizonsFrailtySpecialtyTest.cpp
		server/battles/NewHorizonsRecruitmentTrainingTest.cpp
		server/battles/NewHorizonsDivineDisciplineTest.cpp
		server/battles/NewHorizonsCrownAndAltarTest.cpp
		server/battles/NewHorizonsHavocPerksTest.cpp
		server/battles/NewHorizonsPlaguebearerTest.cpp
		server/battles/NewHorizonsIronWillTest.cpp
		server/battles/NewHorizonsHeroicSpiritTest.cpp
		server/battles/NewHorizonsMercenaryCombatTest.cpp
		server/battles/NewHorizonsOpportunistTest.cpp
		server/battles/NewHorizonsReactiveWeaveTest.cpp
		server/battles/NewHorizonsHalonMetamagicTest.cpp
		battleAI/HeroCommandAITest.cpp
		battleAI/FutureAttackReactionTest.cpp
		battleAI/CanonicalOrderValueTest.cpp
		battleAI/SecondWindOpportunityTest.cpp
		battleAI/HeroOrderCandidatesTest.cpp
		battleAI/SpellAdmissionCharacterizationTest.cpp
		battleAI/HeroActionAdmissionTest.cpp
		battleAI/HeroActionUtilityTest.cpp
		battleAI/HeroActionSelectionTest.cpp
		battleAI/VinesCandidateValueReuseTest.cpp
		battleAI/LifeDrainCandidateValueReuseTest.cpp
		battleAI/LifeDrainCandidateValueReuseAITest.cpp
		battleAI/HeroActionTimingTest.cpp
		battleAI/DamageCalculationTimingRoutingTest.cpp
		battleAI/ProtectAttackPreviewTest.cpp
		battleAI/HeroOrderEffectsProjectionTest.cpp
		battleAI/LegacyHeroOrderProjectionTest.cpp
		battleAI/BraceAttackPreviewTest.cpp
		battleAI/BracePreemptiveForecastTest.cpp
		battleAI/InitialExportWorldTest.cpp
		battleAI/MeleeExpiryOwnershipTest.cpp
		battleAI/MeleeResourceOrderTest.cpp
		battleAI/MovementBonusProvenanceTest.cpp
		battleAI/ReachabilityMapProjectionTest.cpp
		battleAI/EnemyBypassReachabilityTest.cpp
		battleAI/DamageCacheObstacleVisibilityTest.cpp
		battleAI/CreaturePressureCachePreparationTest.cpp
		battleAI/DamageCacheAdmissionTest.cpp
		battleAI/DamageFinalizationParityTest.cpp
		battleAI/PrimaryFortuneProducerTest.cpp
		battleAI/AppliedStrikeFactsTest.cpp
		battleAI/BattleShroudStrikeTest.cpp
		battleAI/StackWithBonusesQueryTest.cpp
		battleAI/OrdinaryMeleeRetaliationTest.cpp
		battleAI/OriginalMeleeSequenceTest.cpp
		battleAI/OwnedExportBranchQueryTest.cpp
		battleAI/OwnedExportLifecycleTest.cpp
		battleAI/OwnedExportMeleeTest.cpp
		battleAI/OwnedBraceArrivalTest.cpp
		battleAI/VisibleHeroBonusQueryTest.cpp
		battleAI/HeroCommandCoefficientInputsTest.cpp
		battleAI/RecipientMeleeInjuryTest.cpp
		battleAI/VisibleCandidateWorldTest.cpp
		battleAI/SelectedAttackReceiptTest.cpp
		battleAI/LegacyRecipientMovementTest.cpp
		battleAI/OwnedExportRoundSequenceTest.cpp
		battleAI/ProjectedTargetHitTest.cpp
		battleAI/StackEffectRemovalTest.cpp
		battleAI/HeroQueueSelectorFixture.cpp
		battleAI/HeroQueueBonusSnapshot.cpp
		battleAI/HeroQueueBonusSnapshotTest.cpp
		battleAI/HeroQueueCompletionSelectorTest.cpp
		battleAI/HeroQueueContinuationSelectorTest.cpp
		battleAI/HeroQueueOrderSelectorTest.cpp
		battleAI/HeroExpectedSpellFixture.cpp
		battleAI/HeroExpectedSpellSelectorTest.cpp
		battleAI/HeroExpectedSpellComponentTest.cpp
		battleAI/NewHorizonsGenieSupportAITest.cpp
		battleAI/NewHorizonsBeneficialCreatureAITest.cpp
		battleAI/NewHorizonsWispLongReachAITest.cpp
		battleAI/NewHorizonsDoubleCommandAITest.cpp
		battleAI/NewHorizonsDivineMandateAITest.cpp
		battleAI/NewHorizonsElementalRebirthAITest.cpp
		battleAI/NewHorizonsRebirthChainAITest.cpp
		battleAI/NewHorizonsBattlePlanAITest.cpp
		battleAI/AttackResourceProjectionTest.cpp
		battleAI/DamageReductionControlTest.cpp
		battleAI/FocusFireAITest.cpp
		battleAI/ProjectedAdditionTerrainTest.cpp
		battleAI/NewHorizonsMagicAITest.cpp
		battleAI/NewHorizonsImplosionAITest.cpp
		server/battles/NewHorizonsTeleportFreedomTest.cpp
		battleAI/NewHorizonsForgetfulnessAITest.cpp
		battleAI/NewHorizonsHolyWrathAITest.cpp
		battleAI/NewHorizonsHandOfFateAITest.cpp
		battleAI/NewHorizonsMisfortuneAITest.cpp
		battleAI/NewHorizonsShieldOfChaosAITest.cpp
		battleAI/NewHorizonsArcaneFocusAITest.cpp
		battleAI/NewHorizonsBattleFormAITest.cpp
		battleAI/NewHorizonsBerserkAITest.cpp
		battleAI/NewHorizonsPuppetMasterAITest.cpp
		battleAI/NewHorizonsHolyArmorAITest.cpp
		battleAI/NewHorizonsHeavenlyGaleAITest.cpp
		battleAI/NewHorizonsDivineRetributionAITest.cpp
		battleAI/NewHorizonsPurifyAITest.cpp
		battleAI/PurifyRuleFixturesTest.cpp
		battleAI/NewHorizonsCrusadeAITest.cpp
		battleAI/NewHorizonsEntangleAITest.cpp
		battleAI/NewHorizonsVengefulVinesAITest.cpp
		battleAI/IndividualRiderDamageIntegrationTest.cpp
		battleAI/NewHorizonsSummonTrollsAITest.cpp
		battleAI/NewHorizonsVerdantPrisonAITest.cpp
		battleAI/NewHorizonsHydrasVitalityAITest.cpp
		battleAI/OptionalHydraForecastTest.cpp
		battleAI/NewHorizonsChaosBlinkAITest.cpp
		battleAI/NewHorizonsCommandEfficiencyAITest.cpp
		battleAI/NewHorizonsCombinedArmsAITest.cpp
		battleAI/NewHorizonsPreparedCasterAITest.cpp
		battleAI/NewHorizonsGrandFormulaAITest.cpp
		battleAI/NewHorizonsTomeAITest.cpp
		battleAI/NewHorizonsHeroGrowthAITest.cpp
		battleAI/NewHorizonsMasteryAttackCountTest.cpp
		battleAI/NewHorizonsLevelGrantAITest.cpp
		battleAI/NewHorizonsTimeStopAITest.cpp
		battleAI/NewHorizonsSpellLockAITest.cpp
		battleAI/NewHorizonsPlagueCompletionAITest.cpp
		battleAI/NewHorizonsPlagueActionCompletionAITest.cpp
		battleAI/NewHorizonsPlagueMovementCompletionAITest.cpp
		battleAI/NewHorizonsHexSnapshotAITest.cpp
		battleAI/NewHorizonsNaturesWrathAITest.cpp
		battleAI/NewHorizonsPandemoniumAITest.cpp
		battleAI/NewHorizonsRealityWarpAITest.cpp
		battleAI/NewHorizonsFrozenAITest.cpp
		battleAI/FrozenPredicateCharacterizationTest.cpp
		battle/NewHorizonsBattleAITimingTest.cpp
		battleAI/SpellTargetsEvaluatorTest.cpp
		battleAI/HypotheticCloneTest.cpp
		battleAI/HypotheticLifeDrainObservationTest.cpp
		battleAI/HypotheticTimedSpellTest.cpp
		battleAI/HypotheticObstacleTest.cpp
		battleAI/HypotheticSpellServerRouteTest.cpp
		battleAI/HypotheticWallTest.cpp
		battleAI/NewHorizonsSylvanLuckAITest.cpp
		battleAI/NewHorizonsNoQuarterAITest.cpp
		battleAI/NewHorizonsRelentlessAssaultAITest.cpp
		battleAI/NewHorizonsBulwarkAITest.cpp
		battleAI/NewHorizonsBattlecraftPreemptiveStrikeAITest.cpp
		battleAI/BattlecraftFutureExchangeTest.cpp
		battleAI/NewHorizonsBattlefieldMasteryAITest.cpp
		battleAI/BattlecraftFutureBoundaryTest.cpp
		battleAI/BattlecraftFutureMultiattackExchangeTest.cpp
		battleAI/NewHorizonsLastStandAITest.cpp
		battleAI/NewHorizonsArmorerVeteranAITest.cpp
		battleAI/NewHorizonsPaviseAITest.cpp
		battleAI/NewHorizonsArcheryAITest.cpp
	)
endif()

if(ENABLE_NULLKILLER2_AI)
	list(APPEND test_SRCS
		server/NewHorizonsMerchantPrinceTest.cpp
		nullkiller2/NewHorizonsProspectorAITest.cpp
		nullkiller2/NewHorizonsAcademicStudyAITest.cpp
		nullkiller2/NewHorizonsSageAITest.cpp
		nullkiller2/NewHorizonsRecruitersContactsAITest.cpp
		server/queries/NewHorizonsHistorianRewardTest.cpp
		server/queries/NewHorizonsArchivistTest.cpp
		nullkiller2/AIUtilityTest.cpp
		battleAI/NewHorizonsElementalOrbAITest.cpp
		nullkiller2/Analyzers/HeroManagerTest.cpp
		nullkiller2/Analyzers/ObjectClusterizerTest.cpp
		nullkiller2/Behaviors/DefenceBehaviorTest.cpp
		nullkiller2/Behaviors/EscapeBehaviorTest.cpp
		nullkiller2/Behaviors/ExplorationBehaviorTest.cpp
		nullkiller2/Behaviors/GatherArmyBehaviorTest.cpp
		nullkiller2/Behaviors/RecruitHeroBehaviorTest.cpp
		nullkiller2/Engine/PriorityEvaluatorTest.cpp
		nullkiller2/Engine/TaskFailureTest.cpp
		nullkiller2/Engine/NewHorizonsLogisticsMovementAITest.cpp
		nullkiller2/Engine/NewHorizonsLighthouseDepartureAITest.cpp
		nullkiller2/NewHorizonsTravelLandingAITest.cpp
		nullkiller2/NewHorizonsTravelRoundTripAITest.cpp
		nullkiller2/Engine/NewHorizonsForcedMarchAITest.cpp
		nullkiller2/Goals/BuyArmyTest.cpp
		nullkiller2/NewHorizonsPortalRecruitmentTest.cpp
		nullkiller2/Goals/CompositionTest.cpp
		nullkiller2/Goals/ExploreNeighbourTileTest.cpp
		nullkiller2/ArmyFormationLeadershipTest.cpp
		nullkiller2/ArmyExchangeExecutionTest.cpp
		nullkiller2/AdventureSpellExecutionTest.cpp
		nullkiller2/EspritDeCorpsArmySelectionTest.cpp
		nullkiller2/Helpers/ExplorationHelperTest.cpp
		nullkiller2/Helpers/NewHorizonsMusterTest.cpp
		nullkiller2/NewHorizonsAdventureSpellUnlockAITest.cpp
		nullkiller2/NewHorizonsArcaneReservoirAITest.cpp
		nullkiller2/NewHorizonsPeacemakerDangerAITest.cpp
		nullkiller2/NewHorizonsEstatesIncomeAITest.cpp
		nullkiller2/NewHorizonsMarketSelectionAITest.cpp
		nullkiller2/NewHorizonsLearningMentorAITest.cpp
		nullkiller2/NewHorizonsLearningMasterTeacherAITest.cpp
		nullkiller2/NewHorizonsTeachingMeetingAITest.cpp
		nullkiller2/NewHorizonsExternalMusterAITest.cpp
		nullkiller2/NewHorizonsTownMusterAITest.cpp
		nullkiller2/NewHorizonsNeutralDiplomacyPlannerTest.cpp
		nullkiller2/Helpers/TinyH3MDimensionDoorExplorationTest.cpp
		nullkiller2/Pathfinding/AIPathNodePoolTest.cpp
		nullkiller2/Pathfinding/ArmyLossTest.cpp
		nullkiller2/Pathfinding/ChainReconstructionTest.cpp
		nullkiller2/Pathfinding/DimensionDoorActionTest.cpp
		nullkiller2/Pathfinding/CastleGateActionTest.cpp
		nullkiller2/Pathfinding/AdventureSpellDailyPathfindingTest.cpp
	)

	list(APPEND test_HEADERS
		nullkiller2/Nulkiller2TestUtils.h
		nullkiller2/NullkillerTest.h
	)
	# This fixture follows adventure preparation into the combat AI consumer.
	list(APPEND test_SRCS nullkiller2/NewHorizonsElementalDwellingPriorityTest.cpp)
	if(ENABLE_BATTLE_AI)
		list(APPEND test_SRCS nullkiller2/NewHorizonsDemonicReserveAITest.cpp)
	endif()
endif()

if(ENABLE_NH_PERF_EXPERIMENTS)
	list(APPEND test_SRCS
		network/InternalConnectionBenchmark.cpp
		network/ExperimentalSetFormationTest.cpp
	)
endif()

# This switch only registers tests; it never installs or activates spell content.
# Missing profile/sentinel is a test failure, not an accepted skip.
if(ENABLE_NEW_HORIZONS_MANAGED_TESTS)
	list(APPEND test_SRCS
		server/battles/NewHorizonsManagedMissileTest.cpp
		battleAI/NewHorizonsManagedMissileAITest.cpp
		server/battles/NewHorizonsAuthoredMagicMapTest.cpp
	)
endif()

assign_source_group(${test_SRCS} ${test_HEADERS})

set(mock_HEADERS
		mock/mock_battle_IBattleState.h
		mock/mock_battle_Unit.h
		mock/mock_Creature.h
		mock/mock_CreatureService.h
		mock/mock_IBattleInfoCallback.h
		mock/mock_scripting_Pool.h
		mock/mock_spells_Mechanics.h
		mock/mock_spells_Problem.h
		mock/mock_spells_Spell.h
		mock/mock_UnitEnvironment.h
		mock/mock_UnitInfo.h
		mock/mock_vstd_RNG.h
		mock/mock_CPSICallback.h
)

if(MSVC)
	set(gtest_force_shared_crt ON CACHE BOOL "Use shared (DLL) run-time lib even when Google Test is built as static lib." FORCE)
endif()
check_cxx_compiler_flag(-Wcharacter-conversion CHARCONV)
if(CHARCONV)
	add_compile_options(-Wno-error=character-conversion) #Used in googletest
endif()
check_cxx_compiler_flag(-Wimplicit-int-float-conversion CONV)
if(CONV)
	add_compile_options(-Wno-error=implicit-int-float-conversion) #Used in googletest
endif()
check_cxx_compiler_flag(-Wdeprecated-copy-with-user-provided-copy COPY)
if(COPY)
	add_compile_options(-Wno-deprecated-copy-with-user-provided-copy) #Used in googletest
endif()
check_cxx_compiler_flag(-Wvirtual-move-assign MOVE_ASSIGN)
if(MOVE_ASSIGN)
	add_compile_options(-Wno-error=virtual-move-assign) #GCC is too strict here
endif()
add_subdirectory_with_folder("3rdparty" "${googleTest_Dir}" "${CMAKE_CURRENT_BINARY_DIR}/googletest" EXCLUDE_FROM_ALL)

add_executable(vcmitest ${test_SRCS} ${test_HEADERS} ${mock_HEADERS})
add_executable(nhBattleEndDispatchTest ../client/tests/BattleEndDispatchTest.cpp)
add_test(NAME nhBattleEndDispatchTest COMMAND nhBattleEndDispatchTest)
add_executable(nhProjectileFlightLifecycleTest ../client/tests/ProjectileFlightLifecycleTest.cpp)
add_test(NAME nhProjectileFlightLifecycleTest COMMAND nhProjectileFlightLifecycleTest)
add_executable(nhWindowSceneRedrawTest ../client/tests/WindowSceneRedrawTest.cpp)
add_test(NAME nhWindowSceneRedrawTest COMMAND nhWindowSceneRedrawTest)
add_executable(nhMapTransitionSnapshotTest ../client/tests/MapTransitionSnapshotTest.cpp)
target_link_libraries(nhMapTransitionSnapshotTest PRIVATE Threads::Threads)
add_test(NAME nhMapTransitionSnapshotTest COMMAND nhMapTransitionSnapshotTest)

add_executable(nhHeroPositiveProcPresentationTest ../client/tests/HeroPositiveProcPresentationTest.cpp)
add_test(NAME nhHeroPositiveProcPresentationTest COMMAND nhHeroPositiveProcPresentationTest)
add_executable(nhAnimationPhaseStartTest ../client/tests/AnimationPhaseStartTest.cpp)
add_test(NAME nhAnimationPhaseStartTest COMMAND nhAnimationPhaseStartTest)

# Asset-free state checks only; native authority tests retain their TEST profile.
set_tests_properties(
	nhHeroDevelopmentNavigationTest nhHeroMasteryDialogStateTest
	nhExchangeSecondarySkillSlotsTest nhStackInfoPanelHoverStateTest nhFrozenApplicationFeedbackTest
	nhProjectileFlightLifecycleTest nhWindowSceneRedrawTest nhMapTransitionSnapshotTest
	nhBattleEndDispatchTest
	nhHeroPositiveProcPresentationTest nhAnimationPhaseStartTest
	nhCureAfflictionSessionTest
	PROPERTIES LABELS "nh-pure;ui;state" TIMEOUT 10)
add_executable(nhReleaseUpdatePolicyTest
	../client/tests/ReleaseUpdatePolicyTest.cpp
	../client/mainmenu/ReleaseUpdatePolicy.cpp)
target_link_libraries(nhReleaseUpdatePolicyTest PRIVATE vcmi)
set_target_properties(nhReleaseUpdatePolicyTest PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_test(NAME nhReleaseUpdatePolicyTest COMMAND nhReleaseUpdatePolicyTest)
add_executable(nhReleaseUpdateTransportProbe
	../client/tests/ReleaseUpdateTransportProbe.cpp
	../client/mainmenu/ReleaseUpdateTransport.cpp)
if(WIN32)
	target_link_libraries(nhReleaseUpdateTransportProbe PRIVATE winhttp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	find_package(CURL REQUIRED)
	target_link_libraries(nhReleaseUpdateTransportProbe PRIVATE CURL::libcurl)
endif()
# The default test is offline. A maintainer may explicitly opt in with --live.
add_test(NAME nhReleaseUpdateTransportCancelled COMMAND nhReleaseUpdateTransportProbe)
if(ENABLE_NULLKILLER2_AI)
	# Nullkiller tests use internal AI symbols, so embed AI objects in vcmitest
	# instead of linking the facade and loading a second copy of lib globals.
	target_sources(vcmitest PRIVATE $<TARGET_OBJECTS:Nullkiller2>)
	if(TARGET vcmiLua)
		# These fixtures exercise internal Lua descriptors and field serialization.
		# Reuse embedded Lua, not a second facade/global instance.
		target_sources(vcmitest PRIVATE
			spells/OverwhelmingFormulaObstacleTest.cpp
			scripting/CureAfflictionSourceTest.cpp
			scripting/LuaImplementationIdentityTest.cpp
			scripting/LuaDamageCalculatorIdentityTest.cpp
			scripting/LuaCombatEventIdentityTest.cpp
			scripting/LuaStackFieldKeyTest.cpp)
	endif()
	set(vcmitest_LIBS gtest gmock vcmiMain vcmiservercommon TBB::tbb ${SYSTEM_LIBS})
	foreach(optional_vcmitest_target IN ITEMS vcmiLua EmptyAI StupidAI MMAI)
		if(TARGET ${optional_vcmitest_target})
			list(APPEND vcmitest_LIBS ${optional_vcmitest_target})
		endif()
	endforeach()
else()
	set(vcmitest_LIBS gtest gmock vcmi vcmiservercommon ${SYSTEM_LIBS})
endif()
target_link_libraries(vcmitest PRIVATE ${vcmitest_LIBS})

target_include_directories(vcmitest
		PUBLIC	${CMAKE_CURRENT_SOURCE_DIR}
		PRIVATE	${GTestSrc}
		PRIVATE	${GTestSrc}/include
		PRIVATE	${GMockSrc}
		PRIVATE	${GMockSrc}/include
)

# Running tests one by one using ctest not recommended due to vcmi having
# slow global initialization.
gtest_discover_tests(vcmitest
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")

vcmi_set_output_dir(vcmitest "")

enable_pch(vcmitest)

file (GLOB_RECURSE testdata "testdata/*.*")
foreach(resource ${testdata})
	get_filename_component(filename ${resource} NAME)
	get_filename_component(dir ${resource} DIRECTORY)
	get_filename_component(dirname ${dir} NAME)
	set (output "")
	while(NOT ${dirname} STREQUAL testdata)
		get_filename_component(path_component ${dir} NAME)
		set (output "${path_component}/${output}")
		get_filename_component(dir ${dir} DIRECTORY)
		get_filename_component(dirname ${dir} NAME)
	endwhile()
	set(output "${CMAKE_BINARY_DIR}/bin/test/testdata/${output}/${filename}")
	configure_file(${resource} ${output} COPYONLY)
endforeach()
