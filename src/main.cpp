#include <curl/curl.h>

#include <cstdlib>
#include <iostream>

int main() {
    const char* user_agent = std::getenv("SEC_USER_AGENT");
    if (user_agent == nullptr || *user_agent == '\0') {
        std::cerr << "Set SEC_USER_AGENT to your project name and contact email.\n";
        return EXIT_FAILURE;
    }

    const CURLcode initialization_result = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialization_result != CURLE_OK) {
        std::cerr << "Could not initialize libcurl: "
                  << curl_easy_strerror(initialization_result) << '\n';
        return EXIT_FAILURE;
    }

    CURL* request = curl_easy_init();
    if (request == nullptr) {
        std::cerr << "Could not create an HTTP request.\n";
        curl_global_cleanup();
        return EXIT_FAILURE;
    }

    curl_easy_setopt(request, CURLOPT_URL,
                     "https://data.sec.gov/submissions/CIK0000320193.json");
    curl_easy_setopt(request, CURLOPT_USERAGENT, user_agent);
    curl_easy_setopt(request, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(request, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(request, CURLOPT_TIMEOUT, 30L);

    const CURLcode result = curl_easy_perform(request);
    if (result != CURLE_OK) {
        std::cerr << "SEC request failed: " << curl_easy_strerror(result) << '\n';
    }

    curl_easy_cleanup(request);
    curl_global_cleanup();
    return result == CURLE_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}