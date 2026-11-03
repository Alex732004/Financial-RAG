#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::size_t requested_count = 3;

bool is_valid_date(const std::string& date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }

    for (std::size_t position = 0; position < date.size(); ++position) {
        if (position == 4 || position == 7) {
            continue;
        }
        if (date[position] < '0' || date[position] > '9') {
            return false;
        }
    }

    const std::chrono::year_month_day calendar_date{
        std::chrono::year{std::stoi(date.substr(0, 4))},
        std::chrono::month{static_cast<unsigned>(std::stoi(date.substr(5, 2)))},
        std::chrono::day{static_cast<unsigned>(std::stoi(date.substr(8, 2)))}};
    return calendar_date.ok();
}

nlohmann::json select_latest_10ks(const nlohmann::json& submissions) {
    const auto& recent = submissions.at("filings").at("recent");
    const auto& forms = recent.at("form");
    const std::array<const char*, 4> metadata_fields{
        "accessionNumber", "filingDate", "reportDate", "primaryDocument"};

    if (!forms.is_array()) {
        throw std::runtime_error("SEC form entries must be an array.");
    }
    for (const char* field : metadata_fields) {
        const auto& values = recent.at(field);
        if (!values.is_array() || values.size() != forms.size()) {
            throw std::runtime_error("SEC filing fields must be arrays of equal length.");
        }
    }

    std::vector<nlohmann::json> candidates;
    for (std::size_t index = 0; index < forms.size(); ++index) {
        if (forms.at(index).get<std::string>() != "10-K") {
            continue;
        }

        nlohmann::json filing = nlohmann::json::object();
        for (const char* field : metadata_fields) {
            const auto& value = recent.at(field).at(index);
            if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
                throw std::runtime_error("10-K field must be a nonempty string: "
                                         + std::string(field));
            }
            filing[field] = value;
        }
        if (!is_valid_date(filing.at("filingDate").get_ref<const std::string&>())
            || !is_valid_date(filing.at("reportDate").get_ref<const std::string&>())) {
            throw std::runtime_error("10-K dates must be valid YYYY-MM-DD values.");
        }
        candidates.push_back(filing);
    }

    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        return left.at("filingDate").template get_ref<const std::string&>()
               > right.at("filingDate").template get_ref<const std::string&>();
    });
    if (candidates.size() > requested_count) {
        candidates.resize(requested_count);
    }

    return {{"requested", requested_count},
            {"found", candidates.size()},
            {"complete", candidates.size() == requested_count},
            {"filings", candidates}};
}

}

int main(int argument_count, char* arguments[]) {
    if (argument_count != 2) {
        std::cerr << "Usage: filing_parser <submissions.json>\n";
        return EXIT_FAILURE;
    }

    try {
        std::ifstream input(arguments[1]);
        if (!input) {
            throw std::runtime_error("Could not open submissions file: "
                                     + std::string(arguments[1]));
        }

        const auto result = select_latest_10ks(nlohmann::json::parse(input));
        if (!result.at("complete").get<bool>()) {
            std::cerr << "Warning: only " << result.at("found")
                      << " of 3 requested 10-K filings were found in filings.recent. "
                      << "Older SEC submission files may contain additional filings.\n";
        }
        std::cout << result.dump(2) << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Could not parse filings: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}