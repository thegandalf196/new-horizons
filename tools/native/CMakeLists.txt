set(save_parity_sources
	SaveParityInput.cpp
	SaveParityEvidence.cpp
	SaveParityCoverage.cpp
)
set(save_parity_headers SaveParityInput.h SaveParityEvidence.h)
add_executable(nhSaveParity SaveParityMain.cpp ${save_parity_sources} ${save_parity_headers})
target_link_libraries(nhSaveParity PRIVATE vcmi vcmiservercommon)
vcmi_set_output_dir(nhSaveParity "")
# No install rule: local, opt-in, read-only inspection of private save evidence.

# Reuses the admitted offline loader; no gameplay execution or installation.
add_executable(nhBattleActions BattleActionExportMain.cpp BattleActionExport.cpp
	BattleActionExportPackets.cpp BattleActionExport.h SaveParityInput.cpp SaveParityInput.h)
target_link_libraries(nhBattleActions PRIVATE vcmi vcmiservercommon)
target_compile_definitions(nhBattleActions PRIVATE "NH_ACTION_EXPORT_BUILD=\"${VCMI_VERSION_FULL}\"")
vcmi_set_output_dir(nhBattleActions "")
