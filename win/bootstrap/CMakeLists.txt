# Standalone OS-only entry point; never inherit the game's shared CRT or PCH.
add_executable(nhWindowsBootstrap WIN32 NewHorizonsBootstrap.cpp BootstrapArguments.h)
set_target_properties(nhWindowsBootstrap PROPERTIES
	OUTPUT_NAME "new-horizons"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bootstrap"
	MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
	DISABLE_PRECOMPILE_HEADERS ON)
target_compile_features(nhWindowsBootstrap PRIVATE cxx_std_20)
target_compile_definitions(nhWindowsBootstrap PRIVATE UNICODE _UNICODE)
target_link_libraries(nhWindowsBootstrap PRIVATE user32 shell32)
set(VCMI_FILE_DESCRIPTION "New Horizons portable entry point")
set(VCMI_ORIGINAL_FILENAME "new-horizons.exe")
set(VCMI_PROJECT_NAME "New Horizons")
set(VCMI_ICON_RESOURCE "IDI_ICON1 ICON \"${CMAKE_SOURCE_DIR}/clientapp/icons/new-horizons.ico\"")
configure_file("${CMAKE_SOURCE_DIR}/win/WindowsFileInfo.rc.in"
	"${CMAKE_CURRENT_BINARY_DIR}/BootstrapVersionInfo.rc" @ONLY)
target_sources(nhWindowsBootstrap PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/BootstrapVersionInfo.rc")
if(MINGW)
	target_link_options(nhWindowsBootstrap PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()
install(TARGETS nhWindowsBootstrap RUNTIME DESTINATION bootstrap)
