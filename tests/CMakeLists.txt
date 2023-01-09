# tests
set(TEST_SRCS emitting.c parsing.c)

create_test_sourcelist(dicmtest dicmtest.c ${TEST_SRCS})
add_executable(dicmtest ${dicmtest})
target_link_libraries(dicmtest PRIVATE dicm)

set(TS te
       # tb ti
)
set(CASES empty simple simple_sqi simple_sqf simple_nested_sqi
          simple_sqi_two_items)

foreach(case ${CASES})
  foreach(ts ${TS})
    # emit
    add_test(
      NAME emitting_${case}_${ts}
      COMMAND
        dicmtest emitting ${ts} ${CMAKE_CURRENT_SOURCE_DIR}/gold/${case}.txt
        ${CMAKE_CURRENT_BINARY_DIR}/emitting_${case}_${ts}.dcm)
    # parse
    add_test(
      NAME parsing_${case}_${ts}
      COMMAND
        dicmtest parsing ${CMAKE_CURRENT_BINARY_DIR}/emitting_${case}_${ts}.dcm
        ${CMAKE_CURRENT_BINARY_DIR}/parsing_${case}_${ts}.txt)
    # cmp
    add_test(
      NAME cmp_${case}_${ts}
      COMMAND
        ${CMAKE_COMMAND} -E compare_files
        ${CMAKE_CURRENT_SOURCE_DIR}/gold/${case}.txt
        ${CMAKE_CURRENT_BINARY_DIR}/parsing_${case}_${ts}.txt)
  endforeach()
endforeach()
