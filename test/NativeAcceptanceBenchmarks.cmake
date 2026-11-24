# Explicitly built workloads; resource loading and execution never run at build time.
option(ENABLE_NH_NATIVE_BENCHMARKS "Build opt-in native performance workloads" OFF)
if(NOT ENABLE_NH_NATIVE_BENCHMARKS)
	return()
endif()

find_package(ZLIB REQUIRED)
add_executable(nhNativeAcceptanceBenchmarks EXCLUDE_FROM_ALL
	main.cpp
	CVcmiTestConfig.cpp
	mock/mock_MapService.cpp
	mock/mock_MapServiceTinyH3M.cpp
	mock/TinyMapGameTest.cpp
	mock/TinyH3MWriter.cpp
	mock/TinyH3MBuilder.cpp
	mock/GameHandlerTestServer.cpp
	mock/mock_IGameEventCallback.cpp
	server/battles/BattleTestFixture.cpp
	battleAI/NativeRosterDecisionProfileTest.cpp
	benchmark/NativeEncounterBenchmarkTest.cpp
	benchmark/NativeWorldBenchmarkTest.cpp
	benchmark/NativeBenchmarkEvidence.h
	benchmark/NativeEncounterBenchmarkSupport.h
	benchmark/NativeAmbushEncounterObserver.h
)
target_include_directories(nhNativeAcceptanceBenchmarks PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}")
target_compile_features(nhNativeAcceptanceBenchmarks PRIVATE cxx_std_20)
target_link_libraries(nhNativeAcceptanceBenchmarks PRIVATE
	gtest gmock vcmi vcmiservercommon TBB::tbb Threads::Threads ZLIB::ZLIB ${SYSTEM_LIBS})
vcmi_set_output_dir(nhNativeAcceptanceBenchmarks "")
enable_pch(nhNativeAcceptanceBenchmarks)
