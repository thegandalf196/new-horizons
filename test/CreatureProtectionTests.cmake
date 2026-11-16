# Focused physical-defense initialization and creature protection regressions.
add_executable(nhCreatureProtectionTest EXCLUDE_FROM_ALL
    main.cpp CVcmiTestConfig.cpp
    mock/mock_MapService.cpp mock/mock_MapServiceTinyH3M.cpp
    mock/mock_BonusBearer.cpp
    mock/TinyMapGameTest.cpp mock/TinyH3MWriter.cpp mock/TinyH3MBuilder.cpp
    server/battles/BattleTestFixture.cpp
    server/battles/NewHorizonsPassiveSpecialtyTest.cpp
    server/battles/CreatureSupplementalStatsTest.cpp
    bonus/BonusEvaluationContextTest.cpp
    spells/MagicalDamageReductionTest.cpp
    server/battles/NewHorizonsMagicalDamageReductionTest.cpp
    ../client/windows/CreatureSupplementalStats.cpp)
target_include_directories(nhCreatureProtectionTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(nhCreatureProtectionTest PRIVATE cxx_std_20)
target_link_libraries(nhCreatureProtectionTest PRIVATE gtest gmock vcmi vcmiservercommon ${SYSTEM_LIBS})
vcmi_set_output_dir(nhCreatureProtectionTest "")
add_test(NAME nhCreatureProtectionTest COMMAND nhCreatureProtectionTest)
set_tests_properties(nhCreatureProtectionTest PROPERTIES TIMEOUT 300 LABELS "battle;protection;ui-readout")
