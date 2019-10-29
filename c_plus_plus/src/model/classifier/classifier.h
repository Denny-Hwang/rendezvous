#ifndef CLASSIFIER_H
#define CLASSIFIER_H

#include <vector>

#include "model/stream/audio/source_position.h"
#include "model/stream/utils/models/spherical_angle_rect.h"

namespace Model
{
class Classifier
{
   public:
    static std::vector<int> classify(const std::vector<SourcePosition> &audioPositions,
                                     const std::vector<SphericalAngleRect> &imagePositions, const int &rangeThreshold);
};

}    // namespace Model

#endif    // CLASSIFIER_H
