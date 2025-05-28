#ifndef JPEGMANAGER_H
#define JPEGMANAGER_H

#include <turbojpeg.h>
#include "image.h"
namespace funnyface
{

class JPEGManager
{
  public:
    JPEGManager(int fd, unsigned long width, unsigned long height, TJSAMP chrominance_subsampling);
    ~JPEGManager();
    bool getJPEGHeaderInfo(Image& image);


    bool saveToFile(const char* fileName, Image image);
    bool readFromFile(const char* fileName, Image& image, TJPF pixelFormat = TJPF_RGB);

    bool decodeImage(const Image srcImage, unsigned char** destBuff);

    /**
     * Encode image to JPEG format.
     * @param srcImage - source image
     * @param dstImage - destination image. It will be filled with encoded data.
     * Destination info will be read from dstImage too.
     * @param quality - JPEG quality. 100 is the best quality. 1 is the worst quality.
     */
    bool encodeImage(const Image srcImage, Image& dstImage, int quality = 100);

    bool encodeAndWriteToOutput(const Image srcImage, int quality = 100, TJPF pixelFormat = TJPF_RGB);

    bool decodeJPEGHeader(Image& image, unsigned long& size);


  private:
    unsigned long computeSizeInBytes(const int width, const int height, TJPF pixelFormat);

    inline unsigned long maxBufferSize(unsigned long width, unsigned long height, TJSAMP chrominance_subsampling) const
    {
        return tjBufSize(width, height, chrominance_subsampling);
    }

    // Decompress handler
    tjhandle d_handle_;
    // Compress handler
    tjhandle c_handle_;

    Image d_image_;

    int o_fd_;
};

} // namespace funnyface

#endif // JPEGMANAGER_H
