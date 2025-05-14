#include "face.h"
#include "math_utils.h"

using namespace dlib;
 
Face::Face(full_object_detection face_landmarks)
{
    faceRect = face_landmarks.get_rect();

    for (unsigned long id = face_landmarks.num_parts() - 1; id > 0; id--)
    {
        std::vector<point> new_points;

        // This comparison is a shame...
        if (id != 0 && id != 17 && id != 22 && id != 27 && id != 36 && id != 42 && id != 48 && id != 60)
        {
            // Interpolate points.
            new_points = DDA(id, id - 1, face_landmarks);
        }
        else
        {
            new_points.push_back(face_landmarks.part(id));
        }

        int index = this->get_facepart_from_landmark_id(id);

        auto it = face_points.find(index);
        if (it == face_points.end())
        {
            face_points.insert(std::pair<int, std::vector<dlib::point>>(index, new_points));
        }
        else
        {
            face_points[index].insert(face_points[index].end(), new_points.begin(), new_points.end());
        }
    }

    std::vector<point> new_points;

    // Es crea el vector
    new_points = DDA(0, 17, face_landmarks);
    face_points.insert(std::pair<int, std::vector<dlib::point>>(FaceIndex::SILHOUETTE, new_points));

    // Incrementem el vector.
    new_points = DDA(16, 26, face_landmarks);
    face_points[FaceIndex::SILHOUETTE].insert(face_points[FaceIndex::SILHOUETTE].end(), new_points.begin(),
                                              new_points.end());

    for (long id = 0; id <= 15; id++)
    {
        new_points = DDA(id, id + 1, face_landmarks);
        face_points[FaceIndex::SILHOUETTE].insert(face_points[FaceIndex::SILHOUETTE].end(), new_points.begin(),
                                                  new_points.end());
    }

    for (long id = 17; id <= 21; id++)
    {
        new_points = DDA(id, id + 1, face_landmarks);
        face_points[FaceIndex::SILHOUETTE].insert(face_points[FaceIndex::SILHOUETTE].end(), new_points.begin(),
                                                  new_points.end());
    }

    for (long id = 22; id <= 25; id++)
    {
        new_points = DDA(id, id + 1, face_landmarks);
        face_points[FaceIndex::SILHOUETTE].insert(face_points[FaceIndex::SILHOUETTE].end(), new_points.begin(),
                                                  new_points.end());
    }
}

Face::~Face()
{
    for (auto& item : face_points)
    {
        item.second.clear();
    }
    face_points.clear();
}

int Face::get_facepart_from_landmark_id(unsigned long id)
{
    // Lookup table for landmark -> facepart translation
    if (id >= 0 && id <= 16)
    {
        // Jaw
        return JAW;
    }
    else if (id >= 17 && id <= 21)
    {
        // Left eyebrow

        return LBROW;
    }
    else if (id >= 22 && id <= 26)
    {
        // Right eyebrow
        return RBROW;
    }
    else if (id >= 27 && id <= 35)
    {
        // Nose
        return NOSE;
    }
    else if (id >= 36 && id <= 41)
    {
        // Left eye
        return LEYE;
    }
    else if (id >= 42 && id <= 47)
    {
        // Right eye
        return REYE;
    }
    else if (id >= 48 && id <= 59)
    {
        // Outer mouth
        return OUTERMOUTH;
    }
    else if (id >= 60 && id <= 67)
    {
        // Inner mouth
        return INNERMOUTH;
    }
    return -1;
}

void Face::paint_outline_all(unsigned char* raw_frame_data)
{
    for (auto const& face_part : face_points)
    {
        if (face_part.first == FaceIndex::SILHOUETTE)
        {
            for (dlib::point p : face_part.second)
            {
                long pos = (p(0) + p(1) * VIDEO_WIDTH_OUT) * 3;
                raw_frame_data[pos] = 0;
                raw_frame_data[pos + 1] = 255;
                raw_frame_data[pos + 2] = 0;
            }
        }
        else
        {
            for (dlib::point p : face_part.second)
            {
                long pos = (p(0) + p(1) * VIDEO_WIDTH_OUT) * 3;
                raw_frame_data[pos] = 0;
                raw_frame_data[pos + 1] = 255;
                raw_frame_data[pos + 2] = 255;
            }
        }
    }
}

