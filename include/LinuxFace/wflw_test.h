#ifndef WFLW_TEST_H
#define WFLW_TEST_H

#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "LinuxFace/imageLoader.h" // Include ImageLoader header
#include "LinuxFace/math_utils.h"  // Assuming this contains Point and Rect

namespace linuxface
{

struct WFLWExample
{
    std::vector<math_utils::Point<double>> landmarks; // 98 landmarks
    math_utils::Rect<double> bounding_box;            // Detection rectangle
    std::array<int, 6> attributes;                    // pose, expression, illumination, make-up, occlusion, blur

    bool isNormalPose() const { return attributes[0] == 0; }
    bool isNormalExpression() const { return attributes[1] == 0; }
    bool isNormalIllumination() const { return attributes[2] == 0; }
    bool hasNoMakeup() const { return attributes[3] == 0; }
    bool hasNoOcclusion() const { return attributes[4] == 0; }
    bool isClear() const { return attributes[5] == 0; }
    std::string image_name;
    std::shared_ptr<Image> image; // Store the loaded image
};

class WFLWLoader
{
  public:
    WFLWLoader(const std::string& data_file_path, const int max_samples = -1);

    bool load_example(int index, WFLWExample& example) const;
    int get_num_examples() const { return examples_.size(); }

  private:
    std::vector<WFLWExample> examples_;

    // Helper function to parse a single line from the data file
    static bool parse_line(const std::string& line, WFLWExample& example);
};

} // namespace linuxface

#endif // WFLW_TEST_H
