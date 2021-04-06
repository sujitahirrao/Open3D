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

#include "open3d/t/geometry/Image.h"

#include <gmock/gmock.h>

#include "core/CoreTest.h"
#include "open3d/core/TensorList.h"
#include "open3d/io/ImageIO.h"
#include "tests/UnitTest.h"

namespace open3d {
namespace tests {

class ImagePermuteDevices : public PermuteDevices {};
INSTANTIATE_TEST_SUITE_P(Image,
                         ImagePermuteDevices,
                         testing::ValuesIn(PermuteDevices::TestCases()));

class ImagePermuteDevicePairs : public PermuteDevicePairs {};
INSTANTIATE_TEST_SUITE_P(
        Image,
        ImagePermuteDevicePairs,
        testing::ValuesIn(ImagePermuteDevicePairs::TestCases()));

TEST_P(ImagePermuteDevices, ConstructorNoArg) {
    t::geometry::Image im;
    EXPECT_EQ(im.GetRows(), 0);
    EXPECT_EQ(im.GetCols(), 0);
    EXPECT_EQ(im.GetChannels(), 1);
    EXPECT_EQ(im.GetDtype(), core::Dtype::Float32);
    EXPECT_EQ(im.GetDevice(), core::Device("CPU:0"));
}

TEST_P(ImagePermuteDevices, Constructor) {
    core::Device device = GetParam();

    // Normal case.
    int64_t rows = 480;
    int64_t cols = 640;
    int64_t channels = 3;
    core::Dtype dtype = core::Dtype::UInt8;
    t::geometry::Image im(rows, cols, channels, dtype, device);
    EXPECT_EQ(im.GetRows(), rows);
    EXPECT_EQ(im.GetCols(), cols);
    EXPECT_EQ(im.GetChannels(), channels);
    EXPECT_EQ(im.GetDtype(), dtype);
    EXPECT_EQ(im.GetDevice(), device);

    // Unsupported shape or channel.
    EXPECT_ANY_THROW(t::geometry::Image(-1, cols, channels, dtype, device));
    EXPECT_ANY_THROW(t::geometry::Image(rows, -1, channels, dtype, device));
    EXPECT_ANY_THROW(t::geometry::Image(rows, cols, 0, dtype, device));
    EXPECT_ANY_THROW(t::geometry::Image(rows, cols, -1, dtype, device));

    // Check all dtypes.
    for (const core::Dtype& dtype : {
                 core::Dtype::Float32,
                 core::Dtype::Float64,
                 core::Dtype::Int32,
                 core::Dtype::Int64,
                 core::Dtype::UInt8,
                 core::Dtype::UInt16,
                 core::Dtype::Bool,
         }) {
        EXPECT_NO_THROW(
                t::geometry::Image(rows, cols, channels, dtype, device));
    }
}

TEST_P(ImagePermuteDevices, ConstructorFromTensor) {
    core::Device device = GetParam();

    int64_t rows = 480;
    int64_t cols = 640;
    int64_t channels = 3;
    core::Dtype dtype = core::Dtype::UInt8;

    // 2D Tensor. IsSame() tests memory sharing and shape matching.
    core::Tensor t_2d({rows, cols}, dtype, device);
    t::geometry::Image im_2d(t_2d);
    EXPECT_FALSE(im_2d.AsTensor().IsSame(t_2d));
    EXPECT_TRUE(im_2d.AsTensor().Reshape(t_2d.GetShape()).IsSame(t_2d));

    // 3D Tensor.
    core::Tensor t_3d({rows, cols, channels}, dtype, device);
    t::geometry::Image im_3d(t_3d);
    EXPECT_TRUE(im_3d.AsTensor().IsSame(t_3d));

    // Not 2D nor 3D.
    core::Tensor t_4d({rows, cols, channels, channels}, dtype, device);
    EXPECT_ANY_THROW(t::geometry::Image im_4d(t_4d); (void)im_4d;);

    // Non-contiguous tensor.
    // t_3d_sliced = t_3d[:, :, 0:3:2]
    core::Tensor t_3d_sliced = t_3d.Slice(2, 0, 3, 2);
    EXPECT_EQ(t_3d_sliced.GetShape(), core::SizeVector({rows, cols, 2}));
    EXPECT_FALSE(t_3d_sliced.IsContiguous());
    EXPECT_ANY_THROW(t::geometry::Image im_nc(t_3d_sliced); (void)im_nc;);
}

TEST_P(ImagePermuteDevicePairs, CopyDevice) {
    core::Device dst_device;
    core::Device src_device;
    std::tie(dst_device, src_device) = GetParam();

    core::Tensor data =
            core::Tensor::Ones({2, 3}, core::Dtype::Float32, src_device);
    t::geometry::Image im(data);

    // Copy is created on the dst_device.
    t::geometry::Image im_copy = im.To(dst_device, /*copy=*/true);

    EXPECT_EQ(im_copy.GetDevice(), dst_device);
    EXPECT_EQ(im_copy.GetDtype(), im.GetDtype());
}

TEST_P(ImagePermuteDevices, Copy) {
    core::Device device = GetParam();

    core::Tensor data =
            core::Tensor::Ones({2, 3}, core::Dtype::Float32, device);
    t::geometry::Image im(data);

    // Copy is on the same device as source.
    t::geometry::Image im_copy = im.Clone();

    // Copy does not share the same memory with source (deep copy).
    EXPECT_FALSE(im_copy.AsTensor().IsSame(im.AsTensor()));

    // Copy has the same attributes and values as source.
    EXPECT_TRUE(im_copy.AsTensor().AllClose(im.AsTensor()));
}

// Test automatic scale determination for conversion from UInt8 / UInt16 ->
// Float32/64 and LinearTransform().
// Currently needs IPP.
TEST_P(ImagePermuteDevices,
       OPEN3D_CONCATENATE(IPP_CONDITIONAL_TEST_STR, To_LinearTransform)) {
    using ::testing::ElementsAreArray;
    using ::testing::FloatEq;
    core::Device device = GetParam();

    // reference data
    const std::vector<uint8_t> input_data = {10, 25, 0, 13};
    auto output_ref = {FloatEq(10. / 255), FloatEq(25. / 255), FloatEq(0.),
                       FloatEq(13. / 255)};
    auto negative_image_ref = {FloatEq(1. - 10. / 255), FloatEq(1. - 25. / 255),
                               FloatEq(1.), FloatEq(1. - 13. / 255)};

    t::geometry::Image input(
            core::Tensor{input_data, {2, 2, 1}, core::Dtype::UInt8, device});
    // UInt8 -> Float32: auto scale = 1./255
    t::geometry::Image output = input.To(core::Dtype::Float32);
    EXPECT_EQ(output.GetDtype(), core::Dtype::Float32);
    EXPECT_THAT(output.AsTensor().ToFlatVector<float>(),
                ElementsAreArray(output_ref));

    // LinearTransform to negative image
    output.LinearTransform(/* scale= */ -1, /* offset= */ 1);
    EXPECT_THAT(output.AsTensor().ToFlatVector<float>(),
                ElementsAreArray(negative_image_ref));

    // UInt8 -> UInt16: auto scale = 1
    output = input.To(core::Dtype::UInt16);
    EXPECT_EQ(output.GetDtype(), core::Dtype::UInt16);
    EXPECT_THAT(output.AsTensor().ToFlatVector<uint16_t>(),
                ElementsAreArray(input_data));
}

TEST_P(ImagePermuteDevices, FilterBilateral) {
    core::Device device = GetParam();

    {  // Float32
        // clang-format off
        const std::vector<float> input_data =
          {0, 0, 0, 0, 0,
           0, 0, 0, 0, 0,
           0, 0, 1, 0, 0,
           0, 0, 0, 0, 0,
           0, 0, 0, 0, 0};
        const std::vector<float> output_ref_ipp =
          {0.0, 0.0, 0.0, 0.0, 0.0,
           0.0, 0.0, 0.199001, 0.0, 0.0,
           0.0, 0.199001, 0.201605, 0.199001, 0.0,
           0.0, 0.0, 0.199001, 0.0, 0.0,
           0.0, 0.0, 0.0, 0.0, 0.0};
        const std::vector<float> output_ref_npp =
          {0.0, 0.0, 0.0, 0.0, 0.0,
           0.0, 0.110249, 0.110802, 0.110249, 0.0,
           0.0, 0.110802, 0.112351, 0.110802, 0.0,
           0.0, 0.110249, 0.110802, 0.110249, 0.0,
           0.0, 0.0, 0.0, 0.0, 0.0};
        // clang-format on

        core::Tensor data = core::Tensor(input_data, {5, 5, 1},
                                         core::Dtype::Float32, device);

        t::geometry::Image im(data);
        if (!t::geometry::Image::HAVE_IPPICV &&
            device.GetType() ==
                    core::Device::DeviceType::CPU) {  // Not Implemented
            ASSERT_THROW(im.FilterBilateral(3, 10, 10), std::runtime_error);
        } else {
            im = im.FilterBilateral(3, 10, 10);
            if (device.GetType() == core::Device::DeviceType::CPU) {
                EXPECT_TRUE(im.AsTensor().AllClose(
                        core::Tensor(output_ref_ipp, {5, 5, 1},
                                     core::Dtype::Float32, device)));
            } else {
                EXPECT_TRUE(im.AsTensor().AllClose(
                        core::Tensor(output_ref_npp, {5, 5, 1},
                                     core::Dtype::Float32, device)));
            }
        }
    }

    {  // UInt8
        // clang-format off
        const std::vector<uint8_t> input_data =
          {0, 0, 0, 0, 0,
           0, 121, 121, 121, 0,
           0, 125, 128, 125, 0,
           0, 121, 121, 121, 0,
           0, 0, 0, 0, 0};
        const std::vector<uint8_t> output_ref_ipp =
          {0, 0, 0, 0, 0,
           0, 122, 122, 122, 0,
           0, 124, 125, 124, 0,
           0, 122, 122, 122, 0,
           0, 0, 0, 0, 0};
        const std::vector<uint8_t> output_ref_npp =
          {0, 0, 0, 0, 0,
           0, 122, 122, 122, 0,
           0, 123, 123, 123, 0,
           0, 122, 122, 122, 0,
           0, 0, 0, 0, 0};
        // clang-format on

        core::Tensor data =
                core::Tensor(input_data, {5, 5, 1}, core::Dtype::UInt8, device);

        t::geometry::Image im(data);
        if (!t::geometry::Image::HAVE_IPPICV &&
            device.GetType() ==
                    core::Device::DeviceType::CPU) {  // Not Implemented
            ASSERT_THROW(im.FilterBilateral(3, 5, 5), std::runtime_error);
        } else {
            im = im.FilterBilateral(3, 5, 5);
            utility::LogInfo("{}", im.AsTensor().View({5, 5}).ToString());

            if (device.GetType() == core::Device::DeviceType::CPU) {
                EXPECT_TRUE(im.AsTensor().AllClose(
                        core::Tensor(output_ref_ipp, {5, 5, 1},
                                     core::Dtype::UInt8, device)));
            } else {
                EXPECT_TRUE(im.AsTensor().AllClose(
                        core::Tensor(output_ref_npp, {5, 5, 1},
                                     core::Dtype::UInt8, device)));
            }
        }
    }
}

// IPP and NPP are consistent when kernel_size = 3x3.
// Note: in 5 x 5 NPP adds a weird offset.
TEST_P(ImagePermuteDevices, FilterGaussian) {
    core::Device device = GetParam();

    {  // Float32
        // clang-format off
        const std::vector<float> input_data =
          {0, 0, 0, 0, 0,
           0, 1, 0, 0, 1,
           0, 0, 0, 0, 0,
           0, 0, 0, 0, 0,
           0, 0, 0, 1, 0};
        const std::vector<float> output_ref =
          {0.0751136, 0.123841, 0.0751136, 0.0751136, 0.198955,
           0.123841, 0.204180, 0.123841, 0.123841, 0.328021,
           0.0751136, 0.123841, 0.0751136, 0.0751136, 0.198955,
           0.0, 0.0, 0.0751136, 0.123841, 0.0751136,
           0.0, 0.0, 0.198955, 0.328021, 0.198955};
        // clang-format on

        core::Tensor data = core::Tensor(input_data, {5, 5, 1},
                                         core::Dtype::Float32, device);
        t::geometry::Image im(data);
        if (!t::geometry::Image::HAVE_IPPICV &&
            device.GetType() ==
                    core::Device::DeviceType::CPU) {  // Not Implemented
            ASSERT_THROW(im.FilterGaussian(3), std::runtime_error);
        } else {
            im = im.FilterGaussian(3);
            EXPECT_TRUE(im.AsTensor().AllClose(core::Tensor(
                    output_ref, {5, 5, 1}, core::Dtype::Float32, device)));
        }
    }

    {  // UInt8
        // clang-format off
        const std::vector<uint8_t> input_data =
          {0, 0, 0, 0, 0,
           0, 128, 0, 0, 255,
           0, 0, 0, 128, 0,
           0, 0, 0, 0, 0,
           0, 0, 0, 255, 0};
        const std::vector<uint8_t> output_ref_ipp =
          {10, 16, 10, 19, 51,
           16, 26, 25, 47, 93,
           10, 16, 25, 45, 67,
           0, 0, 29, 47, 29,
           0, 0, 51, 84, 51};
        const std::vector<uint8_t> output_ref_npp =
          {9, 15, 9, 19, 50,
           15, 26, 25, 47, 93,
           9, 15, 25, 45, 66,
           0, 0, 28, 47, 28,
           0, 0, 50, 83, 50};
        // clang-format on

        core::Tensor data =
                core::Tensor(input_data, {5, 5, 1}, core::Dtype::UInt8, device);
        t::geometry::Image im(data);
        if (!t::geometry::Image::HAVE_IPPICV &&
            device.GetType() ==
                    core::Device::DeviceType::CPU) {  // Not Implemented
            ASSERT_THROW(im.FilterGaussian(3), std::runtime_error);
        } else {
            im = im.FilterGaussian(3);
            utility::LogInfo("{}", im.AsTensor().View({5, 5}).ToString());

            if (device.GetType() == core::Device::DeviceType::CPU) {
                EXPECT_TRUE(im.AsTensor().AllClose(
                        core::Tensor(output_ref_ipp, {5, 5, 1},
                                     core::Dtype::UInt8, device)));
            } else {
                EXPECT_TRUE(im.AsTensor().AllClose(
                        core::Tensor(output_ref_npp, {5, 5, 1},
                                     core::Dtype::UInt8, device)));
            }
        }
    }
}

TEST_P(ImagePermuteDevices, Filter) {
    core::Device device = GetParam();

    {  // Float32
        // clang-format off
        const std::vector<float> input_data =
          {0, 0, 0, 0, 0,
           0, 0, 0, 0, 0,
           0, 0, 1, 0, 0,
           0, 0, 0, 0, 0,
           0, 0, 0, 0, 0};
       const std::vector<float> kernel_data =
          {0.00296902, 0.0133062 , 0.02193824, 0.0133062 , 1.00296902,
           0.0133062 , 0.05963413, 0.09832021, 0.05963413, 0.0133062 ,
           0.02193824, 0.09832021, 0.16210286, 0.09832021, 0.02193824,
           0.0133062 , 0.05963413, 0.09832021, 0.05963413, 0.0133062 ,
           0.00296902, 0.0133062 , 0.02193824, 0.0133062 , -1.00296902
        };
        // clang-format on

        core::Tensor data = core::Tensor(input_data, {5, 5, 1},
                                         core::Dtype::Float32, device);
        core::Tensor kernel =
                core::Tensor(kernel_data, {5, 5}, core::Dtype::Float32, device);
        t::geometry::Image im(data);
        if (!t::geometry::Image::HAVE_IPPICV &&
            device.GetType() ==
                    core::Device::DeviceType::CPU) {  // Not Implemented
            ASSERT_THROW(im.Filter(kernel), std::runtime_error);
        } else {
            t::geometry::Image im_new = im.Filter(kernel);
            EXPECT_TRUE(
                    im_new.AsTensor().Reverse().View({5, 5}).AllClose(kernel));
        }
    }

    {  // UInt8
        // clang-format off
        const std::vector<uint8_t> input_data =
          {0, 0, 0, 0, 0,
           0, 0, 0, 0, 0,
           0, 0, 128, 0, 0,
           0, 0, 0, 0, 0,
           0, 0, 0, 0, 255};
       const std::vector<float> kernel_data =
          {0.00296902, 0.0133062 , 0.02193824, 0.0133062 , 1.00296902,
           0.0133062 , 0.05963413, 0.09832021, 0.05963413, 0.0133062 ,
           0.02193824, 0.09832021, 0.16210286, 0.09832021, 0.02193824,
           0.0133062 , 0.05963413, 0.09832021, 0.05963413, 0.0133062 ,
           0.00296902, 0.0133062 , 0.02193824, 0.0133062 , -1.00296902
        };

       const std::vector<uint8_t> output_ref_ipp =
         {0, 2, 3, 2, 0,
          2, 8, 13, 8, 2,
          3, 13, 0, 0, 0,
          2, 8, 0, 0, 0,
          128, 2, 0, 0, 0
         };
       const std::vector<uint8_t> output_ref_npp =
         {0, 1, 2, 1, 0,
          1, 7, 12, 7, 1,
          2, 12, 0, 0, 0,
          1, 7, 0, 0, 0,
          128, 1, 0, 0, 0
         };
        // clang-format on

        core::Tensor data =
                core::Tensor(input_data, {5, 5, 1}, core::Dtype::UInt8, device);
        core::Tensor kernel =
                core::Tensor(kernel_data, {5, 5}, core::Dtype::Float32, device);
        t::geometry::Image im(data);
        if (!t::geometry::Image::HAVE_IPPICV &&
            device.GetType() ==
                    core::Device::DeviceType::CPU) {  // Not Implemented
            ASSERT_THROW(im.Filter(kernel), std::runtime_error);
        } else {
            im = im.Filter(kernel);
            utility::LogInfo("{}", im.AsTensor().View({5, 5}).ToString());

            if (device.GetType() == core::Device::DeviceType::CPU) {
                EXPECT_TRUE(im.AsTensor().AllClose(
                        core::Tensor(output_ref_ipp, {5, 5, 1},
                                     core::Dtype::UInt8, device)));
            } else {
                EXPECT_TRUE(im.AsTensor().AllClose(
                        core::Tensor(output_ref_npp, {5, 5, 1},
                                     core::Dtype::UInt8, device)));
            }
        }
    }
}

TEST_P(ImagePermuteDevices, FilterSobel) {
    core::Device device = GetParam();

    // clang-format off
    const std::vector<float> input_data =
      {0, 0, 0, 0, 1,
       0, 1, 1, 0, 0,
       0, 0, 1, 0, 0,
       1, 0, 1, 0, 0,
       0, 0, 1, 1, 0};
    const std::vector<float> output_dx_ref =
      {1, 1, -1, 2, 3,
       2, 3, -2, -2, 1,
       0, 3, -1, -4, 0,
       -2, 2, 1, -4, -1,
       -1, 3, 3, -4, -3};
    const std::vector<float> output_dy_ref =
      {1, 3, 3, 0, -3,
       0, 1, 2, 0, -3,
       2, -1, -1, 0, 0,
       0, 0, 1, 2, 1,
       -3, -1, 1, 2, 1};
    // clang-format on

    {  // Float32 -> Float32
        core::Tensor data = core::Tensor(input_data, {5, 5, 1},
                                         core::Dtype::Float32, device);
        t::geometry::Image im(data);
        t::geometry::Image dx, dy;
        if (!t::geometry::Image::HAVE_IPPICV &&
            device.GetType() ==
                    core::Device::DeviceType::CPU) {  // Not Implemented
            ASSERT_THROW(im.FilterSobel(3), std::runtime_error);
        } else {
            std::tie(dx, dy) = im.FilterSobel(3);

            EXPECT_TRUE(dx.AsTensor().AllClose(core::Tensor(
                    output_dx_ref, {5, 5, 1}, core::Dtype::Float32, device)));
            EXPECT_TRUE(dy.AsTensor().AllClose(core::Tensor(
                    output_dy_ref, {5, 5, 1}, core::Dtype::Float32, device)));
            utility::LogInfo("{}", dx.AsTensor().View({5, 5}).ToString());
        }
    }

    {  // UInt8 -> Int16
        core::Tensor data = core::Tensor(input_data, {5, 5, 1},
                                         core::Dtype::Float32, device)
                                    .To(core::Dtype::UInt8);
        t::geometry::Image im(data);
        t::geometry::Image dx, dy;
        if (!t::geometry::Image::HAVE_IPPICV &&
            device.GetType() ==
                    core::Device::DeviceType::CPU) {  // Not Implemented
            ASSERT_THROW(im.FilterSobel(3), std::runtime_error);
        } else {
            std::tie(dx, dy) = im.FilterSobel(3);

            EXPECT_TRUE(dx.AsTensor().AllClose(
                    core::Tensor(output_dx_ref, {5, 5, 1}, core::Dtype::Float32,
                                 device)
                            .To(core::Dtype::Int16)));
            EXPECT_TRUE(dy.AsTensor().AllClose(
                    core::Tensor(output_dy_ref, {5, 5, 1}, core::Dtype::Float32,
                                 device)
                            .To(core::Dtype::Int16)));
            utility::LogInfo("{}", dx.AsTensor().View({5, 5}).ToString());
        }
    }
}

TEST_P(ImagePermuteDevices, Resize) {
    core::Device device = GetParam();

    {  // Float32
        // clang-format off
        const std::vector<float> input_data =
          {0, 0, 1, 1, 1, 1,
           0, 1, 1, 0, 0, 1,
           1, 0, 0, 1, 0, 1,
           0, 1, 1, 0, 1, 1,
           1, 1, 1, 0, 1, 1,
           1, 1, 1, 1, 1, 1};
        const std::vector<float> output_ref =
          {0, 1, 1,
           1, 0, 0,
           1, 1, 1};
        // clang-format on

        core::Tensor data = core::Tensor(input_data, {6, 6, 1},
                                         core::Dtype::Float32, device);
        t::geometry::Image im(data);
        if (!t::geometry::Image::HAVE_IPPICV &&
            device.GetType() ==
                    core::Device::DeviceType::CPU) {  // Not Implemented
            ASSERT_THROW(
                    im.Resize(0.5, t::geometry::Image::InterpType::Nearest),
                    std::runtime_error);
        } else {
            im = im.Resize(0.5, t::geometry::Image::InterpType::Nearest);
            EXPECT_TRUE(im.AsTensor().AllClose(core::Tensor(
                    output_ref, {3, 3, 1}, core::Dtype::Float32, device)));
        }
    }
    {  // UInt8
        // clang-format off
        const std::vector<uint8_t> input_data =
          {0, 0, 128, 1, 1, 1,
           0, 1, 1, 0, 0, 1,
           128, 0, 0, 255, 0, 1,
           0, 1, 128, 0, 1, 128,
           1, 128, 1, 0, 255, 128,
           1, 1, 1, 1, 128, 1};
        const std::vector<uint8_t> output_ref_ipp =
          {0, 32, 1,
           32, 96, 32,
           33, 1, 128};
        const std::vector<uint8_t> output_ref_npp =
          {0, 33, 1,
           32, 96, 33,
           33, 1, 128};
        // clang-format on

        core::Tensor data =
                core::Tensor(input_data, {6, 6, 1}, core::Dtype::UInt8, device);
        t::geometry::Image im(data);
        if (!t::geometry::Image::HAVE_IPPICV &&
            device.GetType() ==
                    core::Device::DeviceType::CPU) {  // Not Implemented
            ASSERT_THROW(im.Resize(0.5, t::geometry::Image::InterpType::Super),
                         std::runtime_error);
        } else {
            t::geometry::Image im_low =
                    im.Resize(0.5, t::geometry::Image::InterpType::Super);
            utility::LogInfo("{}", im_low.AsTensor().View({3, 3}).ToString());

            if (device.GetType() == core::Device::DeviceType::CPU) {
                EXPECT_TRUE(im_low.AsTensor().AllClose(
                        core::Tensor(output_ref_ipp, {3, 3, 1},
                                     core::Dtype::UInt8, device)));
            } else {
                EXPECT_TRUE(im_low.AsTensor().AllClose(
                        core::Tensor(output_ref_npp, {3, 3, 1},
                                     core::Dtype::UInt8, device)));

                // Check output in the CI to see if other inteprolations works
                // with other platforms
                im_low = im.Resize(0.5, t::geometry::Image::InterpType::Linear);
                utility::LogInfo("Linear: {}",
                                 im_low.AsTensor().View({3, 3}).ToString());

                im_low = im.Resize(0.5, t::geometry::Image::InterpType::Cubic);
                utility::LogInfo("Cubic: {}",
                                 im_low.AsTensor().View({3, 3}).ToString());

                im_low =
                        im.Resize(0.5, t::geometry::Image::InterpType::Lanczos);
                utility::LogInfo("Lanczos: {}",
                                 im_low.AsTensor().View({3, 3}).ToString());
            }
        }
    }
}

TEST_P(ImagePermuteDevices, PyrDown) {
    core::Device device = GetParam();

    {  // Float32
        // clang-format off
        const std::vector<float> input_data =
          {0, 0, 0, 1, 0, 1,
           0, 1, 0, 0, 0, 1,
           0, 0, 0, 1, 0, 1,
           1, 0, 0, 0, 0, 1,
           1, 0, 0, 0, 0, 1,
           1, 1, 1, 1, 1, 1};
        const std::vector<float> output_ref =
          {0.0596343, 0.244201, 0.483257,
           0.269109, 0.187536, 0.410317,
           0.752312, 0.347241, 0.521471};
        // clang-format on

        core::Tensor data = core::Tensor(input_data, {6, 6, 1},
                                         core::Dtype::Float32, device);
        t::geometry::Image im(data);

        if (!t::geometry::Image::HAVE_IPPICV &&
            device.GetType() ==
                    core::Device::DeviceType::CPU) {  // Not Implemented
            ASSERT_THROW(im.PyrDown(), std::runtime_error);
        } else {
            im = im.PyrDown();
            EXPECT_TRUE(im.AsTensor().AllClose(core::Tensor(
                    output_ref, {3, 3, 1}, core::Dtype::Float32, device)));
        }
    }

    {  // UInt8
        // clang-format off
        const std::vector<uint8_t> input_data =
          {0, 0, 0, 128, 0, 1,
           0, 128, 0, 0, 0, 1,
           0, 0, 0, 128, 0, 128,
           255, 0, 0, 0, 0, 1,
           1, 0, 0, 0, 0, 1,
           1, 1, 255, 1, 128, 255};
        const std::vector<uint8_t> output_ref_ipp =
          {8, 31, 26,
           51, 25, 30,
           48, 38, 46};
        const std::vector<uint8_t> output_ref_npp =
          {7, 31, 25,
           51, 25, 29,
           48, 38, 46};
        // clang-format on

        core::Tensor data =
                core::Tensor(input_data, {6, 6, 1}, core::Dtype::UInt8, device);
        t::geometry::Image im(data);

        if (!t::geometry::Image::HAVE_IPPICV &&
            device.GetType() ==
                    core::Device::DeviceType::CPU) {  // Not Implemented
            ASSERT_THROW(im.PyrDown(), std::runtime_error);
        } else {
            im = im.PyrDown();
            utility::LogInfo("{}", im.AsTensor().View({3, 3}).ToString());

            if (device.GetType() == core::Device::DeviceType::CPU) {
                EXPECT_TRUE(im.AsTensor().AllClose(
                        core::Tensor(output_ref_ipp, {3, 3, 1},
                                     core::Dtype::UInt8, device)));
            } else {
                EXPECT_TRUE(im.AsTensor().AllClose(
                        core::Tensor(output_ref_npp, {3, 3, 1},
                                     core::Dtype::UInt8, device)));
            }
        }
    }
}

TEST_P(ImagePermuteDevices, Dilate) {
    using ::testing::ElementsAreArray;

    // reference data used to validate the filtering of an image
    // clang-format off
    const std::vector<float> input_data = {
        0, 0, 0, 0, 0, 0, 0, 0,
        1.2, 1, 0, 0, 0, 0, 1, 0,
        0, 0, 1, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0};
    const std::vector<float> output_ref = {
        1.2, 1.2, 1, 0, 0, 1, 1, 1,
        1.2, 1.2, 1, 1, 0, 1, 1, 1,
        1.2, 1.2, 1, 1, 0, 1, 1, 1,
        0, 1, 1, 1, 0, 0, 0, 0};
    // clang-format on

    // test image dimensions
    const int rows = 4;
    const int cols = 8;
    const int channels = 1;
    const int kernel_size = 3;
    core::Device device = GetParam();

    core::Tensor t_input{
            input_data, {rows, cols, channels}, core::Dtype::Float32, device};
    t::geometry::Image input(t_input);
    t::geometry::Image output;

    // UInt8
    core::Tensor t_input_uint8_t =
            t_input.To(core::Dtype::UInt8);  // normal static_cast is OK
    t::geometry::Image input_uint8_t(t_input_uint8_t);
    if (!t::geometry::Image::HAVE_IPPICV &&
        device.GetType() == core::Device::DeviceType::CPU) {  // Not Implemented
        ASSERT_THROW(input_uint8_t.Dilate(kernel_size), std::runtime_error);
    } else {
        output = input_uint8_t.Dilate(kernel_size);
        EXPECT_EQ(output.GetRows(), input.GetRows());
        EXPECT_EQ(output.GetCols(), input.GetCols());
        EXPECT_EQ(output.GetChannels(), input.GetChannels());
        EXPECT_THAT(output.AsTensor().ToFlatVector<uint8_t>(),
                    ElementsAreArray(output_ref));
    }

    // UInt16
    core::Tensor t_input_uint16_t =
            t_input.To(core::Dtype::UInt16);  // normal static_cast is OK
    t::geometry::Image input_uint16_t(t_input_uint16_t);
    if (!t::geometry::Image::HAVE_IPPICV &&
        device.GetType() == core::Device::DeviceType::CPU) {  // Not Implemented
        ASSERT_THROW(input_uint16_t.Dilate(kernel_size), std::runtime_error);
    } else {
        output = input_uint16_t.Dilate(kernel_size);
        EXPECT_EQ(output.GetRows(), input.GetRows());
        EXPECT_EQ(output.GetCols(), input.GetCols());
        EXPECT_EQ(output.GetChannels(), input.GetChannels());
        EXPECT_THAT(output.AsTensor().ToFlatVector<uint16_t>(),
                    ElementsAreArray(output_ref));
    }

    // Float32
    if (!t::geometry::Image::HAVE_IPPICV &&
        device.GetType() == core::Device::DeviceType::CPU) {  // Not Implemented
        ASSERT_THROW(input.Dilate(kernel_size), std::runtime_error);
    } else {
        output = input.Dilate(kernel_size);
        EXPECT_EQ(output.GetRows(), input.GetRows());
        EXPECT_EQ(output.GetCols(), input.GetCols());
        EXPECT_EQ(output.GetChannels(), input.GetChannels());
        EXPECT_THAT(output.AsTensor().ToFlatVector<float>(),
                    ElementsAreArray(output_ref));
    }
}

// tImage: (r, c, ch) | legacy Image: (u, v, ch) = (c, r, ch)
TEST_P(ImagePermuteDevices, ToLegacyImage) {
    core::Device device = GetParam();
    // 2 byte dtype is general enough for uin8_t as well as float
    core::Dtype dtype = core::Dtype::UInt16;

    // 2D tensor for 1 channel image
    core::Tensor t_1ch(std::vector<uint16_t>{0, 1, 2, 3, 4, 5}, {2, 3}, dtype,
                       device);

    // Test 1 channel image conversion
    t::geometry::Image im_1ch(t_1ch);
    geometry::Image leg_im_1ch = im_1ch.ToLegacyImage();
    for (int r = 0; r < im_1ch.GetRows(); ++r)
        for (int c = 0; c < im_1ch.GetCols(); ++c)
            EXPECT_EQ(im_1ch.At(r, c).Item<uint16_t>(),
                      *leg_im_1ch.PointerAt<uint16_t>(c, r));

    // 3D tensor for 3 channel image
    core::Tensor t_3ch(
            std::vector<uint16_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
            {2, 2, 3}, dtype, device);
    // Test 3 channel image conversion
    t::geometry::Image im_3ch(t_3ch);
    geometry::Image leg_im_3ch = im_3ch.ToLegacyImage();
    for (int r = 0; r < im_3ch.GetRows(); ++r)
        for (int c = 0; c < im_3ch.GetCols(); ++c)
            for (int ch = 0; ch < im_3ch.GetChannels(); ++ch)
                EXPECT_EQ(im_3ch.At(r, c, ch).Item<uint16_t>(),
                          *leg_im_3ch.PointerAt<uint16_t>(c, r, ch));
}

}  // namespace tests
}  // namespace open3d
