if(NOT DEFINED GIT_BRANCH)
    execute_process(
            COMMAND           bash -c "git branch | sed -n 's/^\\* //p'"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE   GIT_BRANCH
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

if(NOT DEFINED GIT_HASH)
    execute_process(
            COMMAND           git --no-pager describe --always --dirty
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE   GIT_HASH
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

if(NOT DEFINED GIT_VERSION)
    execute_process(
            COMMAND           git --no-pager describe --tags --always --dirty
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE   GIT_VERSION
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

if(NOT DEFINED GIT_DATE)
    execute_process(
            COMMAND           bash -c "git --no-pager show --date=short --format=\"%ai\" --name-only | head -n 1"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE   GIT_DATE
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

if(NOT DEFINED GIT_TAG)
    execute_process(
            COMMAND           git describe --tags --abbrev=0
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE   GIT_TAG
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

# If CIRCLE_JOB is unset (local compilation), ask uname -m and add locally compiled comment
if(DEFINED CIRCLE_JOB)
    set(FTL_ARCH "${CIRCLE_JOB} (compiled on CI)")
else()
    execute_process(
            COMMAND           uname -m
            OUTPUT_VARIABLE   UNAME
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(FTL_ARCH "${UNAME} (compiled locally)")
endif()

# Get compiler version
execute_process(
        COMMAND           bash -c "${CMAKE_C_COMPILER} --version | head -n 1"
        OUTPUT_VARIABLE   FTL_CC
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

message("Making FTL version on branch ${GIT_BRANCH} - ${GIT_VERSION} / ${GIT_TAG} / ${GIT_HASH} (${GIT_DATE})")

# configure the version file, but output to a temporary location
configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/version.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/version~
        @ONLY
)

# compare with the real version file
execute_process(
        COMMAND
        ${CMAKE_COMMAND} -E compare_files
        ${CMAKE_CURRENT_BINARY_DIR}/version~
        ${CMAKE_CURRENT_BINARY_DIR}/version.h
        RESULT_VARIABLE
        VERSION_NEEDS_UPDATING

        OUTPUT_QUIET
        ERROR_QUIET
)

# update the real version file if necessary
if(VERSION_NEEDS_UPDATING)
    execute_process(
            COMMAND
            ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/version~
            ${CMAKE_CURRENT_BINARY_DIR}/version.h
    )
endif()
