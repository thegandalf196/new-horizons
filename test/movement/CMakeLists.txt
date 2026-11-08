cmake_minimum_required(VERSION 3.20)
project(NHMovementValidation LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()
find_package(Threads REQUIRED)
find_package(GTest REQUIRED)
find_package(Boost CONFIG REQUIRED)

# An asset-free Linux component harness, not a substitute for the full game
# build or BattleAI fixture tests. Link production geometry, compilation and
# caching, with dead-section elimination for unrelated Unit.cpp methods.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
	message(FATAL_ERROR "Use the main project targets on other platforms; this standalone harness requires Linux ELF dead-section elimination")
endif()
get_filename_component(NH_ROOT "../.." ABSOLUTE)
option(NH_MOVEMENT_SANITIZERS "Enable address/undefined behavior sanitizers" OFF)
if(NH_MOVEMENT_SANITIZERS)
	add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
	add_link_options(-fsanitize=address,undefined)
endif()
add_compile_options(-ffunction-sections -fdata-sections)
add_link_options(-Wl,--gc-sections)
add_library(movementComponents STATIC
	${NH_ROOT}/lib/battle/BattleHex.cpp
	${NH_ROOT}/lib/battle/BattleHexArray.cpp
	${NH_ROOT}/lib/battle/Unit.cpp
	${NH_ROOT}/lib/battle/AccessibilityInfo.cpp
	${NH_ROOT}/lib/battle/ReachabilityInfo.cpp
	${NH_ROOT}/lib/battle/BattleMovementQuery.cpp
	${NH_ROOT}/lib/battle/BattleMovementDirect.cpp
	${NH_ROOT}/lib/battle/BattleMovementQueryCompilation.cpp
	${NH_ROOT}/AI/BattleAI/MovementQueryCache.cpp
	StandaloneLogger.cpp)
target_include_directories(movementComponents PUBLIC ${NH_ROOT} ${NH_ROOT}/include ${NH_ROOT}/lib)
target_link_libraries(movementComponents PUBLIC Boost::headers Threads::Threads)

add_executable(nhBattleMovementSolverTest ../battle/BattleMovementSolverTest.cpp)
add_executable(nhBattleMovementCompilationTest ../battle/BattleMovementCompilationTest.cpp)
target_link_libraries(nhBattleMovementCompilationTest PRIVATE movementComponents)
add_executable(nhMovementQueryCacheTest ../battleAI/MovementQueryCacheTest.cpp
	../battleAI/MovementQueryAdmissionTest.cpp)
target_link_libraries(nhMovementQueryCacheTest PRIVATE movementComponents GTest::gtest_main)
add_executable(nhMovementQueryBenchmark ${NH_ROOT}/tools/perf/MovementQueryBenchmark.cpp
	${NH_ROOT}/tools/perf/MovementBenchmarkReference.cpp)
target_link_libraries(nhMovementQueryBenchmark PRIVATE movementComponents)
add_executable(nhLuaDeclaredBonusTypesTest ../scripting/LuaDeclaredBonusTypesTest.cpp)
target_link_libraries(nhLuaDeclaredBonusTypesTest PRIVATE Boost::headers)
target_include_directories(nhLuaDeclaredBonusTypesTest PRIVATE ${NH_ROOT}/include)
add_executable(nhLuaBonusPresenceBenchmark ${NH_ROOT}/tools/perf/LuaBonusPresenceBenchmark.cpp)
target_link_libraries(nhLuaBonusPresenceBenchmark PRIVATE Boost::headers)
target_include_directories(nhLuaBonusPresenceBenchmark PRIVATE ${NH_ROOT}/include)
add_test(NAME nhLuaDeclaredBonusTypesTest COMMAND nhLuaDeclaredBonusTypesTest)
set_tests_properties(nhLuaDeclaredBonusTypesTest PROPERTIES TIMEOUT 60 LABELS "battle;damage;component")
foreach(target nhBattleMovementSolverTest nhBattleMovementCompilationTest nhMovementQueryCacheTest)
	add_test(NAME ${target} COMMAND ${target})
	set_tests_properties(${target} PROPERTIES TIMEOUT 60 LABELS "battle;movement;component")
endforeach()
