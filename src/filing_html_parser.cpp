#include <libxml/HTMLparser.h>
#include <libxml/tree.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using Json = nlohmann::json;

struct XmlStringDeleter {
    void operator()(xmlChar* value) const {
        xmlFree(value);
    }
};

std::string attribute(xmlNode* node, const char* name) {
    const std::unique_ptr<xmlChar, XmlStringDeleter> value(xmlGetProp(node, BAD_CAST name));
    return value ? reinterpret_cast<const char*>(value.get()) : "";
}

std::string node_path(xmlNode* node) {
    const std::unique_ptr<xmlChar, XmlStringDeleter> value(xmlGetNodePath(node));
    return value ? reinterpret_cast<const char*>(value.get()) : "";
}

std::string_view tag(xmlNode* node) {
    return node->name == nullptr ? "" : reinterpret_cast<const char*>(node->name);
}

std::string normalize_text(const std::string& text) {
    std::string normalized;
    bool pending_space = false;
    for (std::size_t position = 0; position < text.size(); ++position) {
        const auto character = static_cast<unsigned char>(text[position]);
        const bool nonbreaking_space = character == 0xc2 && position + 1 < text.size()
                                       && static_cast<unsigned char>(text[position + 1]) == 0xa0;
        if (std::isspace(character) || nonbreaking_space) {
            pending_space = !normalized.empty();
            if (nonbreaking_space) {
                ++position;
            }
        } else {
            if (pending_space) {
                normalized += ' ';
            }
            normalized += text[position];
            pending_space = false;
        }
    }
    return normalized;
}

bool is_hidden(xmlNode* node) {
    const auto name = tag(node);
    if (name == "head" || name == "script" || name == "style" || name == "noscript"
        || name == "template" || name == "ix:hidden" || name == "hidden"
        || xmlHasProp(node, BAD_CAST "hidden") != nullptr || attribute(node, "aria-hidden") == "true") {
        return true;
    }
    auto style = attribute(node, "style");
    std::erase_if(style, [](unsigned char character) { return std::isspace(character); });
    std::transform(style.begin(), style.end(), style.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    style.insert(style.begin(), ';');
        style += ';';
        return style.find(";display:none;") != std::string::npos
            || style.find(";display:none!important;") != std::string::npos
            || style.find(";visibility:hidden;") != std::string::npos
            || style.find(";visibility:hidden!important;") != std::string::npos;
}

bool paragraph_boundary(std::string_view name) {
    return name == "p" || name == "div" || name == "section" || name == "article"
           || name == "li" || name == "ul" || name == "ol" || name == "blockquote"
           || name == "pre" || name == "header" || name == "footer";
}

void collect_text(xmlNode* first, std::string& text) {
    for (auto* node = first; node != nullptr; node = node->next) {
        if (node->type == XML_TEXT_NODE || node->type == XML_CDATA_SECTION_NODE) {
            if (node->content != nullptr) {
                text += reinterpret_cast<const char*>(node->content);
            }
        } else if (node->type == XML_ELEMENT_NODE && !is_hidden(node)) {
            if (tag(node) == "table") {
                text += ' ';
                continue;
            }
            const bool boundary = paragraph_boundary(tag(node)) || tag(node) == "br";
            if (boundary) {
                text += ' ';
            }
            collect_text(node->children, text);
            if (boundary) {
                text += ' ';
            }
        }
    }
}

unsigned span(xmlNode* cell, const char* name) {
    const auto value = attribute(cell, name);
    if (value.empty()) {
        return 1;
    }
    unsigned result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()
        || result > 65534 || (result == 0 && std::string_view(name) == "colspan")) {
        throw std::runtime_error("Unsupported table span: " + value);
    }
    return result;
}

Json parse_table(xmlNode* table);

void collect_nested_tables(xmlNode* first, Json& tables) {
    for (auto* node = first; node != nullptr; node = node->next) {
        if (node->type != XML_ELEMENT_NODE || is_hidden(node)) {
            continue;
        }
        if (tag(node) == "table") {
            tables.push_back(parse_table(node));
        } else {
            collect_nested_tables(node->children, tables);
        }
    }
}

void collect_rows(xmlNode* first, Json& table) {
    for (auto* node = first; node != nullptr; node = node->next) {
        if (node->type != XML_ELEMENT_NODE || is_hidden(node) || tag(node) == "table") {
            continue;
        }
        if (tag(node) == "caption") {
            std::string text;
            collect_text(node->children, text);
            table["caption"] = normalize_text(text);
        } else if (tag(node) == "tr") {
            Json cells = Json::array();
            for (auto* cell = node->children; cell != nullptr; cell = cell->next) {
                if (cell->type != XML_ELEMENT_NODE || is_hidden(cell)
                    || (tag(cell) != "th" && tag(cell) != "td")) {
                    continue;
                }
                std::string text;
                collect_text(cell->children, text);
                Json parsed_cell{{"text", normalize_text(text)},
                                 {"header", tag(cell) == "th"},
                                 {"rowspan", span(cell, "rowspan")},
                                 {"colspan", span(cell, "colspan")}};
                Json nested = Json::array();
                collect_nested_tables(cell->children, nested);
                if (!nested.empty()) {
                    parsed_cell["tables"] = nested;
                }
                cells.push_back(parsed_cell);
            }
            table["rows"].push_back({{"cells", cells}, {"groupXpath", node_path(node->parent)}});
        } else {
            collect_rows(node->children, table);
        }
    }
}

