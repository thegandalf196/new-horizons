# Replay chapter/provenance regressions with the normal library bootstrap and TinyH3M fixture.
# Links the facade and authoritative server archive without embedding another core.
add_executable(nhReplayDayZeroMergeTest EXCLUDE_FROM_ALL
    main.cpp CVcmiTestConfig.cpp
    mock/mock_MapService.cpp mock/mock_MapServiceTinyH3M.cpp
    mock/TinyMapGameTest.cpp mock/TinyH3MWriter.cpp mock/TinyH3MBuilder.cpp
    server/battles/BattleTestFixture.cpp
    ../client/replay/ReplayPlanner.cpp
    serializer/ReplayDayZeroMergeTest.cpp
    serializer/ReplaySnapshotVersionTest.cpp
    serializer/ReplayPayloadVersionTest.cpp
    battle/PreparedReplayAppendTest.cpp
    battle/PreparedReplayBatchTest.cpp
    client/ReplayPlannerPayloadTest.cpp
)
target_include_directories(nhReplayDayZeroMergeTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(nhReplayDayZeroMergeTest PRIVATE cxx_std_20)
target_link_libraries(nhReplayDayZeroMergeTest PRIVATE gtest gmock vcmiservercommon vcmi ${SYSTEM_LIBS})
vcmi_set_output_dir(nhReplayDayZeroMergeTest "")
add_test(NAME nhReplayDayZeroMergeTest COMMAND nhReplayDayZeroMergeTest
    --gtest_filter=ReplayDayZeroMergeTest.*:ReplaySnapshotVersionTest.*:ReplayPayloadVersionTest.*:PreparedReplayAppendTest.*:PreparedReplayBatchTest.*:ReplayPlannerPayloadTest.*)
set_tests_properties(nhReplayDayZeroMergeTest PROPERTIES TIMEOUT 300 LABELS "replay;serialization;component")
