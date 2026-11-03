# FinAgent RAG

A standalone C++20 financial-research project. The tools fetch SEC submissions
metadata, select up to three recent 10-K filings, and download their original
HTML with a source manifest. Table parsing, retrieval, and agent orchestration
are not implemented yet.

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

The metadata client currently supports only Apple (CIK `0000320193`). It does
not yet accept a ticker argument. Download the report documents in step 5.

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

### 5. Download the original filing HTML

First preview the SEC archive URLs and planned relative filenames without making
requests or creating files:

```sh
./build/filing_downloader data/processed/apple_10ks.json 0000320193 data/raw/apple_10ks --dry-run
```

With your real `SEC_USER_AGENT` set, omit `--dry-run` to download the reports:

```sh
./build/filing_downloader data/processed/apple_10ks.json 0000320193 data/raw/apple_10ks
```

The output directory must be new, and its parent directory must already exist.
Each HTML file is stored under its accession-number directory. The batch manifest
records the CIK, filing metadata, source URLs, relative file paths, and downloaded
count. It is saved inside the output directory and also printed to standard output.

Downloads run sequentially with at least 250 ms between requests. The client
checks HTTP status, HTML content type, nonempty content, timeouts, and file-write
errors. It does not follow redirects or automatically retry rejected requests.
Progress and errors go to standard error.

Files are staged in a sibling directory ending in `.part` and published together
after every download and the manifest succeed. Failed batches are cleaned up;
existing output and staging directories are never overwritten. An incomplete
input selection still downloads its available reports but retains
`selectionComplete: false` in the manifest and prints a warning.

## Verification

Run the offline checks after building:

```sh
ctest --test-dir build --output-on-failure
```

Expected result: 28 passing tests. They cover SEC identities, filing selection,
URL construction, unsafe or malformed metadata, partial datasets, overwrite
protection, and cleanup after failed transfers. Tests use synthetic data and a
blocked local proxy for transfer failures; they do not contact the SEC.

Live SEC access is a separate check requiring your real contact identity. The
metadata client saves submissions JSON; the document downloader saves the annual
report HTML. Offline tests do not verify live SEC availability or successful
real-report downloads.

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| CMake cannot find CURL | Install the libcurl development headers and library, then rerun configuration. |
| The client asks for SEC_USER_AGENT | Export the variable in the same terminal used to run the client. |
| The SEC returns HTTP 403 or 429 | Check the contact identity and SEC access policy. Avoid repeatedly retrying blocked or rate-limited requests. |
| The document downloader rejects existing output | Choose a new output directory. Review leftover staging data before removing it yourself. |

## Next Milestone

Collect and inspect three 10-K HTML reports each for Apple and Tesla. Extend the
metadata client to Tesla and follow older submissions files when necessary to
complete the selections. Then build structure-preserving HTML parsing and chunking.

## See Also

- [src/main.cpp](src/main.cpp): the initial SEC metadata client.
- [src/filing_parser.cpp](src/filing_parser.cpp): the offline 10-K metadata selector.
- [src/filing_downloader.cpp](src/filing_downloader.cpp): the original HTML downloader and source manifest writer.
- [CMakeLists.txt](CMakeLists.txt): build and test configuration.
- [tests/check_identity.cmake](tests/check_identity.cmake): offline identity checks.
- [tests/check_filing_parser.cmake](tests/check_filing_parser.cmake): synthetic parser checks.