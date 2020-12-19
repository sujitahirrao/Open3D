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
sys.path.append(os.path.dirname(os.path.realpath(__file__)) + "/..")
from open3d_test import list_devices


@pytest.mark.parametrize("device", list_devices())
def test_knn_index(device):
    dtype = o3c.Dtype.Float32

    t = o3c.Tensor.zeros((10, 3), dtype, device=device)
    nns = o3c.nns.NearestNeighborSearch(t)
    assert nns.knn_index()
    assert nns.fixed_radius_index(0.1)
    assert nns.hybrid_index()

    # Multi radii search is only supported on CPU.
    if device.get_type() == o3d.core.Device.DeviceType.CPU:
        assert nns.multi_radius_index()


@pytest.mark.parametrize("device", list_devices())
def test_knn_search(device):
    dtype = o3c.Dtype.Float32

    dataset_points = o3c.Tensor(
        [[0.0, 0.0, 0.0], [0.0, 0.0, 0.1], [0.0, 0.0, 0.2], [0.0, 0.1, 0.0],
         [0.0, 0.1, 0.1], [0.0, 0.1, 0.2], [0.0, 0.2, 0.0], [0.0, 0.2, 0.1],
         [0.0, 0.2, 0.2], [0.1, 0.0, 0.0]],
        dtype=dtype,
        device=device)
    nns = o3c.nns.NearestNeighborSearch(dataset_points)
    nns.knn_index()

    # Single query point.
    query_points = o3c.Tensor([[0.064705, 0.043921, 0.087843]],
                              dtype=dtype,
                              device=device)
    indices, distances = nns.knn_search(query_points, 3)
    np.testing.assert_equal(indices.cpu().numpy(),
                            np.array([[1, 4, 9]], dtype=np.int64))
    np.testing.assert_allclose(distances.cpu().numpy(),
                               np.array([[0.00626358, 0.00747938, 0.0108912]],
                                        dtype=np.float64),
                               rtol=1e-5,
                               atol=0)

    # Multiple query points.
    query_points = o3c.Tensor(
        [[0.064705, 0.043921, 0.087843], [0.064705, 0.043921, 0.087843]],
        dtype=dtype,
        device=device)
    indices, distances = nns.knn_search(query_points, 3)
    np.testing.assert_equal(indices.cpu().numpy(),
                            np.array([[1, 4, 9], [1, 4, 9]], dtype=np.int64))
    np.testing.assert_allclose(distances.cpu().numpy(),
                               np.array([[0.00626358, 0.00747938, 0.0108912],
                                         [0.00626358, 0.00747938, 0.0108912]],
                                        dtype=np.float64),
                               rtol=1e-5,
                               atol=0)


@pytest.mark.parametrize("device", list_devices())
def test_fixed_radius_search(device):
    dtype = o3c.Dtype.Float64

    dataset_points = o3c.Tensor(
        [[0.0, 0.0, 0.0], [0.0, 0.0, 0.1], [0.0, 0.0, 0.2], [0.0, 0.1, 0.0],
         [0.0, 0.1, 0.1], [0.0, 0.1, 0.2], [0.0, 0.2, 0.0], [0.0, 0.2, 0.1],
         [0.0, 0.2, 0.2], [0.1, 0.0, 0.0]],
        dtype=dtype,
        device=device)
    nns = o3c.nns.NearestNeighborSearch(dataset_points)
    nns.fixed_radius_index(0.1)

    # Single query point.
    query_points = o3c.Tensor([[0.064705, 0.043921, 0.087843]],
                              dtype=dtype,
                              device=device)
    indices, distances, num_neighbors = nns.fixed_radius_search(
        query_points, 0.1)
    np.testing.assert_equal(indices.cpu().numpy(),
                            np.array([1, 4], dtype=np.int64))
    np.testing.assert_allclose(distances.cpu().numpy(),
                               np.array([0.00626358, 0.00747938],
                                        dtype=np.float64),
                               rtol=1e-5,
                               atol=0)
    np.testing.assert_equal(num_neighbors.cpu().numpy(),
                            np.array([2], dtype=np.int64))

    # Multiple query points.
    query_points = o3c.Tensor(
        [[0.064705, 0.043921, 0.087843], [0.064705, 0.043921, 0.087843]],
        dtype=dtype,
        device=device)
    indices, distances, num_neighbors = nns.fixed_radius_search(
        query_points, 0.1)
    np.testing.assert_equal(indices.cpu().numpy(),
                            np.array([1, 4, 1, 4], dtype=np.int64))
    np.testing.assert_allclose(
        distances.cpu().numpy(),
        np.array([0.00626358, 0.00747938, 0.00626358, 0.00747938],
                 dtype=np.float64),
        rtol=1e-5,
        atol=0)
    np.testing.assert_equal(num_neighbors.cpu().numpy(),
                            np.array([2, 2], dtype=np.int64))
