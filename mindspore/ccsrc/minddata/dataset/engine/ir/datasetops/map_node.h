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

#ifndef MINDSPORE_CCSRC_MINDDATA_DATASET_ENGINE_IR_DATASETOPS_MAP_NODE_H_
#define MINDSPORE_CCSRC_MINDDATA_DATASET_ENGINE_IR_DATASETOPS_MAP_NODE_H_

#include <memory>
#include <string>
#include <vector>

#include "minddata/dataset/engine/ir/datasetops/dataset_node.h"

namespace mindspore {
namespace dataset {

class MapNode : public DatasetNode {
 public:
  /// \brief Constructor
  MapNode(std::shared_ptr<DatasetNode> child, std::vector<std::shared_ptr<TensorOperation>> operations,
          std::vector<std::string> input_columns = {}, std::vector<std::string> output_columns = {},
          const std::vector<std::string> &columns = {}, std::shared_ptr<DatasetCache> cache = nullptr,
          std::vector<std::shared_ptr<DSCallback>> callbacks = {});

  /// \brief Destructor
  ~MapNode() = default;

  /// \brief Node name getter
  /// \return Name of the current node
  std::string Name() const override { return kMapNode; }

  /// \brief Print the description
  /// \param out - The output stream to write output to
  void Print(std::ostream &out) const override;

  /// \brief Copy the node to a new object
  /// \return A shared pointer to the new copy
  std::shared_ptr<DatasetNode> Copy() override;

  /// \brief a base class override function to create the required runtime dataset op objects for this class
  /// \return The list of shared pointers to the newly created DatasetOps
  std::vector<std::shared_ptr<DatasetOp>> Build() override;

  /// \brief Parameters validation
  /// \return Status Status::OK() if all the parameters are valid
  Status ValidateParams() override;

  /// \brief Getter of tensor operations
  /// \return Vector of operations the Map node will process
  const auto &TensorOperations() const { return operations_; }
  auto &TensorOperations() { return operations_; }

  /// \brief Base-class override for accepting NodePass visitor
  /// \param[in] p The node to visit
  /// \param[out] modified Indicator if the node was modified
  /// \return Status of the node visit
  Status Accept(NodePass *p, bool *modified) override;

  /// \brief Base-class override for accepting NodePass visitor
  /// \param[in] p The node to visit
  /// \param[out] modified Indicator if the node was modified
  /// \return Status of the node visit
  Status AcceptAfter(NodePass *p, bool *modified) override;

 private:
  std::vector<std::shared_ptr<TensorOperation>> operations_;
  std::vector<std::string> input_columns_;
  std::vector<std::string> output_columns_;
  std::vector<std::string> project_columns_;
  std::vector<std::shared_ptr<DSCallback>> callbacks_;
};

}  // namespace dataset
}  // namespace mindspore
#endif  // MINDSPORE_CCSRC_MINDDATA_DATASET_ENGINE_IR_DATASETOPS_MAP_NODE_H_
