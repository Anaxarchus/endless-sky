# Builds the Dado mod harness into the game. Included from the top-level
# CMakeLists.txt when ES_DADO_MODS is on; needs the Dado toolchain (`dado` and
# `dadoc` on PATH, or DADO_EXECUTABLE / DADOC_EXECUTABLE set). See modding/README.md.

enable_language(C)

# IDEs often run CMake without the shell's PATH, so also look where the installer puts them.
set(_DADO_HINTS "$ENV{HOME}/.local/bin" "$ENV{HOME}/dado" "$ENV{LOCALAPPDATA}/dado/bin")
find_program(DADO_EXECUTABLE dado HINTS ${_DADO_HINTS} REQUIRED)
find_program(DADOC_EXECUTABLE dadoc HINTS ${_DADO_HINTS} REQUIRED)
# The tool scripts run `dado` and `dadoc` by name, so they get this folder on their PATH.
get_filename_component(DADO_BIN_DIR "${DADO_EXECUTABLE}" DIRECTORY)

# Where the toolchain keeps its collections and the script engine's build script.
execute_process(COMMAND "${DADO_EXECUTABLE}" where --no-update-check
	OUTPUT_VARIABLE DADO_WHERE RESULT_VARIABLE DADO_WHERE_RESULT)
if(NOT DADO_WHERE_RESULT EQUAL 0)
	message(FATAL_ERROR "`dado where` failed; run `dado doctor`.")
endif()
string(REGEX MATCH "(^|\n)root=([^\n]*)" _ "${DADO_WHERE}")
set(DADO_ROOT "${CMAKE_MATCH_2}")
string(REGEX MATCH "(^|\n)collections=([^\n]*)" _ "${DADO_WHERE}")
set(DADO_COLLECTIONS "${CMAKE_MATCH_2}")

# dadoc answers #OS and #ARCH from the target triple, and the script engine is
# built for it. Take it from the C compiler, unless a macOS build names another
# architecture (the "MacOS" preset builds x86_64 on any Mac).
execute_process(COMMAND "${CMAKE_C_COMPILER}" -dumpmachine
	OUTPUT_VARIABLE DADO_TRIPLE OUTPUT_STRIP_TRAILING_WHITESPACE)
set(DADO_QJS_TRIPLE_ARGS "")
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
	list(LENGTH CMAKE_OSX_ARCHITECTURES _dado_arch_count)
	if(_dado_arch_count GREATER 1)
		message(FATAL_ERROR "ES_DADO_MODS builds for one architecture at a time; CMAKE_OSX_ARCHITECTURES is \"${CMAKE_OSX_ARCHITECTURES}\".")
	endif()
	set(DADO_TRIPLE "${CMAKE_OSX_ARCHITECTURES}-apple-darwin")
	set(DADO_QJS_TRIPLE_ARGS --triple "${DADO_TRIPLE}" --compile-only)
endif()
message(STATUS "Dado: target ${DADO_TRIPLE}")

# The script engine (QuickJS-ng with Dado's patches), built once per compiler and cached.
set(DADO_QJS_CACHE "$ENV{HOME}/.cache/dado-quickjs" CACHE PATH "Where Dado's script engine builds are cached.")
message(STATUS "Dado: building the script engine with ${CMAKE_C_COMPILER} (cached after the first time)")
execute_process(
	COMMAND ${CMAKE_COMMAND} -E env "DADO_QJS_CACHE=${DADO_QJS_CACHE}"
		sh "${DADO_ROOT}/tools/quickjs/build.sh" --cc "${CMAKE_C_COMPILER}" ${DADO_QJS_TRIPLE_ARGS}
	OUTPUT_VARIABLE DADO_QJS_OUT ERROR_VARIABLE DADO_QJS_ERR RESULT_VARIABLE DADO_QJS_RESULT)
string(STRIP "${DADO_QJS_OUT}" DADO_QJS_OUT)
string(REGEX REPLACE ".*\n" "" DADO_QJS_DIR "${DADO_QJS_OUT}")
if(NOT DADO_QJS_RESULT EQUAL 0 OR NOT EXISTS "${DADO_QJS_DIR}/libquickjs.a")
	message(FATAL_ERROR "Dado's script engine did not build:\n${DADO_QJS_ERR}")
endif()

