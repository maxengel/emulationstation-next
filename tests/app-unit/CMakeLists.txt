# Tests for EmulationStation application code outside es-app/tests/unit
# (audit #307 stream E2), built with the host compiler and AddressSanitizer,
# no SDL, no cross toolchain:
#
#   cmake -S tests/app-unit -B build-app-unit && cmake --build build-app-unit
#   ./build-app-unit/app-unit-tests && ./build-app-unit/proxycards-tests
#
# app-unit-tests: header-only pure code the application calls (the journey
# tiers a settings-first restore carries across its restart; the folder
# merge a rescan uses to keep every holder's FileData alive).
#
# proxycards-tests: the shipped es-app/src/ProxyCards.cpp itself, compiled
# against the small doubles under fakes/ -- the window, the card, the cloud
# sync, the settings and the proxy's answers -- so its threads, waits and
# outcomes run as they do on a device. The source is copied into the build
# tree at configure time so its quoted includes find the doubles and not
# the real headers beside it; a change to the source re-runs the copy.

cmake_minimum_required(VERSION 3.10)
project("es-app-unit-e2" CXX)

get_filename_component(ES_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-g -fno-omit-frame-pointer -fsanitize=address,undefined)
add_link_options(-fsanitize=address,undefined)
find_package(Threads REQUIRED)

add_executable(app-unit-tests
	${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/JourneyTiersTests.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/FolderMergeTests.cpp
)
target_include_directories(app-unit-tests PRIVATE
	${ES_ROOT}/es-app/src
	${ES_ROOT}/es-core/src
	${ES_ROOT}/external
)

configure_file(${ES_ROOT}/es-app/src/ProxyCards.cpp ${CMAKE_BINARY_DIR}/under-test/ProxyCards.cpp COPYONLY)
add_executable(proxycards-tests
	${CMAKE_CURRENT_SOURCE_DIR}/ProxyCardsTests.cpp
	${CMAKE_BINARY_DIR}/under-test/ProxyCards.cpp
	${ES_ROOT}/es-app/src/CloudText.cpp
	${ES_ROOT}/es-core/src/utils/StringUtil.cpp
)
target_include_directories(proxycards-tests BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/fakes)
target_include_directories(proxycards-tests PRIVATE
	${ES_ROOT}/es-app/src
	${ES_ROOT}/es-core/src
	${ES_ROOT}/external
)
target_link_libraries(proxycards-tests Threads::Threads)
