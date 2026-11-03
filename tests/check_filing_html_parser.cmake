if(NOT DEFINED FILING_HTML_PARSER_EXECUTABLE OR NOT DEFINED TEST_DIRECTORY)
    message(FATAL_ERROR "FILING_HTML_PARSER_EXECUTABLE and TEST_DIRECTORY are required.")
endif()

set(html [=[
<!DOCTYPE html>
<html><head><title>Not filing content</title><style>body { color: black; }</style></head>
<body>
<h2>Financial Statements</h2>
<p>(USD&nbsp;in millions)</p>
<table>
  <caption>Consolidated income</caption>
  <tr><th rowspan="2">Metric</th><th colspan="2">Fiscal year</th></tr>
  <tr><th>2025</th><th>2024</th></tr>
  <tr><td>Revenue</td><td>1,234</td><td></td></tr>
</table>
<p>Revenue <span>increased</span>.</p>
<script>Do not index this script.</script>
</body></html>
]=])

set(expect_failure FALSE)
set(expected_text "")
set(use_manifest FALSE)
set(manifest [=[
{
  "cik": "320193",
  "filings": [
    {"accessionNumber": "0000320193-25-000001", "filingDate": "2025-10-31", "reportDate": "2025-09-27",
     "primaryDocument": "filing.html", "localPath": "filing.html",
     "sourceUrl": "https://www.sec.gov/Archives/edgar/data/320193/000032019325000001/filing.html"}
  ]
}
]=])

if(TEST_CASE STREQUAL "inline")
    set(html "<html><body><div>Net <span>in</span><span>come</span>: <ix:nonfraction>1,234</ix:nonfraction><br>USD millions</div></body></html>")
    set(expected_text "Net income: 1,234 USD millions")
elseif(TEST_CASE STREQUAL "hidden")
    set(html [=[<html><body><div hidden>Hidden one</div><div style=" DISPLAY : none !important;">Hidden two</div>
        <div aria-hidden="true">Hidden three</div><ix:hidden>Hidden facts</ix:hidden>
        <p style="display:nonexistent">Visible text</p></body></html>]=])
    set(expected_text "Visible text")
elseif(TEST_CASE STREQUAL "nested")
    set(html "<html><body><table><tr><td>Outer<table><tr><th>Revenue</th><td>123</td></tr></table>End</td></tr></table></body></html>")
elseif(TEST_CASE STREQUAL "groups")
    set(html [=[<html><body><table><tbody><tr><th rowspan="0">Metric</th><td>123</td></tr>
        <tr><td>456</td></tr></tbody><tbody><tr><td>Other</td></tr></tbody></table></body></html>]=])
elseif(TEST_CASE STREQUAL "entities")
    set(html "<html><body><p>Cash &amp; cash equivalents&nbsp;USD 10</p></body></html>")
    set(expected_text "Cash & cash equivalents USD 10")
elseif(TEST_CASE STREQUAL "recovered")
    set(html "<html><body><p>Recoverable <b>text")
    set(expected_text "Recoverable text")
elseif(TEST_CASE STREQUAL "empty")
    set(html "")
    set(expect_failure TRUE)
elseif(TEST_CASE STREQUAL "no_content")
    set(html "<html><body><script>Not filing content</script></body></html>")
    set(expect_failure TRUE)
elseif(TEST_CASE STREQUAL "bad_span")
    set(html "<html><body><table><tr><td colspan='invalid'>123</td></tr></table></body></html>")
    set(expect_failure TRUE)
elseif(TEST_CASE STREQUAL "missing_file" OR TEST_CASE STREQUAL "usage")
    set(expect_failure TRUE)
elseif(TEST_CASE MATCHES "^manifest" OR TEST_CASE STREQUAL "malformed_manifest")
    set(use_manifest TRUE)
    if(TEST_CASE STREQUAL "manifest_mismatch")
        string(JSON manifest SET "${manifest}" filings 0 localPath "\"other.html\"")
        set(expect_failure TRUE)
    elseif(TEST_CASE STREQUAL "manifest_duplicate")
        string(JSON duplicate GET "${manifest}" filings 0)
        string(JSON manifest SET "${manifest}" filings 1 "${duplicate}")
        set(expect_failure TRUE)
    elseif(TEST_CASE STREQUAL "manifest_missing_fields")
        string(JSON manifest REMOVE "${manifest}" filings 0 sourceUrl)
        set(expect_failure TRUE)
    elseif(TEST_CASE STREQUAL "malformed_manifest")
        set(manifest "{")
        set(expect_failure TRUE)
    elseif(NOT TEST_CASE STREQUAL "manifest")
        message(FATAL_ERROR "Unknown manifest test case: ${TEST_CASE}")
    endif()
elseif(NOT TEST_CASE STREQUAL "structure")
    message(FATAL_ERROR "Unknown HTML parser test case: ${TEST_CASE}")
endif()

file(MAKE_DIRECTORY "${TEST_DIRECTORY}")
set(input_file "${TEST_DIRECTORY}/filing.html")
if(TEST_CASE STREQUAL "missing_file")
    file(REMOVE "${input_file}")
else()
    file(WRITE "${input_file}" "${html}")
endif()
set(parser_arguments "${input_file}")
if(use_manifest)
    set(manifest_file "${TEST_DIRECTORY}/manifest.json")
    file(WRITE "${manifest_file}" "${manifest}")
    list(APPEND parser_arguments "${manifest_file}")
elseif(TEST_CASE STREQUAL "usage")
    set(parser_arguments)
