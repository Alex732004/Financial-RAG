# FinAgent RAG

A standalone C++20 financial-research project. This initial skeleton fetches
Apple's SEC submissions metadata as the first step toward collecting 10-K filings.
It does not yet download report HTML, parse tables, or run retrieval or an agent.

## Prerequisites

- A C++20 compiler.
- CMake 3.20 or newer.
- libcurl development headers and library. On macOS, these are available through
  the Xcode Command Line Tools.
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

The client currently supports only Apple (CIK `0000320193`). It does not yet
accept a ticker argument or select individual 10-K filings.

The response goes to standard output; the command redirects it into the raw-data
directory. Errors go to standard error. Only use the output if the command exits
successfully: a failed request can leave an empty or partial file.

Keep original metadata and future report HTML in the raw-data directory. Reserve
the processed-data directory for parsed documents and chunks. Downloaded data and
build products are ignored by Git.

## Verification

Run the offline checks after building:

```sh
ctest --test-dir build --output-on-failure
```

Expected result: two passing tests. They check that missing or empty
`SEC_USER_AGENT` values fail with an explanatory error and no filing output.
Neither test contacts the SEC.

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

Parse the submissions JSON and download the latest three available 10-K HTML
filings for each of Apple and Tesla, preserving source metadata alongside them.

## See Also

- [src/main.cpp](src/main.cpp): the initial SEC metadata client.
- [CMakeLists.txt](CMakeLists.txt): build and test configuration.
- [tests/check_identity.cmake](tests/check_identity.cmake): offline identity checks.