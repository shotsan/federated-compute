/*
 * Copyright 2020 Google LLC
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
#ifndef FCP_CLIENT_ENGINE_SIMPLE_PLAN_ENGINE_H_
#define FCP_CLIENT_ENGINE_SIMPLE_PLAN_ENGINE_H_

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/any.pb.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "fcp/base/monitoring.h"
#include "fcp/client/engine/common.h"
#include "fcp/client/engine/plan_engine_helpers.h"
#include "fcp/client/engine/tf_wrapper.h"
#include "fcp/client/event_publisher.h"
#include "fcp/client/flags.h"
#include "fcp/client/histogram_counters.pb.h"
#include "fcp/client/interruptible_runner.h"
#include "fcp/client/log_manager.h"
#include "fcp/client/opstats/opstats_logger.h"
#include "fcp/client/simple_task_environment.h"
#include "fcp/protos/plan.pb.h"
#include "tensorflow/core/framework/tensor.h"

namespace fcp {
namespace client {
namespace engine {

// A class used to "run" (interpret) a TensorflowSpec-based plan. Each instance
// should generally only be used once to run a plan.
class SimplePlanEngine {
 public:
  SimplePlanEngine(SimpleTaskEnvironment* task_env, LogManager* log_manager,
                   EventPublisher* event_publisher,
                   ::fcp::client::opstats::OpStatsLogger* opstats_logger,
                   const InterruptibleRunner::TimingConfig* timing_config,
                   const Flags* flags);

  PlanResult RunPlan(
      const google::internal::federated::plan::TensorflowSpec& tensorflow_spec,
      const std::string& graph, const ::google::protobuf::Any& config_proto,
      std::unique_ptr<std::vector<std::pair<std::string, tensorflow::Tensor>>>
          inputs,
      const std::vector<std::string>& output_names,
      absl::Time run_plan_start_time, absl::Time reference_time,
      std::function<void()> log_computation_started,
      std::function<void()> log_computation_finished,
      const SelectorContext& selector_context);

 private:
  // Runs the plan. Returns one of three error codes:
  // OK, INVALID_ARGUMENT, CANCELLED.
  absl::StatusOr<std::vector<tensorflow::Tensor>> RunPlanInternal(
      TensorFlowWrapper* tf_wrapper,
      const google::internal::federated::plan::TensorflowSpec& tensorflow_spec,
      std::unique_ptr<std::vector<std::pair<std::string, tensorflow::Tensor>>>
          inputs,
      const std::vector<std::string>& output_names,
      absl::Time run_plan_start_time,
      std::function<void()> log_computation_started,
      std::function<void()> log_computation_finished,
      const SelectorContext& selector_context,
      std::atomic<int>* total_example_count,
      std::atomic<int64_t>* total_example_size_bytes,
      ExampleIteratorStatus* example_iterator_status);

  // Invokes TensorFlowWrapper, and takes care of logging TensorFlow errors and
  // external interruptions via event_publisher.
  // If the TF call fails because it got aborted externally, returns CANCELLED.
  // If the TF call fails with an INVALID argument, indicating a TF error,
  //   publishes an event, then returns INVALID_ARGUMENT
  // If the TF call reports an OUT_OF_RANGE error ("internal" abortion) or the
  // TF call is successful, returns OK.
  absl::StatusOr<std::vector<tensorflow::Tensor>> RunTensorFlowInternal(
      TensorFlowWrapper* tf_wrapper,
      const std::vector<std::pair<std::string, tensorflow::Tensor>>& inputs,
      const std::vector<std::string>& output_tensor_names,
      const std::vector<std::string>& target_node_names,
      std::atomic<int>* total_example_count,
      std::atomic<int64_t>* total_example_size_bytes, absl::Time start);

  // Logs duration between reference_time and call to this function to the
  // specified HistogramCounter.
  void LogTimeSince(HistogramCounters histogram_counter,
                    absl::Time reference_time);

  SimpleTaskEnvironment* task_env_;
  LogManager* log_manager_;
  EventPublisher* event_publisher_;
  ::fcp::client::opstats::OpStatsLogger* opstats_logger_;
  const InterruptibleRunner::TimingConfig* timing_config_;
  const Flags* flags_;
};

}  // namespace engine
}  // namespace client
}  // namespace fcp

#endif  // FCP_CLIENT_ENGINE_SIMPLE_PLAN_ENGINE_H_
