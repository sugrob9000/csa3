cmake_minimum_required(VERSION 3.27)
project(csa-lab3 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

option(ENABLE_CLANG_TIDY ON)

if(ENABLE_CLANG_TIDY)
	find_program(clang-tidy clang-tidy REQUIRED)
	message(STATUS "Found clang-tidy: ${clang-tidy}")
	set(clang-tidy-checks "clang-analyzer-*,llvm-include-order,misc-*,performance-*")
	set(CMAKE_CXX_CLANG_TIDY
		${clang-tidy}
		-header-filter=${CMAKE_SOURCE_DIR}
		-checks=${clang-tidy-checks}
		-warnings-as-errors=*)
endif()

find_package(fmt CONFIG REQUIRED)

function(create_executable name src-dir)
	file(GLOB_RECURSE src CONFIGURE_DEPENDS ${src-dir}/*.cpp ${src-dir}/*.hpp)
	add_executable(${name} ${src})
	target_link_libraries(${name} fmt::fmt)

	if(CMAKE_CXX_COMPILER_ID STREQUAL GNU)
		set(cxx-warnings
			-Wall
			-Wextra
			-Wpedantic
			-Wshadow
			-Wattributes
			-Wstrict-aliasing)
		set(cxx-options ${cxx-warnings})
		target_compile_options(${name} PRIVATE ${cxx-options})
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
