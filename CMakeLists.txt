cmake_minimum_required(VERSION 3.20)

project(primo LANGUAGES CXX)

# Default to a Release build when the user (or an IDE/script) doesn't ask
# for a specific one: an unset CMAKE_BUILD_TYPE is unoptimized with no
# NDEBUG, and gives no indication in the binary that that's what happened
# (see the version banner's build-type field) -- easy to mistake for a
# real performance regression instead of a plain unconfigured build.
# Multi-config generators (Ninja Multi-Config, Visual Studio, Xcode) select
# configuration at build time instead, so this only applies when there
# isn't one.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

include(CheckCXXSourceCompiles)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PRIMO_WARNINGS_AS_ERRORS "Treat Primo C++ compiler warnings as errors" ON)
set(PRIMO_SANITIZERS "" CACHE STRING "Semicolon-separated sanitizer list for Primo targets, e.g. address;undefined")
set(PRIMO_CADICAL_ROOT "" CACHE PATH "CaDiCaL installation prefix to prefer over contrib/local")
set(PRIMO_GIT_REVISION_OVERRIDE "" CACHE STRING "Git revision banner for source snapshots without .git")

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/contrib/local")

function(primo_enable_sanitizers target)
  if(NOT PRIMO_SANITIZERS)
    return()
  endif()
  if(MSVC)
    if("address" IN_LIST PRIMO_SANITIZERS)
      target_compile_options(${target} PRIVATE /fsanitize=address)
      target_link_options(${target} PRIVATE /fsanitize=address)
    else()
      message(FATAL_ERROR "MSVC sanitizer support is limited to PRIMO_SANITIZERS=address")
    endif()
    return()
  endif()

  list(JOIN PRIMO_SANITIZERS "," sanitizer_flags)
  target_compile_options(${target} PRIVATE -fsanitize=${sanitizer_flags} -fno-omit-frame-pointer)
  target_link_options(${target} PRIVATE -fsanitize=${sanitizer_flags})
endfunction()

