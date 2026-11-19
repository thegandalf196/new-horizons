# SPDX-License-Identifier: GPL-2.0-or-later
# Append a bounded target-local profile after the existing StdInc preamble.
option(ENABLE_NH_HOT_HEADER_PCH "Experiment with target-local hot headers in the existing PCH" OFF)
set(NH_HOT_HEADER_PCH_PROFILE "hot-headers-v1" CACHE INTERNAL "Experimental PCH header profile" FORCE)
get_filename_component(_NH_HOT_HEADER_PCH_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(ENABLE_NH_HOT_HEADER_PCH AND NOT ENABLE_PCH)
	message(FATAL_ERROR "ENABLE_NH_HOT_HEADER_PCH requires ENABLE_PCH=ON")
endif()
foreach(target vcmiMain BattleAI vcmiclientcommon)
	set(NH_HOT_HEADER_PCH_HEADERS_${target} "" CACHE INTERNAL "Selected relative hot PCH headers" FORCE)
endforeach()

function(nh_enable_hot_header_pch target)
	if(NOT ENABLE_NH_HOT_HEADER_PCH)
		return()
	endif()
	if(NOT target MATCHES "^(vcmiMain|BattleAI|vcmiclientcommon)$")
		return()
	endif()
	set(headers
		lib/mapObjects/CGHeroInstance.h
		lib/GameLibrary.h
		lib/battle/CBattleInfoCallback.h
		lib/CStack.h)
	set(NH_HOT_HEADER_PCH_HEADERS_${target} "${headers}" CACHE INTERNAL "Selected relative hot PCH headers" FORCE)
	foreach(header IN LISTS headers)
		target_precompile_headers(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${_NH_HOT_HEADER_PCH_ROOT}/${header}>")
	endforeach()
endfunction()
