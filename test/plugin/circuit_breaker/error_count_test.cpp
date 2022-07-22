//  Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use
//  this file
//  except in compliance with the License. You may obtain a copy of the License
//  at
//
//  https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed
//  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//  CONDITIONS OF ANY KIND, either express or implied. See the License for the
//  specific
//  language governing permissions and limitations under the License.
//

#include "plugin/circuit_breaker/error_count.h"

#include <gtest/gtest.h>

#include <string>

#include "mock/mock_local_registry.h"
#include "model/constants.h"
#include "plugin/circuit_breaker/chain.h"
#include "test_context.h"
#include "test_utils.h"
#include "utils/utils.h"

namespace polaris {

const int kTestMultiThreadGetOrCreateTime = 100;

class ErrorCountCircuitBreakerTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    context_ = TestContext::CreateContext();
    service_key_ = context_->GetContextImpl()->GetDiscoverService().service_;
    default_config_ = Config::CreateEmptyConfig();
    ASSERT_TRUE(default_config_ != nullptr);
    TestUtils::SetUpFakeTime();
    error_count_circuit_breaker_ = new ErrorCountCircuitBreaker();
    instance_gauge_.instance_id = "uuid-1";
    instance_gauge_.call_daley = 10;
    instance_gauge_.call_ret_code = 0;
    instance_gauge_.call_ret_status = kCallRetOk;
    chain_data_ = new CircuitBreakerChainData();
    CircuitBreakerPluginData plugin_data;
    plugin_data.plugin_name = "errorCount";
    plugin_data.request_after_half_open = constants::kRequestCountAfterHalfOpenDefault;
    chain_data_->AppendPluginData(plugin_data);
    circuit_breaker_status_ = new InstancesCircuitBreakerStatus(chain_data_, 1, service_key_,
                                                                context_->GetContextImpl()->GetServiceRecord(), true);
  }

  virtual void TearDown() {
    if (error_count_circuit_breaker_ != nullptr) delete error_count_circuit_breaker_;
    if (circuit_breaker_status_ != nullptr) delete circuit_breaker_status_;
    if (chain_data_ != nullptr) delete chain_data_;
    if (default_config_ != nullptr) delete default_config_;
    TestUtils::TearDownFakeTime();
    if (context_ != nullptr) delete context_;
  }

  static void *MultiThreadGetOrCreateErrorCountMap(void *arg);

  static void *MultiThreadGetOrCreateErrorCountInstance(void *arg);

  static void *MultiThreadRealTimeErrorCount(void *arg);

 protected:
  ErrorCountCircuitBreaker *error_count_circuit_breaker_;
  InstanceGauge instance_gauge_;
  Config *default_config_;
  ServiceKey service_key_;
  Context *context_;
  CircuitBreakerChainData *chain_data_;
  InstancesCircuitBreakerStatus *circuit_breaker_status_;
};

// 单线程创建或获取统计状态
TEST_F(ErrorCountCircuitBreakerTest, OneThreadGetOrCreateStatus) {
  // 首次获取
  std::string instance_id = "instance";
  ErrorCountStatus *status = error_count_circuit_breaker_->GetOrCreateErrorCountStatus(instance_id);
  ASSERT_EQ(status->status, kCircuitBreakerClose);
  ASSERT_EQ(status->error_count, 0);
  ASSERT_EQ(status->last_update_time, 0);
  ASSERT_EQ(status->success_count, 0);

  // 修改数据
  status->error_count = 1;
  status->last_update_time = 2;

  // 再次获取，是同一个状态对象
  ErrorCountStatus *status2 = error_count_circuit_breaker_->GetOrCreateErrorCountStatus(instance_id);
  ASSERT_EQ(status, status2);
  ASSERT_EQ(status2->error_count, 1);
  ASSERT_EQ(status2->last_update_time, 2);
}

// 每个线程创建不同的实例
void *ErrorCountCircuitBreakerTest::MultiThreadGetOrCreateErrorCountMap(void *arg) {
  ErrorCountCircuitBreakerTest *test = static_cast<ErrorCountCircuitBreakerTest *>(arg);
  pthread_t tid = pthread_self();
  std::stringstream instance_id;
  instance_id << "instance_" << tid;
  for (int i = 0; i < kTestMultiThreadGetOrCreateTime; ++i) {
    ErrorCountStatus *status = test->error_count_circuit_breaker_->GetOrCreateErrorCountStatus(instance_id.str());
    EXPECT_EQ(status->error_count, i);
    EXPECT_EQ(status->last_update_time, i);

    // 单线程修改数据
    status->error_count++;
    status->last_update_time++;
  }
  return nullptr;
}