function(primo_enable_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    if(PRIMO_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wsign-conversion
      -Wshadow
      -Wold-style-cast
      -Wcast-align
      -Wunused
      -Woverloaded-virtual
      -Wnull-dereference
      -Wdouble-promotion
      -Wformat=2
      -Wimplicit-fallthrough
    )
    if(PRIMO_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

find_path(CLI11_INCLUDE_DIR
  NAMES CLI/CLI.hpp
  HINTS "${CMAKE_SOURCE_DIR}/contrib/local/include"
)
if(CLI11_INCLUDE_DIR)
  message(STATUS "Found CLI11: ${CLI11_INCLUDE_DIR}")
else()
  message(FATAL_ERROR "CLI11 not found. Run `make contrib-required` or install CLI11 so CLI/CLI.hpp is available.")
endif()

set(PRIMO_CADICAL_INCLUDE_HINTS "${CMAKE_SOURCE_DIR}/contrib/local/include")
set(PRIMO_CADICAL_LIBRARY_HINTS "${CMAKE_SOURCE_DIR}/contrib/local/lib")
if(PRIMO_CADICAL_ROOT)
  list(PREPEND PRIMO_CADICAL_INCLUDE_HINTS "${PRIMO_CADICAL_ROOT}/include")
  list(PREPEND PRIMO_CADICAL_LIBRARY_HINTS "${PRIMO_CADICAL_ROOT}/lib")
endif()
find_path(IPASIR_UP_INCLUDE_DIR
  NAMES ipasir_up.h ipasir.h
  HINTS ${PRIMO_CADICAL_INCLUDE_HINTS}
)
find_library(CADICAL_LIBRARY
  NAMES cadical
  HINTS ${PRIMO_CADICAL_LIBRARY_HINTS}
)

if(IPASIR_UP_INCLUDE_DIR AND CADICAL_LIBRARY)
  message(STATUS "Found IPASIR/CaDiCaL headers and library")
  message(STATUS "  IPASIR headers: ${IPASIR_UP_INCLUDE_DIR}")
  message(STATUS "  CaDiCaL library: ${CADICAL_LIBRARY}")
  set(CMAKE_REQUIRED_INCLUDES "${IPASIR_UP_INCLUDE_DIR}")
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  check_cxx_source_compiles(
    "#include <cadical.hpp>
     int main() { auto fn = &CaDiCaL::Solver::resize; (void) fn; return 0; }"
    PRIMO_CADICAL_HAS_RESIZE
  )
  check_cxx_source_compiles(
    "#include <cadical.hpp>
     int main() { auto fn = &CaDiCaL::Solver::reserve; (void) fn; return 0; }"
    PRIMO_CADICAL_HAS_RESERVE
  )
  check_cxx_source_compiles(
    "#include <cadical.hpp>
     int main() { auto fn = &CaDiCaL::Solver::declare_more_variables; (void) fn; return 0; }"
    PRIMO_CADICAL_HAS_DECLARE_MORE_VARIABLES
  )
  check_cxx_source_compiles(
    "#include <cadical.hpp>
     struct P : CaDiCaL::ExternalPropagator {
       void notify_assignment(const std::vector<int>&) override {}
       void notify_new_decision_level() override {}
       void notify_backtrack(size_t) override {}
       bool cb_check_found_model(const std::vector<int>&) override { return true; }
       bool cb_has_external_clause(bool&) override { return false; }
       int cb_add_external_clause_lit() override { return 0; }
     };
     int main() { P p; (void)p; }"
    PRIMO_CADICAL_EXTERNAL_NOTIFY_VECTOR
  )
  check_cxx_source_compiles(
    "#include <cadical.hpp>
     struct P : CaDiCaL::ExternalPropagator {
       void notify_assignment(int, bool) override {}
       void notify_new_decision_level() override {}
       void notify_backtrack(size_t) override {}
       bool cb_check_found_model(const std::vector<int>&) override { return true; }
       bool cb_has_external_clause() override { return false; }
       int cb_add_external_clause_lit() override { return 0; }
     };
     int main() { P p; (void)p; }"
    PRIMO_CADICAL_EXTERNAL_NOTIFY_SCALAR
  )
  unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
  unset(CMAKE_REQUIRED_INCLUDES)
else()
  message(STATUS "IPASIR-UP/CaDiCaL not found; SAT backend remains a scaffold")
endif()

find_path(GMP_INCLUDE_DIR
  NAMES gmp.h
  HINTS "${CMAKE_SOURCE_DIR}/contrib/local/include"
)
find_path(GMPXX_INCLUDE_DIR
  NAMES gmpxx.h
  HINTS "${CMAKE_SOURCE_DIR}/contrib/local/include"
)
find_library(GMPXX_LIBRARY
  NAMES gmpxx
  HINTS "${CMAKE_SOURCE_DIR}/contrib/local/lib"
)
find_library(GMP_LIBRARY
  NAMES gmp
  HINTS "${CMAKE_SOURCE_DIR}/contrib/local/lib"
)
if(GMP_INCLUDE_DIR AND GMPXX_INCLUDE_DIR AND GMP_LIBRARY AND GMPXX_LIBRARY)
  message(STATUS "Found GMP: ${GMP_LIBRARY}")
else()
  message(FATAL_ERROR "GMP (with C++ wrapper) not found. Run `make contrib-gmp` or install GMP so gmp.h/gmpxx.h are available.")
endif()

file(GLOB_RECURSE PRIMO_CLI_SOURCES CONFIGURE_DEPENDS src/cli/*.cpp)
file(GLOB_RECURSE PRIMO_TERM_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
file(GLOB_RECURSE PRIMO_SAT_SOURCES CONFIGURE_DEPENDS src/sat/*.cpp)
file(GLOB_RECURSE PRIMO_SMT_SOURCES CONFIGURE_DEPENDS src/smt/*.cpp)
file(GLOB_RECURSE PRIMO_SMTLIB_SOURCES CONFIGURE_DEPENDS src/smtlib/*.cpp)
file(GLOB_RECURSE PRIMO_THEORY_SOURCES CONFIGURE_DEPENDS src/theory/*.cpp)
file(GLOB_RECURSE PRIMO_UTIL_SOURCES CONFIGURE_DEPENDS src/util/*.cpp)
function(primo_configure_owned_library target)
  primo_enable_warnings(${target})
  primo_enable_sanitizers(${target})
  target_include_directories(${target} PUBLIC src)
endfunction()

add_library(primo_util STATIC ${PRIMO_UTIL_SOURCES})
primo_configure_owned_library(primo_util)
target_include_directories(primo_util SYSTEM PUBLIC "${GMP_INCLUDE_DIR}" "${GMPXX_INCLUDE_DIR}")
target_link_libraries(primo_util PUBLIC "${GMPXX_LIBRARY}" "${GMP_LIBRARY}")

add_library(primo_terms STATIC ${PRIMO_TERM_SOURCES})
primo_configure_owned_library(primo_terms)
target_link_libraries(primo_terms PUBLIC primo_util)

add_library(primo_sat STATIC ${PRIMO_SAT_SOURCES})
primo_configure_owned_library(primo_sat)
if(IPASIR_UP_INCLUDE_DIR AND CADICAL_LIBRARY)
  target_compile_definitions(primo_sat PUBLIC PRIMO_HAVE_IPASIR=1)
  target_compile_definitions(primo_sat PUBLIC
    PRIMO_CADICAL_HAS_RESIZE=$<BOOL:${PRIMO_CADICAL_HAS_RESIZE}>
    PRIMO_CADICAL_HAS_RESERVE=$<BOOL:${PRIMO_CADICAL_HAS_RESERVE}>
    PRIMO_CADICAL_HAS_DECLARE_MORE_VARIABLES=$<BOOL:${PRIMO_CADICAL_HAS_DECLARE_MORE_VARIABLES}>
    PRIMO_CADICAL_EXTERNAL_NOTIFY_VECTOR=$<BOOL:${PRIMO_CADICAL_EXTERNAL_NOTIFY_VECTOR}>
    PRIMO_CADICAL_EXTERNAL_NOTIFY_SCALAR=$<BOOL:${PRIMO_CADICAL_EXTERNAL_NOTIFY_SCALAR}>
  )
  target_include_directories(primo_sat SYSTEM PUBLIC "${IPASIR_UP_INCLUDE_DIR}")
  target_link_libraries(primo_sat PUBLIC "${CADICAL_LIBRARY}")
else()
  target_compile_definitions(primo_sat PUBLIC PRIMO_HAVE_IPASIR=0)
endif()

add_library(primo_theory STATIC ${PRIMO_THEORY_SOURCES})
primo_configure_owned_library(primo_theory)
target_link_libraries(primo_theory PUBLIC primo_terms primo_sat primo_util)

add_library(primo_smtlib STATIC ${PRIMO_SMTLIB_SOURCES})
primo_configure_owned_library(primo_smtlib)
target_link_libraries(primo_smtlib PUBLIC primo_terms)

add_library(primo_smt STATIC ${PRIMO_SMT_SOURCES})
primo_configure_owned_library(primo_smt)
target_link_libraries(primo_smt PUBLIC primo_terms primo_sat primo_smtlib primo_theory primo_util)

add_library(primo_cli_support STATIC ${PRIMO_CLI_SOURCES})
primo_configure_owned_library(primo_cli_support)
target_link_libraries(primo_cli_support PUBLIC primo_smt)

# Compatibility umbrella for executables and tests that exercise several
# layers. New narrow targets should link the lowest layer they use.
add_library(primo_core INTERFACE)
target_link_libraries(primo_core INTERFACE
  primo_smt primo_smtlib primo_theory primo_sat primo_terms primo_util primo_cli_support
)

add_executable(primo src/main.cpp)
if(PRIMO_SANITIZERS)
  list(JOIN PRIMO_SANITIZERS "," primo_sanitizers_joined)
else()
  set(primo_sanitizers_joined "none")
endif()
target_compile_definitions(primo PRIVATE
  "PRIMO_BUILD_TYPE=\"$<IF:$<CONFIG:>,unspecified,$<CONFIG>>\""
  "PRIMO_SANITIZERS_BANNER=\"${primo_sanitizers_joined}\""
)
add_custom_target(primo_git_revision_header ALL
  COMMAND "${CMAKE_COMMAND}"
    -DPRIMO_SOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DPRIMO_GIT_REVISION_HEADER=${CMAKE_BINARY_DIR}/generated/primo_git_revision.hpp
    -DPRIMO_GIT_REVISION_OVERRIDE=${PRIMO_GIT_REVISION_OVERRIDE}
    -P ${CMAKE_SOURCE_DIR}/scripts/write_git_revision.cmake
  BYPRODUCTS "${CMAKE_BINARY_DIR}/generated/primo_git_revision.hpp"
  VERBATIM
)
add_dependencies(primo primo_git_revision_header)
primo_enable_warnings(primo)
primo_enable_sanitizers(primo)
target_link_libraries(primo PRIVATE primo_core)
target_include_directories(primo PRIVATE src)
target_include_directories(primo PRIVATE "${CMAKE_BINARY_DIR}")
target_include_directories(primo SYSTEM PRIVATE "${CLI11_INCLUDE_DIR}")

enable_testing()
add_test(NAME primo_version COMMAND primo --version)
add_test(
  NAME primo_lra_heuristic_cli
  COMMAND ${CMAKE_COMMAND}
    -DPRIMO_EXECUTABLE=$<TARGET_FILE:primo>
    -DPRIMO_TEST_INPUT=${CMAKE_SOURCE_DIR}/benchmarks/smoke/qf_lra/default_heuristics.smt2
    -DPRIMO_INITIAL_PHASE_INPUT=${CMAKE_SOURCE_DIR}/benchmarks/smoke/qf_uf/sat_uninterpreted_constants.smt2
    -P ${CMAKE_SOURCE_DIR}/tests/cmake/check_lra_heuristic_cli.cmake
)
add_test(
  NAME primo_preprocessing_cli_options
  COMMAND ${CMAKE_COMMAND}
    -DPRIMO_EXECUTABLE=$<TARGET_FILE:primo>
    -DPRIMO_TEST_INPUT=${CMAKE_SOURCE_DIR}/benchmarks/smoke/qf_uf/sat_uninterpreted_constants.smt2
    -P ${CMAKE_SOURCE_DIR}/tests/cmake/check_preprocessing_cli_options.cmake
)
add_test(
  NAME primo_experimental_tactic_cli
  COMMAND ${CMAKE_COMMAND}
    -DPRIMO_EXECUTABLE=$<TARGET_FILE:primo>
    -DPRIMO_TEST_INPUT=${CMAKE_SOURCE_DIR}/benchmarks/smoke/qf_uf/sat_uninterpreted_constants.smt2
    -P ${CMAKE_SOURCE_DIR}/tests/cmake/check_experimental_tactic_cli.cmake
)
add_test(
  NAME primo_frontend_type_error_cli
  COMMAND ${CMAKE_COMMAND}
    -DPRIMO_EXECUTABLE=$<TARGET_FILE:primo>
    -DPRIMO_TEST_INPUT=${CMAKE_SOURCE_DIR}/tests/smtlib/smoke/frontend_type_error.smt2
    -P ${CMAKE_SOURCE_DIR}/tests/cmake/check_frontend_type_error_cli.cmake
)

add_test(NAME primo_assumptions_cli
  COMMAND ${CMAKE_COMMAND}
    -DPRIMO=$<TARGET_FILE:primo>
    -DINPUT=${CMAKE_SOURCE_DIR}/tests/smtlib/assumption_scopes.smt2
    -P ${CMAKE_SOURCE_DIR}/tests/cmake/check_assumptions_cli.cmake)

add_test(NAME primo_assumptions_uf_cli
  COMMAND ${CMAKE_COMMAND}
    -DPRIMO=$<TARGET_FILE:primo>
    -DINPUT=${CMAKE_SOURCE_DIR}/tests/smtlib/assumption_uf_scopes.smt2
    -P ${CMAKE_SOURCE_DIR}/tests/cmake/check_assumptions_cli.cmake)

# The cancellation watchdog is process behaviour, so it is checked by driving
# the binary with a real signal. bash and python3 are only needed here.
find_program(PRIMO_BASH_EXECUTABLE bash)
find_program(PRIMO_PYTHON_EXECUTABLE python3)
if(PRIMO_PYTHON_EXECUTABLE)
  add_test(NAME primo_interactive_session
    COMMAND ${PRIMO_PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/interactive_session.py $<TARGET_FILE:primo>)
  set_tests_properties(primo_interactive_session PROPERTIES TIMEOUT 30)
endif()
if(PRIMO_BASH_EXECUTABLE AND PRIMO_PYTHON_EXECUTABLE)
  add_test(
    NAME primo_sigterm_reports_unknown
    COMMAND ${CMAKE_COMMAND}
      -DPRIMO_EXECUTABLE=$<TARGET_FILE:primo>
      -DPRIMO_PYTHON=${PRIMO_PYTHON_EXECUTABLE}
      -DPRIMO_GENERATOR=${CMAKE_SOURCE_DIR}/scripts/make_interruptible_input.py
      -DPRIMO_TEST_INPUT=${CMAKE_SOURCE_DIR}/tests/smtlib/smoke/interruptible_pigeonhole.smt2
      -DPRIMO_SMOKE_INPUT=${CMAKE_SOURCE_DIR}/benchmarks/smoke/qf_uf/sat_uninterpreted_constants.smt2
      -P ${CMAKE_SOURCE_DIR}/tests/cmake/check_sigterm_reports_unknown.cmake
  )
endif()

add_executable(primo_core_tests tests/unit/core_tests.cpp)
primo_enable_warnings(primo_core_tests)
primo_enable_sanitizers(primo_core_tests)
target_link_libraries(primo_core_tests PRIVATE primo_core)
add_test(NAME primo_core_tests COMMAND primo_core_tests)

add_executable(primo_binder_tests tests/unit/binder_tests.cpp)
primo_enable_warnings(primo_binder_tests)
primo_enable_sanitizers(primo_binder_tests)
target_link_libraries(primo_binder_tests PRIVATE primo_core)
add_test(NAME primo_binder_tests COMMAND primo_binder_tests)

add_executable(primo_quantifier_frontend_tests tests/unit/quantifier_frontend_tests.cpp)
primo_enable_warnings(primo_quantifier_frontend_tests)
primo_enable_sanitizers(primo_quantifier_frontend_tests)
target_link_libraries(primo_quantifier_frontend_tests PRIVATE primo_core)
add_test(NAME primo_quantifier_frontend_tests COMMAND primo_quantifier_frontend_tests)

add_executable(primo_quantifier_solver_tests tests/unit/quantifier_solver_tests.cpp)
primo_enable_warnings(primo_quantifier_solver_tests)
primo_enable_sanitizers(primo_quantifier_solver_tests)
target_link_libraries(primo_quantifier_solver_tests PRIVATE primo_core)
add_test(NAME primo_quantifier_solver_tests COMMAND primo_quantifier_solver_tests)

add_executable(primo_instance_simplifier_tests tests/unit/instance_simplifier_tests.cpp)
primo_enable_warnings(primo_instance_simplifier_tests)
primo_enable_sanitizers(primo_instance_simplifier_tests)
target_link_libraries(primo_instance_simplifier_tests PRIVATE primo_core)
add_test(NAME primo_instance_simplifier_tests COMMAND primo_instance_simplifier_tests)

add_executable(primo_tuple_order_tests tests/unit/tuple_order_tests.cpp)
primo_enable_warnings(primo_tuple_order_tests)
primo_enable_sanitizers(primo_tuple_order_tests)
target_link_libraries(primo_tuple_order_tests PRIVATE primo_core)
add_test(NAME primo_tuple_order_tests COMMAND primo_tuple_order_tests)

add_executable(primo_option_names_tests tests/unit/option_names_tests.cpp)
primo_enable_warnings(primo_option_names_tests)
primo_enable_sanitizers(primo_option_names_tests)
target_link_libraries(primo_option_names_tests PRIVATE primo_core)
add_test(NAME primo_option_names_tests COMMAND primo_option_names_tests)

add_executable(primo_term_printer_tests tests/unit/term_printer_tests.cpp)
primo_enable_warnings(primo_term_printer_tests)
primo_enable_sanitizers(primo_term_printer_tests)
target_link_libraries(primo_term_printer_tests PRIVATE primo_core)
add_test(NAME primo_term_printer_tests COMMAND primo_term_printer_tests)

add_executable(primo_model_tests tests/unit/model_tests.cpp)
primo_enable_warnings(primo_model_tests)
primo_enable_sanitizers(primo_model_tests)
target_link_libraries(primo_model_tests PRIVATE primo_core)
add_test(NAME primo_model_tests COMMAND primo_model_tests)

add_executable(primo_smtlib_parser_tests tests/unit/smtlib_parser_tests.cpp)
primo_enable_warnings(primo_smtlib_parser_tests)
primo_enable_sanitizers(primo_smtlib_parser_tests)
target_link_libraries(primo_smtlib_parser_tests PRIVATE primo_core)
add_test(NAME primo_smtlib_parser_tests COMMAND primo_smtlib_parser_tests)

add_executable(primo_parser_corpus tests/parser_corpus.cpp)
primo_enable_warnings(primo_parser_corpus)
primo_enable_sanitizers(primo_parser_corpus)
target_link_libraries(primo_parser_corpus PRIVATE primo_smtlib)
if(PRIMO_PYTHON_EXECUTABLE)
  add_test(NAME primo_parser_corpus_smoke
    COMMAND ${PRIMO_PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/parser_corpus_smoke.py
            $<TARGET_FILE:primo_parser_corpus>)
  set_tests_properties(primo_parser_corpus_smoke PROPERTIES TIMEOUT 20)
endif()

add_executable(primo_smt_session_tests tests/unit/smt_session_tests.cpp)
primo_enable_warnings(primo_smt_session_tests)
primo_enable_sanitizers(primo_smt_session_tests)
target_link_libraries(primo_smt_session_tests PRIVATE primo_core)
add_test(NAME primo_smt_session_tests COMMAND primo_smt_session_tests)

add_executable(primo_sat_backend_tests tests/unit/sat_backend_tests.cpp)
primo_enable_warnings(primo_sat_backend_tests)
primo_enable_sanitizers(primo_sat_backend_tests)
target_link_libraries(primo_sat_backend_tests PRIVATE primo_core)
add_test(NAME primo_sat_backend_tests COMMAND primo_sat_backend_tests)

add_executable(primo_boolean_solver_tests tests/unit/boolean_solver_tests.cpp)
primo_enable_warnings(primo_boolean_solver_tests)
primo_enable_sanitizers(primo_boolean_solver_tests)
target_link_libraries(primo_boolean_solver_tests PRIVATE primo_core)
add_test(NAME primo_boolean_solver_tests COMMAND primo_boolean_solver_tests)

add_executable(primo_euf_solver_tests tests/unit/euf_solver_tests.cpp)
primo_enable_warnings(primo_euf_solver_tests)
primo_enable_sanitizers(primo_euf_solver_tests)
target_link_libraries(primo_euf_solver_tests PRIVATE primo_core)
add_test(NAME primo_euf_solver_tests COMMAND primo_euf_solver_tests)

add_executable(primo_assignment_coordinator_tests tests/unit/assignment_coordinator_tests.cpp)
primo_enable_warnings(primo_assignment_coordinator_tests)
primo_enable_sanitizers(primo_assignment_coordinator_tests)
target_link_libraries(primo_assignment_coordinator_tests PRIVATE primo_core)
add_test(NAME primo_assignment_coordinator_tests COMMAND primo_assignment_coordinator_tests)

add_executable(primo_euf_callback_tests tests/unit/euf_callback_tests.cpp)
primo_enable_warnings(primo_euf_callback_tests)
primo_enable_sanitizers(primo_euf_callback_tests)
target_link_libraries(primo_euf_callback_tests PRIVATE primo_core)
add_test(NAME primo_euf_callback_tests COMMAND primo_euf_callback_tests)

add_executable(primo_preprocessing_tests tests/unit/preprocessing_tests.cpp)
primo_enable_warnings(primo_preprocessing_tests)
primo_enable_sanitizers(primo_preprocessing_tests)
target_link_libraries(primo_preprocessing_tests PRIVATE primo_core)
add_test(NAME primo_preprocessing_tests COMMAND primo_preprocessing_tests)

add_executable(primo_tactic_tests tests/unit/tactic_tests.cpp)
primo_enable_warnings(primo_tactic_tests)
primo_enable_sanitizers(primo_tactic_tests)
target_link_libraries(primo_tactic_tests PRIVATE primo_core)
add_test(NAME primo_tactic_tests COMMAND primo_tactic_tests)

add_executable(primo_strategy_lifetime_tests tests/unit/strategy_lifetime_tests.cpp)
primo_enable_warnings(primo_strategy_lifetime_tests)
primo_enable_sanitizers(primo_strategy_lifetime_tests)
target_link_libraries(primo_strategy_lifetime_tests PRIVATE primo_smt)
add_test(NAME primo_strategy_lifetime_tests COMMAND primo_strategy_lifetime_tests)

add_executable(primo_tactic_composition_tests tests/unit/tactic_composition_tests.cpp)
primo_enable_warnings(primo_tactic_composition_tests)
primo_enable_sanitizers(primo_tactic_composition_tests)
target_link_libraries(primo_tactic_composition_tests PRIVATE primo_core)
add_test(NAME primo_tactic_composition_tests COMMAND primo_tactic_composition_tests)

add_executable(primo_theory_combination_tests tests/unit/theory_combination_tests.cpp)
primo_enable_warnings(primo_theory_combination_tests)
primo_enable_sanitizers(primo_theory_combination_tests)
target_link_libraries(primo_theory_combination_tests PRIVATE primo_core)
add_test(NAME primo_theory_combination_tests COMMAND primo_theory_combination_tests)

add_executable(primo_component_combination_tests tests/unit/component_combination_tests.cpp)
primo_enable_warnings(primo_component_combination_tests)
primo_enable_sanitizers(primo_component_combination_tests)
target_link_libraries(primo_component_combination_tests PRIVATE primo_core)
add_test(NAME primo_component_combination_tests COMMAND primo_component_combination_tests)

add_executable(primo_rational_tests tests/unit/rational_tests.cpp)
primo_enable_warnings(primo_rational_tests)
primo_enable_sanitizers(primo_rational_tests)
target_link_libraries(primo_rational_tests PRIVATE primo_core)
add_test(NAME primo_rational_tests COMMAND primo_rational_tests)

add_executable(primo_qf_lra_solver_tests tests/unit/qf_lra_solver_tests.cpp)
primo_enable_warnings(primo_qf_lra_solver_tests)
primo_enable_sanitizers(primo_qf_lra_solver_tests)
target_link_libraries(primo_qf_lra_solver_tests PRIVATE primo_core)
add_test(NAME primo_qf_lra_solver_tests COMMAND primo_qf_lra_solver_tests)

add_executable(primo_linear_polynomial_tests tests/unit/linear_polynomial_tests.cpp)
primo_enable_warnings(primo_linear_polynomial_tests)
primo_enable_sanitizers(primo_linear_polynomial_tests)
target_link_libraries(primo_linear_polynomial_tests PRIVATE primo_core)
add_test(NAME primo_linear_polynomial_tests COMMAND primo_linear_polynomial_tests)

add_executable(primo_lra_atom_tests tests/unit/lra_atom_tests.cpp)
primo_enable_warnings(primo_lra_atom_tests)
primo_enable_sanitizers(primo_lra_atom_tests)
target_link_libraries(primo_lra_atom_tests PRIVATE primo_core)
add_test(NAME primo_lra_atom_tests COMMAND primo_lra_atom_tests)

add_executable(primo_lra_tableau_tests tests/unit/lra_tableau_tests.cpp)
primo_enable_warnings(primo_lra_tableau_tests)
primo_enable_sanitizers(primo_lra_tableau_tests)
target_link_libraries(primo_lra_tableau_tests PRIVATE primo_core)
add_test(NAME primo_lra_tableau_tests COMMAND primo_lra_tableau_tests)

add_executable(primo_lia_completion_tests tests/unit/lia_completion_tests.cpp)
primo_enable_warnings(primo_lia_completion_tests)
primo_enable_sanitizers(primo_lia_completion_tests)
target_link_libraries(primo_lia_completion_tests PRIVATE primo_core)
add_test(NAME primo_lia_completion_tests COMMAND primo_lia_completion_tests)

add_executable(primo_lia_branch_and_bound_tests tests/unit/lia_branch_and_bound_tests.cpp)
primo_enable_warnings(primo_lia_branch_and_bound_tests)
primo_enable_sanitizers(primo_lia_branch_and_bound_tests)
target_link_libraries(primo_lia_branch_and_bound_tests PRIVATE primo_core)
add_test(NAME primo_lia_branch_and_bound_tests COMMAND primo_lia_branch_and_bound_tests)

add_executable(primo_qf_lia_solver_tests tests/unit/qf_lia_solver_tests.cpp)
primo_enable_warnings(primo_qf_lia_solver_tests)
primo_enable_sanitizers(primo_qf_lia_solver_tests)
target_link_libraries(primo_qf_lia_solver_tests PRIVATE primo_core)
add_test(NAME primo_qf_lia_solver_tests COMMAND primo_qf_lia_solver_tests)

add_executable(primo_qf_uflia_solver_tests tests/unit/qf_uflia_solver_tests.cpp)
primo_enable_warnings(primo_qf_uflia_solver_tests)
primo_enable_sanitizers(primo_qf_uflia_solver_tests)
target_link_libraries(primo_qf_uflia_solver_tests PRIVATE primo_core)
add_test(NAME primo_qf_uflia_solver_tests COMMAND primo_qf_uflia_solver_tests)

add_executable(primo_lia_gcd_consistency_tests tests/unit/lia_gcd_consistency_tests.cpp)
primo_enable_warnings(primo_lia_gcd_consistency_tests)
primo_enable_sanitizers(primo_lia_gcd_consistency_tests)
target_link_libraries(primo_lia_gcd_consistency_tests PRIVATE primo_core)
add_test(NAME primo_lia_gcd_consistency_tests COMMAND primo_lia_gcd_consistency_tests)

add_executable(primo_lia_patching_tests tests/unit/lia_patching_tests.cpp)
primo_enable_warnings(primo_lia_patching_tests)
primo_enable_sanitizers(primo_lia_patching_tests)
target_link_libraries(primo_lia_patching_tests PRIVATE primo_core)
add_test(NAME primo_lia_patching_tests COMMAND primo_lia_patching_tests)

add_executable(primo_lia_explanation_tests tests/unit/lia_explanation_tests.cpp)
primo_enable_warnings(primo_lia_explanation_tests)
primo_enable_sanitizers(primo_lia_explanation_tests)
target_link_libraries(primo_lia_explanation_tests PRIVATE primo_core)
add_test(NAME primo_lia_explanation_tests COMMAND primo_lia_explanation_tests)

add_executable(primo_lia_gomory_cuts_tests tests/unit/lia_gomory_cuts_tests.cpp)
primo_enable_warnings(primo_lia_gomory_cuts_tests)
primo_enable_sanitizers(primo_lia_gomory_cuts_tests)
target_link_libraries(primo_lia_gomory_cuts_tests PRIVATE primo_core)
add_test(NAME primo_lia_gomory_cuts_tests COMMAND primo_lia_gomory_cuts_tests)
