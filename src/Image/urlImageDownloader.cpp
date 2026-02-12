#include "LinuxFace/Image/urlImageDownloader.h"

#include <curl/curl.h>
#include <vector>

#include "LinuxFace/codecFactory.h"
#include "LinuxFace/common.h"

namespace linuxface
{

static size_t curlWriteToVector(void* contents, size_t size, size_t nmemb, void* userp)
{
    const size_t total = size * nmemb;
    auto* out = static_cast<std::vector<unsigned char>*>(userp);
    const unsigned char* data = static_cast<unsigned char*>(contents);
    out->insert(out->end(), data, data + total);
    return total;
}

UrlImageDownloader::UrlImageDownloader(const ConfigBuilder& cfg)
{
    // Try to create a decoder according to provided configuration
    decoder_ = CodecFactory::create<Decoder>(cfg);

    // If creation failed and user didn't provide an explicit imageFormat, try a sane default (JPEG->RGB)
    if (!decoder_ && !cfg.has("imageFormat"))
    {
        ConfigBuilder defaultCfg;
        defaultCfg.imageFormat(ImageFormat::JPEG).pixelFormat(TJPF_RGB);
        decoder_ = CodecFactory::create<Decoder>(defaultCfg);
        if (decoder_)
        {
            common::logDebug("UrlImageDownloader - created default JPEG decoder in constructor");
            return;
        }
    }

    if (!decoder_)
    {
        common::logWarn(
            "UrlImageDownloader - decoder creation failed in constructor (no decoder available). "
            "If you expect JPEG support ensure libturbojpeg is installed and the build enabled the JPEG decoder.");
    }
}

UrlImageDownloader::~UrlImageDownloader() = default;

bool UrlImageDownloader::downloadToImage(const std::string& url, std::unique_ptr<Image>& outImage, long timeoutSeconds)
{
    if (!decoder_)
    {
        // Fallback: try to create a default JPEG->RGB decoder on-demand
        ConfigBuilder cfg;
        cfg.imageFormat(ImageFormat::JPEG).pixelFormat(TJPF_RGB);
        decoder_ = CodecFactory::create<Decoder>(cfg);
        if (!decoder_)
        {
            common::logError("UrlImageDownloader::downloadToImage - no decoder available");
            return false;
        }
    }

    if (url.empty())
    {
        common::logError("UrlImageDownloader::downloadToImage - empty URL");
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        common::logError("UrlImageDownloader::downloadToImage - curl_easy_init() failed");
        return false;
    }

    std::vector<unsigned char> bytes;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &curlWriteToVector);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bytes);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "LinuxFace/1.0");

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
    {
        common::logError("UrlImageDownloader::downloadToImage - curl error: %s", curl_easy_strerror(res));
        return false;
    }

    if (httpCode < 200 || httpCode >= 300)
    {
        common::logError("UrlImageDownloader::downloadToImage - HTTP error: %ld", httpCode);
        return false;
    }

    if (bytes.empty())
    {
        common::logError("UrlImageDownloader::downloadToImage - downloaded payload is empty");
        return false;
    }

    // Wrap downloaded bytes in a temporary non-owning Image so decoder can inspect header
    Image srcImage(const_cast<unsigned char*>(bytes.data()), bytes.size(), /*takeOwnership=*/false);
    srcImage.info.format = ImageFormat::JPEG;

    unsigned long rawNeededSize = 0;
    if (!decoder_->decodeHeader(srcImage, rawNeededSize))
    {
        common::logError("UrlImageDownloader::downloadToImage - decodeHeader failed (not a valid JPEG?)");
        return false;
    }

    if (rawNeededSize == 0)
    {
        common::logError("UrlImageDownloader::downloadToImage - decodeHeader returned zero size");
        return false;
    }

    // Decode into a freshly allocated Image
    auto decoded = std::make_unique<Image>(rawNeededSize);
    if (!decoder_->decode(srcImage, *decoded))
    {
        common::logError("UrlImageDownloader::downloadToImage - JPEG decode failed");
        return false;
    }

    // Ensure metadata is set (decoder implementations normally fill this)
    if (!decoded->info.is_valid)
    {
        decoded->info.format = ImageFormat::RGB;
        decoded->info.pixelSizeBytes = 3;
    }

    outImage = std::move(decoded);
    return true;
}

} // namespace linuxface
