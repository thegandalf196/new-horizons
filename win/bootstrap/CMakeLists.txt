# Can configure this OS-only pair independently; no engine/SDK build is needed.
cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	project(NewHorizonsEntries LANGUAGES CXX RC)
	include("../../cmake_modules/VersionDefinition.cmake")
endif()
get_filename_component(NH_ENTRY_SOURCE_ROOT "../.." ABSOLUTE)

function(nh_windows_entry target output description)
	add_executable(${target} WIN32 NewHorizonsBootstrap.cpp BootstrapArguments.h)
	set_target_properties(${target} PROPERTIES
		OUTPUT_NAME "${output}"
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bootstrap"
		MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
		DISABLE_PRECOMPILE_HEADERS ON)
	target_compile_features(${target} PRIVATE cxx_std_20)
	target_compile_definitions(${target} PRIVATE UNICODE _UNICODE)
	target_link_libraries(${target} PRIVATE user32 shell32)
	set(VCMI_FILE_DESCRIPTION "${description}")
	set(VCMI_ORIGINAL_FILENAME "${output}.exe")
	set(VCMI_PROJECT_NAME "New Horizons")
	set(VCMI_ICON_RESOURCE "IDI_ICON1 ICON \"${NH_ENTRY_SOURCE_ROOT}/clientapp/icons/new-horizons.ico\"")
	configure_file("${NH_ENTRY_SOURCE_ROOT}/win/WindowsFileInfo.rc.in"
		"${CMAKE_CURRENT_BINARY_DIR}/${target}VersionInfo.rc" @ONLY)
	target_sources(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${target}VersionInfo.rc")
	if(MINGW)
		target_link_options(${target} PRIVATE -municode -static -static-libgcc -static-libstdc++)
	endif()
	install(TARGETS ${target} RUNTIME DESTINATION bootstrap)
endfunction()

nh_windows_entry(nhWindowsBootstrap new-horizons "New Horizons portable entry point")
nh_windows_entry(nhWindowsSetup Setup "New Horizons original asset selection")
target_compile_definitions(nhWindowsSetup PRIVATE NH_SETUP_ONLY)
