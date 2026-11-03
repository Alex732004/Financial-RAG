cmake_minimum_required(VERSION 3.20)

project(FinAgentRAG VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CURL REQUIRED)

find_package(nlohmann_json 3.12 QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.12.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_executable(sec_filings src/main.cpp)
target_compile_features(sec_filings PRIVATE cxx_std_20)
set_target_properties(sec_filings PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(sec_filings PRIVATE CURL::libcurl)

add_executable(filing_parser src/filing_parser.cpp)
target_compile_features(filing_parser PRIVATE cxx_std_20)
set_target_properties(filing_parser PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(filing_parser PRIVATE nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(sec_filings PRIVATE /W4)
    target_compile_options(filing_parser PRIVATE /W4)
else()
    target_compile_options(sec_filings PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(filing_parser PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)

if(BUILD_TESTING)
    foreach(identity_case IN ITEMS missing empty)
        add_test(
            NAME sec_filings_${identity_case}_user_agent
            COMMAND "${CMAKE_COMMAND}"
                "-DSEC_FILINGS_EXECUTABLE=$<TARGET_FILE:sec_filings>"
                "-DIDENTITY_CASE=${identity_case}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_identity.cmake"
        )
    endforeach()

    foreach(parser_case IN ITEMS
        selection insufficient empty malformed missing_fields mismatched_lengths
        wrong_type invalid_date missing_file usage
    )
        add_test(
            NAME filing_parser_${parser_case}
            COMMAND "${CMAKE_COMMAND}"
                "-DFILING_PARSER_EXECUTABLE=$<TARGET_FILE:filing_parser>"
                "-DTEST_CASE=${parser_case}"
                "-DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/parser-tests/${parser_case}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_filing_parser.cmake"
        )
    endforeach()
endif()