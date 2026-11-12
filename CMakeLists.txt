# MonteCalc - CMake build (Windows with Visual Studio, also Linux/macOS)
#
# Windows (x64 Developer Command Prompt / "x64 Native Tools" or PowerShell after
# vcvars64.bat), with LibreOffice and the LibreOffice SDK installed:
#
#   set PATH=C:\Program Files\LibreOffice\program;%PATH%
#   cmake -S . -B build-win -G "NMake Makefiles" -DLO_DIR="C:/Program Files/LibreOffice"
#   cmake --build build-win
#   -> build-win/MonteCalc-windows_x86_64.oxt
#
# The program directory must be on PATH while building: the SDK tools
# (unoidl-write, cppumaker) load the LibreOffice DLLs from there.
cmake_minimum_required(VERSION 3.16)
project(MonteCalc CXX)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(WIN32)
  set(_lo_default "C:/Program Files/LibreOffice")
elseif(APPLE)
  set(_lo_default "/Applications/LibreOffice.app/Contents")
else()
  set(_lo_default "/usr/lib/libreoffice")
endif()
set(LO_DIR "${_lo_default}" CACHE PATH "LibreOffice installation directory")
set(LO_SDK_DIR "${LO_DIR}/sdk" CACHE PATH "LibreOffice SDK directory")
if(APPLE)
  set(LO_PROGRAM "${LO_DIR}/Frameworks" CACHE PATH "Directory with the UNO libraries")
  set(LO_TYPES "${LO_DIR}/Resources/types.rdb" "${LO_DIR}/Resources/types/offapi.rdb")
else()
  set(LO_PROGRAM "${LO_DIR}/program" CACHE PATH "Directory with the UNO libraries")
  set(LO_TYPES "${LO_PROGRAM}/types.rdb" "${LO_PROGRAM}/types/offapi.rdb")
endif()
foreach(f ${LO_TYPES})
  if(NOT EXISTS "${f}")
    message(FATAL_ERROR "LibreOffice type library not found: ${f} (set LO_DIR)")
  endif()
endforeach()
find_program(UNOIDL_WRITE unoidl-write PATHS "${LO_SDK_DIR}/bin" "${LO_PROGRAM}" NO_DEFAULT_PATH REQUIRED)
find_program(CPPUMAKER cppumaker PATHS "${LO_SDK_DIR}/bin" "${LO_PROGRAM}" NO_DEFAULT_PATH REQUIRED)

set(GEN "${CMAKE_BINARY_DIR}/gen")
set(INC "${CMAKE_BINARY_DIR}/inc")
file(MAKE_DIRECTORY "${GEN}" "${INC}")

# 1. generated function table, IDL and type library
add_custom_command(
  OUTPUT "${GEN}/XMonteCalc.idl" "${GEN}/functions_decl.inc" "${GEN}/functions_impl.inc" "${GEN}/functions_meta.inc"
  COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/gen_functions.py" "${GEN}"
  DEPENDS "${CMAKE_SOURCE_DIR}/tools/gen_functions.py"
  COMMENT "Generating the function table")
add_custom_command(
  OUTPUT "${CMAKE_BINARY_DIR}/montecalc.rdb"
  COMMAND "${UNOIDL_WRITE}" ${LO_TYPES} "${GEN}/XMonteCalc.idl" "${CMAKE_BINARY_DIR}/montecalc.rdb"
  DEPENDS "${GEN}/XMonteCalc.idl"
  COMMENT "Writing the type library")
# 2. C++ headers for the office API and for the add-in interface (absolute -O path required)
set(_extypes)
foreach(f ${LO_TYPES})
  list(APPEND _extypes "-X${f}")
endforeach()
add_custom_command(
  OUTPUT "${INC}/office.stamp"
  COMMAND "${CPPUMAKER}" -Gc "-O${INC}" ${LO_TYPES}
  COMMAND ${CMAKE_COMMAND} -E touch "${INC}/office.stamp"
  COMMENT "Generating the office API headers (once)")
add_custom_command(
  OUTPUT "${INC}/own.stamp"
  COMMAND "${CPPUMAKER}" -Gc "-O${INC}" -Torg.montecalc.XMonteCalc ${_extypes} "${CMAKE_BINARY_DIR}/montecalc.rdb"
  COMMAND ${CMAKE_COMMAND} -E touch "${INC}/own.stamp"
  DEPENDS "${CMAKE_BINARY_DIR}/montecalc.rdb" "${INC}/office.stamp"
  COMMENT "Generating the add-in interface headers")
