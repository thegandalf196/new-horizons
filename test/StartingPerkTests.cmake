# Focused captured-rule and hero-starting-state regressions. These use the same
# library bootstrap as vcmitest; they do not compile unrelated battle/AI tests.
add_executable(nhStartingPerkTest
	main.cpp CVcmiTestConfig.cpp
	hero/NewHorizonsPerkRulesTest.cpp
	hero/NewHorizonsPerkStateTest.cpp
	hero/SerializedPerkAdmissionTest.cpp
	hero/NewHorizonsValidatedPerkRulesTest.cpp
	hero/NewHorizonsPerkOperationTest.cpp
	hero/PerkInitializationReference.cpp
	hero/StartingPerkDefaultsTest.cpp
	map/NewHorizonsStartingDevelopmentTest.cpp
	mock/GameHandlerTestServer.cpp mock/TinyMapGameTest.cpp
	mock/mock_MapServiceTinyH3M.cpp mock/TinyH3MWriter.cpp mock/TinyH3MBuilder.cpp)
target_include_directories(nhStartingPerkTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(nhStartingPerkTest PRIVATE gtest gmock vcmi vcmiservercommon ${SYSTEM_LIBS})
vcmi_set_output_dir(nhStartingPerkTest "")
enable_pch(nhStartingPerkTest)
add_test(NAME nhStartingPerkTest COMMAND nhStartingPerkTest)
set_tests_properties(nhStartingPerkTest PROPERTIES TIMEOUT 300 LABELS "hero;initialization;captured-rules")
