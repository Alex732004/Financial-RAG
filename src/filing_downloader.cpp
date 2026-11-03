#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

class CurlRuntime {
public:
    CurlRuntime() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw std::runtime_error("Could not initialize libcurl.");
        }
    }

    ~CurlRuntime() {
        curl_global_cleanup();
    }

    CurlRuntime(const CurlRuntime&) = delete;
    CurlRuntime& operator=(const CurlRuntime&) = delete;
};

bool is_digits(const std::string& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char character) {
        return character >= '0' && character <= '9';
    });
}

nlohmann::json build_manifest(const nlohmann::json& selection, std::string cik) {
    if (cik.size() > 10 || !is_digits(cik) || cik.find_first_not_of('0') == std::string::npos) {
        throw std::runtime_error("CIK must contain 1 to 10 digits and must not be zero.");
    }
    cik.erase(0, cik.find_first_not_of('0'));

    const auto& filings = selection.at("filings");
    if (!filings.is_array() || filings.empty() || filings.size() > 3) {
        throw std::runtime_error("Selection must contain between one and three filings.");
    }
    if (selection.at("requested") != 3 || selection.at("found") != filings.size()
        || selection.at("complete") != (filings.size() == 3)) {
        throw std::runtime_error("Selection counts and completeness must match the filings.");
    }

    auto manifest = nlohmann::json{{"cik", cik},
                                   {"selectionComplete", selection.at("complete")},
                                   {"downloaded", 0},
                                   {"filings", nlohmann::json::array()}};
    std::set<std::string> accessions;
    for (auto filing : filings) {
        for (const char* field : {"accessionNumber", "filingDate", "reportDate", "primaryDocument"}) {
            if (filing.at(field).get<std::string>().empty()) {
                throw std::runtime_error("Filing metadata must contain nonempty strings.");
            }
        }
        const auto accession = filing.at("accessionNumber").get<std::string>();
        if (accession.size() != 20 || accession[10] != '-' || accession[13] != '-') {
            throw std::runtime_error("Invalid SEC accession number.");
        }
        std::string accession_digits = accession;
        std::erase(accession_digits, '-');
        if (!is_digits(accession_digits) || accession_digits.size() != 18
            || !accessions.insert(accession).second) {
            throw std::runtime_error("Accession numbers must be valid and unique.");
        }

        const auto document = filing.at("primaryDocument").get<std::string>();
        const bool safe_filename = std::all_of(document.begin(), document.end(), [](unsigned char character) {
            return std::isalnum(character) || character == '.' || character == '-' || character == '_';
        });
        if (!safe_filename || (!document.ends_with(".htm") && !document.ends_with(".html"))) {
            throw std::runtime_error("Primary document must be a plain .htm or .html filename.");
        }

        filing["sourceUrl"] = "https://www.sec.gov/Archives/edgar/data/" + cik + "/"
                              + accession_digits + "/" + document;
        filing["localPath"] = (std::filesystem::path(accession) / document).generic_string();
        manifest["filings"].push_back(filing);
    }
    return manifest;
}

std::size_t write_html(char* data, std::size_t size, std::size_t count, void* context) {
    auto& output = *static_cast<std::ofstream*>(context);
    const std::size_t length = size * count;
    output.write(data, static_cast<std::streamsize>(length));
    return output ? length : 0;
}

void download_html(const std::string& url, const std::filesystem::path& path, const char* user_agent) {
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> request(curl_easy_init(), curl_easy_cleanup);
    if (!request) {
        throw std::runtime_error("Could not create an HTTP request.");
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("Could not open document output: " + path.string());
    }

    curl_easy_setopt(request.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(request.get(), CURLOPT_USERAGENT, user_agent);
    curl_easy_setopt(request.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(request.get(), CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(request.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(request.get(), CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(request.get(), CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(request.get(), CURLOPT_WRITEFUNCTION, write_html);
    curl_easy_setopt(request.get(), CURLOPT_WRITEDATA, &output);

    const CURLcode result = curl_easy_perform(request.get());
    output.close();
    if (result != CURLE_OK) {
        throw std::runtime_error("SEC download failed for " + url + ": " + curl_easy_strerror(result));
    }
    long response_code = 0;
    char* content_type = nullptr;
    curl_easy_getinfo(request.get(), CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_getinfo(request.get(), CURLINFO_CONTENT_TYPE, &content_type);
    const std::string media_type = content_type == nullptr ? "" : content_type;
    if (response_code != 200) {
        throw std::runtime_error("Unexpected SEC HTTP status: " + std::to_string(response_code));
    }
    if (!output || std::filesystem::file_size(path) == 0
        || (!media_type.starts_with("text/html") && !media_type.starts_with("application/xhtml+xml"))) {
        throw std::runtime_error("SEC response must be nonempty HTML and saved without errors.");
    }
}

void download_filings(nlohmann::json& manifest, std::filesystem::path directory, const char* user_agent) {
    directory = directory.lexically_normal();
    if (directory.filename().empty()) {
        directory = directory.parent_path();
    }
    auto staging = directory;
    staging += ".part";
    if (directory.empty() || std::filesystem::exists(directory) || std::filesystem::is_symlink(directory)) {
        throw std::runtime_error("Choose a new output directory; existing output will not be overwritten.");
    }
    if (!std::filesystem::create_directory(staging)) {
        throw std::runtime_error("Staging directory already exists: " + staging.string());
    }

    try {
        bool first_request = true;
        for (const auto& filing : manifest.at("filings")) {
            const auto destination = staging / filing.at("localPath").get<std::string>();
            std::filesystem::create_directories(destination.parent_path());
            if (!first_request) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
            first_request = false;
            const auto url = filing.at("sourceUrl").get<std::string>();
            std::cerr << "Downloading " << url << '\n';
            download_html(url, destination, user_agent);
        }
        manifest["downloaded"] = manifest.at("filings").size();
        std::ofstream manifest_file(staging / "manifest.json", std::ios::binary);
        manifest_file << manifest.dump(2) << '\n';
        manifest_file.close();
        if (!manifest_file) {
            throw std::runtime_error("Could not write the filing manifest.");
        }
        std::filesystem::rename(staging, directory);
    } catch (...) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(staging, cleanup_error);
        throw;
    }
}

}

int main(int argument_count, char* arguments[]) {
    if ((argument_count != 4 && argument_count != 5)
        || (argument_count == 5 && std::string(arguments[4]) != "--dry-run")) {
        std::cerr << "Usage: filing_downloader <selected-filings.json> <CIK> <new-output-directory> [--dry-run]\n";
        return EXIT_FAILURE;
    }

    try {
        std::ifstream input(arguments[1]);
        if (!input) {
            throw std::runtime_error("Could not open selected filings: " + std::string(arguments[1]));
        }
        auto manifest = build_manifest(nlohmann::json::parse(input), arguments[2]);
        if (!manifest.at("selectionComplete").get<bool>()) {
            std::cerr << "Warning: the selection contains fewer than three 10-K filings.\n";
        }
        if (argument_count == 5) {
            manifest["dryRun"] = true;
            std::cout << manifest.dump(2) << '\n';
            return EXIT_SUCCESS;
        }

        const char* user_agent = std::getenv("SEC_USER_AGENT");
        if (user_agent == nullptr || *user_agent == '\0'
            || std::string(user_agent).find_first_of("\r\n") != std::string::npos) {
            throw std::runtime_error("Set SEC_USER_AGENT to your project name and contact email.");
        }
        CurlRuntime curl_runtime;
        download_filings(manifest, arguments[3], user_agent);
        std::cout << manifest.dump(2) << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Could not download filings: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}














