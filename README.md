# FinAgent RAG

A standalone C++20 financial-research project. The current tools fetch Apple's
SEC submissions metadata and select up to three recent 10-K filings from saved
metadata. They do not yet download report HTML, parse tables, or run retrieval
or an agent.

## Prerequisites

- A C++20 compiler.
- CMake 3.20 or newer.
- libcurl development headers and library. On macOS, these are available through
  the Xcode Command Line Tools.
- nlohmann/json 3.12 or newer. If it is not installed, CMake fetches version
  3.12.0; this requires Git and internet access during the initial configuration.
- Internet access and a real contact identity for live SEC requests. No SEC API
  key is required.

## Steps

### 1. Build the client

Run from the project root:

```sh
cmake -S . -B build
cmake --build build
```

### 2. Set your SEC identity

Replace the example name and email with your actual contact details before
making requests. The client sends this value as its HTTP User-Agent.

```sh
export SEC_USER_AGENT='FinAgentRAG YourName your-contact-email@example.com'
```

### 3. Fetch Apple's filing metadata

```sh
./build/sec_filings > data/raw/apple_submissions.json
```

The downloader currently supports only Apple (CIK `0000320193`). It does not yet
accept a ticker argument or download the annual report documents themselves.

The response goes to standard output; the command redirects it into the raw-data
directory. Errors go to standard error. Only use the output if the command exits
successfully: a failed request can leave an empty or partial file.

Keep original metadata and future report HTML in the raw-data directory. Reserve
the processed-data directory for parsed documents and chunks. Downloaded data and
build products are ignored by Git.

### 4. Select the latest 10-K filings

```sh
./build/filing_parser data/raw/apple_submissions.json > data/processed/apple_10ks.json
```

This command runs offline and does not require `SEC_USER_AGENT`. It selects only
exact `10-K` matches from `filings.recent`, excluding amendments such as `10-K/A`,
and sorts them by filing date, newest first. It accepts any company's submissions
JSON with the same SEC schema, not just Apple's.

The output contains `requested`, `found`, `complete`, and a `filings` array. Each
filing includes its `accessionNumber`, `filingDate`, `reportDate`, and
`primaryDocument`.

If fewer than three filings are available, the command still succeeds but sets
`complete` to `false` and prints a warning to standard error. Check that flag
before treating the dataset as complete. Older SEC submission files may contain
additional filings; this parser does not fetch them automatically.

Malformed JSON, missing or misaligned metadata, invalid dates, and unreadable
input files cause a nonzero exit code without filing output.

## Verification

Run the offline checks after building:

```sh
ctest --test-dir build --output-on-failure
```

Expected result: twelve passing tests. They cover missing or empty SEC identities,
filing selection and ordering, incomplete or empty datasets, invalid JSON and
metadata, and command-line errors. All tests are offline and use synthetic data.

Live SEC access is a separate check that requires your real contact identity.
On success, the fetch command saves Apple's submissions JSON, not the actual
10-K annual reports.

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| CMake cannot find CURL | Install the libcurl development headers and library, then rerun configuration. |
| The client asks for SEC_USER_AGENT | Export the variable in the same terminal used to run the client. |
| The SEC returns HTTP 403 or 429 | Check the contact identity and SEC access policy. Avoid repeatedly retrying blocked or rate-limited requests. |

## Next Milestone

Use the selected accession numbers and document filenames to download the
original 10-K HTML. Extend the downloader to Tesla and follow older submissions
files when needed to collect three filings per company, preserving source
metadata alongside them.

## See Also

- [src/main.cpp](src/main.cpp): the initial SEC metadata client.
- [src/filing_parser.cpp](src/filing_parser.cpp): the offline 10-K metadata selector.
- [CMakeLists.txt](CMakeLists.txt): build and test configuration.
- [tests/check_identity.cmake](tests/check_identity.cmake): offline identity checks.
- [tests/check_filing_parser.cmake](tests/check_filing_parser.cmake): synthetic parser checks.