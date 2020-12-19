// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2020 www.open3d.org
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

#include <string>
#include <unordered_map>

#include "open3d/t/geometry/RGBDImage.h"
#include "pybind/docstring.h"
#include "pybind/pybind_utils.h"
#include "pybind/t/geometry/geometry.h"

namespace open3d {
namespace t {
namespace geometry {

// Image functions have similar arguments, thus the arg docstrings may be shared
static const std::unordered_map<std::string, std::string>
        map_shared_argument_docstrings = {
                {"color", "The color image."},
                {"depth", "The depth image."},
                {"aligned",
                 "Are the two images aligned (same viewpoint and resolution)?"},
                {"image", "The Image object."},
                {"tensor",
                 "Tensor of the image. The tensor must be contiguous. The "
                 "tensor must be 2D (rows, cols) or 3D (rows, cols, "
                 "channels)."},
                {"rows",
                 "Number of rows of the image, i.e. image height. rows must be "
                 "non-negative."},
                {"cols",
                 "Number of columns of the image, i.e. image width. cols must "
                 "be non-negative."},
                {"channels",
                 "Number of channels of the image. E.g. for RGB image, "
                 "channels == 3; for grayscale image, channels == 1. channels "
                 "must be greater than 0."},
                {"dtype", "Data type of the image."},
                {"device", "Device where the image is stored."}};

void pybind_image(py::module &m) {
    py::class_<Image, PyGeometry<Image>, Geometry> image(
            m, "Image", py::buffer_protocol(),
            "The Image class stores image with customizable rols, cols, "
            "channels, dtype and device.");
    // Constructors
    image.def(py::init<int64_t, int64_t, int64_t, core::Dtype, core::Device>(),
              "Row-major storage is used, similar to OpenCV. Use (row, col, "
              "channel) indexing order for image creation and accessing. In "
              "general, (r, c, ch) are the preferred variable names for "
              "consistency, and avoid using width, height, u, v, x, y for "
              "coordinates.",
              "rows"_a = 0, "cols"_a = 0, "channels"_a = 1,
              "dtype"_a = core::Dtype::Float32,
              "device"_a = core::Device("CPU:0"))
            .def(py::init<core::Tensor &>(),
                 "Construct from a tensor. The tensor won't be copied and "
                 "memory will be shared.",
                 "tensor"_a);
    docstring::ClassMethodDocInject(m, "Image", "__init__",
                                    map_shared_argument_docstrings);
    // Buffer protocol.
    image.def_buffer([](Image &I) -> py::buffer_info {
        return py::buffer_info(I.GetDataPtr(), I.GetDtype().ByteSize(),
                               pybind_utils::DtypeToArrayFormat(I.GetDtype()),
                               I.AsTensor().NumDims(), I.AsTensor().GetShape(),
                               I.AsTensor().GetStrides());
    });
    // Info.
    image.def_property_readonly("dtype", &Image::GetDtype,
                                "Get dtype of the image")
            .def_property_readonly("device", &Image::GetDevice,
                                   "Get the device of the image.")
            .def_property_readonly("rows", &Image::GetRows,
                                   "Get the number of rows of the image.")
            .def_property_readonly("columns", &Image::GetCols,
                                   "Get the number of columns of the image.")
            .def_property_readonly("channels", &Image::GetChannels,
                                   "Get the number of channels of the image.")
            // functions
            .def("clear", &Image::Clear, "Clear stored data.")
            .def("is_empty", &Image::IsEmpty, "Is any data stored?")
            .def("get_min_bound", &Image::GetMinBound,
                 "Compute min 2D coordinates for the data (always {0, 0}).")
            .def("get_max_bound", &Image::GetMaxBound,
                 "Compute max 2D coordinates for the data ({rows, cols}).")
            .def("__repr__", &Image::ToString);

    // Conversion.
    image.def("to_legacy_image", &Image::ToLegacyImage,
              "Convert to legacy Image type.");
    image.def_static("from_legacy_image", &Image::FromLegacyImage,
                     "image_legacy"_a, "device"_a = core::Device("CPU:0"),
                     "Create a Image from a legacy Open3D Image.");
    image.def("as_tensor", &Image::AsTensor);

    docstring::ClassMethodDocInject(m, "Image", "get_min_bound");
    docstring::ClassMethodDocInject(m, "Image", "get_max_bound");
    docstring::ClassMethodDocInject(m, "Image", "clear");
    docstring::ClassMethodDocInject(m, "Image", "is_empty");
    docstring::ClassMethodDocInject(m, "Image", "to_legacy_image");

    py::class_<RGBDImage, PyGeometry<RGBDImage>, Geometry> rgbd_image(
            m, "RGBDImage",
            "RGBDImage is a pair of color and depth images. For most "
            "procesing, the image pair should be aligned (same viewpoint and  "
            "resolution).");
    rgbd_image
            // Constructors.
            .def(py::init<>(), "Construct an empty RGBDImage.")
            .def(py::init<const Image &, const Image &, bool>(),
                 "Parameterized constructor", "color"_a, "depth"_a,
                 "aligned"_a = true)
            // Depth and color images.
            .def_readwrite("color", &RGBDImage::color_, "The color image.")
            .def_readwrite("depth", &RGBDImage::depth_, "The depth image.")
            .def_readwrite("aligned_", &RGBDImage::aligned_,
                           "Are the depth and color images aligned (same "
                           "viewpoint and resolution)?")
            // Functions.
            .def("clear", &RGBDImage::Clear, "Clear stored data.")
            .def("is_empty", &RGBDImage::IsEmpty, "Is any data stored?")
            .def("are_aligned", &RGBDImage::AreAligned,
                 "Are the depth and color images aligned (same viewpoint and "
                 "resolution)?")
            .def("get_min_bound", &RGBDImage::GetMinBound,
                 "Compute min 2D coordinates for the data (always {0, 0}).")
            .def("get_max_bound", &RGBDImage::GetMaxBound,
                 "Compute max 2D coordinates for the data.")
            // Conversion.
            .def("to_legacy_rgbd_image", &RGBDImage::ToLegacyRGBDImage,
                 "Convert to legacy RGBDImage type.")
            // Description.
            .def("__repr__", &RGBDImage::ToString);

    docstring::ClassMethodDocInject(m, "RGBDImage", "get_min_bound");
    docstring::ClassMethodDocInject(m, "RGBDImage", "get_max_bound");
    docstring::ClassMethodDocInject(m, "RGBDImage", "clear");
    docstring::ClassMethodDocInject(m, "RGBDImage", "is_empty");
    docstring::ClassMethodDocInject(m, "RGBDImage", "to_legacy_rgbd_image");
    docstring::ClassMethodDocInject(m, "RGBDImage", "__init__",
                                    map_shared_argument_docstrings);
}

}  // namespace geometry
}  // namespace t
}  // namespace open3d