add_custom_target(montecalc_headers DEPENDS "${INC}/own.stamp" "${GEN}/functions_impl.inc" "${GEN}/functions_meta.inc")

# 3. the component library
file(GLOB CORE_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/core/*.cpp")
file(GLOB UNO_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/uno/*.cpp")
add_library(montecalc SHARED ${CORE_SRC} ${UNO_SRC})
add_dependencies(montecalc montecalc_headers)
set_target_properties(montecalc PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_VISIBILITY_PRESET hidden)
target_include_directories(montecalc PRIVATE "${INC}" "${GEN}" "${LO_SDK_DIR}/include")

if(MSVC)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_cppu_env mscx)
  else()
    set(_cppu_env msci)
  endif()
  target_compile_definitions(montecalc PRIVATE WNT CPPU_ENV=${_cppu_env} _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(montecalc PRIVATE /EHsc /utf-8 /permissive- /Zc:__cplusplus /W3 /wd4251 /wd4275 /bigobj /Zi)
  # symbols (.pdb next to the build) for thread stacks when a test hangs; the release stays optimized
  target_link_options(montecalc PRIVATE /DEBUG /OPT:REF /OPT:ICF)
  set_property(TARGET montecalc PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")  # LibreOffice uses /MD
  find_library(LO_CPPU icppu PATHS "${LO_SDK_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
  find_library(LO_CPPUHELPER icppuhelper PATHS "${LO_SDK_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
  find_library(LO_SAL isal PATHS "${LO_SDK_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
  target_link_libraries(montecalc PRIVATE ${LO_CPPUHELPER} ${LO_CPPU} ${LO_SAL} winhttp)
  set_target_properties(montecalc PROPERTIES PREFIX "" OUTPUT_NAME montecalc)  # montecalc.dll
  set(OXT_PLATFORM windows_x86_64)
else()
  if(APPLE)
    target_compile_definitions(montecalc PRIVATE MACOSX UNX CPPU_ENV=gcc3)
    set(OXT_PLATFORM macos_${CMAKE_SYSTEM_PROCESSOR})
    string(REPLACE "arm64" "aarch64" OXT_PLATFORM "${OXT_PLATFORM}")
  else()
    target_compile_definitions(montecalc PRIVATE LINUX UNX CPPU_ENV=gcc3)
    set(OXT_PLATFORM linux_x86_64)
  endif()
  target_compile_options(montecalc PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
  target_link_directories(montecalc PRIVATE "${LO_PROGRAM}" "${LO_SDK_DIR}/lib")
  target_link_libraries(montecalc PRIVATE uno_cppuhelpergcc3 uno_cppu uno_sal ${CMAKE_DL_LIBS} pthread)
endif()

# 4. the extension package
file(GLOB_RECURSE EXT_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/ext/*")
set(OXT "${CMAKE_BINARY_DIR}/MonteCalc-${OXT_PLATFORM}.oxt")
add_custom_command(
  OUTPUT "${OXT}"
  COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/package_oxt.py" ${OXT_PLATFORM} "$<TARGET_FILE:montecalc>"
          "${CMAKE_BINARY_DIR}/montecalc.rdb" "${OXT}"
  DEPENDS montecalc "${CMAKE_BINARY_DIR}/montecalc.rdb" "${CMAKE_SOURCE_DIR}/tools/package_oxt.py" ${EXT_FILES}
  COMMENT "Packaging ${OXT}")
add_custom_target(oxt ALL DEPENDS "${OXT}")

# 5. core unit tests (no LibreOffice needed)
enable_testing()
add_executable(core_tests "${CMAKE_SOURCE_DIR}/tests/core_tests.cpp" ${CORE_SRC})
set_target_properties(core_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if(MSVC)
  target_compile_options(core_tests PRIVATE /EHsc /utf-8 /permissive- /bigobj)
  target_compile_definitions(core_tests PRIVATE _USE_MATH_DEFINES NOMINMAX)
endif()
add_test(NAME core_tests COMMAND core_tests)
