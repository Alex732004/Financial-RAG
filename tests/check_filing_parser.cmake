if(NOT DEFINED FILING_PARSER_EXECUTABLE OR NOT DEFINED TEST_DIRECTORY)
    message(FATAL_ERROR "FILING_PARSER_EXECUTABLE and TEST_DIRECTORY are required.")
endif()

set(submissions [=[
{
  "filings": {
    "recent": {
      "form": ["10-K", "10-Q", "10-K", "10-K/A", "10-K", "10-K"],
      "accessionNumber": ["0000320193-22-000001", "0000320193-25-000002", "0000320193-24-000001", "0000320193-26-000001", "0000320193-25-000001", "0000320193-23-000001"],
      "filingDate": ["2022-10-28", "2025-08-01", "2024-11-01", "2026-01-30", "2025-10-31", "2023-11-03"],
      "reportDate": ["2022-09-24", "2025-06-30", "2024-09-28", "2025-09-27", "2025-09-27", "2023-09-30"],
      "primaryDocument": ["annual-2022.htm", "quarterly-2025.htm", "annual-2024.htm", "amended-2025.htm", "annual-2025.htm", "annual-2023.htm"]
    }
  }
}
]=])

set(expected_rows 4 2 5)
set(expect_failure FALSE)

if(TEST_CASE STREQUAL "insufficient")
    string(JSON submissions SET "${submissions}" filings recent form 0 "\"10-Q\"")
    string(JSON submissions SET "${submissions}" filings recent form 5 "\"10-Q\"")
    set(expected_rows 4 2)
elseif(TEST_CASE STREQUAL "empty")
    string(JSON submissions SET "${submissions}" filings recent [=[
        {"form": [], "accessionNumber": [], "filingDate": [], "reportDate": [], "primaryDocument": []}
    ]=])
    set(expected_rows)
elseif(TEST_CASE STREQUAL "malformed")
    set(submissions "{")
    set(expect_failure TRUE)
elseif(TEST_CASE STREQUAL "missing_fields")
    string(JSON submissions REMOVE "${submissions}" filings recent primaryDocument)
    set(expect_failure TRUE)
elseif(TEST_CASE STREQUAL "mismatched_lengths")
    string(JSON submissions REMOVE "${submissions}" filings recent primaryDocument 5)
    set(expect_failure TRUE)
elseif(TEST_CASE STREQUAL "wrong_type")
    string(JSON submissions SET "${submissions}" filings recent accessionNumber 4 123)
    set(expect_failure TRUE)
elseif(TEST_CASE STREQUAL "invalid_date")
    string(JSON submissions SET "${submissions}" filings recent filingDate 4 "\"2025-02-30\"")
    set(expect_failure TRUE)
elseif(TEST_CASE STREQUAL "missing_file" OR TEST_CASE STREQUAL "usage")
    set(expect_failure TRUE)
elseif(NOT TEST_CASE STREQUAL "selection")
    message(FATAL_ERROR "Unknown parser test case: ${TEST_CASE}")
endif()

file(MAKE_DIRECTORY "${TEST_DIRECTORY}")
set(input_file "${TEST_DIRECTORY}/submissions.json")
if(TEST_CASE STREQUAL "missing_file")
    file(REMOVE "${input_file}")
elseif(NOT TEST_CASE STREQUAL "usage")
    file(WRITE "${input_file}" "${submissions}")
endif()

set(parser_arguments "${input_file}")
if(TEST_CASE STREQUAL "usage")
    set(parser_arguments)
endif()

execute_process(
    COMMAND "${FILING_PARSER_EXECUTABLE}" ${parser_arguments}
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE error_output
    TIMEOUT 5
)

if(expect_failure)
    if(NOT exit_code STREQUAL "1")
        message(FATAL_ERROR "Expected parser failure, got: ${exit_code}.")
    endif()
    if(NOT standard_output STREQUAL "" OR error_output STREQUAL "")
        message(FATAL_ERROR "Invalid input must produce an error and no filing output.")
    endif()
    return()
endif()

if(NOT exit_code STREQUAL "0")
    message(FATAL_ERROR "Expected parser success, got: ${exit_code}. ${error_output}")
endif()

list(LENGTH expected_rows expected_count)
string(JSON filing_count LENGTH "${standard_output}" filings)
string(JSON complete GET "${standard_output}" complete)
string(JSON found GET "${standard_output}" found)
string(JSON requested GET "${standard_output}" requested)
if(NOT filing_count EQUAL expected_count OR NOT found EQUAL expected_count OR NOT requested EQUAL 3)
    message(FATAL_ERROR "Unexpected filing count: ${standard_output}")
endif()

if(expected_count LESS 3)
    if(complete OR error_output STREQUAL "")
        message(FATAL_ERROR "Incomplete selections must be flagged and produce a warning.")
    endif()
elseif(NOT complete OR NOT error_output STREQUAL "")
    message(FATAL_ERROR "Complete selections must not produce warnings: ${error_output}")
endif()

if(expected_count GREATER 0)
    math(EXPR last_position "${expected_count} - 1")
    foreach(position RANGE 0 ${last_position})
        list(GET expected_rows ${position} source_row)
        foreach(field IN ITEMS accessionNumber filingDate reportDate primaryDocument)
            string(JSON expected GET "${submissions}" filings recent "${field}" ${source_row})
            string(JSON actual GET "${standard_output}" filings ${position} "${field}")
            if(NOT actual STREQUAL expected)
                message(FATAL_ERROR "Incorrect ${field} at selection position ${position}.")
            endif()
        endforeach()
    endforeach()
endif()