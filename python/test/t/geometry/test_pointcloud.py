# ----------------------------------------------------------------------------
# -                        Open3D: www.open3d.org                            -
# ----------------------------------------------------------------------------
# The MIT License (MIT)
#
# Copyright (c) 2020 www.open3d.org
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
# ----------------------------------------------------------------------------

import open3d as o3d
import open3d.core as o3c
import numpy as np
import pytest

import sys
import os
sys.path.append(os.path.dirname(os.path.realpath(__file__)) + "/../..")
from open3d_test import list_devices


# Cast o3d.pybind.core.Tensor (C++) to o3d.core.Tensor (Python). The underlying
# memory are the same.
# TODO: remove this after unifying o3d.pybind.core.Tensor and o3c.Tensor
def cast_to_py_tensor(c_tensor):
    if isinstance(c_tensor, o3d.pybind.core.Tensor):
        py_tensor = o3c.Tensor([])
        py_tensor.shallow_copy_from(c_tensor)
        return py_tensor
    else:
        return c_tensor


@pytest.mark.parametrize("device", list_devices())
def test_constructor_and_accessors(device):
    dtype = o3c.Dtype.Float32

    # Constructor.
    pcd = o3d.t.geometry.PointCloud(device)
    assert "points" not in pcd.point
    assert "colors" not in pcd.point
    assert isinstance(pcd.point, o3d.t.geometry.TensorMap)

    # Assignment.
    pcd.point["points"] = o3c.Tensor.ones((0, 3), dtype, device)
    pcd.point["colors"] = o3c.Tensor.ones((0, 3), dtype, device)
    assert len(pcd.point["points"]) == 0
    assert len(pcd.point["colors"]) == 0

    pcd.point["points"] = o3c.Tensor.ones((1, 3), dtype, device)
    pcd.point["colors"] = o3c.Tensor.ones((1, 3), dtype, device)
    assert len(pcd.point["points"]) == 1
    assert len(pcd.point["colors"]) == 1

    # Edit and access values.
    points = cast_to_py_tensor(pcd.point["points"])
    points[0] = o3c.Tensor([1, 2, 3], dtype, device)
    assert pcd.point["points"].allclose(o3c.Tensor([[1, 2, 3]], dtype, device))


@pytest.mark.parametrize("device", list_devices())
def test_from_legacy_pointcloud(device):
    dtype = o3c.Dtype.Float32

    legacy_pcd = o3d.geometry.PointCloud()
    legacy_pcd.points = o3d.utility.Vector3dVector(
        np.array([[0, 1, 2], [3, 4, 5]]))
    legacy_pcd.colors = o3d.utility.Vector3dVector(
        np.array([[6, 7, 8], [9, 10, 11]]))

    pcd = o3d.t.geometry.PointCloud.from_legacy_pointcloud(
        legacy_pcd, dtype, device)
    assert pcd.point["points"].allclose(
        o3c.Tensor([[0, 1, 2], [3, 4, 5]], dtype, device))
    assert pcd.point["colors"].allclose(
        o3c.Tensor([[6, 7, 8], [9, 10, 11]], dtype, device))


@pytest.mark.parametrize("device", list_devices())
def test_to_legacy_pointcloud(device):
    dtype = o3c.Dtype.Float32

    pcd = o3d.t.geometry.PointCloud(device)
    pcd.point["points"] = o3c.Tensor([[0, 1, 2], [3, 4, 5]], dtype, device)
    pcd.point["colors"] = o3c.Tensor([[6, 7, 8], [9, 10, 11]], dtype, device)

    legacy_pcd = pcd.to_legacy_pointcloud()
    np.testing.assert_allclose(np.asarray(legacy_pcd.points),
                               np.array([[0, 1, 2], [3, 4, 5]]))
    np.testing.assert_allclose(np.asarray(legacy_pcd.colors),
                               np.array([[6, 7, 8], [9, 10, 11]]))


@pytest.mark.parametrize("device", list_devices())
def test_member_functions(device):
    dtype = o3c.Dtype.Float32

    # get_min_bound, get_max_bound, get_center.
    pcd = o3d.t.geometry.PointCloud(device)
    pcd.point["points"] = o3c.Tensor([[1, 10, 20], [30, 2, 40], [50, 60, 3]],
                                     dtype, device)
    assert pcd.get_min_bound().allclose(o3c.Tensor([1, 2, 3], dtype, device))
    assert pcd.get_max_bound().allclose(o3c.Tensor([50, 60, 40], dtype, device))
    assert pcd.get_center().allclose(o3c.Tensor([27, 24, 21], dtype, device))

    # transform.
    pcd = o3d.t.geometry.PointCloud(device)
    transform_t = o3c.Tensor(
        [[1, 1, 0, 1], [0, 1, 1, 1], [0, 1, 0, 1], [0, 0, 0, 1]], dtype, device)
    pcd.point["points"] = o3c.Tensor([[1, 1, 1]], dtype, device)
    pcd.point["normals"] = o3c.Tensor([[1, 1, 1]], dtype, device)
    pcd.transform(transform_t)
    assert pcd.point["points"].allclose(o3c.Tensor([[3, 3, 2]], dtype, device))
    assert pcd.point["normals"].allclose(o3c.Tensor([[2, 2, 1]], dtype, device))

    # translate.
    pcd = o3d.t.geometry.PointCloud(device)
    transloation = o3c.Tensor([10, 20, 30], dtype, device)

    pcd.point["points"] = o3c.Tensor([[0, 1, 2], [6, 7, 8]], dtype, device)
    pcd.translate(transloation, True)
    assert pcd.point["points"].allclose(
        o3c.Tensor([[10, 21, 32], [16, 27, 38]], dtype, device))

    pcd.point["points"] = o3c.Tensor([[0, 1, 2], [6, 7, 8]], dtype, device)
    pcd.translate(transloation, False)
    assert pcd.point["points"].allclose(
        o3c.Tensor([[7, 17, 27], [13, 23, 33]], dtype, device))

    # scale
    pcd = o3d.t.geometry.PointCloud(device)
    pcd.point["points"] = o3c.Tensor([[0, 0, 0], [1, 1, 1], [2, 2, 2]], dtype,
                                     device)
    center = o3c.Tensor([1, 1, 1], dtype, device)
    pcd.scale(4, center)
    assert pcd.point["points"].allclose(
        o3c.Tensor([[-3, -3, -3], [1, 1, 1], [5, 5, 5]], dtype, device))

    # rotate.
    pcd = o3d.t.geometry.PointCloud(device)
    rotation = o3c.Tensor([[1, 1, 0], [0, 1, 1], [0, 1, 0]], dtype, device)
    center = o3c.Tensor([1, 1, 1], dtype, device)
    pcd.point["points"] = o3c.Tensor([[2, 2, 2]], dtype, device)
    pcd.point["normals"] = o3c.Tensor([[1, 1, 1]], dtype, device)
    pcd.rotate(rotation, center)
    assert pcd.point["points"].allclose(o3c.Tensor([[3, 3, 2]], dtype, device))
    assert pcd.point["normals"].allclose(o3c.Tensor([[2, 2, 1]], dtype, device))
