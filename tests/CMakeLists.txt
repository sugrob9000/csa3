function(create_test name proc-input)
  message(STATUS "Adding test ${name}")

  add_test(
    NAME ${name}-compile
    COMMAND sh -c "${CMAKE_BINARY_DIR}/compiler ${CMAKE_CURRENT_SOURCE_DIR}/${name}-in ${name}-image")
  set_tests_properties(${name}-compile PROPERTIES FIXTURES_SETUP ${name}-fixture-compiled)

  add_test(
    NAME ${name}-disasm
    COMMAND sh -c "${CMAKE_BINARY_DIR}/disasm ${name}-image > ${name}-disasm")
  set_tests_properties(${name}-disasm PROPERTIES
    FIXTURES_REQUIRED ${name}-fixture-compiled
    FIXTURES_SETUP ${name}-fixture-disasmed)

  add_test(
    NAME ${name}-run-proc
    COMMAND sh -c "echo -n '${proc-input}' | ${CMAKE_BINARY_DIR}/processor ${name}-image 2> ${name}-proc-log")
  set_tests_properties(${name}-run-proc PROPERTIES
    FIXTURES_REQUIRED ${name}-fixture-compiled
    FIXTURES_SETUP ${name}-fixture-run)

  add_test(
    NAME ${name}-compare-disasm
    COMMAND
    ${CMAKE_COMMAND} -E compare_files
    ${name}-disasm
    ${CMAKE_CURRENT_SOURCE_DIR}/${name}-expected-disasm)
  set_tests_properties(${name}-compare-disasm PROPERTIES FIXTURES_REQUIRED ${name}-fixture-disasmed)

  add_test(
    NAME ${name}-compare-proc-log
    COMMAND
    ${CMAKE_COMMAND} -E compare_files
    ${name}-proc-log
    ${CMAKE_CURRENT_SOURCE_DIR}/${name}-expected-proc-log)
  set_tests_properties(${name}-compare-disasm PROPERTIES FIXTURES_REQUIRED ${name}-fixture-run)
endfunction()

create_test("1" "hello")
create_test("2" "hello")
create_test("3" "hello")