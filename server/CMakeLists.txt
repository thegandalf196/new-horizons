set(vcmiservercommon_SRCS
		StdInc.cpp

		battles/BattleActionProcessor.cpp
		battles/FrozenAttackResolution.cpp
		battles/BattleFlowProcessor.cpp
		battles/BattleProcessor.cpp
		battles/BattleResultProcessor.cpp
		battles/PhysicalAfflictionEffectPreparation.cpp

		queries/BattleQueries.cpp
		queries/CQuery.cpp
		queries/LuaScriptQuery.cpp
		queries/MapQueries.cpp
		queries/VisitQueries.cpp
		queries/QueriesProcessor.cpp

		processors/HeroPoolProcessor.cpp
		processors/NewTurnProcessor.cpp
		processors/PlayerMessageProcessor.cpp
		processors/TurnOrderProcessor.cpp

		CGameHandler.cpp
		NewHorizonsLearningMentor.cpp
		GlobalLobbyProcessor.cpp
		ServerSpellCastEnvironment.cpp
		CVCMIServer.cpp
		ClientPackPublication.cpp
		BattleUnitsChangedInput.cpp
		PreparedBattlePublication.cpp
		PreparedBattleWorldPublication.cpp
		PreparedBattleWorldPublicationFactory.cpp
		NetPacksServer.cpp
		NetPacksLobbyServer.cpp
		TurnTimerHandler.cpp
		TurnStartVisitScheduler.cpp
)

set(vcmiservercommon_HEADERS
		StdInc.h

		battles/BattleActionProcessor.h
		battles/FrozenAttackResolution.h
		battles/BattleFlowProcessor.h
		battles/BattleProcessor.h
		battles/BattleResultProcessor.h

		queries/BattleQueries.h
		queries/CQuery.h
		queries/LuaScriptQuery.h
		queries/MapQueries.h
		queries/VisitQueries.h
		queries/QueriesProcessor.h
		queries/IQueryStackListener.h

		processors/HeroPoolProcessor.h
		processors/NewTurnProcessor.h
		processors/PlayerMessageProcessor.h
		processors/TurnOrderProcessor.h

		CGameHandler.h
		GlobalLobbyProcessor.h
		IGameServer.h
		ServerSpellCastEnvironment.h
		CVCMIServer.h
		BattleUnitsChangedInput.h
		PreparedBattleWorldPublication.h
		LobbyNetPackVisitors.h
		ServerNetPackVisitors.h
		TurnTimerHandler.h
		TurnStartVisitScheduler.h
)

assign_source_group(${vcmiservercommon_SRCS} ${vcmiservercommon_HEADERS})

add_library(vcmiservercommon STATIC ${vcmiservercommon_SRCS} ${vcmiservercommon_HEADERS})
# Tests can embed AI object libraries directly, so server common must consume
# the core library interface without also pulling the facade and its objects.
set(vcmiservercommon_LIBS
	"$<TARGET_GENEX_EVAL:vcmiMain,$<TARGET_PROPERTY:vcmiMain,INTERFACE_LINK_LIBRARIES>>"
)

if(CMAKE_SYSTEM_NAME MATCHES FreeBSD OR HAIKU)
	list(APPEND vcmiservercommon_LIBS execinfo)
endif()

target_link_libraries(vcmiservercommon PRIVATE ${vcmiservercommon_LIBS} minizip::minizip)

target_include_directories(vcmiservercommon
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE $<TARGET_PROPERTY:vcmiMain,INTERFACE_INCLUDE_DIRECTORIES>
)

target_compile_definitions(vcmiservercommon
	PRIVATE $<TARGET_PROPERTY:vcmiMain,INTERFACE_COMPILE_DEFINITIONS>
)

if(WIN32)
	set_target_properties(vcmiservercommon
		PROPERTIES
			OUTPUT_NAME "VCMI_vcmiservercommon"
			PROJECT_LABEL "VCMI_vcmiservercommon"
	)
	if (MINGW)
		# Link Windows system libs: WinSock2 (ws2_32)
		target_link_libraries(vcmiservercommon PRIVATE ws2_32)
	endif()
endif()

vcmi_set_output_dir(vcmiservercommon "")
enable_pch(vcmiservercommon)

# Never define the test macro on the ordinary server archive or its PCH.
# Direct OBJECT linkage supplies every flow symbol to the integration executable;
# the ordinary archive's flow member is therefore not extracted there.
option(ENABLE_NH_CURE_CLIENT_INTEGRATION "Build retained real Cure callback integration client" OFF)
if(ENABLE_NH_CURE_CLIENT_INTEGRATION)
	add_library(nhCureSimulationFixture OBJECT EXCLUDE_FROM_ALL
		battles/BattleFlowProcessor.cpp tests/CureFixture.cpp
		tests/CureFixture.h tests/CureFixtureState.h)
	target_include_directories(nhCureSimulationFixture PRIVATE
		$<TARGET_PROPERTY:vcmiservercommon,INCLUDE_DIRECTORIES>)
	target_compile_definitions(nhCureSimulationFixture PRIVATE
		$<TARGET_PROPERTY:vcmiservercommon,COMPILE_DEFINITIONS> NH_CURE_SIMULATION_FIXTURE)
	target_compile_options(nhCureSimulationFixture PRIVATE
		$<TARGET_PROPERTY:vcmiservercommon,COMPILE_OPTIONS>)
	target_link_libraries(nhCureSimulationFixture PRIVATE vcmi minizip::minizip)
endif()