# The harness package, transpiled to one C file and its header.
set(DADO_HARNESS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/modding/harness")
set(DADO_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/dado")
file(GLOB DADO_HARNESS_SOURCES CONFIGURE_DEPENDS "${DADO_HARNESS_DIR}/*")
add_custom_command(
	OUTPUT "${DADO_GENERATED}/esmod.c" "${DADO_GENERATED}/esmod.h"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${DADO_GENERATED}"
	COMMAND "${DADOC_EXECUTABLE}" --collections=${DADO_COLLECTIONS} --package --library
		--target=${DADO_TRIPLE} --emit=c -o "${DADO_GENERATED}/esmod.c" "${DADO_HARNESS_DIR}/entry.dado"
	COMMAND ${CMAKE_COMMAND} -DFILE=${DADO_GENERATED}/esmod.c -P "${CMAKE_CURRENT_SOURCE_DIR}/modding/tools/fix-max-align.cmake"
	COMMAND "${DADOC_EXECUTABLE}" --collections=${DADO_COLLECTIONS} --package --library
		--target=${DADO_TRIPLE} --emit=h -o "${DADO_GENERATED}/esmod.h" "${DADO_HARNESS_DIR}/entry.dado"
	DEPENDS ${DADO_HARNESS_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/modding/tools/fix-max-align.cmake"
	COMMENT "Dado: transpiling the mod harness"
	VERBATIM)

add_library(EsDadoHarness STATIC "${DADO_GENERATED}/esmod.c")
set_target_properties(EsDadoHarness PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_definitions(EsDadoHarness PRIVATE _GNU_SOURCE)
# Generated code: the game's warning flags are not for it.
target_compile_options(EsDadoHarness PRIVATE -funsigned-char -w)
target_include_directories(EsDadoHarness
	PRIVATE "${DADO_QJS_DIR}"
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/modding/include" "${DADO_GENERATED}")
target_link_libraries(EsDadoHarness PUBLIC "${DADO_QJS_DIR}/libquickjs.a" Threads::Threads ${CMAKE_DL_LIBS})
if(NOT APPLE AND NOT WIN32)
	target_link_libraries(EsDadoHarness PUBLIC m)
endif()

# Through ExternalLibraries, so the unit tests, which link EndlessSkyLib's objects
# rather than the library, get the harness too.
target_link_libraries(ExternalLibraries INTERFACE EsDadoHarness)
target_compile_definitions(EndlessSkyLib PUBLIC ES_DADO_MODS)

# Targets for working on mods. Each runs modding/tools/dado-mods.sh, which needs
# esbuild (on PATH, or through npx) to bundle a mod.
set(DADO_MODS_ENV ${CMAKE_COMMAND} -E env "CC=${CMAKE_C_COMPILER}" "CXX=${CMAKE_CXX_COMPILER}"
	"DADO_QJS_CACHE=${DADO_QJS_CACHE}" "PATH=${DADO_BIN_DIR}:$ENV{PATH}")
set(DADO_MODS_SH sh "${CMAKE_CURRENT_SOURCE_DIR}/modding/tools/dado-mods.sh")

# The examples as a plugin. A game run from a build folder also loads the
# repository's (gitignored) plugins folder, so this is all it takes to try them.
set(DADO_EXAMPLE_PLUGIN "${CMAKE_CURRENT_SOURCE_DIR}/plugins/Dado Example" CACHE PATH
	"Where the dado_example_plugin target assembles the example mods as a plugin.")
add_custom_target(dado_example_plugin
	COMMAND ${DADO_MODS_ENV} ${DADO_MODS_SH} plugin "${DADO_EXAMPLE_PLUGIN}"
	COMMENT "Dado: building the example mods into ${DADO_EXAMPLE_PLUGIN}"
	VERBATIM USES_TERMINAL)

# Rebuild the committed .js of the examples and of the integration tests' mods.
add_custom_target(dado_mods
	COMMAND ${DADO_MODS_ENV} ${DADO_MODS_SH} examples
	COMMENT "Dado: rebuilding the example and test mods"
	VERBATIM USES_TERMINAL)

# Regenerate the SDK's Game declarations from the harness.
add_custom_target(dado_sdk
	COMMAND ${DADO_MODS_ENV} ${DADO_MODS_SH} sdk
	COMMENT "Dado: regenerating modding/sdk/es/game.dado"
	VERBATIM USES_TERMINAL)

# The harness against a fake game, with every example and test mod.
add_custom_target(dado_mods_test
	COMMAND ${DADO_MODS_ENV} ${DADO_MODS_SH} test
	VERBATIM USES_TERMINAL)
if(BUILD_TESTING)
	add_test(NAME "Dado mod harness" COMMAND ${DADO_MODS_ENV} ${DADO_MODS_SH} test)
	set_tests_properties("Dado mod harness" PROPERTIES LABELS dado TIMEOUT 600)
endif()
