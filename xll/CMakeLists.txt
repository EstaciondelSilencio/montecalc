# MonteCalc for Microsoft Excel - XLL build (Windows, Visual Studio; no LibreOffice needed)
#
#   "x64 Native Tools Command Prompt" (or x86 for 32-bit Excel):
#   cmake -S xll -B build-xll -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-xll
#   -> build-xll/MonteCalc.xll
#
# On Linux the same sources build the test program tests/xll_test.cpp (mock Excel).
cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0091 NEW)   # static MSVC runtime: the .xll needs no Visual C++ redistributable
project(MonteCalcXLL CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(GEN "${CMAKE_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${GEN}")
set(GENERATED "${GEN}/xll_exports.inc" "${GEN}/xll_register.inc" "${GEN}/functions_decl_xll.inc" "${GEN}/functions_impl_xll.inc"
    "${GEN}/montecalc_xll.def")
add_custom_command(
  OUTPUT ${GENERATED}
  COMMAND Python3::Interpreter "${ROOT}/tools/gen_functions.py" "${GEN}"
  DEPENDS "${ROOT}/tools/gen_functions.py" "${ROOT}/tools/gen_ui.py"
  COMMENT "Generating the function table")

file(GLOB CORE_SRC CONFIGURE_DEPENDS "${ROOT}/core/*.cpp")
set(XLL_SRC ${CORE_SRC} "${ROOT}/uno/addin.cpp" "${ROOT}/uno/i18n.cpp" "${ROOT}/xll/xll.cpp")

if(WIN32)
  add_library(montecalc_xll SHARED ${XLL_SRC} "${GEN}/montecalc_xll.def" ${GENERATED})
  set_target_properties(montecalc_xll PROPERTIES OUTPUT_NAME MonteCalc PREFIX "" SUFFIX ".xll")
  set(TARGET montecalc_xll)
else()
  add_executable(xll_test ${XLL_SRC} "${ROOT}/tests/xll_test.cpp" ${GENERATED})
  set(TARGET xll_test)
  enable_testing()
  add_test(NAME xll_test COMMAND xll_test)
endif()
target_compile_definitions(${TARGET} PRIVATE MONTECALC_XLL _CRT_SECURE_NO_WARNINGS NOMINMAX)
target_include_directories(${TARGET} PRIVATE "${GEN}" "${ROOT}/uno" "${ROOT}/xll")
if(MSVC)
  target_compile_options(${TARGET} PRIVATE /EHsc /utf-8 /permissive- /bigobj /W3 /wd4244 /wd4267)
endif()
