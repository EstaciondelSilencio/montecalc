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
set(GENERATED "${GEN}/xll_exports.inc" "${GEN}/montecalc_version.h" "${GEN}/xll_register.inc" "${GEN}/functions_decl_xll.inc" "${GEN}/functions_impl_xll.inc"
    "${GEN}/montecalc_xll.def")
add_custom_command(
  OUTPUT ${GENERATED}
  COMMAND Python3::Interpreter "${ROOT}/tools/gen_functions.py" "${GEN}"
  DEPENDS "${ROOT}/tools/gen_functions.py" "${ROOT}/tools/gen_ui.py"
  COMMENT "Generating the function table")

file(GLOB CORE_SRC CONFIGURE_DEPENDS "${ROOT}/core/*.cpp")
set(XLL_SRC ${CORE_SRC} "${ROOT}/uno/addin.cpp" "${ROOT}/uno/i18n.cpp" "${ROOT}/uno/runloop.cpp" "${ROOT}/xll/xll.cpp" "${ROOT}/xll/xlformula.cpp")
# ribbon, windows and commands (Windows only; shared with the LibreOffice extension)
set(XLL_WIN_SRC
    "${ROOT}/uno/model.cpp" "${ROOT}/uno/engine.cpp" "${ROOT}/uno/windows.cpp" "${ROOT}/uno/influence.cpp" "${ROOT}/uno/deps.cpp"
    "${ROOT}/uno/charts.cpp" "${ROOT}/uno/painter.cpp" "${ROOT}/uno/report.cpp" "${ROOT}/uno/mentor.cpp" "${ROOT}/uno/http.cpp"
    "${ROOT}/xll/com.cpp" "${ROOT}/xll/model_xl.cpp" "${ROOT}/xll/win32ui.cpp" "${ROOT}/xll/ribbon.cpp")
set(XLL_WIN_LIBS ole32 oleaut32 uuid gdiplus oleacc shlwapi winhttp gdi32 user32 advapi32)

if(WIN32)
  enable_language(RC)
  # resources: the MC FLO logo (icon 1) and the version information
  set(XLL_RC "${ROOT}/xll/montecalc.rc")
  set_source_files_properties("${XLL_RC}" PROPERTIES OBJECT_DEPENDS "${GEN}/montecalc_version.h;${ROOT}/xll/montecalc.ico")
  add_library(montecalc_xll SHARED ${XLL_SRC} ${XLL_WIN_SRC} "${XLL_RC}" "${GEN}/montecalc_xll.def" ${GENERATED})
  set_target_properties(montecalc_xll PROPERTIES OUTPUT_NAME MonteCalc PREFIX "" SUFFIX ".xll")
  target_link_libraries(montecalc_xll PRIVATE ${XLL_WIN_LIBS})
  # ribbon and window test (no Excel needed): xll_win_test ribbon out.xml / xll_win_test window results 3
  add_executable(xll_win_test ${XLL_SRC} ${XLL_WIN_SRC} "${XLL_RC}" "${ROOT}/tests/xll_wine_test.cpp" ${GENERATED})
  target_link_libraries(xll_win_test PRIVATE ${XLL_WIN_LIBS})
  target_compile_definitions(xll_win_test PRIVATE MONTECALC_XLL _CRT_SECURE_NO_WARNINGS NOMINMAX)
  target_include_directories(xll_win_test PRIVATE "${GEN}" "${ROOT}/uno" "${ROOT}/xll")
  if(MSVC)
    target_compile_options(xll_win_test PRIVATE /EHsc /utf-8 /permissive- /bigobj /W3 /wd4244 /wd4267)
  endif()
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
