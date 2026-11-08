# Focused migration fixtures retain the real game bootstrap and authoritative
# server/AI code. This target does not replace vcmitest or make missing assets pass.
option(ENABLE_NATIVE_MIGRATION_TESTS "Build focused native-rule migration integration tests" OFF)
if(NOT ENABLE_NATIVE_MIGRATION_TESTS)
	return()
endif()
if(NOT ENABLE_TEST OR NOT ENABLE_LUA_ORACLE OR NOT TARGET BattleAI
	OR NOT TARGET vcmiLua OR NOT TARGET vcmiservercommon)
	message(FATAL_ERROR "ENABLE_NATIVE_MIGRATION_TESTS requires ENABLE_TEST, ENABLE_LUA_ORACLE, BattleAI and vcmiservercommon")
endif()

add_executable(nhNativeMigrationTest
	StdInc.cpp
	main.cpp
	CVcmiTestConfig.cpp
	mock/TinyH3MBuilder.cpp
	mock/TinyH3MWriter.cpp
	mock/mock_MapServiceTinyH3M.cpp
	mock/TinyMapGameTest.cpp
	mock/mock_IGameEventCallback.cpp
	mock/BattleFake.cpp
	mock/mock_BonusBearer.cpp
	server/battles/BattleTestFixture.cpp
	spells/effects/EffectFixture.cpp
	scripting/NativeCombatEventsParityTest.cpp
	scripting/NativeCombatEventAuthorityParityTest.cpp
	scripting/NativeDamageCaptureParityTest.cpp
	scripting/NativeJsonParametersParityTest.cpp
	spells/effects/NativeSpellLuaParityTest.cpp
	spells/effects/NativeHealthSpellLuaParityTest.cpp
	spells/effects/NativeStatusSpellLuaParityTest.cpp
	spells/effects/NativeLocationSpellLuaParityTest.cpp
	spells/effects/NativeUtilitySpellLuaParityTest.cpp
	spells/effects/NativeCreatureSpellLuaParityTest.cpp
	spells/effects/NativeHealthTransferSpellLuaParityTest.cpp
	spells/effects/NativeStructureSpellLuaParityTest.cpp
	spells/effects/NativeSummonWorldLuaParityTest.cpp
	spells/effects/NativeTransferWorldLuaParityTest.cpp
	map/MapScriptTest.cpp
	server/battles/NativeTimedSiblingOrderTest.cpp
	server/battles/NewHorizonsHexOfPainTest.cpp
	server/battles/NewHorizonsTemporaryMagicDispelTest.cpp
	server/battles/NewHorizonsWarMachinesPerksTest.cpp
	battleAI/MovementQueryCacheTest.cpp
	battleAI/MovementQueryAdmissionTest.cpp
	battleAI/MovementQueryProjectionTest.cpp
	battleAI/SelectedProbeScoreTest.cpp
	battleAI/CreaturePressureCachePreparationTest.cpp
)
target_include_directories(nhNativeMigrationTest PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR} ${GTestSrc} ${GTestSrc}/include
	${GMockSrc} ${GMockSrc}/include)
# Internal AI/Lua fixtures embed one core instance. Linking the shipping facade
# here as well would introduce a second set of library globals.
target_link_libraries(nhNativeMigrationTest PRIVATE
	gtest gmock vcmiMain vcmiservercommon vcmiLua TBB::tbb ${SYSTEM_LIBS})
foreach(migration_ai IN ITEMS EmptyAI BattleAI Nullkiller2 StupidAI MMAI)
	if(TARGET ${migration_ai})
		target_link_libraries(nhNativeMigrationTest PRIVATE ${migration_ai})
	endif()
endforeach()
vcmi_set_output_dir(nhNativeMigrationTest "")
enable_pch(nhNativeMigrationTest)
add_custom_command(TARGET nhNativeMigrationTest POST_BUILD
	COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/cmake_modules/create_link.cmake
		${CMAKE_SOURCE_DIR}/scripts ${CMAKE_BINARY_DIR}/bin/${CMAKE_CFG_INTDIR}/scripts)
add_test(NAME nhNativeMigrationTest COMMAND nhNativeMigrationTest)
set_tests_properties(nhNativeMigrationTest PROPERTIES
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	LABELS "native-rules;lua-oracle;integration"
	TIMEOUT 600)
