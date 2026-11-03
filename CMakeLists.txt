cmake_minimum_required(VERSION 3.20)

project(FinAgentRAG VERSION 0.1.0 LANGUAGES CXX)

find_package(CURL REQUIRED)

add_executable(sec_filings src/main.cpp)
target_compile_features(sec_filings PRIVATE cxx_std_20)
set_target_properties(sec_filings PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(sec_filings PRIVATE CURL::libcurl)

if(MSVC)
    target_compile_options(sec_filings PRIVATE /W4)
else()
    target_compile_options(sec_filings PRIVATE -Wall -Wextra -Wpedantic)
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
endif()