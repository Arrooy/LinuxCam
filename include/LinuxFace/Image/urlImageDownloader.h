#ifndef URLIMAGEDOWNLOADER_H
#define URLIMAGEDOWNLOADER_H

#include <memory>
#include <string>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/codecFactory.h"

namespace linuxface
{

// Helper to download a JPEG image from a URL and decode it into an `Image`.
// The class keeps a Decoder instance so multiple downloads can reuse the decoder
// without reallocating it each time.
// TODO: add tests (mock libcurl or add an integration test).
class UrlImageDownloader
{
  public:
    // Construct with optional decoder configuration. The constructor will attempt
    // to create a Decoder using `cfg`. If creation fails and no explicit
    // `imageFormat` is provided in `cfg`, the constructor will try a sensible
    // default (JPEG -> RGB). `downloadToImage` also lazily creates a decoder
    // on-demand if necessary.
    explicit UrlImageDownloader(const ConfigBuilder& cfg = ConfigBuilder());

    ~UrlImageDownloader();

    UrlImageDownloader(const UrlImageDownloader&) = delete;
    UrlImageDownloader& operator=(const UrlImageDownloader&) = delete;

    UrlImageDownloader(UrlImageDownloader&&) noexcept = default;
    UrlImageDownloader& operator=(UrlImageDownloader&&) noexcept = default;

    // Returns true if the internal decoder was successfully created
    bool isValid() const noexcept { return decoder_ != nullptr; }

    // Download image at `url` and decode it into `outImage`.
    // Returns true on success and fills `outImage` with an RGB image.
    // Timeout is in seconds (default 10s).
    bool downloadToImage(const std::string& url, std::unique_ptr<Image>& outImage, long timeoutSeconds = 10);

    // Convenience one-shot call (keeps previous static-style behaviour)
    static bool downloadToImageOneShot(const std::string& url, std::unique_ptr<Image>& outImage,
                                      long timeoutSeconds = 10)
    {
        UrlImageDownloader d; // default config
        return d.downloadToImage(url, outImage, timeoutSeconds);
    }

  private:
    std::unique_ptr<Decoder> decoder_;
};

} // namespace linuxface

#endif // URLIMAGEDOWNLOADER_H