std::vector<dlib::point> Face::getFacePartPoints(FaceIndex facepart)
{
    return face_points[facepart];
}

void Face::paintRectangle(unsigned char* raw_frame_data)
{
    std::vector<dlib::vector<long, 2>> points;

    auto left = DDA(faceRect.left(), faceRect.top(), faceRect.left(), faceRect.bottom());
    points.insert(points.end(), left.begin(), left.end());

    auto top = DDA(faceRect.left(), faceRect.top(), faceRect.right(), faceRect.top());
    points.insert(points.end(), top.begin(), top.end());

    auto bottom = DDA(faceRect.left(), faceRect.bottom(), faceRect.right(), faceRect.bottom());
    points.insert(points.end(), bottom.begin(), bottom.end());

    auto right = DDA(faceRect.right(), faceRect.top(), faceRect.right(), faceRect.bottom());
    points.insert(points.end(), right.begin(), right.end());

    for (dlib::point p : points)
    {
        long pos = (p(0) + p(1) * VIDEO_WIDTH_OUT) * PIXEL_SIZE;
        raw_frame_data[pos] = 0;
        raw_frame_data[pos + 1] = 255;
        raw_frame_data[pos + 2] = 0;
    }
}

void Face::paintInside(FaceIndex facepart, unsigned char* raw_frame_data)
{
    std::vector<dlib::point> points = face_points[facepart];

    int inside = 0;
    long startIndex = 0;

    long startPixel = (faceRect.left() + faceRect.top() * VIDEO_WIDTH_OUT) * PIXEL_SIZE;
    // El 25 es per corregir el bottom massa curt. Aixi no talla la cara,
    long endPixel = (faceRect.right() + (faceRect.bottom() + 25) * VIDEO_WIDTH_OUT) * PIXEL_SIZE;

    for (long i = startPixel; i < endPixel; i += PIXEL_SIZE)
    {
        for (dlib::point p : points)
        {
            if ((p(0) + p(1) * VIDEO_WIDTH_OUT) * PIXEL_SIZE == i)
            {
                if (inside == 0)
                {
                    startIndex = i;
                }
                else
                {
                    for (long v = startIndex; v <= i; v += PIXEL_SIZE)
                    {
                        raw_frame_data[v] = 0;
                        raw_frame_data[v + 1] = 0; // raw_frame_data[v+1] / 2;
                        raw_frame_data[v + 2] = 0;
                    }
                }

                // Ingora pixels seguits.
                if (startIndex != i - PIXEL_SIZE)
                {
                    inside = 1 - inside;
                }
                else
                {
                    raw_frame_data[startIndex] = 0;
                    raw_frame_data[startIndex + 1] = 0; // raw_frame_data[v+1] / 2;
                    raw_frame_data[startIndex + 2] = 0;
                    startIndex = i;
                }
                break;
            }
        }

        if (i % (VIDEO_WIDTH_OUT * PIXEL_SIZE) == 0)
        {
            // S'ha acabat la linia i estavem buscant un pixel parella.
            if (inside == 1)
            {
                raw_frame_data[startIndex] = 0;
                raw_frame_data[startIndex + 1] = 0;
                raw_frame_data[startIndex + 2] = 0;
            }
            inside = 0;
        }
    }
}

void Face::paintOutline(FaceIndex facepart, unsigned char* raw_frame_data)
{
    std::vector<dlib::point> points = face_points[facepart];
    for (dlib::point p : points)
    {
        long pos = (p(0) + p(1) * VIDEO_WIDTH_OUT) * PIXEL_SIZE;
        raw_frame_data[pos] = 0;
        raw_frame_data[pos + 1] = 255;
        raw_frame_data[pos + 2] = 255;
    }
}

std::vector<point> Face::DDA(unsigned long landmarkA, unsigned long landmarkB, full_object_detection face_landmarks)
{
    return math_utils::DDA(face_landmarks.part(landmarkA)(0), face_landmarks.part(landmarkA)(1), face_landmarks.part(landmarkB)(0),
               face_landmarks.part(landmarkB)(1));
}
