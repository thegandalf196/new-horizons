# SPDX-License-Identifier: GPL-2.0-or-later
# Called only after linking the reporter-capable desktop client target.
if(NOT NH_BUG_REPORTER_TARGET STREQUAL "vcmiclient" OR NOT DEFINED NH_CLIENT
    OR NOT EXISTS "${NH_CLIENT}" OR IS_DIRECTORY "${NH_CLIENT}" OR IS_SYMLINK "${NH_CLIENT}")
    message(FATAL_ERROR "Reporter client target and a regular linked client are required")
endif()
file(SHA256 "${NH_CLIENT}" client_sha256)
get_filename_component(client_directory "${NH_CLIENT}" DIRECTORY)
set(marker "${client_directory}/new-horizons.features")
if(EXISTS "${marker}.tmp" OR IS_SYMLINK "${marker}.tmp")
    message(FATAL_ERROR "Client feature temporary path already exists")
endif()
file(WRITE "${marker}.tmp" "NH_CLIENT_FEATURES_V1\nBUG_REPORT_V1_SHA256=${client_sha256}\n")
file(RENAME "${marker}.tmp" "${marker}")
