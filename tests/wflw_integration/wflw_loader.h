#ifndef WFLW_LOADER_H
#define WFLW_LOADER_H

#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "LinuxFace/imageLoader.h"
#include "LinuxFace/math_utils.h"

namespace linuxface::test
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
    std::string image_path;
    mutable std::unique_ptr<Image> image;

    // Move constructor and assignment
    WFLWExample() = default;
    WFLWExample(WFLWExample&&) = default;
    WFLWExample& operator=(WFLWExample&&) = default;

    // Delete copy constructor and assignment to prevent accidental copies
    WFLWExample(const WFLWExample&) = delete;
    WFLWExample& operator=(const WFLWExample&) = delete;
};

class WFLWLoader
{
  public:
    WFLWLoader(const std::string& data_file_path, const int max_samples = -1);
    
    // Constructor that loads only challenging condition examples (any non-normal attribute)
    WFLWLoader(const std::string& data_file_path, bool load_challenging_only, const int max_samples = -1);

    bool load_example(int index, WFLWExample& example) const;
    int get_num_examples() const { return examples_.size(); }

    // Get examples by specific attributes for targeted testing
    std::vector<int>
    getExamplesByAttribute(bool normal_pose = true, bool normal_expression = true, bool normal_illumination = true,
                           bool no_makeup = true, bool no_occlusion = true, bool is_clear = true, 
                           int max_results = -1) const;

  private:
    std::vector<WFLWExample> examples_;
    
    // Shared implementation for both constructors
    void load_examples_from_file(const std::string& data_file_path, bool load_challenging_only, const int max_samples);
    
    static bool parse_line(const std::string& line, WFLWExample& example);
};

} // namespace linuxface::test

#endif // WFLW_LOADER_H
