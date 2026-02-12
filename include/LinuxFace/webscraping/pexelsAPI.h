#ifndef PEXELSAPI_H
#define PEXELSAPI_H

#include <LinuxFace/common.h>
#include <LinuxFace/Image/urlImageDownloader.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>

// Pexels image source variants (keys used in Pexels API `src` object)
enum class PexelsSrc
{
    ORIGINAL,
    LARGE,
    LARGE2X,
    MEDIUM,
    SMALL,
    PORTRAIT,
    LANDSCAPE,
    TINY
};

// Utility functions associated with PexelsSrc (keeps enum compact while providing helpers)
struct PexelsSrcHelper
{
    static const char* toString(PexelsSrc s) noexcept
    {
        switch (s)
        {
            case PexelsSrc::ORIGINAL: return "original";
            case PexelsSrc::LARGE:    return "large";
            case PexelsSrc::LARGE2X:  return "large2x";
            case PexelsSrc::MEDIUM:   return "medium";
            case PexelsSrc::SMALL:    return "small";
            case PexelsSrc::PORTRAIT: return "portrait";
            case PexelsSrc::LANDSCAPE: return "landscape";
            case PexelsSrc::TINY:     return "tiny";
            default:                  return "original";
        }
    }

    static const std::vector<const char*>& names()
    {
        static const std::vector<const char*> values = {toString(PexelsSrc::ORIGINAL), toString(PexelsSrc::LARGE),
                                                         toString(PexelsSrc::LARGE2X), toString(PexelsSrc::MEDIUM),
                                                         toString(PexelsSrc::SMALL), toString(PexelsSrc::PORTRAIT),
                                                         toString(PexelsSrc::LANDSCAPE), toString(PexelsSrc::TINY)};
        return values;
    }

    static PexelsSrc fromIndex(int idx) noexcept
    {
        const int map[] = {static_cast<int>(PexelsSrc::ORIGINAL), static_cast<int>(PexelsSrc::LARGE),
                           static_cast<int>(PexelsSrc::LARGE2X), static_cast<int>(PexelsSrc::MEDIUM),
                           static_cast<int>(PexelsSrc::SMALL), static_cast<int>(PexelsSrc::PORTRAIT),
                           static_cast<int>(PexelsSrc::LANDSCAPE), static_cast<int>(PexelsSrc::TINY)};
        const int size = static_cast<int>(sizeof(map) / sizeof(map[0]));
        if (idx < 0 || idx >= size) idx = 0;
        return static_cast<PexelsSrc>(map[idx]);
    }

    static int count() noexcept { return static_cast<int>(names().size()); }
};

struct SearchResult
{
    std::string alt;
    unsigned int width;
    unsigned int height;

    // Map of available src variants returned by Pexels API (enum -> url)
    struct PexelsSrcHash
    {
        size_t operator()(PexelsSrc s) const noexcept
        {
            using UT = std::underlying_type_t<PexelsSrc>;
            return std::hash<UT>()(static_cast<UT>(s));
        }
    };

    std::unordered_map<PexelsSrc, std::string, PexelsSrcHash> srcs;

    SearchResult(nlohmann::json j, const std::string& query)
    {
        alt = getOrWarn<std::string>(j, "alt", query);
        width = getOrWarn<unsigned int>(j, "width", query);
        height = getOrWarn<unsigned int>(j, "height", query);

        if (j.contains("src") && j["src"].is_object())
        {
            const auto& src = j["src"];
            const std::vector<std::pair<std::string, PexelsSrc>> mappings = {{"original", PexelsSrc::ORIGINAL},
                                                                              {"large", PexelsSrc::LARGE},
                                                                              {"large2x", PexelsSrc::LARGE2X},
                                                                              {"medium", PexelsSrc::MEDIUM},
                                                                              {"small", PexelsSrc::SMALL},
                                                                              {"portrait", PexelsSrc::PORTRAIT},
                                                                              {"landscape", PexelsSrc::LANDSCAPE},
                                                                              {"tiny", PexelsSrc::TINY}};

            for (const auto& [k, enumKey] : mappings)
            {
                if (src.contains(k) && src[k].is_string())
                {
                    srcs[enumKey] = src[k].get<std::string>();
                }
            }
        }
    }

    // Get URL for a specific PexelsSrc variant (returns empty string if not available)
    std::string getUrlFor(PexelsSrc s) const
    {
        auto it = srcs.find(s);
        if (it != srcs.end())
            return it->second;
        return std::string();
    }

  private:
    template <typename T>
    T getOrWarn(const nlohmann::json& j, const std::string& key, const std::string& query)
    {
        if (!j.contains(key))
        {
            linuxface::common::logWarn("PexelsAPI::search - photo missing '%s' field for query: %s", key.c_str(),
                                       query.c_str());
            return T();
        }
        return j[key].get<T>();
    }
};

class PexelsAPI
{
  public:
    explicit PexelsAPI(std::string apiKey);
    PexelsAPI(const PexelsAPI&) = delete;
    PexelsAPI& operator=(const PexelsAPI&) = delete;

    // Perform a search and return Search result
    std::vector<SearchResult> search(const std::string& query, int num_results) const;

    // Convenience: return a vector of image URLs extracted from the response
    std::vector<std::string> fetchImageUrls(const std::string& query, int per_page = 15, int page = 1) const;

    // Request statistics returned by Pexels on successful responses
    struct RequestStats
    {
        int limit{-1};              // X-Ratelimit-Limit
        int remaining{-1};          // X-Ratelimit-Remaining
        int consumed{-1};           // computed: limit - remaining (or -1 when unknown)
        double daysUntilReset{0.0}; // computed: (reset - now) in days, 0.0 if reset in the past
        std::time_t reset{0};       // X-Ratelimit-Reset (UNIX timestamp)
        bool valid{false};          // true if headers were present and parsed
    };

    // Return the last-known request statistics (thread-safe)
    RequestStats getRequestStats() const noexcept;

  private:
    std::string apiKey_;

    // Performs a GET request and returns the response body as string
    // Also parses and updates `lastRequestStats_` when the response is successful
    std::string performGet(const std::string& url, const std::vector<std::string>& headers) const;


    nlohmann::json searchJson(const std::string& query, int per_page, int page) const;

    // libcurl write callback
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);

    // Last parsed request statistics (mutable because performGet is const)
    mutable RequestStats lastRequestStats_;
    mutable std::mutex statsMutex_;

    static constexpr const char* BASE_URL = "https://api.pexels.com/v1";
};

#endif // PEXELSAPI_H
