# tests
set(TEST_SRCS parsing.c)

create_test_sourcelist(dicmtest dicmtest.c ${TEST_SRCS})
add_executable(dicmtest ${dicmtest})
target_link_libraries(dicmtest PRIVATE dicm)

set(TS te
       # tb ti
)
set(CASES simple empty)

foreach(case ${CASES})
  foreach(ts ${TS})
    foreach(name ${TEST_SRCS})
      get_filename_component(testname ${name} NAME_WE)
      add_test(
        NAME ${testname}_${case}_${ts}
        COMMAND
          dicmtest ${testname}
          ${CMAKE_CURRENT_SOURCE_DIR}/cases/${ts}/${case}.dcm
          ${CMAKE_CURRENT_BINARY_DIR}/${testname}_${case}${ts}.txt)
      add_test(
        NAME ${testname}_${case}_${ts}_cmp
        COMMAND
          ${CMAKE_COMMAND} -E compare_files
          ${CMAKE_CURRENT_SOURCE_DIR}/gold/${case}.txt
          ${CMAKE_CURRENT_BINARY_DIR}/${testname}_${case}${ts}.txt)
    endforeach()
  endforeach()
endforeach()
