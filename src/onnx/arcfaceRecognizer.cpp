#include "LinuxFace/onnx/arcfaceRecognizer.h"

#include <cmath>
#include <fstream>

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/common.h"
#include "LinuxFace/math_utils.h"
#include "LinuxFace/profiler.h"

using namespace linuxface;

ArcfaceRecognizer::ArcfaceRecognizer(const std::string& onnxModelPath)
    : OnnxDetector(onnxModelPath), inswapper_compatible_mode_(false)
{
    // Initialize emap matrix as empty - will be loaded when enableInswapperCompatibility is called
    emap_matrix_.clear();
}

std::unique_ptr<Image>
ArcfaceRecognizer::preprocess(const Image& inputImg, const std::vector<math_utils::Point<>>& faceLandmark5)
{
    const int targetSize = 112;
    auto [aligned, affine] =
        image_utils::affineFaceTransform(inputImg, faceLandmark5, image_utils::TEMPLATE_112, targetSize);
    if (aligned)
    {
        return std::move(aligned);
    }
    common::logError("ArcfaceRecognizer: Failed to align face, using fallback scaling.");
    // Fallback: just scale the whole image
    return inputImg.scale(targetSize, targetSize);
}

std::pair<std::unique_ptr<Image>, std::array<double, 6>>
ArcfaceRecognizer::preprocessWithCache(const Image& inputImg, Face& face)
{
    const int targetSize = 112;
    
    // Get 5-point landmarks for face alignment
    const auto& landmarks = face.getFivePointLandmarksArcFaceOrder2D();
    
    // Check cache first
    std::unique_ptr<Image> alignedFace;
    std::array<double, 6> affineTransform;
    
    if (!face.getAlignmentFromCache(targetSize, image_utils::TEMPLATE_112, ImageFormat::RGB, alignedFace, affineTransform,
                                     inputImg.info.width, inputImg.info.height))
    {
        // Cache miss - compute alignment
        auto [computedAlignedFace, computedAffine] =
            image_utils::affineFaceTransform(inputImg, landmarks, image_utils::TEMPLATE_112, targetSize);
        
        if (computedAlignedFace)
        {
            affineTransform = computedAffine;
            // Store in cache for future use
            face.cacheAlignment(computedAlignedFace->deepCopy(), affineTransform, targetSize, image_utils::TEMPLATE_112, 
                               ImageFormat::RGB, inputImg.info.width, inputImg.info.height);
            alignedFace = std::move(computedAlignedFace);
        }
        else
        {
            common::logError("ArcfaceRecognizer: Failed to align face, using fallback scaling.");
            // Fallback: just scale the whole image - don't cache this fallback
            return {inputImg.scale(targetSize, targetSize), std::array<double, 6>{1, 0, 0, 0, 1, 0}};
        }
    }
    
    return {std::move(alignedFace), affineTransform};
}

Ort::Value ArcfaceRecognizer::transform(const std::unique_ptr<Image>& imgRs)
{
    input_node_dims[0] = 1;
    input_node_dims[1] = imgRs->isColorImage() ? 3 : 1;
    input_node_dims[2] = imgRs->info.height;
    input_node_dims[3] = imgRs->info.width;
    Ort::Value inputTensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());
    auto* tensorData = inputTensor.GetTensorMutableData<float>();
    // Use MINMAX normalization as a common default for ArcFace
    TensorPadding padding = TensorPadding::noPadding();
    imgRs->toTensor(tensorData, padding, imgRs->info.width, imgRs->info.height, NormalizationType::MINMAX);
    return inputTensor;
}

bool ArcfaceRecognizer::recognize(const Image& inputImg, const std::vector<math_utils::Point<>>& faceLandmark5,
                                  std::vector<float>& embedding, bool inswapperCompatible)
{
    Profiler::getInstance().start("ArcfaceRecognizer", "recognize");
    if (!ready_)
    {
        return false;
    }
    const std::unique_ptr<Image> cropImage = preprocess(inputImg, faceLandmark5);
    const Ort::Value inputTensor = transform(cropImage);
    const Ort::RunOptions runOptions;
    auto outputTensors = detector_session_->Run(runOptions, input_node_names_.data(), &inputTensor, 1,
                                                output_node_names_.data(), output_node_names_str_.size());
    auto* pdata = outputTensors[0].GetTensorMutableData<float>();
    embedding.clear();
    embedding.assign(pdata, pdata + 512);

    // Apply inswapper transformation if requested and enabled
    if (inswapperCompatible && inswapper_compatible_mode_)
    {
        embedding = transformEmbeddingForInswapper(embedding);
    }
    math_utils::l2norm(embedding);

    Profiler::getInstance().stop("ArcfaceRecognizer", "recognize");
    return true;
}

