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

#include <assert.h>
#include <stdio.h>

#include "polaris/limit.h"

int main(int argc, char** argv) {
  std::string address = argc >= 2 ? argv[3] : "127.0.0.1:8081";
  std::string config =
      "global:\n"
      "  serverConnector:\n"
      "    addresses:[" +
      address +
      "]\n"
      "    connectTimeout: 250ms\n";

  polaris::LimitApi* limit_api = polaris::LimitApi::CreateFromString(config);
  assert(limit_api != nullptr);

  delete limit_api;
  return 0;
}