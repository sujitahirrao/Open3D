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

#include "open3d/core/hashmap/CUDA/HashmapBufferCUDA.cuh"
#include "open3d/core/hashmap/CUDA/InternalNodeManager.h"
#include "open3d/core/hashmap/CUDA/Macros.h"
#include "open3d/core/hashmap/CUDA/Traits.h"
#include "open3d/core/hashmap/DeviceHashmap.h"

namespace open3d {
namespace core {

template <typename Hash, typename KeyEq>
class CUDAHashmapImplContext {
public:
    CUDAHashmapImplContext();

    __host__ void Setup(int64_t init_buckets,
                        int64_t init_capacity,
                        int64_t dsize_key,
                        int64_t dsize_value,
                        const InternalNodeManagerContext& node_mgr_ctx,
                        const CUDAHashmapBufferContext& kv_mgr_ctx);

    __device__ bool Insert(bool lane_active,
                           uint32_t lane_id,
                           uint32_t bucket_id,
                           const void* key_ptr,
                           addr_t iterator_addr);

    __device__ Pair<addr_t, bool> Find(bool lane_active,
                                       uint32_t lane_id,
                                       uint32_t bucket_id,
                                       const void* key_ptr);

    __device__ Pair<addr_t, bool> Erase(bool lane_active,
                                        uint32_t lane_id,
                                        uint32_t bucket_id,
                                        const void* key_ptr);

    __device__ void WarpSyncKey(const void* key_ptr,
                                uint32_t lane_id,
                                void* ret_key_ptr);
    __device__ int32_t WarpFindKey(const void* src_key_ptr,
                                   uint32_t lane_id,
                                   addr_t ptr);
    __device__ int32_t WarpFindEmpty(addr_t unit_data);

    // Hash function.
    __device__ int64_t ComputeBucket(const void* key_ptr) const;

    // Node manager.
    __device__ addr_t AllocateSlab(uint32_t lane_id);
    __device__ void FreeSlab(addr_t slab_ptr);

    // Helpers.
    __device__ addr_t* get_unit_ptr_from_list_nodes(addr_t slab_ptr,
                                                    uint32_t lane_id) {
        return node_mgr_ctx_.get_unit_ptr_from_slab(slab_ptr, lane_id);
    }
    __device__ addr_t* get_unit_ptr_from_list_head(uint32_t bucket_id,
                                                   uint32_t lane_id) {
        return reinterpret_cast<uint32_t*>(bucket_list_head_) +
               bucket_id * kWarpSize + lane_id;
    }

public:
    Hash hash_fn_;
    KeyEq cmp_fn_;

    int64_t bucket_count_;
    int64_t capacity_;
    int64_t dsize_key_;
    int64_t dsize_value_;