// 多线程创建或获取统计状态
TEST_F(ErrorCountCircuitBreakerTest, MultiThreadGetOrCreateStatusMap) {
  int thread_num = 5;
  std::vector<pthread_t> thread_id_list;
  pthread_t tid;
  // 多线程并发查询和修改Map中的数据，每给线程只修改自己对应的实例
  for (int i = 0; i < thread_num; i++) {
    pthread_create(&tid, nullptr, MultiThreadGetOrCreateErrorCountMap, this);
    thread_id_list.push_back(tid);
  }
  EXPECT_EQ(thread_id_list.size(), thread_num);
  for (size_t i = 0; i < thread_id_list.size(); i++) {
    pthread_join(thread_id_list[i], nullptr);
  }
  for (int i = 0; i < thread_num; ++i) {
    std::stringstream instance_id;
    instance_id << "instance_" << thread_id_list[i];
    ErrorCountStatus *status = error_count_circuit_breaker_->GetOrCreateErrorCountStatus(instance_id.str());
    ASSERT_EQ(status->error_count, kTestMultiThreadGetOrCreateTime);
    ASSERT_EQ(status->last_update_time, kTestMultiThreadGetOrCreateTime);
  }
}

// 每个线程创建相同的实例
void *ErrorCountCircuitBreakerTest::MultiThreadGetOrCreateErrorCountInstance(void *arg) {
  ErrorCountCircuitBreakerTest *test = static_cast<ErrorCountCircuitBreakerTest *>(arg);
  for (int i = 0; i < kTestMultiThreadGetOrCreateTime; ++i) {
    std::stringstream instance_id;
    instance_id << "instance_" << i;
    ErrorCountStatus *status = test->error_count_circuit_breaker_->GetOrCreateErrorCountStatus(instance_id.str());
    // 并发修改数据
    status->error_count++;
    status->last_update_time++;
  }
  return nullptr;
}

// 多线程创建或获取统计状态
TEST_F(ErrorCountCircuitBreakerTest, MultiThreadGetOrCreateStatusInstance) {
  int thread_num = 5;
  std::vector<pthread_t> thread_id_list;
  pthread_t tid;
  // 多线程并发创建修改相同实例数据
  for (int i = 0; i < thread_num; i++) {
    pthread_create(&tid, nullptr, MultiThreadGetOrCreateErrorCountInstance, this);
    thread_id_list.push_back(tid);
  }
  EXPECT_EQ(thread_id_list.size(), thread_num);
  for (size_t i = 0; i < thread_id_list.size(); i++) {
    pthread_join(thread_id_list[i], nullptr);
  }
  for (int i = 0; i < kTestMultiThreadGetOrCreateTime; ++i) {
    std::stringstream instance_id;
    instance_id << "instance_" << i;
    ErrorCountStatus *status = error_count_circuit_breaker_->GetOrCreateErrorCountStatus(instance_id.str());
    ASSERT_EQ(status->error_count, thread_num);
    ASSERT_EQ(status->last_update_time, thread_num);
  }
}

