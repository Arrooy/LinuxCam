#include "LinuxFace/onnx/arcfaceRecognizer.h"

#include <cmath>
#include <fstream>

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/common.h"
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
    float norm = 0.0f;
    for (const auto& val : embedding)
    {
        norm += val * val;
    }
    norm = std::sqrt(norm);
    for (auto& val : embedding)
    {
        val /= norm;
    }

    // Apply inswapper transformation if requested and enabled
    if (inswapperCompatible && inswapper_compatible_mode_)
    {
        embedding = transformEmbeddingForInswapper(embedding);
    }

    Profiler::getInstance().stop("ArcfaceRecognizer", "recognize");
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

    // Apply emap transformation: transformed = emap * embedding
    std::vector<float> transformed(EmbeddingSize, 0.0f);
    for (int i = 0; i < EmbeddingSize; ++i)
    {
        for (int j = 0; j < EmbeddingSize; ++j)
        {
            transformed[i] += emap_matrix_[i * EmbeddingSize + j] * arcfaceEmbedding[j];
        }
    }

    // Normalize the transformed embedding
    float norm = 0.0f;
    for (const auto& val : transformed)
    {
        norm += val * val;
    }
    norm = std::sqrt(norm);
    
    if (norm > 1e-8f) // Avoid division by zero
    {
        for (auto& val : transformed)
        {
            val /= norm;
        }
    }

    return transformed;
}

bool ArcfaceRecognizer::loadEmapMatrixFromOnnx(const std::string& inswapperModelPath)
{
    // For now, we'll implement a simple binary file loader
    // The emap matrix should be extracted from the inswapper model and saved as a binary file
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
