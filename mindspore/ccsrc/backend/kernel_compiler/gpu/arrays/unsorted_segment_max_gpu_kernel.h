/**
 * Copyright 2020 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_GPU_UNSORTED_SEGMENT_MAX_H_
#define MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_GPU_UNSORTED_SEGMENT_MAX_H_

#include <vector>
#include <limits>
#include "backend/kernel_compiler/gpu/gpu_kernel.h"
#include "backend/kernel_compiler/gpu/gpu_kernel_factory.h"
#include "backend/kernel_compiler/gpu/cuda_impl/unsorted_segment_max.cuh"

namespace mindspore {
namespace kernel {
template <typename T, typename S>
class UnsortedSegmentMaxGpuKernel : public GpuKernel {
 public:
  UnsortedSegmentMaxGpuKernel() { ResetResource(); }
  ~UnsortedSegmentMaxGpuKernel() override = default;

  const std::vector<size_t> &GetInputSizeList() const override { return input_size_list_; }
  const std::vector<size_t> &GetOutputSizeList() const override { return output_size_list_; }
  const std::vector<size_t> &GetWorkspaceSizeList() const override { return workspace_size_list_; }

  bool Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &,
              const std::vector<AddressPtr> &outputs, void *stream_ptr) override {
    if (is_null_input_) {
      return true;
    }
    T *input_addr = GetDeviceAddress<T>(inputs, 0);
    S *indices_addr = GetDeviceAddress<S>(inputs, 1);
    T *output_addr = GetDeviceAddress<T>(outputs, 0);

    CHECK_CUDA_RET_WITH_EXCEPT(kernel_node_,
                               cudaMemsetAsync(output_addr, std::numeric_limits<T>::min(), outputs[0]->size,
                                               reinterpret_cast<cudaStream_t>(stream_ptr)),
                               "cudaMemSet Failed");
    CalUnsortedSegmentMax(input_addr, indices_addr, num_segments_, outer_size_, inner_size_, output_addr,
                          reinterpret_cast<cudaStream_t>(stream_ptr));
    return true;
  }

  bool Init(const CNodePtr &kernel_node) override {
    kernel_node_ = kernel_node;
    auto input_shapes = AnfAlgo::GetInputRealDeviceShapeIfExist(kernel_node, 0);
    is_null_input_ = CHECK_NULL_INPUT(input_shapes);
    if (is_null_input_) {
      MS_LOG(WARNING) << "UnsortedSegmentMax input is null";
      InitSizeLists();
      return true;
    }
    auto segment_ids_shapes = AnfAlgo::GetInputRealDeviceShapeIfExist(kernel_node, 1);
    auto output_shapes = AnfAlgo::GetOutputRealDeviceShapeIfExist(kernel_node, 0);

    size_t input_num = AnfAlgo::GetInputTensorNum(kernel_node);
    if (input_num == 3) {
      MS_LOG(INFO) << "UnsortedSegmentMax Kernel Input count is 3 - dynamic mode";
    } else {
      MS_LOG(INFO) << "UnsortedSegmentMax Kernel Input count is 2";
    }
    auto value_count = AnfAlgo::GetOutputRealDeviceShapeIfExist(kernel_node, 0);
    if (value_count.size() != 1) {
      MS_LOG(ERROR) << "For UnsortedSegmentMax, output shape incorrect rank. Expect Rank: 1, got Rank: "
                    << value_count.size() << ".";
    }
    num_segments_ = output_shapes[0];
    input_size_ = 1;
    for (size_t i = 0; i < input_shapes.size(); i++) {
      input_size_ *= input_shapes[i];
    }

    segment_ids_size_ = 1;
    for (size_t i = 0; i < segment_ids_shapes.size(); i++) {
      segment_ids_size_ *= segment_ids_shapes[i];
    }

    output_size_ = 1;
    for (size_t i = 0; i < output_shapes.size(); i++) {
      output_size_ *= output_shapes[i];
    }

    outer_size_ = input_shapes[0];
    inner_size_ = 1;
    for (size_t i = 1; i < input_shapes.size(); i++) {
      inner_size_ *= input_shapes[i];
    }

    InitSizeLists();
    return true;
  }

  void ResetResource() noexcept override {
    num_segments_ = 1;
    inner_size_ = 1;
    outer_size_ = 1;
    input_size_ = 1;
    segment_ids_size_ = 1;
    output_size_ = 1;
    is_null_input_ = false;
    input_size_list_.clear();
    output_size_list_.clear();
    workspace_size_list_.clear();
  }

 protected:
  void InitSizeLists() override {
    input_size_list_.push_back(input_size_ * sizeof(T));
    input_size_list_.push_back(segment_ids_size_ * sizeof(int));
    output_size_list_.push_back(output_size_ * sizeof(T));
  }

 private:
  int64_t num_segments_;
  size_t inner_size_;
  size_t outer_size_;
  size_t input_size_;
  size_t segment_ids_size_;
  size_t output_size_;
  bool is_null_input_;

  std::vector<size_t> input_size_list_;
  std::vector<size_t> output_size_list_;
  std::vector<size_t> workspace_size_list_;
};
}  // namespace kernel
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_GPU_UNSORTED_SEGMENT_MAX_H_
