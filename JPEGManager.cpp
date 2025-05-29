#include "JPEGManager.h"

#include <fcntl.h>
#include <unistd.h>

#include "common.h"

using namespace funnyface;

JPEGManager::JPEGManager(int fd, unsigned long width, unsigned long height, TJSAMP chrominance_subsampling)
{
    d_image_.data = nullptr;

    if ((c_handle_ = tjInitCompress()) == nullptr)
    {
        common::errno_log("JPEGManager::JPEGManager Constructor failed to init compressor");
        common::errno_log((const char*) tjGetErrorStr2(c_handle_));
        return;
    }

    if ((d_handle_ = tjInitDecompress()) == nullptr)
    {
        common::errno_log("JPEGManager::JPEGManager Constructor failed to init decompressor");
        common::errno_log((const char*) tjGetErrorStr2(d_handle_));
        return;
    }

    // Initialize the output buffer
    d_image_.size = maxBufferSize(width, height, chrominance_subsampling);
    common::log_info("JPEGManager::JPEGManager Constructor: JPEG buffer size: %d", d_image_.size);
    d_image_.data = (unsigned char*) malloc(sizeof(unsigned char) * d_image_.size);
    d_image_.info.width = width;
    d_image_.info.height = height;
    d_image_.info.TJSampleFormat = chrominance_subsampling;
    o_fd_ = fd;
}

JPEGManager::~JPEGManager()
{
    if (d_image_.data != nullptr)
    {
        free(d_image_.data);
    }

    // Deallocate data buffer
    int tj_stat = tjDestroy(c_handle_);

    if (tj_stat != 0)
    {
        common::errno_log("JPEGManager::~JPEGManager Deconstructor failed to deinit compressor");
        common::errno_log((const char*) tjGetErrorStr2(c_handle_));
    }

    tj_stat = tjDestroy(d_handle_);

    if (tj_stat != 0)
    {
        common::errno_log("JPEGManager::~JPEGManager Deconstructor failed to deinit decompressor");
        common::errno_log((const char*) tjGetErrorStr2(d_handle_));
    }
}

bool JPEGManager::getJPEGHeaderInfo(Image& image)
{
    int sample_format{TJSAMP_UNKNOWN};
    int color_space{-1};

    int tj_stat = tjDecompressHeader3(d_handle_, image.data, image.size, &image.info.width, &image.info.height,
                                      &sample_format, &color_space);

    if (tj_stat != 0 || sample_format == -1 || color_space == -1)
    {
        common::errno_log("JPEGManager::getJPEGHeaderInfo - Failed to decompress image header");
        common::log_error("JPEGManager::getJPEGHeaderInfo - stat %d cs %d sf%d", tj_stat, color_space, sample_format);
        common::errno_log((const char*) tjGetErrorStr2(d_handle_));
        return false;
    }
    // else
    // {
    //     common::log_info("JPEGManager::getJPEGHeaderInfo - width %d height %d Sample format is %d", image.info.width,
    //     image.info.height, sample_format);
    // }
    image.info.TJSampleFormat = static_cast<TJSAMP>(sample_format);
    image.info.TJColorSpace = static_cast<TJCS>(color_space);
    return true;
}

bool JPEGManager::decodeImage(const Image srcImage, unsigned char** destBuff)
{
    int tj_stat = tjDecompress2(d_handle_, srcImage.data, srcImage.size, *destBuff, 0, 0, 0,
                                srcImage.info.TJPixelFormat, TJFLAG_NOREALLOC);

    if (tj_stat != 0)
    {
        common::errno_log("JPEGManager::decodeImage - Failed to decode image");
        common::errno_log((const char*) tjGetErrorStr2(d_handle_));
        return false;
    }
    return true;
}

