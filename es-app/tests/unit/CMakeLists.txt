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

# Two files of EmulationStation and no more: the pure text, and the string
# helpers it calls. Anything that needs a window, a font or a file does not
# belong in this binary -- see README.md.
add_executable(es-unit-tests
	${CMAKE_CURRENT_SOURCE_DIR}/CloudTextTests.cpp
	${ES_ROOT}/es-app/src/CloudText.cpp
	${ES_ROOT}/es-core/src/utils/StringUtil.cpp
)

target_include_directories(es-unit-tests PRIVATE
	${ES_ROOT}/es-app/src
	${ES_ROOT}/es-core/src
	${ES_ROOT}/external
)

set_target_properties(es-unit-tests PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
