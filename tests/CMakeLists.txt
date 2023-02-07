# tests
set(TEST_SRCS emitting.c parsing.c version.c)

create_test_sourcelist(dicmtest dicmtest.c ${TEST_SRCS})
add_executable(dicmtest ${dicmtest})
target_link_libraries(dicmtest PRIVATE dicm)

# simple tests:
add_test(NAME version COMMAND dicmtest version)

set(STRUCTURE_NAMES
    evrle_encapsulated #
    ivrle_raw #
    # evrle_raw # little-endian evrbe_raw # big-endian
)
set(COMMON_CASES simple simple_sqi simple_empty_sqi simple_sqi_empty_item
                 simple_nested_sqi simple_sqi_two_items)
set(raw_CASES simple_pd)
set(encapsulated_CASES simple_sqf simple_sqf_empty_frag simple_nested_sqf)

function(add_roundtrip_tests structure_name case struct_dir gold_folder
         roundtrip_folder)
  set(case_name ${structure_name}_${case})
  # define input/output
  set(input ${gold_folder}/${struct_dir}/${case})
  set(output ${roundtrip_folder}/${structure_name}/${case})
  # emit
  add_test(NAME emitting_${case_name}
           COMMAND dicmtest emitting ${structure_name} ${input}.txt
                   ${output}.dcm)
  # parse
  add_test(NAME parsing_${case_name} COMMAND dicmtest parsing ${structure_name}
                                             ${output}.dcm ${output}.txt)
  set_tests_properties(parsing_${case_name} PROPERTIES DEPENDS
                                                       emitting_${case_name})
  # cmp
  add_test(NAME cmp_${case_name} COMMAND ${CMAKE_COMMAND} -E compare_files
                                         ${input}.txt ${output}.txt)
  set_tests_properties(cmp_${case_name} PROPERTIES DEPENDS parsing_${case_name})
endfunction()

set(gold_folder ${CMAKE_CURRENT_SOURCE_DIR}/gold)
set(roundtrip_folder ${CMAKE_CURRENT_BINARY_DIR}/roundtrip)
foreach(structure_name ${STRUCTURE_NAMES})
  # prepare folder to store generated files:
  file(MAKE_DIRECTORY ${roundtrip_folder}/${structure_name})
  # define test folders:
  string(REPLACE "_" ";" structure_list ${structure_name})
  list(GET structure_list 0 toplevel_name)
  list(GET structure_list 1 sublevel_name)
  string(SUBSTRING ${toplevel_name} 0 3 toplevel_shortname)
  string(SUBSTRING ${sublevel_name} 0 3 sublevel_shortname)
  # setup common tests
  foreach(case ${COMMON_CASES})
    add_roundtrip_tests(${structure_name} ${case} ${toplevel_shortname}
                        ${gold_folder} ${roundtrip_folder})
  endforeach()
  # setup sub tests
  set(sub_CASES ${${sublevel_name}_CASES})
  foreach(case ${sub_CASES})
    add_roundtrip_tests(
      ${structure_name} ${case} ${toplevel_shortname}/${sublevel_shortname}
      ${gold_folder} ${roundtrip_folder})
  endforeach()
endforeach()

function(add_truncated_tests structure_name truncated_dataset testdata_root_dir
         truncated_folder)
  set(case_name ${structure_name}_${truncated_dataset})
  string(SUBSTRING ${structure_name} 0 3 struct_dir)
  # define input/output
  set(input ${testdata_root_dir}/truncated/${struct_dir}/${truncated_dataset})
  set(output ${truncated_folder}/${structure_name}/${truncated_dataset})
  # parse
  add_test(NAME parsing_${case_name} COMMAND dicmtest parsing ${structure_name}
                                             ${input}.dcm ${output}.txt)
  set_tests_properties(parsing_${case_name} PROPERTIES WILL_FAIL TRUE)
  # cmp
  add_test(NAME cmp_${case_name} COMMAND ${CMAKE_COMMAND} -E compare_files
                                         ${input}.txt ${output}.txt)
  set_tests_properties(cmp_${case_name} PROPERTIES DEPENDS parsing_${case_name})
  # emit
  add_test(NAME emitting_${case_name}
           COMMAND dicmtest emitting ${structure_name} ${output}.txt
                   ${output}.dcm)
  set_tests_properties(emitting_${case_name} PROPERTIES WILL_FAIL TRUE)
endfunction()

if(DICM_TEST_DATA_ROOT)
  set(truncated_folder ${CMAKE_CURRENT_BINARY_DIR}/truncated)
  set(TRUNCATED_DATASETS empty single_byte single_tag single_tag_vr
                         missing_value)
  foreach(structure_name ${STRUCTURE_NAMES})
    # prepare folder to store generated files:
    file(MAKE_DIRECTORY ${truncated_folder}/${structure_name})
    foreach(truncated_dataset ${TRUNCATED_DATASETS})
      add_truncated_tests(${structure_name} ${truncated_dataset}
                          ${DICM_TEST_DATA_ROOT} ${truncated_folder})
    endforeach() # structure
  endforeach()
endif() # DICM_TEST_DATA_ROOT
