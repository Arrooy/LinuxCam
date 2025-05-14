/* -*- c++ -*- */

#ifndef FACE_H
#define FACE_H

#include <dlib/image_io.h>
#include <dlib/image_processing.h>
#include <dlib/image_processing/frontal_face_detector.h>
#include <dlib/image_processing/render_face_detections.h>

#include <iostream>
#include <map>
#include <vector>

// Interpolate all landmarks into a face.
// Divides all landmarks into the different face parts.
class Face
{
  public:
    // Order in the definition is important!
    enum FaceIndex
    {
        JAW,
        LEYE,
        REYE,
        LBROW,
        RBROW,
        NOSE,
        OUTERMOUTH,
        INNERMOUTH,
        SILHOUETTE
    };

    Face(full_object_detection face_landmarks);
    ~Face();

    
    int get_facepart_from_landmark_id(unsigned long id);
    void paint_outline_all(unsigned char* raw_frame_data);
    std::vector<dlib::point> getFacePartPoints(FaceIndex facepart);
    void paintRectangle(unsigned char* raw_frame_data);
    void paintInside(FaceIndex facepart, unsigned char* raw_frame_data);
    void paintOutline(FaceIndex facepart, unsigned char* raw_frame_data);

  private:
    std::vector<dlib::point> DDA(unsigned long landmarkA, unsigned long landmarkB, full_object_detection face_landmarks);

    // Each FaceIndex(int) contains a vector of pixels that conform that face part.
    std::map<int, std::vector<dlib::point>> face_points;
    dlib::rectangle faceRect;
};


#endif // FACE_H
