# Focused Seth Royal Mummy recruitment legality and AI planning regressions.
# Reuses the facade and authoritative server archive; never embeds another core.
if(ENABLE_NULLKILLER2_AI)
    add_executable(nhSethRoyalRecruitmentTest EXCLUDE_FROM_ALL
        main.cpp CVcmiTestConfig.cpp
        mock/GameHandlerTestServer.cpp mock/TinyMapGameTest.cpp
        mock/mock_MapServiceTinyH3M.cpp mock/TinyH3MWriter.cpp mock/TinyH3MBuilder.cpp
        server/NewHorizonsSethRecruitmentTest.cpp
        nullkiller2/NewHorizonsSethRecruitmentAITest.cpp)
    target_include_directories(nhSethRoyalRecruitmentTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
    target_compile_features(nhSethRoyalRecruitmentTest PRIVATE cxx_std_20)
    target_link_libraries(nhSethRoyalRecruitmentTest PRIVATE
        gtest gmock vcmi vcmiservercommon ${SYSTEM_LIBS})
    vcmi_set_output_dir(nhSethRoyalRecruitmentTest "")
    enable_pch(nhSethRoyalRecruitmentTest)
    add_test(NAME nhSethRoyalRecruitmentTest COMMAND nhSethRoyalRecruitmentTest)
    set_tests_properties(nhSethRoyalRecruitmentTest PROPERTIES
        TIMEOUT 300 LABELS "hero;recruitment;ai;component")
endif()
