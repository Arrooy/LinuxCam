#if 0
#include "FunnyFace/onnx/fsanet.h"

#include "FunnyFace/math_utils.h"

using namespace funnyface;

std::vector<math_utils::Rect> FsanetDetector::detect(const std::unique_ptr<Image>& image)
{
    std::array<int64_t, 4> input_shape = {batch_size_, channels_, width_, height_};


    // Convert from image to tensor.

    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(allocator_, input_shape.data(), input_shape.size());

    // Get pointer to tensor data.
    float* tensor_data = input_tensor.GetTensorMutableData<float>();
    image->toTensor(
        tensor_data); // TODO: FIXME: Seems like input format isnt correct. This model expects only 1 channel.

    // Run inference
    const char* input_names[] = {"input.1"};
    const char* output_names[] = {"448", "471", "494", "451", "474", "497", "454", "477", "500"};

    auto outputs = detector_session_.Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);

    // Stride-specific setup
    struct StrideGroup
    {
        int stride;
        int anchor_count;
        int index_score;
        int index_box;
        int index_kps;
    };

    std::vector<StrideGroup> stride_groups = {
        {8,  12800, 0, 3, 6},
        {16, 3200,  1, 4, 7},
        {32, 800,   2, 5, 8}
    };

    std::vector<math_utils::Rect> rects;
    for (const auto& group : stride_groups)
    {
        // Generate anchors
        auto anchors = generate_anchors(width_, height_, group.stride);

        const float* scores = get_tensor_data<float>(outputs[group.index_score]);  // [N]
        const float* boxes = get_tensor_data<float>(outputs[group.index_box]);     // [N * 4]
        const float* keypoints = get_tensor_data<float>(outputs[group.index_kps]); // [N * 10]


        // Process and collect
        std::vector<Detection> dets = process_stride_outputs(scores, boxes, keypoints, anchors, group.anchor_count,
                                                             0); // TODO: modify score threshold
        for (const auto& det : dets)
        {
            rects.push_back(det.box);
        }
    }

    // auto l = image->info.x;
    // auto t = image->info.y;
    // auto r = image->info.x + 300;
    // auto b = image->info.y + 300;
    // rects.push_back(math_utils::Rect(l, t, r, b));

    return rects;
}

/////////////////////////UNUSED for now.

// struct Detection
// {
//     math_utils::Rect box;
//     float score;
//     std::vector<math_utils::Point> keypoints;
// };

// math_utils::Rect decode_box_to_rect(const math_utils::Anchor& anchor, const float* box_pred, float score)
// {
//     float cx = anchor.cx + box_pred[0] * anchor.stride;
//     float cy = anchor.cy + box_pred[1] * anchor.stride;
//     float w = std::exp(box_pred[2]) * anchor.stride;
//     float h = std::exp(box_pred[3]) * anchor.stride;

//     long left = static_cast<long>(cx - w / 2.0f + 0.5f);
//     long top = static_cast<long>(cy - h / 2.0f + 0.5f);
//     long right = static_cast<long>(cx + w / 2.0f + 0.5f);
//     long bottom = static_cast<long>(cy + h / 2.0f + 0.5f);
//     (void) score; // TODO: use score to filter boxes

//     return math_utils::Rect(left, top, right, bottom);
// }

// std::vector<math_utils::Point> decode_keypoints_to_points(const math_utils::Anchor& anchor, const float* kps_pred)
// {
//     std::vector<math_utils::Point> keypoints;
//     keypoints.reserve(5);
//     for (int i = 0; i < 5; ++i)
//     {
//         float x = anchor.cx + kps_pred[i * 2] * anchor.stride;
//         float y = anchor.cy + kps_pred[i * 2 + 1] * anchor.stride;
//         keypoints.emplace_back(static_cast<long>(x + 0.5f), static_cast<long>(y + 0.5f));
//     }
//     return keypoints;
// }
// std::vector<Detection>
// process_stride_outputs(const float* scores, const float* boxes, const float* keypoints,
//                        const std::vector<math_utils::Anchor>& anchors, int num, float score_thresh = 0.5f)
// {
//     std::vector<Detection> results;

//     for (int i = 0; i < num; ++i)
//     {
//         float score = 1.0f / (1.0f + std::exp(-scores[i])); // sigmoid
//         if (score > score_thresh)
//         {
//             const math_utils::Anchor& anchor = anchors[i];
//             const float* box = &boxes[i * 4];
//             const float* kps = &keypoints[i * 10];

//             math_utils::Rect rect = decode_box_to_rect(anchor, box, score);
//             std::vector<math_utils::Point> pts = decode_keypoints_to_points(anchor, kps);

//             results.push_back({rect, score, std::move(pts)});
//         }
//     }

//     return results;
// }

// std::vector<math_utils::Anchor> generate_anchors(int input_w, int input_h, int stride)
// {
//     std::vector<math_utils::Anchor> anchors;
//     for (int y = 0; y < input_h / stride; ++y)
//     {
//         for (int x = 0; x < input_w / stride; ++x)
//         {
//             anchors.push_back({(x + 0.5f) * stride, (y + 0.5f) * stride, stride});
//         }
//     }
//     return anchors;
// }

#endif
