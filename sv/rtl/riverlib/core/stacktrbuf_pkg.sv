// 
//  Copyright 2022 Sergey Khabarov, sergeykhbr@gmail.com
// 
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
// 
//      http://www.apache.org/licenses/LICENSE-2.0
// 
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
// 
package stacktrbuf_pkg;

import river_cfg_pkg::*;

typedef struct {
    logic [4:0] raddr;
    logic [(2 * RISCV_ARCH)-1:0] stackbuf[0: STACK_TRACE_BUF_SIZE - 1];
} StackTraceBuffer_registers;

endpackage: stacktrbuf_pkg
