// #include "LinuxFace/wflw_test.h"

// #include "LinuxFace/common.h"
// #include "config.hpp"

// namespace linuxface
// {

// WFLWLoader::WFLWLoader(const std::string& data_file_path, const int max_samples)
// {
//     std::ifstream file(data_file_path);
//     if (!file.is_open())
//     {
//         common::log_error("Could not open data file: %s", data_file_path.c_str());
//         return;
//     }

//     std::string line;
//     int num_examples = 0;
//     while (std::getline(file, line))
//     {
//         WFLWExample example;
//         if (parse_line(line, example))
//         {
//             examples_.push_back(example);
//             ++num_examples;
//             if (max_samples > 0 && num_examples >= max_samples)
//             {
//                 break;
//             }
//         }
//         else
//         {
//             common::log_warn("Skipping invalid line: %s", line.c_str());
//         }
//     }
//     file.close();
// }

// bool WFLWLoader::load_example(int index, WFLWExample& example) const
// {
//     if (index < 0 || static_cast<size_t>(index) >= examples_.size())
//     {
//         common::log_error("Invalid example index: %lu", index);
//         return false;
//     }
//     example = examples_[index];
//     return true;
// }

// bool WFLWLoader::parse_line(const std::string& line, WFLWExample& example)
// {
//     std::stringstream ss(line);
//     std::string token;

//     // Parse 98 landmarks (x, y)
//     for (int i = 0; i < 98; ++i)
//     {
//         double x, y;
//         if (!(ss >> x >> y))
//         {
//             return false;
//         }
//         example.landmarks.emplace_back(x, y);
//     }

//     // Parse bounding box (x_min, y_min, x_max, y_max)
//     double x_min, y_min, x_max, y_max;
//     if (!(ss >> x_min >> y_min >> x_max >> y_max))
//     {
//         return false;
//     }
//     example.bounding_box = math_utils::Rect<double>(x_min, y_min, x_max, y_max);

//     // Parse 6 attributes
//     for (int i = 0; i < 6; ++i)
//     {
//         if (!(ss >> example.attributes[i]))
//         {
//             return false;
//         }
//     }

//     // Parse image name
//     if (!(ss >> example.image_name))
//     {
//         return false;
//     }

//     // Load the image using ImageLoader
//     const std::string base_image_dir = Config::getInstance().getWFLWFolderPath();
//     const std::string image_path = base_image_dir + "/WFLW_images/" + example.image_name;
//     example.image = ImageLoader::loadImageFromFile(image_path);
//     if (!example.image)
//     {
//         common::log_error("Could not load image: %s", image_path.c_str());
//         return false; // Or handle the error as appropriate for your application
//     }
//     common::log_info("Loaded image: %s", image_path.c_str());
//     return true;
// }

// } // namespace linuxface

// // Example usage (in your main function or another test file)
// /*
// #include "LinuxFace/wflw_test.h"

// int main() {
//     linuxface::WFLWLoader
// loader("../WFLW/WFLW_annotations/list_98pt_rect_attr_train_test/list_98pt_rect_attr_test.txt"); // Replace with actual
// path

//     if (loader.get_num_examples() == 0) {
//         std::cerr << "Error: No examples loaded." << std::endl;
//         return 1;
//     }

//     linuxface::WFLWExample example;
//     if (loader.load_example(0, example)) { // Load the first example
//         common::log_info("Loaded example from image: %s", example.image_name.c_str());
//         common::log_info("Bounding box: (%f, %f) - (%f, %f)", example.bounding_box.l, example.bounding_box.t,
//                          example.bounding_box.r, example.bounding_box.b);
//         common::log_info("Attributes: %d %d %d %d %d %d", example.attributes[0], example.attributes[1],
//                          example.attributes[2], example.attributes[3], example.attributes[4], example.attributes[5]);
//         common::log_info("Number of landmarks: %zu", example.landmarks.size());
//         // You can further process the landmarks here...
//     }

//     return 0;
// }
// */
