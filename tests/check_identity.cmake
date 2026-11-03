if(NOT DEFINED SEC_FILINGS_EXECUTABLE)
    message(FATAL_ERROR "SEC_FILINGS_EXECUTABLE is required.")
endif()

if(IDENTITY_CASE STREQUAL "missing")
    set(identity_environment "--unset=SEC_USER_AGENT")
elseif(IDENTITY_CASE STREQUAL "empty")
    set(identity_environment "SEC_USER_AGENT=")
else()
    message(FATAL_ERROR "Unknown identity test case: ${IDENTITY_CASE}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "${identity_environment}" "${SEC_FILINGS_EXECUTABLE}"
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE error_output
    TIMEOUT 5
)

if(NOT exit_code STREQUAL "1")
    message(FATAL_ERROR "Expected exit code 1, got: ${exit_code}. ${error_output}")
endif()

if(NOT standard_output STREQUAL "")
    message(FATAL_ERROR "Invalid contact identity must not produce filing data.")
endif()

if(NOT error_output MATCHES "SEC_USER_AGENT")
    message(FATAL_ERROR "Expected an error identifying the missing SEC_USER_AGENT.")
endif()