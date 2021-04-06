// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2018 www.open3d.org
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
// ----------------------------------------------------------------------------

#include "open3d/core/Tensor.h"
#include "open3d/core/kernel/CUDALauncher.cuh"
#include "open3d/t/pipelines/kernel/ComputeTransformImpl.h"
#include "open3d/t/pipelines/kernel/TransformationConverter.h"

namespace open3d {
namespace t {
namespace pipelines {
namespace kernel {

void ComputePosePointToPlaneCUDA(const float *source_points_ptr,
                                 const float *target_points_ptr,
                                 const float *target_normals_ptr,
                                 const int64_t *correspondences_first,
                                 const int64_t *correspondences_second,
                                 const int n,
                                 core::Tensor &pose,
                                 const core::Dtype &dtype,
                                 const core::Device &device) {
    core::Dtype solve_dtype = core::Dtype::Float32;

    // atai: {n, 21} Stores local sum for ATA stacked vertically
    core::Tensor atai = core::Tensor::Empty({n, 21}, solve_dtype, device);
    float *atai_ptr = atai.GetDataPtr<float>();

    // atbi: {n, 6} Stores local sum for ATB.T() stacked vertically
    core::Tensor atbi = core::Tensor::Empty({n, 6}, solve_dtype, device);
    float *atbi_ptr = atbi.GetDataPtr<float>();

    // This kernel computes the {n,21} shape atai tensor
    // and {n,6} shape atbi tensor.
    core::kernel::CUDALauncher::LaunchGeneralKernel(
            n, [=] OPEN3D_DEVICE(int64_t workload_idx) {
                const int64_t &source_idx =
                        3 * correspondences_first[workload_idx];
                const int64_t &target_idx =
                        3 * correspondences_second[workload_idx];

                const int64_t atai_stride = 21 * workload_idx;
                const int64_t atbi_stride = 6 * workload_idx;

                const float &sx = source_points_ptr[source_idx + 0];
                const float &sy = source_points_ptr[source_idx + 1];
                const float &sz = source_points_ptr[source_idx + 2];
                const float &tx = target_points_ptr[target_idx + 0];
                const float &ty = target_points_ptr[target_idx + 1];
                const float &tz = target_points_ptr[target_idx + 2];
                const float &nx = target_normals_ptr[target_idx + 0];
                const float &ny = target_normals_ptr[target_idx + 1];
                const float &nz = target_normals_ptr[target_idx + 2];

                const float bi =
                        (tx - sx) * nx + (ty - sy) * ny + (tz - sz) * nz;
                const float ai[] = {(nz * sy - ny * sz),
                                    (nx * sz - nz * sx),
                                    (ny * sx - nx * sy),
                                    nx,
                                    ny,
                                    nz};

                for (int i = 0, j = 0; j < 6; j++) {
                    for (int k = 0; k <= j; k++) {
                        atai_ptr[atai_stride + i] = ai[j] * ai[k];
                        i++;
                    }
                    atbi_ptr[atbi_stride + j] = ai[j] * bi;
                }
            });

    // Reduce matrix atai (to 1x21) and atbi (to ATB.T() 1x6).
    core::Tensor ata_1x21 = atai.Sum({0}, true);
    core::Tensor ATB = atbi.Sum({0}, true).T();

    //   ata_1x21 is a {1,21} vector having elements of the matrix ATA such
    //     that the corresponding elemetes in ATA are like:
    //     0
    //     1   2
    //     3   4   5
    //     6   7   8   9
    //     10  11  12  13  14
    //     15  16  17  18  19  20
    //     Since, ATA is a symmertric matrix, it can be regenerated from this.
    core::Tensor ATA = core::Tensor::Empty({6, 6}, solve_dtype, device);
    float *ATA_ptr = ATA.GetDataPtr<float>();
    float *ata_1x21_ptr = ata_1x21.GetDataPtr<float>();

    core::kernel::CUDALauncher::LaunchGeneralKernel(
            1, [=] OPEN3D_DEVICE(int64_t workload_idx) {
                for (int i = 0, j = 0; j < 6; j++) {
                    for (int k = 0; k <= j; k++) {
                        ATA_ptr[j * 6 + k] = ata_1x21_ptr[i];
                        ATA_ptr[k * 6 + j] = ata_1x21_ptr[i];
                        i++;
                    }
                }
            });

    // ATA(6,6) . Pose(6,1) = ATB(6,1)
    pose = ATA.Solve(ATB).Reshape({-1}).To(dtype);
}

}  // namespace kernel
}  // namespace pipelines
}  // namespace t
}  // namespace open3d