// 单线程错误数计算
TEST_F(ErrorCountCircuitBreakerTest, SingleThreadErrorCount) {
  error_count_circuit_breaker_->Init(default_config_, context_);
  ErrorCountStatus *error_count_status =
      error_count_circuit_breaker_->GetOrCreateErrorCountStatus(instance_gauge_.instance_id);
  // 错误数很多，但一直不连续，每当差一次达到阈值又成功一次
  uint64_t request_count = constants::kContinuousErrorThresholdDefault * 10;
  for (uint64_t i = 0; i < request_count; i++) {
    if (i % constants::kContinuousErrorThresholdDefault == 0) {
      instance_gauge_.call_ret_status = kCallRetOk;
    } else {
      instance_gauge_.call_ret_status = kCallRetError;
    }
    TestUtils::FakeNowIncrement(i);
    error_count_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
    ASSERT_EQ(error_count_status->error_count, i % constants::kContinuousErrorThresholdDefault);
    ASSERT_EQ(error_count_status->success_count, 0);
    ASSERT_EQ(error_count_status->last_update_time, 0);
    ASSERT_EQ(error_count_status->status, kCircuitBreakerClose);
  }

  // 再错误一次就达到阈值，不管时间差多少，熔断器变为打开状态
  instance_gauge_.call_ret_status = kCallRetError;
  TestUtils::FakeNowIncrement(10 * 1000);
  error_count_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
  ASSERT_EQ(error_count_status->error_count, constants::kContinuousErrorThresholdDefault);
  ASSERT_EQ(error_count_status->success_count, 0);
  ASSERT_EQ(error_count_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_count_status->status, kCircuitBreakerOpen);

  // 熔断之后上报更多的错误，不会记录连续错误
  request_count = constants::kHalfOpenSleepWindowDefault / 2;
  for (uint64_t i = 0; i < request_count; i++) {
    TestUtils::FakeNowIncrement(1);
    error_count_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
    error_count_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
    ASSERT_EQ(error_count_status->error_count, constants::kContinuousErrorThresholdDefault);
    ASSERT_EQ(error_count_status->success_count, 0);
    ASSERT_LT(error_count_status->last_update_time, Time::GetCoarseSteadyTimeMs());
    ASSERT_EQ(error_count_status->status, kCircuitBreakerOpen);
  }

  // 变成打开状态后，过了配置的时间变成半开状态
  TestUtils::FakeNowIncrement(constants::kHalfOpenSleepWindowDefault - request_count);
  error_count_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_count_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_count_status->status, kCircuitBreakerHalfOpen);

  // 半开后错误一定次数立刻重新熔断
  request_count = constants::kRequestCountAfterHalfOpenDefault - constants::kSuccessCountAfterHalfOpenDefault;
  for (uint64_t i = 0; i < request_count; i++) {
    TestUtils::FakeNowIncrement(1);
    error_count_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
    ASSERT_EQ(error_count_status->error_count, i + 1);
    ASSERT_EQ(error_count_status->success_count, 0);
    ASSERT_LT(error_count_status->last_update_time, Time::GetCoarseSteadyTimeMs());
    ASSERT_EQ(error_count_status->status, kCircuitBreakerHalfOpen);
  }
  TestUtils::FakeNowIncrement(1);
  error_count_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
  ASSERT_EQ(error_count_status->error_count, request_count + 1);
  ASSERT_EQ(error_count_status->success_count, 0);
  ASSERT_EQ(error_count_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_count_status->status, kCircuitBreakerOpen);

  // 重新半开
  TestUtils::FakeNowIncrement(constants::kHalfOpenSleepWindowDefault - 1);
  error_count_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_count_status->status, kCircuitBreakerOpen);
  TestUtils::FakeNowIncrement(1);
  error_count_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_count_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_count_status->status, kCircuitBreakerHalfOpen);

  // 全部放量结束后，成功数不够重新熔断
  for (uint64_t i = 1; i < constants::kRequestCountAfterHalfOpenDefault; i++) {
    TestUtils::FakeNowIncrement(1);
    instance_gauge_.call_ret_status = i < constants::kSuccessCountAfterHalfOpenDefault ? kCallRetOk : kCallRetError;
    error_count_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
    if (i < constants::kSuccessCountAfterHalfOpenDefault) {
      ASSERT_EQ(error_count_status->success_count, i);
    } else {
      ASSERT_EQ(error_count_status->success_count, constants::kSuccessCountAfterHalfOpenDefault - 1);
    }
    ASSERT_LT(error_count_status->last_update_time, Time::GetCoarseSteadyTimeMs());
    ASSERT_EQ(error_count_status->status, kCircuitBreakerHalfOpen);
  }
  TestUtils::FakeNowIncrement(constants::kHalfOpenSleepWindowDefault);
  error_count_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_LE(error_count_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_count_status->status, kCircuitBreakerHalfOpen);
  instance_gauge_.call_ret_status = kCallRetError;
  error_count_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
  ASSERT_EQ(error_count_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_count_status->status, kCircuitBreakerOpen);

  // 重新半开
  TestUtils::FakeNowIncrement(constants::kHalfOpenSleepWindowDefault);
  error_count_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_count_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_count_status->status, kCircuitBreakerHalfOpen);

  // 成功请求数达到条件，恢复
  instance_gauge_.call_ret_status = kCallRetOk;
  request_count = constants::kSuccessCountAfterHalfOpenDefault;
  for (uint64_t i = 0; i < request_count; i++) {
    TestUtils::FakeNowIncrement(1);
    error_count_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
    ASSERT_EQ(error_count_status->success_count, i + 1);
  }
  ASSERT_EQ(error_count_status->success_count, constants::kSuccessCountAfterHalfOpenDefault);
  error_count_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_count_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_count_status->status, kCircuitBreakerClose);
}

struct ErrorCountRealTimeArg {
  ErrorCountRealTimeArg(ErrorCountCircuitBreakerTest *test, InstanceGauge gauge, int report_time)
      : test_(test), gauge_(gauge), report_time_(report_time) {}
  ErrorCountCircuitBreakerTest *test_;
  InstanceGauge gauge_;
  int report_time_;
};

