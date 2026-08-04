build_lib(
  LIBNAME satcompute
  SOURCE_FILES
    model/satcompute-version.cc
    model/scenario-config.cc
    model/sha256.cc
    topology/snapshot/snapshot-reader.cc
    topology/snapshot/snapshot-schedule.cc
  HEADER_FILES
    model/satcompute-version.h
    model/scenario-config.h
    model/sha256.h
    topology/snapshot/snapshot-reader.h
    topology/snapshot/snapshot-schedule.h
    topology/snapshot/snapshot-types.h
  LIBRARIES_TO_LINK ${libcore}
)

# The platform entry point is a project executable, not an ns-3 example.  It is
# therefore available when the global examples and tests switches are disabled.
build_exec(
  EXECNAME satcompute
  EXECNAME_PREFIX satcompute_app_
  SOURCE_FILES app/satcompute.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute
)

# Project-owned C++ checks are normal executables because SCP-SatComPlate does
# not enable ns-3's global test suite.
build_exec(
  EXECNAME satcompute-config-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/config-loader-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)

build_exec(
  EXECNAME satcompute-snapshot-test
  EXECNAME_PREFIX satcompute_test_
  SOURCE_FILES tests/unit/snapshot-test.cc
  LIBRARIES_TO_LINK ${libsatcompute}
  EXECUTABLE_DIRECTORY_PATH
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/contrib/satcompute/tests
)
