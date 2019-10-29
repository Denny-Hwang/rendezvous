#include "cuda_dewarping_helper.cuh"

#include "model/stream/utils/math/math_constants.h"

// __device__ Point<float> calculateSourcePixelPosition(const Dim2<int>& dst, const DewarpingParameters& params, int
// index)
// {
//     float x = index % dst.width;
//     float y = index / dst.width;

//     float textureCoordsX = x / dst.width;
//     float textureCoordsY = y / dst.height;

//     float heightFactor = (1 - ((params.bottomOffset + params.topOffset) / params.dewarpHeight)) *
//         textureCoordsY + params.topOffset / params.dewarpHeight;
//     float factor = params.outRadiusDiff * (1 - params.bottomDistorsionFactor) *
//         __sinf(math::PI * textureCoordsX) * __sinf((math::PI * heightFactor) / 2.0);
//     float radius = textureCoordsY * params.dewarpHeight + params.inRadius + factor +
//         (1 - textureCoordsY) * params.topOffset - textureCoordsY * params.bottomOffset;
//     float theta = ((textureCoordsX * params.dewarpWidth) + params.xOffset) / params.centerRadius;

//     Point<float> srcPixelPosition;
//     srcPixelPosition.x = params.xCenter + radius * __sinf(theta);
//     srcPixelPosition.y = params.yCenter + radius * __cosf(theta);

//     return srcPixelPosition;
// }

// __device__ LinearPixelFilter calculateLinearPixelFilter(const Point<float>& pixel, const Dim3<int>& dim)
// {
//     int xRoundDown = int(pixel.x);
//     int yRoundDown = int(pixel.y);
//     float xRatio = pixel.x - xRoundDown;
//     float yRatio = pixel.y - yRoundDown;
//     float xOpposite = 1 - xRatio;
//     float yOpposite = 1 - yRatio;

//     LinearPixelFilter linearPixelFilter;

//     linearPixelFilter.pc1.index = (xRoundDown + (yRoundDown * dim.width)) * dim.channels;
//     linearPixelFilter.pc2.index = linearPixelFilter.pc1.index + dim.channels;
//     linearPixelFilter.pc3.index = linearPixelFilter.pc1.index + dim.width * dim.channels;
//     linearPixelFilter.pc4.index = linearPixelFilter.pc2.index + dim.width * dim.channels;

//     linearPixelFilter.pc1.ratio = xOpposite * yOpposite;
//     linearPixelFilter.pc2.ratio = xRatio * yOpposite;
//     linearPixelFilter.pc3.ratio = xOpposite * yRatio;
//     linearPixelFilter.pc4.ratio = xRatio * yRatio;

//     return linearPixelFilter;
// }

// __device__ int calculateSourcePixelIndex(const Point<float>& pixel, const Dim3<int>& dim)
// {
//     return (int(pixel.x) + int(pixel.y) * dim.width) * dim.channels;
// }

// int calculateKernelBlockCount(const Dim2<int>& dim, int blockSize)
// {
//    return (dim.width * dim.height + blockSize - 1) / blockSize;
// }