Json parse_table(xmlNode* table) {
    Json result{{"type", "table"}, {"xpath", node_path(table)},
                {"caption", ""}, {"rows", Json::array()}};
    collect_rows(table->children, result);
    return result;
}

class FilingExtractor {
public:
    Json extract(xmlNode* root) {
        walk(root);
        flush();
        if (blocks_.empty()) {
            throw std::runtime_error("The HTML contains no visible filing content.");
        }
        return blocks_;
    }

private:
    Json blocks_ = Json::array();
    std::string pending_;
    xmlNode* origin_ = nullptr;

    void append(Json block) {
        block["id"] = "block-" + std::to_string(blocks_.size() + 1);
        blocks_.push_back(block);
    }

    void flush() {
        const auto text = normalize_text(pending_);
        if (!text.empty()) {
            append({{"type", "paragraph"}, {"text", text}, {"xpath", node_path(origin_)}});
        }
        pending_.clear();
        origin_ = nullptr;
    }

    void walk(xmlNode* first) {
        for (auto* node = first; node != nullptr; node = node->next) {
            if (node->type == XML_TEXT_NODE || node->type == XML_CDATA_SECTION_NODE) {
                if (node->content != nullptr) {
                    if (origin_ == nullptr) {
                        origin_ = node;
                    }
                    pending_ += reinterpret_cast<const char*>(node->content);
                }
                continue;
            }
            if (node->type != XML_ELEMENT_NODE || is_hidden(node)) {
                continue;
            }
            const auto name = tag(node);
            if (name == "table") {
                flush();
                append(parse_table(node));
            } else if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
                flush();
                std::string text;
                collect_text(node->children, text);
                text = normalize_text(text);
                if (!text.empty()) {
                    append({{"type", "heading"}, {"level", name[1] - '0'},
                            {"text", text}, {"xpath", node_path(node)}});
                }
            } else if (name == "br") {
                pending_ += ' ';
            } else {
                const bool boundary = paragraph_boundary(name);
                if (boundary) {
                    flush();
                }
                walk(node->children);
                if (boundary) {
                    flush();
                }
            }
        }
    }
};

Json source_metadata(const std::filesystem::path& input_path, const std::filesystem::path& manifest_path) {
    Json source{{"path", input_path.generic_string()}};
    if (manifest_path.empty()) {
        return source;
    }
    std::ifstream input(manifest_path);
    if (!input) {
        throw std::runtime_error("Could not open source manifest: " + manifest_path.string());
    }
    const auto manifest = Json::parse(input);
    const auto& filings = manifest.at("filings");
    if (!filings.is_array()) {
        throw std::runtime_error("Manifest filings must be an array.");
    }

    const auto target = std::filesystem::weakly_canonical(input_path);
    bool matched = false;
    for (const auto& filing : filings) {
        const auto relative = std::filesystem::path(filing.at("localPath").get<std::string>());
        if (relative.empty() || relative.is_absolute()
            || std::any_of(relative.begin(), relative.end(), [](const auto& part) { return part == ".."; })) {
            throw std::runtime_error("Manifest document paths must stay inside the download directory.");
        }
        if (std::filesystem::weakly_canonical(manifest_path.parent_path() / relative) != target) {
            continue;
        }
        if (matched) {
            throw std::runtime_error("Manifest contains duplicate entries for this filing.");
        }
        for (const char* field : {"accessionNumber", "filingDate", "reportDate", "primaryDocument", "sourceUrl"}) {
            const auto value = filing.at(field).get<std::string>();
            if (value.empty()) {
                throw std::runtime_error("Manifest source fields must be nonempty strings.");
            }
            source[field] = value;
        }
        const auto cik = manifest.at("cik").get<std::string>();
        if (cik.empty()) {
            throw std::runtime_error("Manifest must identify its company CIK.");
        }
        source["cik"] = cik;
        source["manifest"] = manifest_path.generic_string();
        matched = true;
    }
    if (!matched) {
        throw std::runtime_error("No manifest entry matches this HTML file.");
    }
    return source;
}

}

int main(int argument_count, char* arguments[]) {
    if (argument_count != 2 && argument_count != 3) {
        std::cerr << "Usage: filing_html_parser <filing.html> [download-manifest.json]\n";
        return EXIT_FAILURE;
    }
    try {
        std::ifstream input(arguments[1], std::ios::binary);
        if (!input) {
            throw std::runtime_error("Could not open filing HTML: " + std::string(arguments[1]));
        }
        const std::string html((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (input.bad() || html.empty() || html.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("Filing HTML must be readable, nonempty, and smaller than 2 GB.");
        }

        const int options = HTML_PARSE_RECOVER | HTML_PARSE_NONET | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING;
        const std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)> document(
            htmlReadMemory(html.data(), static_cast<int>(html.size()), arguments[1], nullptr, options), xmlFreeDoc);
        if (!document) {
            throw std::runtime_error("libxml2 could not parse the filing HTML.");
        }
        FilingExtractor extractor;
        const auto manifest_path = argument_count == 3 ? std::filesystem::path(arguments[2]) : std::filesystem::path{};
        const Json result{{"source", source_metadata(arguments[1], manifest_path)},
                          {"blocks", extractor.extract(xmlDocGetRootElement(document.get()))}};
        std::cout << result.dump(2) << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Could not parse filing HTML: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}