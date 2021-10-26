/*
 *  Copyright 2018 Sergey Khabarov, sergeykhbr@gmail.com
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifndef __DEBUGGER_SRC_CPU_FNC_PLUGIN_CMDS_CMD_BR_RISCV_H__
#define __DEBUGGER_SRC_CPU_FNC_PLUGIN_CMDS_CMD_BR_RISCV_H__

#include "generic/cmd_br_generic.h"

namespace debugger {

class CmdBrRiscv : public CmdBrGeneric {
 public:
    explicit CmdBrRiscv(uint64_t dmibar, ITap *tap);

 protected:
    virtual void getSwBreakpointInstr(Reg64Type *instr, uint32_t *len);
};

}  // namespace debugger

#endif  // __DEBUGGER_SRC_CPU_FNC_PLUGIN_CMDS_CMD_BR_RISCV_H__
