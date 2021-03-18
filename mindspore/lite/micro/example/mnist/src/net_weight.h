
/**
 * Copyright 2021 Huawei Technologies Co., Ltd
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

#include "nnacl/common_func.h"
#include "nnacl/errorcode.h"
#include "nnacl/fp32/softmax_fp32.h"
#include "nnacl/int8/common_func_int8.h"
#include "nnacl/int8/conv3x3_int8.h"
#include "nnacl/int8/conv_int8.h"
#include "nnacl/int8/matmul_int8.h"
#include "nnacl/int8/pooling_int8.h"
#include "nnacl/int8/quant_dtype_cast_int8.h"
#include "nnacl/int8/reshape_int8.h"
#include "wrapper/int8/matmul_int8_wrapper.h"
#include <stdlib.h>
#include <string.h>
#include "microtensor.h"

extern unsigned char *net_B;
extern int16_t net_W10[];
extern int32_t net_W11[];
extern int16_t net_W12[];
extern int32_t net_W13[];
extern int32_t *net_W14;
extern int8_t *net_W15;
extern int32_t *net_W16;
extern int32_t *net_W17;
extern int8_t *net_W18;
extern int32_t *net_W19;
