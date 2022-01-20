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
#include "fcp/client/federated_protocol_util.h"

#include <algorithm>
#include <string>

#include "google/protobuf/duration.pb.h"
#include "absl/random/random.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "fcp/client/diag_codes.pb.h"
#include "fcp/client/log_manager.h"
#include "fcp/protos/federated_api.pb.h"

namespace fcp {
namespace client {

namespace {

// Takes the given minimum and maximum delays, and uniformly randomly
// chooses a delay in that range.
absl::Duration PickRetryDelayFromRange(absl::Duration min_delay,
                                       absl::Duration max_delay,
                                       absl::BitGen& bit_gen) {
  // Sanitize inputs (ensure min_delay is >= 0, and max_delay is >= min_delay).
  min_delay = std::max(absl::ZeroDuration(), min_delay);
  max_delay = std::max(max_delay, min_delay);

  // Pick a value.
  absl::Duration window_width = max_delay - min_delay;
  double random = absl::Uniform(bit_gen, 0, 1.0);
  return min_delay + (window_width * random);
}

// Converts an absl::Duration to a google::protobuf::Duration.
// Note that we assume the durations we deal with here are representable by
// both formats.
::google::protobuf::Duration ConvertAbslToProtoDuration(
    absl::Duration absl_duration) {
  google::protobuf::Duration proto_duration;
  proto_duration.set_seconds(int32_t(
      absl::IDivDuration(absl_duration, absl::Seconds(1), &absl_duration)));
  proto_duration.set_nanos(int32_t(
      absl::IDivDuration(absl_duration, absl::Nanoseconds(1), &absl_duration)));
  return proto_duration;
}

}  // namespace

absl::Time PickRetryTimeFromRange(const ::google::protobuf::Duration& min_delay,
                                  const ::google::protobuf::Duration& max_delay,
                                  absl::BitGen& bit_gen) {
  return absl::Now() +
         PickRetryDelayFromRange(absl::Seconds(min_delay.seconds()) +
                                     absl::Nanoseconds(min_delay.nanos()),
                                 absl::Seconds(max_delay.seconds()) +
                                     absl::Nanoseconds(max_delay.nanos()),
                                 bit_gen);
}

::google::internal::federatedml::v2::RetryWindow
GenerateRetryWindowFromTargetDelay(absl::Duration target_delay,
                                   double jitter_percent,
                                   absl::BitGen& bit_gen) {
  // Sanitize the jitter_percent input, ensuring it's within [0.0 and 1.0]
  jitter_percent = std::min(1.0, std::max(0.0, jitter_percent));
  // Pick a retry delay from the target range.
  absl::Duration retry_delay =
      PickRetryDelayFromRange(target_delay * (1.0 - jitter_percent),
                              target_delay * (1.0 + jitter_percent), bit_gen);
  ::google::internal::federatedml::v2::RetryWindow result;
  *result.mutable_delay_min() = *result.mutable_delay_max() =
      ConvertAbslToProtoDuration(retry_delay);
  return result;
}

::google::internal::federatedml::v2::RetryWindow
GenerateRetryWindowFromRetryTime(absl::Time retry_time) {
  // Convert the target retry time back to a duration, based on the current
  // time. I.e. if at 09:50AM the retry window was received and the chosen
  // target retry time was 11:00AM, and if it is now 09:55AM, then the
  // calculated duration will be 1 hour and 5 minutes.
  absl::Duration retry_delay = retry_time - absl::Now();
  // If the target retry time has already passed, then use a zero-length
  // duration.
  retry_delay = std::max(absl::ZeroDuration(), retry_delay);

  // Generate a RetryWindow with delay_min and delay_max both set to the same
  // value.
  ::google::internal::federatedml::v2::RetryWindow retry_window;
  *retry_window.mutable_delay_min() = *retry_window.mutable_delay_max() =
      ConvertAbslToProtoDuration(retry_delay);
  return retry_window;
}

std::string ExtractTaskNameFromAggregationSessionId(
    const std::string& session_id, const std::string& population_name,
    LogManager& log_manager) {
  auto population_start = session_id.find(population_name + "/");
  auto task_end = session_id.find('#');
  if (population_start != 0 || task_end == std::string::npos ||
      task_end <= population_name.length() + 1) {
    log_manager.LogDiag(ProdDiagCode::OPSTATS_TASK_NAME_EXTRACTION_FAILED);
    return session_id;
  } else {
    return session_id.substr(population_name.length() + 1,
                             task_end - population_name.length() - 1);
  }
}

}  // namespace client
}  // namespace fcp