bool JPEGManager::readFromFile(const char* fileName, Image& image, TJPF pixelFormat)
{
    // Read file size.
    int fd = open(fileName, O_RDONLY);
    size_t compressed_size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET); // seek to start of file

    unsigned char* aux = (unsigned char*) malloc(sizeof(unsigned char) * compressed_size);
    // Read the image contents
    size_t read_size = read(fd, aux, compressed_size);
    close(fd);
    if (read_size != compressed_size)
    {
        common::log_error("JPEGManager::readFromFile - Error reading file. Read %d. Image is %d", read_size,
                          compressed_size);
        free(aux);
        return false;
    }

    // Use the output image as a temporary variable to get the header info.
    image.data = aux;
    image.size = compressed_size;
    image.info.TJPixelFormat = pixelFormat;

    // Get compression data
    if (!getJPEGHeaderInfo(image))
    {
        free(aux);
        return false;
    }

    // Use a temp var because we cant use the same input/ouput memory
    // TODO: investigate this.
    Image compressed_image_aux;
    compressed_image_aux.data = aux; // TODO: FIXME: This is bad!
    compressed_image_aux.size = compressed_size;
    compressed_image_aux.info = image.info;

    // Decode jpg to pixel format
    if (!decodeImage(compressed_image_aux, &image.data))
    {
        free(aux);
        return false;
    }

    // Update pixel format of the result
    image.info.TJPixelFormat = pixelFormat;

    image.size = computeSizeInBytes(image.info.width, image.info.height, image.info.TJPixelFormat);
    free(aux);
    return true;
}

bool JPEGManager::decodeJPEGHeader(Image& image, unsigned long& size)
{
    if (!getJPEGHeaderInfo(image))
    {
        return false;
    }

    image.info.pixelSizeBytes = tjPixelSize[static_cast<int>(image.info.TJPixelFormat)];
    // common::log_info("Pixel size is %d", image.info.pixelSizeBytes);
    size = computeSizeInBytes(image.info.width, image.info.height, image.info.TJPixelFormat);
    return true;
}

unsigned long JPEGManager::computeSizeInBytes(const int width, const int height, TJPF pixelFormat)
{
    // Compute image size from previous data.
    int pixelSizeBytes = tjPixelSize[static_cast<int>(pixelFormat)];
    return width * height * pixelSizeBytes;
}

bool JPEGManager::saveToFile(const char* fileName, Image image)
{
    int jpgfile;

    if ((jpgfile = open(fileName, O_WRONLY | O_CREAT, 0660)) < 0)
    {
        common::log_error("JPEGManager::saveToFile failed to open file");
        return false;
    }

    if (static_cast<ssize_t>(image.size) != write(jpgfile, image.data, image.size))
    {
        common::log_warn("JPEGManager::saveTofile - Error saving to file. Not all bytes where stored.");
        return false;
    }
    close(jpgfile);
    return true;
}

bool JPEGManager::encodeImage(const Image srcImage, Image& dstImage, int quality)

{
    int tj_stat =
        tjCompress2(c_handle_, srcImage.data, dstImage.info.width, 0, dstImage.info.height, dstImage.info.TJPixelFormat,
                    &dstImage.data, &dstImage.size, dstImage.info.TJSampleFormat, quality, TJFLAG_NOREALLOC);

    // common::log_info(
    //     "Encoding params SrcImage size: %lu, DstImage: %d x %d (%lu) - PxF %d - SampleFormat %d - Quality %d",
    //     srcImage.size, dstImage.info.width, dstImage.info.height, dstImage.size, dstImage.info.TJPixelFormat,
    //     dstImage.info.TJSampleFormat, quality);

    if (tj_stat != 0)
    {
        common::errno_log("JPEGManager::encodeImage - Compressor failed to compress!");
        common::errno_log((const char*) tjGetErrorStr2(c_handle_));
        return false;
    }

    return true;
}

bool JPEGManager::encodeAndWriteToOutput(const Image srcImage, int quality, TJPF pixelFormat)
{
    d_image_.info.TJPixelFormat = pixelFormat;
    if (!encodeImage(srcImage, d_image_, quality))
    {
        return false;
    }

    int written = write(o_fd_, d_image_.data, d_image_.size);
    if (written < 0)
    {
        close(o_fd_);
        common::log_error("JPEGManager::encodeAndWriteToOutput - Cant write to output!");
        return false;
    }
    return true;
}