bool ArcfaceRecognizer::recognize(const Image& inputImg, Face& face, std::vector<float>& embedding,
                                  bool inswapperCompatible)
{
    Profiler::getInstance().start("ArcfaceRecognizer", "recognize_cached");
    if (!ready_)
    {
        return false;
    }
    
    // Use cached alignment if available, compute if not
    auto [cropImage, affine] = preprocessWithCache(inputImg, face);
    
    const Ort::Value inputTensor = transform(cropImage);
    const Ort::RunOptions runOptions;
    auto outputTensors = detector_session_->Run(runOptions, input_node_names_.data(), &inputTensor, 1,
                                                output_node_names_.data(), output_node_names_str_.size());
    auto* pdata = outputTensors[0].GetTensorMutableData<float>();
    embedding.clear();
    embedding.assign(pdata, pdata + 512);

    // Apply inswapper transformation if requested and enabled
    if (inswapperCompatible && inswapper_compatible_mode_)
    {
        embedding = transformEmbeddingForInswapper(embedding);
    }
    math_utils::l2norm(embedding);

    Profiler::getInstance().stop("ArcfaceRecognizer", "recognize_cached");
    return true;
}

bool ArcfaceRecognizer::enableInswapperCompatibility(const std::string& inswapperModelPath)
{
    return loadEmapMatrixFromOnnx(inswapperModelPath);
}

std::vector<float> ArcfaceRecognizer::transformEmbeddingForInswapper(const std::vector<float>& arcfaceEmbedding)
{
    if (!inswapper_compatible_mode_ || emap_matrix_.size() != EmbeddingSize * EmbeddingSize)
    {
        common::logError("ArcfaceRecognizer: Inswapper compatibility not properly initialized");
        return arcfaceEmbedding; // Return original embedding if transformation fails
    }

    if (arcfaceEmbedding.size() != EmbeddingSize)
    {
        common::logError("ArcfaceRecognizer: Invalid embedding size for transformation. Expected %d, got %zu",
                         EmbeddingSize, arcfaceEmbedding.size());
        return arcfaceEmbedding;
    }

    // Apply emap transformation exactly like face_utils::dot_product
    // result[j] = sum(vec[i] * matrix[i * matrix_cols + j]) for each column j
    // This computes: transformed = arcfaceEmbedding^T * emap_matrix
    std::vector<float> transformed(EmbeddingSize, 0.0f);
    for (int j = 0; j < EmbeddingSize; ++j)
    {
        float sum = 0.0f;
        for (int i = 0; i < EmbeddingSize; ++i)
        {
            sum += arcfaceEmbedding[i] * emap_matrix_[i * EmbeddingSize + j];
        }
        transformed[j] = sum;
    }


    return transformed;
}

bool ArcfaceRecognizer::loadEmapMatrixFromOnnx(const std::string& inswapperModelPath)
{
    std::string emapFilePath = inswapperModelPath + ".emap";

    std::ifstream file(emapFilePath, std::ios::binary);
    if (!file.is_open())
    {
        common::logError("ArcfaceRecognizer: Could not open emap file: %s", emapFilePath.c_str());
        common::logInfo("ArcfaceRecognizer: Please extract the emap matrix from the inswapper model first");
        return false;
    }

    // Read the matrix size (should be 512x512 = 262144 floats)
    const size_t expectedSize = EmbeddingSize * EmbeddingSize;
    emap_matrix_.resize(expectedSize);

    file.read(reinterpret_cast<char*>(emap_matrix_.data()), expectedSize * sizeof(float));

    if (file.gcount() != static_cast<std::streamsize>(expectedSize * sizeof(float)))
    {
        common::logError("ArcfaceRecognizer: Failed to read complete emap matrix from file");
        emap_matrix_.clear();
        return false;
    }

    file.close();
    inswapper_compatible_mode_ = true;

    common::logInfo("ArcfaceRecognizer: Successfully loaded emap matrix for inswapper compatibility");
    return true;
}