void *ErrorCountCircuitBreakerTest::MultiThreadRealTimeErrorCount(void *arg) {
  ErrorCountRealTimeArg *test_arg = static_cast<ErrorCountRealTimeArg *>(arg);
  for (int i = 0; i < test_arg->report_time_; ++i) {
    test_arg->test_->error_count_circuit_breaker_->RealTimeCircuitBreak(test_arg->gauge_,
                                                                        test_arg->test_->circuit_breaker_status_);
  }
  delete test_arg;
  return nullptr;
}

// 多线程错误数计算
TEST_F(ErrorCountCircuitBreakerTest, MultiThreadErrorCount) {
  delete default_config_;
  std::string err_msg;
  int thread_num = 5;
  int report_time = 100;
  // 会自动修复配置
  std::string content = "continuousErrorThreshold:\n  " + std::to_string(thread_num * report_time) +
                        "\nrequestCountAfterHalfOpen:\n  " + std::to_string(thread_num * report_time) +
                        "\nsuccessCountAfterHalfOpen:\n  " + std::to_string(thread_num * report_time + 10);
  default_config_ = Config::CreateFromString(content, err_msg);
  POLARIS_ASSERT(default_config_ != nullptr && err_msg.empty());
  error_count_circuit_breaker_->Init(default_config_, context_);
  std::vector<pthread_t> thread_id_list;
  pthread_t tid;
  // 多线程并发上报失败
  instance_gauge_.call_ret_status = kCallRetTimeout;
  for (int i = 0; i < thread_num; i++) {
    ErrorCountRealTimeArg *arg = new ErrorCountRealTimeArg(this, instance_gauge_, report_time);
    pthread_create(&tid, nullptr, MultiThreadRealTimeErrorCount, arg);
    thread_id_list.push_back(tid);
  }
  for (size_t i = 0; i < thread_id_list.size(); i++) {
    pthread_join(thread_id_list[i], nullptr);
  }
  thread_id_list.clear();
  ErrorCountStatus *error_count_status =
      error_count_circuit_breaker_->GetOrCreateErrorCountStatus(instance_gauge_.instance_id);
  ASSERT_EQ(error_count_status->status, kCircuitBreakerOpen);
  ASSERT_EQ(error_count_status->error_count, thread_num * report_time);

  // 超时半开
  TestUtils::FakeNowIncrement(constants::kHalfOpenSleepWindowDefault);
  error_count_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_count_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_count_status->status, kCircuitBreakerHalfOpen);

  // 多线程并发上报成功
  instance_gauge_.call_ret_status = kCallRetOk;
  for (int i = 0; i < thread_num; i++) {
    ErrorCountRealTimeArg *arg = new ErrorCountRealTimeArg(this, instance_gauge_, report_time);
    pthread_create(&tid, nullptr, MultiThreadRealTimeErrorCount, arg);
    thread_id_list.push_back(tid);
  }
  for (size_t i = 0; i < thread_id_list.size(); i++) {
    pthread_join(thread_id_list[i], nullptr);
  }
  error_count_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_count_status->status, kCircuitBreakerClose);
  ASSERT_EQ(error_count_status->success_count, thread_num * report_time);
}

TEST_F(ErrorCountCircuitBreakerTest, TestMetricExpire) {
  error_count_circuit_breaker_->Init(default_config_, context_);
  error_count_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
  auto error_count_status = error_count_circuit_breaker_->GetOrCreateErrorCountStatus(instance_gauge_.instance_id);
  error_count_status->status = kCircuitBreakerOpen;
  TestUtils::FakeNowIncrement(constants::kMetricExpiredTimeDefault - 1);

  InstanceExistChecker not_exist = [](const std::string &) { return false; };
  InstanceExistChecker exist = [](const std::string &) { return true; };

  // 未过期
  error_count_circuit_breaker_->CleanStatus(circuit_breaker_status_, not_exist);
  ASSERT_EQ(error_count_status->status, kCircuitBreakerOpen);

  TestUtils::FakeNowIncrement(1);
  // 过期，但实例还存在，不清理
  error_count_circuit_breaker_->CleanStatus(circuit_breaker_status_, exist);
  auto old_error_count_status = error_count_circuit_breaker_->GetOrCreateErrorCountStatus(instance_gauge_.instance_id);
  ASSERT_EQ(old_error_count_status->status, kCircuitBreakerOpen);

  // 过期，实例不存在，清理
  TestUtils::FakeNowIncrement(constants::kMetricExpiredTimeDefault);
  error_count_circuit_breaker_->CleanStatus(circuit_breaker_status_, not_exist);
  auto new_error_count_status = error_count_circuit_breaker_->GetOrCreateErrorCountStatus(instance_gauge_.instance_id);
  ASSERT_EQ(new_error_count_status->status, kCircuitBreakerClose);
}

}  // namespace polaris
