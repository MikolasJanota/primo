function(check_profile label phase monotone)
  execute_process(
    COMMAND "${PRIMO_EXECUTABLE}" --quiet --stats --check-model ${ARGN} "${PRIMO_TEST_INPUT}"
    RESULT_VARIABLE actual_exit
    OUTPUT_VARIABLE actual_stdout
    ERROR_VARIABLE actual_stderr
  )
  if(NOT actual_exit EQUAL 0 OR NOT actual_stdout STREQUAL "sat\n")
    message(FATAL_ERROR "${label}: exit=${actual_exit}\n${actual_stdout}\n${actual_stderr}")
  endif()
  foreach(pair IN ITEMS "lra.model-phase.hints;${phase}"
                        "preprocessing.monotone-elimination.variables;${monotone}")
    list(GET pair 0 statistic)
    list(GET pair 1 expected)
    if(expected)
      if(NOT actual_stderr MATCHES ":${statistic} [1-9][0-9]*(\n|$)")
        message(FATAL_ERROR "${label}: missing active ${statistic}\n${actual_stderr}")
      endif()
    else()
      # The public statistics printer omits zero counters.
      if(actual_stderr MATCHES ":${statistic} [1-9][0-9]*(\n|$)")
        message(FATAL_ERROR "${label}: unexpected active ${statistic}\n${actual_stderr}")
      endif()
    endif()
  endforeach()
endfunction()

check_profile("automatic" TRUE TRUE)
check_profile("canonical auto" TRUE TRUE --arithmetic-model-phase=auto)
check_profile("canonical on" TRUE TRUE --arithmetic-model-phase=on)
check_profile("canonical off" FALSE TRUE --arithmetic-model-phase=off)
check_profile("canonical auto respects preprocessing" FALSE FALSE
              --preprocessing-mode=off --arithmetic-model-phase=auto)
check_profile("canonical on overrides preprocessing" TRUE FALSE
              --preprocessing-mode=off --arithmetic-model-phase=on)
check_profile("explicit both" TRUE TRUE --lra-model-phase --monotone-elimination)
check_profile("explicit opt-outs" FALSE FALSE --no-lra-model-phase --no-monotone-elimination)
check_profile("phase only" TRUE FALSE --no-monotone-elimination)
check_profile("monotone only" FALSE TRUE --no-lra-model-phase)
check_profile("explicit preprocessing profile" FALSE FALSE --preprocessing-mode=off)
check_profile("explicit flags override preprocessing profile" TRUE TRUE
              --preprocessing-mode=off --lra-model-phase --monotone-elimination)
check_profile("false-valued positive flags" FALSE FALSE
              --lra-model-phase=false --monotone-elimination=false)
check_profile("opt-outs take precedence" FALSE FALSE
              --lra-model-phase --no-lra-model-phase
              --monotone-elimination --no-monotone-elimination)
execute_process(COMMAND "${PRIMO_EXECUTABLE}" --help
                RESULT_VARIABLE help_exit OUTPUT_VARIABLE help_stdout)
if(NOT help_exit EQUAL 0)
  message(FATAL_ERROR "help failed: ${help_exit}")
endif()
foreach(option IN ITEMS monotone-elimination)
  if(NOT help_stdout MATCHES "--${option} +\\[default: auto"
     OR NOT help_stdout MATCHES "--no-${option}")
    message(FATAL_ERROR "missing automatic policy/opt-out in help for ${option}\n${help_stdout}")
  endif()
endforeach()
if(NOT help_stdout MATCHES "--arithmetic-model-phase[^\n]*on,off,auto[^\n]*\\[auto\\]"
   OR NOT help_stdout MATCHES "--initial-theory-phase"
   OR NOT help_stdout MATCHES "Compatibility aliases:"
   OR NOT help_stdout MATCHES "--no-lra-model-phase")
  message(FATAL_ERROR "missing phase policy names or compatibility forms\n${help_stdout}")
endif()

function(reject_phase label)
  execute_process(COMMAND "${PRIMO_EXECUTABLE}" --quiet ${ARGN} "${PRIMO_TEST_INPUT}"
                  RESULT_VARIABLE actual_exit OUTPUT_VARIABLE actual_stdout
                  ERROR_VARIABLE actual_stderr)
  if(actual_exit EQUAL 0 OR actual_stdout MATCHES "sat\n")
    message(FATAL_ERROR "${label}: accepted invalid phase arguments\n${actual_stderr}")
  endif()
endfunction()
foreach(value IN ITEMS true false AUTO "")
  reject_phase("invalid arithmetic phase ${value}" "--arithmetic-model-phase=${value}")
endforeach()
reject_phase("canonical plus legacy on" --arithmetic-model-phase=on --lra-model-phase)
reject_phase("canonical plus legacy off" --arithmetic-model-phase=auto --no-lra-model-phase)

# Exercise the initial-phase alias on a route that actually installs the hints.
foreach(spelling IN ITEMS initial-theory-phase theory-phase)
  foreach(mode IN ITEMS negative positive)
    execute_process(
      COMMAND "${PRIMO_EXECUTABLE}" --quiet --stats --preprocessing-mode=off
              "--${spelling}=${mode}" "${PRIMO_INITIAL_PHASE_INPUT}"
      RESULT_VARIABLE actual_exit OUTPUT_VARIABLE actual_stdout ERROR_VARIABLE actual_stderr)
    if(NOT actual_exit EQUAL 0 OR NOT actual_stdout STREQUAL "sat\n")
      message(FATAL_ERROR "${spelling}=${mode} failed\n${actual_stderr}")
    endif()
    if(mode STREQUAL "negative")
      if(NOT actual_stderr MATCHES ":sat.phase.theory-negative [1-9][0-9]*")
        message(FATAL_ERROR "${spelling}: initial negative hint not installed\n${actual_stderr}")
      endif()
    elseif(actual_stderr MATCHES ":sat.phase.theory-negative [1-9][0-9]*")
      message(FATAL_ERROR "${spelling}: negative hint installed for positive policy")
    endif()
  endforeach()
endforeach()
