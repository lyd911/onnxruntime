// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "tensor_allocator.h"
#include "mem_pattern.h"
#include "ort_value_pattern_planner.h"
#include "utils.h"
#include "tensorprotoutils.h"

namespace onnxruntime {

class TensorAllocatorWithMemPattern : public ITensorAllocator {
 private:
  OrtValuePatternPlanner planner_;
  MemoryPatternGroup mem_patterns_;
  std::vector<BufferUniquePtr>& weights_buffers_;
  std::map<OrtMemoryInfo, void*> buffers_;
  bool is_sealed_ = false;
  const ExecutionPlanBase& seq_plan_;

  common::Status AllocatePlannedBuffers() {
    const size_t location_len = mem_patterns_.locations.size();
    for (size_t i = 0; i < location_len; ++i) {
      auto& location = mem_patterns_.locations[i];
      auto alloc = GetAllocator(location);
      if (!alloc)
        return Status(common::ONNXRUNTIME, common::FAIL,
                      "Failed to get allocator for location: " + location.ToString());

      if (mem_patterns_.patterns[i].PeakSize() > 0) {
        void* buffer = utils::AllocateBlock(*alloc, mem_patterns_.patterns[i].PeakSize());

        weights_buffers_.push_back(BufferUniquePtr(buffer, alloc));
        auto kvp = buffers_.insert(std::make_pair(location, buffer));
        if (!kvp.second) {
          alloc->Free(buffer);
          return Status(common::ONNXRUNTIME, common::FAIL, "duplicated location");
        }
      }
    }
    return Status::OK();
  }

 public:
  TensorAllocatorWithMemPattern(const ExecutionPlanBase& execution_plan, const ExecutionProviders& exec_providers,
                                std::vector<BufferUniquePtr>& weights_buffers)
      : ITensorAllocator(exec_providers),
        planner_(execution_plan),
        weights_buffers_(weights_buffers),
        seq_plan_(execution_plan) {}

  common::Status FinalizePlan() override {
    ORT_RETURN_IF_ERROR(planner_.GeneratePatterns(&mem_patterns_));
    ORT_RETURN_IF_ERROR(AllocatePlannedBuffers());
    is_sealed_ = true;
    return Status::OK();
  }

  common::Status GetPreallocatedBuffer(int ort_value_index, const char* name,
                                       std::unique_ptr<MemBuffer>& out) override {
    if (!is_sealed_) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Internal error.");
    }
    const struct OrtMemoryInfo& location = seq_plan_.GetLocation(ort_value_index);
    auto pattern = mem_patterns_.GetPatterns(location);
    if (pattern == nullptr) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Mem pattern for initializer ", name, " is not found");
    }
    // if block is not found, means this ort_value is not traced
    // fall back to allocate separate buffer.
    // if it->second.get() is null, then fall back to the block not found case
    auto block = pattern->GetBlock(ort_value_index);
    auto it = buffers_.find(location);
    if (it == buffers_.end()) {
      if (block != nullptr && block->size_ == 0) {
        // Because the size is 0, this miss find is expected. we won't allocate a buffer with size of zero.
        out = onnxruntime::make_unique<MemBuffer>(nullptr, 0, location);
        return Status::OK();
      }
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Weight buffer for initializer '", name, "' is not found");
    }

    if (block == nullptr || it->second == nullptr) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Get preallocated buffer for initializer '", name, "' failed");
    }

    out = onnxruntime::make_unique<MemBuffer>(reinterpret_cast<char*>(it->second) + block->offset_, block->size_, location);
    return Status::OK();
  }
  common::Status Trace(int id, const ONNX_NAMESPACE::TensorProto* value) override {
    if (is_sealed_) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Internal error.");
    }
    size_t len = 0;
    static constexpr int alignment = 256;
    ORT_RETURN_IF_ERROR(utils::GetSizeInBytesFromTensorProto<alignment>(*value, &len));
    ORT_RETURN_IF_ERROR(planner_.TraceAllocation(id, len));
    return Status::OK();
  }
};
}  // namespace onnxruntime
