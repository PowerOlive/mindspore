/**
 * Copyright 2019-2020 Huawei Technologies Co., Ltd
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

#ifndef LITE_MINDSPORE_LITE_C_OPS_SUB_H_
#define LITE_MINDSPORE_LITE_C_OPS_SUB_H_

#include <vector>
#include <set>
#include <cmath>
#include "ir/dtype/type_id.h"
#include "src/ops/arithmetic.h"

namespace mindspore {
namespace lite {
class Sub : public Arithmetic {
 public:
#ifdef PRIMITIVE_WRITEABLE
  MS_DECLARE_PARENT(Sub, Arithmetic);
  Sub() = default;
  explicit Sub(schema::PrimitiveT *primitive) : Arithmetic(primitive) {}
  void SetActivationType(int activation_type);

#else
  explicit Sub(schema::Primitive *primitive) : Arithmetic(primitive) {}
#endif
  int GetActivationType() const;
};
}  // namespace lite
}  // namespace mindspore

#endif  // LITE_MINDSPORE_LITE_C_OPS_SUB_H_
