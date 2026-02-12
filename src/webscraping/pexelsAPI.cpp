#include "LinuxFace/webscraping/pexelsAPI.h"

#include <algorithm>
#include <curl/curl.h>
#include <memory>
#include <sstream>
#include <stdexcept>

using linuxface::common::logDebug;
using linuxface::common::logError;
using linuxface::common::logWarn;
using linuxface::common::logInfo;
using nlohmann::json;

PexelsAPI::PexelsAPI(std::string apiKey) : apiKey_(std::move(apiKey))
{
    if (apiKey_.empty())
    {
        logError("PexelsAPI: apiKey must not be empty");
        throw std::invalid_argument("PexelsAPI: apiKey must not be empty");
    }
    logDebug("PexelsAPI constructed (key length=%zu)", apiKey_.size());
}

// Helper: trim whitespace from both ends of a string
static inline std::string trim(const std::string& s)
{
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return std::string();
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

size_t PexelsAPI::writeCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total = size * nmemb;
    std::string* out = static_cast<std::string*>(userp);
    out->append(static_cast<char*>(contents), total);
    return total;
}

// libcurl header callback: collect header lines into a std::map<std::string,std::string>* passed via userdata
static size_t pexelsHeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata)
{
    const size_t total = size * nitems;
    if (userdata == nullptr || total == 0)
        return total;

    auto* hdrs = static_cast<std::map<std::string, std::string>*>(userdata);
    std::string line(buffer, total);

    // Ignore status line (e.g. HTTP/1.1 200 OK)
    if (line.find(':') == std::string::npos) return total;

    auto colon = line.find(':');
    std::string name = trim(line.substr(0, colon));
    std::string value = trim(line.substr(colon + 1));

    // Store header names lowercased so lookups are case-insensitive (HTTP header names are case-insensitive)
    if (!name.empty())
    {
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        (*hdrs)[name] = value;
    }

    return total;
}

std::string PexelsAPI::performGet(const std::string& url, const std::vector<std::string>& headers) const
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        logError("curl_easy_init() failed");
        throw std::runtime_error("curl_easy_init() failed");
    }

    std::string response;
    struct curl_slist* hdrs = nullptr;
    for (const auto& h : headers)
    {
        hdrs = curl_slist_append(hdrs, h.c_str());
    }

    // collect response headers so we can parse rate-limit info
    std::map<std::string, std::string> responseHeaders;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &PexelsAPI::writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &pexelsHeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &responseHeaders);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
    {
        logError("curl error: %s", curl_easy_strerror(res));
        std::ostringstream oss;
        oss << "curl error: " << curl_easy_strerror(res);
        throw std::runtime_error(oss.str());
    }

    if (status < 200 || status >= 300)
    {
        std::ostringstream oss;
        oss << "http error: " << status << " (body=" << response << ")";
        throw std::runtime_error(oss.str());
    }

    // On successful 2xx responses, parse Pexels rate-limit headers (if present) and log them
    PexelsAPI::RequestStats stats;
    // lookup using lowercased header names (header callback lowercases keys)
    auto itLimit = responseHeaders.find("x-ratelimit-limit");
    auto itRemaining = responseHeaders.find("x-ratelimit-remaining");
    auto itReset = responseHeaders.find("x-ratelimit-reset");

    if (itLimit != responseHeaders.end() || itRemaining != responseHeaders.end() || itReset != responseHeaders.end())
    {
        try
        {
            if (itLimit != responseHeaders.end())
                stats.limit = std::stoi(itLimit->second);
            if (itRemaining != responseHeaders.end())
                stats.remaining = std::stoi(itRemaining->second);
            if (itReset != responseHeaders.end())
                stats.reset = static_cast<std::time_t>(std::stoll(itReset->second));

            // compute derived values (non-fatal if limit/remaining not provided)
            stats.valid = true;
            if (stats.limit >= 0 && stats.remaining >= 0)
            {
                stats.consumed = stats.limit - stats.remaining;
            }
            else
            {
                stats.consumed = -1;
            }

            // compute days until reset using current time
            {
                const std::time_t now = std::time(nullptr);
                if (stats.reset > now)
                {
                    const double secs = static_cast<double>(stats.reset - now);
                    stats.daysUntilReset = secs / 86400.0; // seconds -> days
                }
                else
                {
                    stats.daysUntilReset = 0.0;
                }
            }

            // store thread-safely
            {
                const std::lock_guard<std::mutex> lock(statsMutex_);
                lastRequestStats_ = stats;
            }

            // Log the statistics for visibility (include computed values)
            logInfo("PexelsAPI rate-limit: limit=%d remaining=%d consumed=%d daysUntilReset=%.2f reset=%lld",
                    stats.limit, stats.remaining, stats.consumed, stats.daysUntilReset,
                    static_cast<long long>(stats.reset));

            // If some headers were not returned, log which ones at info level
            std::vector<std::string> missing;
            if (itLimit == responseHeaders.end()) missing.push_back("X-Ratelimit-Limit");
            if (itRemaining == responseHeaders.end()) missing.push_back("X-Ratelimit-Remaining");
            if (itReset == responseHeaders.end()) missing.push_back("X-Ratelimit-Reset");
            if (!missing.empty())
            {
                std::ostringstream mh;
                for (size_t i = 0; i < missing.size(); ++i)
                {
                    if (i) mh << ", ";
                    mh << missing[i];
                }
                logInfo("PexelsAPI: rate-limit headers missing: %s", mh.str().c_str());
            }

            // Warn when remaining quota is low
            if (stats.remaining >= 0 && stats.limit > 0 && stats.remaining < (stats.limit / 10 + 1))
            {
                logWarn("PexelsAPI rate limit low: %d/%d requests remaining (reset at %lld)",
                        stats.remaining, stats.limit, static_cast<long long>(stats.reset));
            }
        }
        catch (const std::exception& ex)
        {
            // parsing failure shouldn't break the request — just log
            logWarn("PexelsAPI: failed parsing rate-limit headers: %s", ex.what());
        }
    }
    else
    {
        // No rate-limit headers returned on this successful response — log at info level
        // Also dump which headers *are* present to help debugging
        if (!responseHeaders.empty())
        {
            std::ostringstream present;
            bool first = true;
            for (const auto& h : responseHeaders)
            {
                if (!first) present << ", ";
                present << h.first;
                first = false;
            }
            logInfo("PexelsAPI: no rate-limit headers present on successful response (url=%s). headers present: %s",
                    url.c_str(), present.str().c_str());
        }
        else
        {
            logInfo("PexelsAPI: no response headers present on successful response (url=%s)", url.c_str());
        }
    }

    return response;
}