endif()
execute_process(
    COMMAND "${FILING_HTML_PARSER_EXECUTABLE}" ${parser_arguments}
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE error_output
    TIMEOUT 5
)
if(expect_failure)
    if(NOT exit_code STREQUAL "1" OR NOT standard_output STREQUAL "" OR error_output STREQUAL "")
        message(FATAL_ERROR "Invalid inputs must fail without structured output: ${exit_code}. ${error_output}")
    endif()
    return()
endif()
if(NOT exit_code STREQUAL "0" OR NOT error_output STREQUAL "")
    message(FATAL_ERROR "Expected successful HTML parsing: ${exit_code}. ${error_output}")
endif()

string(JSON block_count LENGTH "${standard_output}" blocks)
string(JSON source_path GET "${standard_output}" source path)
if(NOT source_path STREQUAL input_file)
    message(FATAL_ERROR "Source path must identify the parsed file.")
endif()
if(NOT expected_text STREQUAL "")
    string(JSON text GET "${standard_output}" blocks 0 text)
    if(NOT block_count EQUAL 1 OR NOT text STREQUAL expected_text)
        message(FATAL_ERROR "Inline text or visibility was not preserved: ${standard_output}")
    endif()
    return()
elseif(TEST_CASE STREQUAL "nested")
    string(JSON parent_rows LENGTH "${standard_output}" blocks 0 rows)
    string(JSON nested_rows LENGTH "${standard_output}" blocks 0 rows 0 cells 0 tables 0 rows)
    string(JSON nested_value GET "${standard_output}" blocks 0 rows 0 cells 0 tables 0 rows 0 cells 1 text)
    string(JSON parent_text GET "${standard_output}" blocks 0 rows 0 cells 0 text)
    if(NOT block_count EQUAL 1 OR NOT parent_rows EQUAL 1 OR NOT nested_rows EQUAL 1
    OR NOT nested_value STREQUAL "123" OR NOT parent_text STREQUAL "Outer End")
        message(FATAL_ERROR "Nested table rows must stay separate from their parent: ${standard_output}")
    endif()
    return()
elseif(TEST_CASE STREQUAL "groups")
    string(JSON rowspan GET "${standard_output}" blocks 0 rows 0 cells 0 rowspan)
    string(JSON first_group GET "${standard_output}" blocks 0 rows 0 groupXpath)
    string(JSON second_group GET "${standard_output}" blocks 0 rows 1 groupXpath)
    string(JSON third_group GET "${standard_output}" blocks 0 rows 2 groupXpath)
    if(NOT rowspan EQUAL 0 OR NOT first_group STREQUAL second_group OR first_group STREQUAL third_group)
        message(FATAL_ERROR "Zero rowspan and row-group boundaries must be preserved.")
    endif()
    return()
elseif(TEST_CASE STREQUAL "manifest")
    foreach(field IN ITEMS accessionNumber filingDate reportDate primaryDocument sourceUrl)
        string(JSON expected GET "${manifest}" filings 0 "${field}")
        string(JSON actual GET "${standard_output}" source "${field}")
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "Missing source provenance field: ${field}")
        endif()
    endforeach()
    string(JSON cik GET "${standard_output}" source cik)
    if(NOT cik STREQUAL "320193")
        message(FATAL_ERROR "Source CIK must come from the matching manifest.")
    endif()
endif()
if(NOT block_count EQUAL 4)
    message(FATAL_ERROR "Expected heading, units, table, and paragraph in order: ${standard_output}")
endif()
set(expected_types heading paragraph table paragraph)
foreach(position RANGE 0 3)
    list(GET expected_types ${position} expected_type)
    string(JSON actual_type GET "${standard_output}" blocks ${position} type)
    string(JSON block_id GET "${standard_output}" blocks ${position} id)
    string(JSON xpath GET "${standard_output}" blocks ${position} xpath)
    math(EXPR block_number "${position} + 1")
    if(NOT actual_type STREQUAL expected_type OR NOT block_id STREQUAL "block-${block_number}" OR xpath STREQUAL "")
        message(FATAL_ERROR "Incorrect ordered block or missing citation anchor.")
    endif()
endforeach()

string(JSON units GET "${standard_output}" blocks 1 text)
string(JSON caption GET "${standard_output}" blocks 2 caption)
string(JSON row_count LENGTH "${standard_output}" blocks 2 rows)
string(JSON rowspan GET "${standard_output}" blocks 2 rows 0 cells 0 rowspan)
string(JSON colspan GET "${standard_output}" blocks 2 rows 0 cells 1 colspan)
string(JSON header GET "${standard_output}" blocks 2 rows 1 cells 0 header)
string(JSON year GET "${standard_output}" blocks 2 rows 1 cells 0 text)
string(JSON empty_cell GET "${standard_output}" blocks 2 rows 2 cells 2 text)
string(JSON value GET "${standard_output}" blocks 2 rows 2 cells 1 text)
string(JSON paragraph GET "${standard_output}" blocks 3 text)
if(NOT units STREQUAL "(USD in millions)" OR NOT caption STREQUAL "Consolidated income"
   OR NOT row_count EQUAL 3 OR NOT rowspan EQUAL 2 OR NOT colspan EQUAL 2
   OR NOT header OR NOT year STREQUAL "2025" OR NOT empty_cell STREQUAL ""
   OR NOT value STREQUAL "1,234" OR NOT paragraph STREQUAL "Revenue increased.")
    message(FATAL_ERROR "Financial text, table spans, or empty-cell alignment were lost: ${standard_output}")
endif()