function(create_golden_test binary name)
	add_test(
		NAME ${name}-run
		COMMAND sh -c "${CMAKE_BINARY_DIR}/${binary} < ${CMAKE_CURRENT_SOURCE_DIR}/${name}-in > ${name}-out")

	add_test(
		NAME ${name}-compare
		COMMAND
		${CMAKE_COMMAND} -E compare_files
		${name}-out
		${CMAKE_CURRENT_SOURCE_DIR}/${name}-expected-out)

	# Use a fixture to setup a dependency between creating the output
	# and comparing it with the golden output

	set_tests_properties(${name}-run PROPERTIES FIXTURES_SETUP ${name}-fixture)
	set_tests_properties(${name}-compare PROPERTIES FIXTURES_REQUIRED ${name}-fixture)
endfunction()

create_golden_test(compiler "compiler-1")
