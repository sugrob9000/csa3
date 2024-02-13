cmake_minimum_required(VERSION 3.27)
project(csa-lab3 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

option(ENABLE_CLANG_TIDY ON)

if(ENABLE_CLANG_TIDY)
  find_program(clang-tidy clang-tidy REQUIRED)
  message(STATUS "Found clang-tidy: ${clang-tidy}")

  file(STRINGS "clang-tidy-checks" checks)
  list(JOIN checks "," checks)
  message(STATUS "${checks}")

  set(CMAKE_CXX_CLANG_TIDY
    ${clang-tidy}
    -header-filter=${CMAKE_SOURCE_DIR}
    -checks=${checks}
    -warnings-as-errors=*)
endif()

find_package(fmt 9.0.0 CONFIG REQUIRED)

function(create_executable name src-dir)
  file(GLOB_RECURSE src CONFIGURE_DEPENDS ${src-dir}/*.cpp ${src-dir}/*.hpp)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE fmt::fmt)

  if(CMAKE_CXX_COMPILER_ID STREQUAL GNU)
    target_compile_options(${name} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wshadow
      -Wattributes
      -Wstrict-aliasing
      -Wno-sign-compare)
    if(CMAKE_BUILD_TYPE STREQUAL Debug)
      target_compile_options(${name} PRIVATE -ggdb)
    endif()
  else()
    message(WARNING "C++ compiler is not gcc, will miss some options")
  endif()
endfunction()

create_executable(compiler ./compiler)
create_executable(processor ./processor)

include(CTest)
if(BUILD_TESTING)
  message(STATUS "Configuring tests")
  enable_testing()
  add_subdirectory(tester)
endif()
