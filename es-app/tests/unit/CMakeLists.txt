# The unit tests for EmulationStation's pure code (#120 box 4).
#
# Standalone on purpose: this file is a project of its own when it is the
# top of the tree, so the tests build with the host compiler and no SDL,
# no freetype, no curl, and no cross toolchain --
#
#   cmake -S es-app/tests/unit -B build-tests && cmake --build build-tests
#   ./build-tests/es-unit-tests
#
# and it is also picked up by the top-level CMakeLists.txt under
# -DES_BUILD_TESTS=ON, which defaults OFF so the image build never sees it.

cmake_minimum_required(VERSION 3.10)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	project("es-unit-tests" CXX)
endif()

get_filename_component(ES_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)

# Ten files of EmulationStation and no more: the pure text, the offline
# proxy's answers, the string helpers they call (with the credential mask
# every logged command line goes through, fork #177), the token check's
# retry schedule, the index's rule for what a game is owed, the time of day
# a save's date carries (fork #195), the Wi-Fi rows' reading of wifictl
# (fork #191), and the offline proxy's address with the test for a URL on it
# (fork #199), the save state manager's job queue (fork #205 / #206: one job
# at a time, a file pending deletion hides its tile, a copy hides nothing) -- plus the
# log's header-only rules (es-core/src/LogPolicy.h,
# fork #178). Anything that needs a window, a font or a file does not belong
# in this binary -- see README.md.
add_executable(es-unit-tests
	${CMAKE_CURRENT_SOURCE_DIR}/CloudTextTests.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/CheevosRetryTests.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/OfflineAchievementsTextTests.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/CheevosIndexTests.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/TimeTextTests.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/MaskSecretsTests.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/LogPolicyTests.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/WifiTextTests.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/OfflineProxyUrlTests.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/SaveStateJobQueueTests.cpp
	${ES_ROOT}/es-app/src/CloudText.cpp
	${ES_ROOT}/es-app/src/CheevosRetry.cpp
	${ES_ROOT}/es-app/src/OfflineAchievementsText.cpp
	${ES_ROOT}/es-app/src/CheevosIndex.cpp
	${ES_ROOT}/es-app/src/WifiText.cpp
	${ES_ROOT}/es-app/src/SaveStateJobQueue.cpp
	${ES_ROOT}/es-core/src/utils/StringUtil.cpp
	${ES_ROOT}/es-core/src/utils/TimeText.cpp
	${ES_ROOT}/es-core/src/utils/OfflineProxyUrl.cpp
)

# OfflineAchievementsText reads JSON with rapidjson, header-only, which the
# image build takes from the toolchain's sysroot. On a host without it, name
# a copy: -DRAPIDJSON_INCLUDE_DIR=<build root>/build/rapidjson-*/include. A
# missing library stops the configure here rather than building a binary
# that silently tests less (engineering-practices.md, guards fail closed).
find_path(RAPIDJSON_INCLUDE_DIR rapidjson/document.h)
if(NOT RAPIDJSON_INCLUDE_DIR)
	message(FATAL_ERROR "rapidjson/document.h was not found; pass -DRAPIDJSON_INCLUDE_DIR=<dir holding rapidjson/>")
endif()

target_include_directories(es-unit-tests PRIVATE
	${ES_ROOT}/es-app/src
	${ES_ROOT}/es-core/src
	${ES_ROOT}/external
	${RAPIDJSON_INCLUDE_DIR}
)

set_target_properties(es-unit-tests PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
