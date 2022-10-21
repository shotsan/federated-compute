/*
 * Copyright 2022 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "fcp/aggregation/tensorflow/checkpoint_reader.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "fcp/aggregation/core/datatype.h"
#include "fcp/aggregation/core/tensor.h"
#include "fcp/aggregation/tensorflow/converters.h"
#include "fcp/base/monitoring.h"
#include "tensorflow/c/checkpoint_reader.h"
#include "tensorflow/c/tf_status.h"
#include "tensorflow/c/tf_status_helper.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/framework/tensor_shape.h"
#include "tensorflow/core/framework/types.pb.h"

namespace fcp::aggregation::tensorflow {

namespace tf = ::tensorflow;

absl::StatusOr<std::unique_ptr<CheckpointReader>> CheckpointReader::Create(
    const std::string& filename) {
  tf::TF_StatusPtr tf_status(TF_NewStatus());
  auto tf_checkpoint_reader =
      std::make_unique<tf::checkpoint::CheckpointReader>(filename,
                                                         tf_status.get());
  if (TF_GetCode(tf_status.get()) != TF_OK) {
    return absl::InternalError(
        absl::StrFormat("Couldn't read checkpoint: %s : %s", filename,
                        TF_Message(tf_status.get())));
  }

  // Populate the DataType map.
  DataTypeMap data_type_map;
  for (const auto& [name, tf_dtype] :
       tf_checkpoint_reader->GetVariableToDataTypeMap()) {
    FCP_ASSIGN_OR_RETURN(DataType dtype, ConvertDataType(tf_dtype));
    data_type_map.emplace(name, dtype);
  }

  // Populate the TensorShape map.
  TensorShapeMap shape_map;
  for (const auto& [name, tf_shape] :
       tf_checkpoint_reader->GetVariableToShapeMap()) {
    shape_map.emplace(name, ConvertShape(tf_shape));
  }

  return std::unique_ptr<CheckpointReader>(
      new CheckpointReader(std::move(tf_checkpoint_reader),
                           std::move(data_type_map), std::move(shape_map)));
}

CheckpointReader::CheckpointReader(
    std::unique_ptr<tf::checkpoint::CheckpointReader>
        tensorflow_checkpoint_reader,
    DataTypeMap data_type_map, TensorShapeMap shape_map)
    : tf_checkpoint_reader_(std::move(tensorflow_checkpoint_reader)),
      data_type_map_(std::move(data_type_map)),
      shape_map_(std::move(shape_map)) {}

// A primitive TensorData implementation that wraps the original
// tf::Tensor data.
// TensorDataAdapter gets the ownership of the wrapped tensor, which keeps
// the underlying data alive.
class TensorDataAdapter : public TensorData {
 public:
  explicit TensorDataAdapter(std::unique_ptr<tf::Tensor> tensor)
      : tensor_(std::move(tensor)) {}

  // The source tf::Tensor has the data as one continues blob.
  int num_slices() const override { return 1; }
  size_t byte_size() const override { return tensor_->tensor_data().size(); }

  Slice get_slice(int n) const override {
    FCP_CHECK(n == 0);
    return {0, tensor_->tensor_data().size(), tensor_->tensor_data().data()};
  }

 private:
  std::unique_ptr<tf::Tensor> tensor_;
};

StatusOr<Tensor> CheckpointReader::GetTensor(const std::string& name) const {
  std::unique_ptr<tf::Tensor> tensor;
  const tf::TF_StatusPtr read_status(TF_NewStatus());
  tf_checkpoint_reader_->GetTensor(name, &tensor, read_status.get());
  if (TF_GetCode(read_status.get()) != TF_OK) {
    return absl::NotFoundError(
        absl::StrFormat("Checkpoint doesn't have tensor %s", name));
  }
  FCP_ASSIGN_OR_RETURN(DataType dtype, ConvertDataType(tensor->dtype()));
  TensorShape shape = ConvertShape(tensor->shape());
  return Tensor::Create(dtype, std::move(shape),
                        std::make_unique<TensorDataAdapter>(std::move(tensor)));
}

}  // namespace fcp::aggregation::tensorflow