    Slab* bucket_list_head_;
    InternalNodeManagerContext node_mgr_ctx_;
    CUDAHashmapBufferContext kv_mgr_ctx_;
};

/// Kernels
template <typename Hash, typename KeyEq>
__global__ void InsertKernelPass0(CUDAHashmapImplContext<Hash, KeyEq> hash_ctx,
                                  const void* input_keys,
                                  addr_t* output_addrs,
                                  int heap_counter_prev,
                                  int64_t count);

template <typename Hash, typename KeyEq>
__global__ void InsertKernelPass1(CUDAHashmapImplContext<Hash, KeyEq> hash_ctx,
                                  const void* input_keys,
                                  addr_t* output_addrs,
                                  bool* output_masks,
                                  int64_t count);

template <typename Hash, typename KeyEq>
__global__ void InsertKernelPass2(CUDAHashmapImplContext<Hash, KeyEq> hash_ctx,
                                  const void* input_values,
                                  addr_t* output_addrs,
                                  bool* output_masks,
                                  int64_t count);

template <typename Hash, typename KeyEq>
__global__ void FindKernel(CUDAHashmapImplContext<Hash, KeyEq> hash_ctx,
                           const void* input_keys,
                           addr_t* output_addrs,
                           bool* output_masks,
                           int64_t count);

template <typename Hash, typename KeyEq>
__global__ void EraseKernelPass0(CUDAHashmapImplContext<Hash, KeyEq> hash_ctx,
                                 const void* input_keys,
                                 addr_t* output_addrs,
                                 bool* output_masks,
                                 int64_t count);

template <typename Hash, typename KeyEq>
__global__ void EraseKernelPass1(CUDAHashmapImplContext<Hash, KeyEq> hash_ctx,
                                 addr_t* output_addrs,
                                 bool* output_masks,
                                 int64_t count);

template <typename Hash, typename KeyEq>
__global__ void GetActiveIndicesKernel(
        CUDAHashmapImplContext<Hash, KeyEq> hash_ctx,
        addr_t* output_addrs,
        uint32_t* output_iterator_count);

template <typename Hash, typename KeyEq>
__global__ void CountElemsPerBucketKernel(
        CUDAHashmapImplContext<Hash, KeyEq> hash_ctx,
        int64_t* bucket_elem_counts);

template <typename Hash, typename KeyEq>
CUDAHashmapImplContext<Hash, KeyEq>::CUDAHashmapImplContext()
    : bucket_count_(0), bucket_list_head_(nullptr) {}

template <typename Hash, typename KeyEq>
void CUDAHashmapImplContext<Hash, KeyEq>::Setup(
        int64_t init_buckets,
        int64_t init_capacity,
        int64_t dsize_key,
        int64_t dsize_value,
        const InternalNodeManagerContext& allocator_ctx,
        const CUDAHashmapBufferContext& pair_allocator_ctx) {
    bucket_count_ = init_buckets;
    capacity_ = init_capacity;
    dsize_key_ = dsize_key;
    dsize_value_ = dsize_value;

    node_mgr_ctx_ = allocator_ctx;
    kv_mgr_ctx_ = pair_allocator_ctx;

    hash_fn_.key_size_in_int_ = dsize_key / sizeof(int);
    cmp_fn_.key_size_in_int_ = dsize_key / sizeof(int);
}

template <typename Hash, typename KeyEq>
__device__ bool CUDAHashmapImplContext<Hash, KeyEq>::Insert(
        bool lane_active,
        uint32_t lane_id,
        uint32_t bucket_id,
        const void* key,
        addr_t iterator_addr) {
    uint32_t work_queue = 0;
    uint32_t prev_work_queue = 0;
    uint32_t curr_slab_ptr = kHeadSlabAddr;
    uint8_t src_key[kMaxKeyByteSize];

    bool mask = false;

    // > Loop when we have active lanes
    while ((work_queue = __ballot_sync(kSyncLanesMask, lane_active))) {
        // 0. Restart from linked list head if last insertion is finished
        curr_slab_ptr =
                (prev_work_queue != work_queue) ? kHeadSlabAddr : curr_slab_ptr;
        uint32_t src_lane = __ffs(work_queue) - 1;
        uint32_t src_bucket =
                __shfl_sync(kSyncLanesMask, bucket_id, src_lane, kWarpSize);

        WarpSyncKey(key, src_lane, src_key);

        // Each lane in the warp reads a unit in the slab
        uint32_t unit_data =
                (curr_slab_ptr == kHeadSlabAddr)
                        ? *(get_unit_ptr_from_list_head(src_bucket, lane_id))
                        : *(get_unit_ptr_from_list_nodes(curr_slab_ptr,
                                                         lane_id));

        int32_t lane_found = WarpFindKey(src_key, lane_id, unit_data);
        int32_t lane_empty = WarpFindEmpty(unit_data);

        // Branch 1: key already existing, ABORT
        if (lane_found >= 0) {
            if (lane_id == src_lane) {
                // free memory heap
                lane_active = false;
            }
        }

        // Branch 2: empty slot available, try to insert
        else if (lane_empty >= 0) {
            if (lane_id == src_lane) {
                // TODO: check why we cannot put malloc here
                const uint32_t* unit_data_ptr =
                        (curr_slab_ptr == kHeadSlabAddr)
                                ? get_unit_ptr_from_list_head(src_bucket,
                                                              lane_empty)
                                : get_unit_ptr_from_list_nodes(curr_slab_ptr,
                                                               lane_empty);

                addr_t old_iterator_addr =
                        atomicCAS((unsigned int*)unit_data_ptr, kEmptyNodeAddr,
                                  iterator_addr);

                // Remember to clean up in another pass
                // Branch 2.1: SUCCEED
                if (old_iterator_addr == kEmptyNodeAddr) {
                    lane_active = false;
                    mask = true;
                }
                // Branch 2.2: failed: RESTART
                // In the consequent attempt,
                // > if the same key was inserted in this slot,
                //   we fall back to Branch 1;
                // > if a different key was inserted,
                //   we go to Branch 2 or 3.
            }
        }

        // Branch 3: nothing found in this slab, goto next slab
        else {
            // broadcast next slab
            addr_t next_slab_ptr = __shfl_sync(kSyncLanesMask, unit_data,
                                               kNextSlabPtrLaneId, kWarpSize);

            // Branch 3.1: next slab existing, RESTART this lane
            if (next_slab_ptr != kEmptySlabAddr) {
                curr_slab_ptr = next_slab_ptr;
            }

            // Branch 3.2: next slab empty, try to allocate one
            else {
                addr_t new_next_slab_ptr = AllocateSlab(lane_id);

                if (lane_id == kNextSlabPtrLaneId) {
                    const uint32_t* unit_data_ptr =
                            (curr_slab_ptr == kHeadSlabAddr)
                                    ? get_unit_ptr_from_list_head(
                                              src_bucket, kNextSlabPtrLaneId)
                                    : get_unit_ptr_from_list_nodes(
                                              curr_slab_ptr,
                                              kNextSlabPtrLaneId);

                    addr_t old_next_slab_ptr =
                            atomicCAS((unsigned int*)unit_data_ptr,
                                      kEmptySlabAddr, new_next_slab_ptr);

                    // Branch 3.2.1: other thread allocated, RESTART lane. In
                    // the consequent attempt, goto Branch 2'
                    if (old_next_slab_ptr != kEmptySlabAddr) {
                        FreeSlab(new_next_slab_ptr);
                    }
                    // Branch 3.2.2: this thread allocated, RESTART lane, 'goto
                    // Branch 2'
                }
            }
        }

        prev_work_queue = work_queue;
    }

    return mask;
}

template <typename Hash, typename KeyEq>
__device__ Pair<addr_t, bool> CUDAHashmapImplContext<Hash, KeyEq>::Find(
        bool lane_active,
        uint32_t lane_id,
        uint32_t bucket_id,
        const void* query_key) {
    uint32_t work_queue = 0;
    uint32_t prev_work_queue = work_queue;
    uint32_t curr_slab_ptr = kHeadSlabAddr;

    addr_t iterator = kNullAddr;
    bool mask = false;

    // > Loop when we have active lanes.
    while ((work_queue = __ballot_sync(kSyncLanesMask, lane_active))) {
        // 0. Restart from linked list head if the last query is finished.
        curr_slab_ptr =
                (prev_work_queue != work_queue) ? kHeadSlabAddr : curr_slab_ptr;
        uint32_t src_lane = __ffs(work_queue) - 1;
        uint32_t src_bucket =
                __shfl_sync(kSyncLanesMask, bucket_id, src_lane, kWarpSize);

        uint8_t src_key[kMaxKeyByteSize];
        WarpSyncKey(query_key, src_lane, src_key);

        // Each lane in the warp reads a unit in the slab in parallel.
        const uint32_t unit_data =
                (curr_slab_ptr == kHeadSlabAddr)
                        ? *(get_unit_ptr_from_list_head(src_bucket, lane_id))
                        : *(get_unit_ptr_from_list_nodes(curr_slab_ptr,
                                                         lane_id));

        int32_t lane_found = WarpFindKey(src_key, lane_id, unit_data);

        // 1. Found in this slab, SUCCEED.
        if (lane_found >= 0) {
            // broadcast found value
            addr_t found_pair_internal_ptr = __shfl_sync(
                    kSyncLanesMask, unit_data, lane_found, kWarpSize);

            if (lane_id == src_lane) {
                lane_active = false;

                // Actually iterator_addr
                iterator = found_pair_internal_ptr;
                mask = true;
            }
        }

        // 2. Not found in this slab.
        else {
            // Broadcast next slab: lane 31 reads 'next'.
            addr_t next_slab_ptr = __shfl_sync(kSyncLanesMask, unit_data,
                                               kNextSlabPtrLaneId, kWarpSize);

            // 2.1. Next slab is empty, ABORT.
            if (next_slab_ptr == kEmptySlabAddr) {
                if (lane_id == src_lane) {
                    lane_active = false;
                }
            }
            // 2.2. Next slab exists, RESTART.
            else {
                curr_slab_ptr = next_slab_ptr;
            }
        }

        prev_work_queue = work_queue;
    }

    return make_pair(iterator, mask);
}

template <typename Hash, typename KeyEq>
__device__ Pair<addr_t, bool> CUDAHashmapImplContext<Hash, KeyEq>::Erase(
        bool lane_active,
        uint32_t lane_id,
        uint32_t bucket_id,
        const void* key) {
    uint32_t work_queue = 0;
    uint32_t prev_work_queue = 0;
    uint32_t curr_slab_ptr = kHeadSlabAddr;
    uint8_t src_key[kMaxKeyByteSize];

    addr_t iterator_addr = 0;
    bool mask = false;

    // > Loop when we have active lanes.
    while ((work_queue = __ballot_sync(kSyncLanesMask, lane_active))) {
        // 0. Restart from linked list head if last insertion is finished.
        curr_slab_ptr =
                (prev_work_queue != work_queue) ? kHeadSlabAddr : curr_slab_ptr;
        uint32_t src_lane = __ffs(work_queue) - 1;
        uint32_t src_bucket =
                __shfl_sync(kSyncLanesMask, bucket_id, src_lane, kWarpSize);

        WarpSyncKey(key, src_lane, src_key);

        const uint32_t unit_data =
                (curr_slab_ptr == kHeadSlabAddr)
                        ? *(get_unit_ptr_from_list_head(src_bucket, lane_id))
                        : *(get_unit_ptr_from_list_nodes(curr_slab_ptr,
                                                         lane_id));

        int32_t lane_found = WarpFindKey(src_key, lane_id, unit_data);

        // Branch 1: key found.
        if (lane_found >= 0) {
            if (lane_id == src_lane) {
                uint32_t* unit_data_ptr =
                        (curr_slab_ptr == kHeadSlabAddr)
                                ? get_unit_ptr_from_list_head(src_bucket,
                                                              lane_found)
                                : get_unit_ptr_from_list_nodes(curr_slab_ptr,
                                                               lane_found);

                uint32_t pair_to_delete = atomicExch(
                        (unsigned int*)unit_data_ptr, kEmptyNodeAddr);
                mask = pair_to_delete != kEmptyNodeAddr;
                iterator_addr = pair_to_delete;
                // Branch 1.2: other thread did the job, avoid double free
            }
        } else {  // no matching slot found:
            addr_t next_slab_ptr = __shfl_sync(kSyncLanesMask, unit_data,
                                               kNextSlabPtrLaneId, kWarpSize);
            if (next_slab_ptr == kEmptySlabAddr) {
                // not found:
                if (lane_id == src_lane) {
                    lane_active = false;
                }
            } else {
                curr_slab_ptr = next_slab_ptr;
            }
        }
        prev_work_queue = work_queue;
    }

    return make_pair(iterator_addr, mask);
}

template <typename Hash, typename KeyEq>
__device__ void CUDAHashmapImplContext<Hash, KeyEq>::WarpSyncKey(
        const void* key_ptr, uint32_t lane_id, void* ret_key_ptr) {
    auto dst_key_ptr = static_cast<int*>(ret_key_ptr);
    auto src_key_ptr = static_cast<const int*>(key_ptr);
    for (int i = 0; i < hash_fn_.key_size_in_int_; ++i) {
        dst_key_ptr[i] =
                __shfl_sync(kSyncLanesMask, src_key_ptr[i], lane_id, kWarpSize);
    }
}

template <typename Hash, typename KeyEq>
__device__ int32_t CUDAHashmapImplContext<Hash, KeyEq>::WarpFindKey(
        const void* key_ptr, uint32_t lane_id, addr_t ptr) {
    bool is_lane_found =
            // Select key lanes.
            ((1 << lane_id) & kNodePtrLanesMask)
            // Validate key addrs.
            && (ptr != kEmptyNodeAddr)
            // Find keys in memory heap.
            && cmp_fn_(kv_mgr_ctx_.ExtractIterator(ptr).first, key_ptr);

    return __ffs(__ballot_sync(kNodePtrLanesMask, is_lane_found)) - 1;
}

template <typename Hash, typename KeyEq>
__device__ int32_t
CUDAHashmapImplContext<Hash, KeyEq>::WarpFindEmpty(addr_t ptr) {
    bool is_lane_empty = (ptr == kEmptyNodeAddr);
    return __ffs(__ballot_sync(kNodePtrLanesMask, is_lane_empty)) - 1;
}

template <typename Hash, typename KeyEq>
__device__ int64_t
CUDAHashmapImplContext<Hash, KeyEq>::ComputeBucket(const void* key) const {
    return hash_fn_(key) % bucket_count_;
}

template <typename Hash, typename KeyEq>
__device__ addr_t
CUDAHashmapImplContext<Hash, KeyEq>::AllocateSlab(uint32_t lane_id) {
    return node_mgr_ctx_.WarpAllocate(lane_id);
}

template <typename Hash, typename KeyEq>
__device__ __forceinline__ void CUDAHashmapImplContext<Hash, KeyEq>::FreeSlab(
        addr_t slab_ptr) {
    node_mgr_ctx_.FreeUntouched(slab_ptr);
}

template <typename Hash, typename KeyEq>
__global__ void InsertKernelPass0(CUDAHashmapImplContext<Hash, KeyEq> hash_ctx,
                                  const void* input_keys,
                                  addr_t* output_addrs,
                                  int heap_counter_prev,
                                  int64_t count) {
    uint32_t tid = threadIdx.x + blockIdx.x * blockDim.x;

    if (tid < count) {
        // First write ALL input_keys to avoid potential thread conflicts.
        addr_t iterator_addr =
                hash_ctx.kv_mgr_ctx_.heap_[heap_counter_prev + tid];
        iterator_t iterator =
                hash_ctx.kv_mgr_ctx_.ExtractIterator(iterator_addr);

        MEMCPY_AS_INTS(iterator.first,
                       static_cast<const uint8_t*>(input_keys) +
                               tid * hash_ctx.dsize_key_,
                       hash_ctx.dsize_key_);
        output_addrs[tid] = iterator_addr;
    }
}

template <typename Hash, typename KeyEq>
__global__ void InsertKernelPass1(CUDAHashmapImplContext<Hash, KeyEq> hash_ctx,
                                  const void* input_keys,
                                  addr_t* output_addrs,
                                  bool* output_masks,
                                  int64_t count) {
    uint32_t tid = threadIdx.x + blockIdx.x * blockDim.x;
    uint32_t lane_id = tid & 0x1F;

    if (tid - lane_id >= count) {
        return;
    }

    hash_ctx.node_mgr_ctx_.Init(tid, lane_id);

    bool lane_active = false;
    uint32_t bucket_id = 0;
    addr_t iterator_addr = 0;

    // Dummy.
    uint8_t dummy_key[kMaxKeyByteSize];
    const void* key = reinterpret_cast<const void*>(dummy_key);

    if (tid < count) {
        lane_active = true;
        key = static_cast<const uint8_t*>(input_keys) +
              tid * hash_ctx.dsize_key_;
        iterator_addr = output_addrs[tid];
        bucket_id = hash_ctx.ComputeBucket(key);
    }

    // Index out-of-bound threads still have to run for warp synchronization.
    bool mask = hash_ctx.Insert(lane_active, lane_id, bucket_id, key,
                                iterator_addr);

    if (tid < count) {
        output_masks[tid] = mask;
    }
}

template <typename Hash, typename KeyEq>
__global__ void InsertKernelPass2(CUDAHashmapImplContext<Hash, KeyEq> hash_ctx,
                                  const void* input_values,
                                  addr_t* output_addrs,
                                  bool* output_masks,
                                  int64_t count) {
    uint32_t tid = threadIdx.x + blockIdx.x * blockDim.x;

    if (tid < count) {
        addr_t iterator_addr = output_addrs[tid];

        if (output_masks[tid]) {
            iterator_t iterator =
                    hash_ctx.kv_mgr_ctx_.ExtractIterator(iterator_addr);

            // Success: copy remaining input_values
            if (input_values != nullptr) {
                MEMCPY_AS_INTS(iterator.second,
                               static_cast<const uint8_t*>(input_values) +
                                       tid * hash_ctx.dsize_value_,
                               hash_ctx.dsize_value_);
            }

        } else {
            hash_ctx.kv_mgr_ctx_.DeviceFree(iterator_addr);
        }
    }
}

template <typename Hash, typename KeyEq>
__global__ void FindKernel(CUDAHashmapImplContext<Hash, KeyEq> hash_ctx,
                           const void* input_keys,
                           addr_t* output_addrs,
                           bool* output_masks,
                           int64_t count) {
    uint32_t tid = threadIdx.x + blockIdx.x * blockDim.x;
    uint32_t lane_id = threadIdx.x & 0x1F;

    // This warp is idle.
    if ((tid - lane_id) >= count) {
        return;
    }

    // Initialize the memory allocator on each warp.
    hash_ctx.node_mgr_ctx_.Init(tid, lane_id);

    bool lane_active = false;
    uint32_t bucket_id = 0;

    // Dummy.
    uint8_t dummy_key[kMaxKeyByteSize];
    const void* key = reinterpret_cast<const void*>(dummy_key);
    Pair<addr_t, bool> result;

    if (tid < count) {
        lane_active = true;
        key = static_cast<const uint8_t*>(input_keys) +
              tid * hash_ctx.dsize_key_;
        bucket_id = hash_ctx.ComputeBucket(key);
    }

    result = hash_ctx.Find(lane_active, lane_id, bucket_id, key);

    if (tid < count) {
        output_addrs[tid] = result.first;
        output_masks[tid] = result.second;
    }
}

template <typename Hash, typename KeyEq>
__global__ void EraseKernelPass0(CUDAHashmapImplContext<Hash, KeyEq> hash_ctx,
                                 const void* input_keys,
                                 addr_t* output_addrs,
                                 bool* output_masks,
                                 int64_t count) {
    uint32_t tid = threadIdx.x + blockIdx.x * blockDim.x;
    uint32_t lane_id = threadIdx.x & 0x1F;

    if (tid - lane_id >= count) {
        return;
    }

    hash_ctx.node_mgr_ctx_.Init(tid, lane_id);

    bool lane_active = false;
    uint32_t bucket_id = 0;

    uint8_t dummy_key[kMaxKeyByteSize];
    const void* key = reinterpret_cast<const void*>(dummy_key);

    if (tid < count) {
        lane_active = true;
        key = static_cast<const uint8_t*>(input_keys) +
              tid * hash_ctx.dsize_key_;
        bucket_id = hash_ctx.ComputeBucket(key);
    }

    auto result = hash_ctx.Erase(lane_active, lane_id, bucket_id, key);

    if (tid < count) {
        output_addrs[tid] = result.first;
        output_masks[tid] = result.second;
    }
}

template <typename Hash, typename KeyEq>
__global__ void EraseKernelPass1(CUDAHashmapImplContext<Hash, KeyEq> hash_ctx,
                                 addr_t* output_addrs,
                                 bool* output_masks,
                                 int64_t count) {
    uint32_t tid = threadIdx.x + blockIdx.x * blockDim.x;
    if (tid < count && output_masks[tid]) {
        hash_ctx.kv_mgr_ctx_.DeviceFree(output_addrs[tid]);
    }
}

template <typename Hash, typename KeyEq>
__global__ void GetActiveIndicesKernel(
        CUDAHashmapImplContext<Hash, KeyEq> hash_ctx,
        addr_t* output_addrs,
        uint32_t* output_iterator_count) {
    uint32_t tid = threadIdx.x + blockIdx.x * blockDim.x;
    uint32_t lane_id = threadIdx.x & 0x1F;

    // Assigning a warp per bucket.
    uint32_t bucket_id = tid >> 5;
    if (bucket_id >= hash_ctx.bucket_count_) {
        return;
    }

    hash_ctx.node_mgr_ctx_.Init(tid, lane_id);

    uint32_t src_unit_data =
            *hash_ctx.get_unit_ptr_from_list_head(bucket_id, lane_id);
    bool is_active = src_unit_data != kEmptyNodeAddr;

    if (is_active && ((1 << lane_id) & kNodePtrLanesMask)) {
        uint32_t index = atomicAdd(output_iterator_count, 1);
        output_addrs[index] = src_unit_data;
    }

    addr_t next = __shfl_sync(kSyncLanesMask, src_unit_data, kNextSlabPtrLaneId,
                              kWarpSize);

    // Count following nodes,
    while (next != kEmptySlabAddr) {
        src_unit_data = *hash_ctx.get_unit_ptr_from_list_nodes(next, lane_id);
        is_active = (src_unit_data != kEmptyNodeAddr);

        if (is_active && ((1 << lane_id) & kNodePtrLanesMask)) {
            uint32_t index = atomicAdd(output_iterator_count, 1);
            output_addrs[index] = src_unit_data;
        }
        next = __shfl_sync(kSyncLanesMask, src_unit_data, kNextSlabPtrLaneId,
                           kWarpSize);
    }
}

template <typename Hash, typename KeyEq>
__global__ void CountElemsPerBucketKernel(
        CUDAHashmapImplContext<Hash, KeyEq> hash_ctx,
        int64_t* bucket_elem_counts) {
    uint32_t tid = threadIdx.x + blockIdx.x * blockDim.x;
    uint32_t lane_id = threadIdx.x & 0x1F;

    // Assigning a warp per bucket.
    uint32_t bucket_id = tid >> 5;
    if (bucket_id >= hash_ctx.bucket_count_) {
        return;
    }

    hash_ctx.node_mgr_ctx_.Init(tid, lane_id);

    uint32_t count = 0;

    // Count head node.
    uint32_t src_unit_data =
            *hash_ctx.get_unit_ptr_from_list_head(bucket_id, lane_id);
    count += __popc(
            __ballot_sync(kNodePtrLanesMask, src_unit_data != kEmptyNodeAddr));
    addr_t next = __shfl_sync(kSyncLanesMask, src_unit_data, kNextSlabPtrLaneId,
                              kWarpSize);

    // Count following nodes.
    while (next != kEmptySlabAddr) {
        src_unit_data = *hash_ctx.get_unit_ptr_from_list_nodes(next, lane_id);
        count += __popc(__ballot_sync(kNodePtrLanesMask,
                                      src_unit_data != kEmptyNodeAddr));
        next = __shfl_sync(kSyncLanesMask, src_unit_data, kNextSlabPtrLaneId,
                           kWarpSize);
    }

    // Write back the results.
    if (lane_id == 0) {
        bucket_elem_counts[bucket_id] = count;
    }
}

}  // namespace core
}  // namespace open3d
