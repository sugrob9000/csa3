cmake_minimum_required(VERSION 3.27)
project(csa-lab3 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

find_program(clang-tidy clang-tidy)
if(clang-tidy)
	message(STATUS "Found clang-tidy: ${clang-tidy}")
	set(checks "clang-analyzer-*,llvm-include-order,misc-*,performance-*")
	set(CMAKE_CXX_CLANG_TIDY
		${clang-tidy}
		-header-filter=${CMAKE_SOURCE_DIR}
		-checks=${checks})
else()
	message(WARNING "No clang-tidy")
endif()

find_package(fmt CONFIG REQUIRED)

function(create_executable name src-dir)
	file(GLOB_RECURSE src CONFIGURE_DEPENDS ${src-dir}/*.cpp ${src-dir}/*.hpp)
	add_executable(${name} ${src})
	target_link_libraries(${name} fmt::fmt)
endfunction()

create_executable(compiler ./compiler)
create_executable(processor ./processor)
