foreach(mode IN ITEMS uniform difference)
  execute_process(
    COMMAND "${PRIMO_EXECUTABLE}" --quiet --stats --check-model --preprocessing-mode=off
      --no-lia-round-integer-bounds --no-lia-primitive-int-rows --no-lia-integer-slacks
      --no-lia-patch-integral-moves --lia-patch-limit=0 --lia-gomory-cut-limit=0
      "--lia-cube=${mode}" "${PRIMO_TEST_INPUT}"
    RESULT_VARIABLE actual_exit OUTPUT_VARIABLE actual_stdout ERROR_VARIABLE actual_stderr)
  if(NOT actual_exit EQUAL 0 OR NOT actual_stdout MATCHES "^sat\n" OR
     NOT actual_stderr MATCHES ":lia.cube-models 1(\n|$)")
    message(FATAL_ERROR "${mode}: exit=${actual_exit}\n${actual_stdout}\n${actual_stderr}")
  endif()
endforeach()
foreach(value IN ITEMS true false auto "")
  execute_process(COMMAND "${PRIMO_EXECUTABLE}" "--lia-cube=${value}" "${PRIMO_TEST_INPUT}"
    RESULT_VARIABLE actual_exit OUTPUT_VARIABLE actual_stdout ERROR_VARIABLE actual_stderr)
  if(actual_exit EQUAL 0)
    message(FATAL_ERROR "invalid cube mode accepted: ${value}")
  endif()
endforeach()
