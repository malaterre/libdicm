# tests
set(TEST_SRCS emitting.c parsing.c version.c)

create_test_sourcelist(dicmtest dicmtest.c ${TEST_SRCS})
add_executable(dicmtest ${dicmtest})
target_link_libraries(dicmtest PRIVATE dicm)

# simple tests:
add_test(NAME version COMMAND dicmtest version)

set(TS te
       # tb ti
)
set(CASES
    empty
    simple
    simple_sqi
    simple_empty_sqi
    simple_sqi_empty_item
    simple_sqf
    simple_sqf_empty_frag
    simple_nested_sqi
    simple_nested_sqf
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
    set_tests_properties(parsing_${case}_${ts}
                         PROPERTIES DEPENDS emitting_${case}_${ts})
    # cmp
    add_test(
      NAME cmp_${case}_${ts}
      COMMAND
        ${CMAKE_COMMAND} -E compare_files
        ${CMAKE_CURRENT_SOURCE_DIR}/gold/${case}.txt
        ${CMAKE_CURRENT_BINARY_DIR}/parsing_${case}_${ts}.txt)
    set_tests_properties(cmp_${case}_${ts} PROPERTIES DEPENDS
                                                      parsing_${case}_${ts})
  endforeach()
endforeach()

if(DICM_TEST_DATA_ROOT)
  set(TESTDATA_ROOT_DIR ${DICM_TEST_DATA_ROOT})
  set(TRUNCATED_DATASETS single_byte single_tag single_tag_vr missing_value)

  foreach(truncated_dataset ${TRUNCATED_DATASETS})
    foreach(ts ${TS})
      # parse
      add_test(
        NAME parsing_${truncated_dataset}_${ts}
        COMMAND
          dicmtest parsing
          ${TESTDATA_ROOT_DIR}/truncated/${truncated_dataset}.dcm
          ${CMAKE_CURRENT_BINARY_DIR}/parsing_${truncated_dataset}_${ts}.txt)
      set_tests_properties(parsing_${truncated_dataset}_${ts}
                           PROPERTIES WILL_FAIL TRUE)
      # cmp
      add_test(
        NAME cmp_${truncated_dataset}_${ts}
        COMMAND
          ${CMAKE_COMMAND} -E compare_files
          ${TESTDATA_ROOT_DIR}/truncated/${truncated_dataset}.txt
          ${CMAKE_CURRENT_BINARY_DIR}/parsing_${truncated_dataset}_${ts}.txt)
      set_tests_properties(
        cmp_${truncated_dataset}_${ts}
        PROPERTIES DEPENDS parsing_${truncated_dataset}_${ts})

    endforeach() # ts
  endforeach()
endif() # DICM_TEST_DATA_ROOT
