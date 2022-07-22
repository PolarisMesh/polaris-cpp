//  Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//  https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed
//  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//  CONDITIONS OF ANY KIND, either express or implied. See the License for the specific
//  language governing permissions and limitations under the License.
//

#ifndef POLARIS_CPP_POLARIS_PLUGIN_WEIGHT_ADJUSTER_WEIGHT_ADJUSTER_H_
#define POLARIS_CPP_POLARIS_PLUGIN_WEIGHT_ADJUSTER_WEIGHT_ADJUSTER_H_

#include "polaris/defs.h"
#include "polaris/plugin.h"

namespace polaris {

class Service;

/// @brief 动态权重调整接口
class WeightAdjuster : public Plugin {
 public:
  /// @brief 析构函数
  virtual ~WeightAdjuster() {}

  /// @brief 通过配置进行初始化
  virtual ReturnCode Init(Config* config, Context* context) = 0;

  virtual ReturnCode ServiceInstanceUpdate(ServiceData* new_service_data, ServiceData* old_service_data) = 0;

  virtual bool DoAdjust(ServiceData* service_data) = 0;
};

class DefaultWeightAdjuster : public WeightAdjuster {
 public:
  DefaultWeightAdjuster() {}

  virtual ~DefaultWeightAdjuster() {}

  virtual ReturnCode Init(Config*, Context*) { return kReturnOk; }

  virtual ReturnCode ServiceInstanceUpdate(ServiceData*, ServiceData*) { return kReturnOk; }

  virtual bool DoAdjust(ServiceData*) { return false; }
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_WEIGHT_ADJUSTER_WEIGHT_ADJUSTER_H_