nlohmann::json PexelsAPI::searchJson(const std::string& query, int per_page, int page) const
{
    if (query.empty())
    {
        throw std::invalid_argument("query must not be empty");
    }
    if (per_page <= 0)
    {
        per_page = 15;
    }
    if (page <= 0)
    {
        page = 1;
    }

    // URL-encode the query using libcurl helper
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        throw std::runtime_error("curl_easy_init() failed (encode)");
    }
    char* esc = curl_easy_escape(curl, query.c_str(), static_cast<int>(query.size()));
    if (!esc)
    {
        curl_easy_cleanup(curl);
        throw std::runtime_error("failed to URL-encode query");
    }

    //TODO: Hacked fields that we can also use and are not documented online:
    /*
        people_count=1 or people_count=3_plus (other may exist like any or 2)
        people_age any by default
            child
            baby
            teenager
            adult
            senior_adult
        date_from any by default
        	last_24_hours
            last_week
            last_month
            last_year
    */
    std::ostringstream url;
    url << BASE_URL << "/search?query=" << esc << "&people_count=1" << "&per_page=" << per_page << "&page=" << page;

    curl_free(esc);
    curl_easy_cleanup(curl);

    std::vector<std::string> hdrs = {std::string("Authorization: ") + apiKey_, "Accept: application/json"};
    std::string body = performGet(url.str(), hdrs);

    try
    {
        return json::parse(body);
    }
    catch (const std::exception& ex)
    {
        std::ostringstream oss;
        oss << "failed to parse pexels response: " << ex.what();
        throw std::runtime_error(oss.str());
    }
}

std::vector<SearchResult> PexelsAPI::search(const std::string& query, int num_results) const
{
    // TODO: paging handling
    json j = searchJson(query, num_results, 0);

    std::vector<SearchResult> results;
    if (j.contains("photos") && j["photos"].is_array() && !j["photos"].empty())
    {
        for (const auto& photo : j["photos"])
        {
            results.push_back(SearchResult(photo, query));
        }
    }
    else
    {
        logWarn("PexelsAPI::search - no photos found for query: %s", query.c_str());
    }
    return results;
}

std::vector<std::string> PexelsAPI::fetchImageUrls(const std::string& query, int per_page, int page) const
{
    json j = searchJson(query, per_page, page);
    std::vector<std::string> urls;

    if (!j.contains("photos") || !j["photos"].is_array())
    {
        return urls;
    }

    for (const auto& p : j["photos"])
    {
        // prefer 'original' then 'large', then any available src field
        try
        {
            if (p.contains("src") && p["src"].is_object())
            {
                const auto& src = p["src"];
                if (src.contains("original"))
                {
                    urls.push_back(src["original"].get<std::string>());
                }
                else if (src.contains("large"))
                {
                    urls.push_back(src["large"].get<std::string>());
                }
                else if (!src.empty())
                {
                    urls.push_back(src.begin()->get<std::string>());
                }
            }
        }
        catch (...)
        {
            // ignore malformed entries and continue
        }
    }

    return urls;
}

// Return a copy of the last-known request statistics. Thread-safe and noexcept.
PexelsAPI::RequestStats PexelsAPI::getRequestStats() const noexcept
{
    std::lock_guard<std::mutex> lock(statsMutex_);
    return lastRequestStats_;
}
