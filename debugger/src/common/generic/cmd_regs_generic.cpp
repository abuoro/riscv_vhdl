/*
 *  Copyright 2023 Sergey Khabarov, sergeykhbr@gmail.com
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

#include "cmd_regs_generic.h"

namespace debugger {

CmdRegsGeneric::CmdRegsGeneric(IJtag *ijtag)
    : ICommand ("regs", ijtag) {

    briefDescr_.make_string("List of Core's registers values");
    detailedDescr_.make_string(
        "Description:\n"
        "    Print values of CPU's registers.\n"
        "Return:\n"
        "    Dictionary if no names specified, list of int64_t otherwise.\n"
        "Usage:\n"
        "    regs\n"
        "    regs name1 name2 ..\n"
        "Example:\n"
        "    regs\n"
        "    regs a0 s0 sp\n");
}

int CmdRegsGeneric::isValid(AttributeType *args) {
    if (!cmdName_.is_equal((*args)[0u].to_string())) {
        return CMD_INVALID;
    } 
    if (args->size() >= 1) {
        return CMD_VALID;
    }
    return CMD_WRONG_ARGS;
}

void CmdRegsGeneric::exec(AttributeType *args, AttributeType *res) {
    IJtag::dmi_abstractcs_type abstractcs;
    IJtag::dmi_command_type command;

    Reg64Type u;
    if (args->size() != 1) {
        res->make_list(args->size() - 1);
        for (unsigned i = 1; i < args->size(); i++) {
            const char *name = (*args)[i].to_string();
            tap_->read(reg2addr(name), 8, u.buf);
            (*res)[i - 1].make_uint64(u.val);
        }
        return;
    }

    command.u32 = 0;
    command.regaccess.cmdtype = 0;
    command.regaccess.aarsize = IJtag::CMD_AAxSIZE_64BITS;
    command.regaccess.transfer = 1;

    const ECpuRegMapping *preg = getpMappedReg();
    res->make_dict();
    while (preg->name[0]) {
        command.regaccess.regno = preg->offset;
        ijtag_->scanDmi(IJtag::DMI_COMMAND, command.u32, IJtag::DmiOp_Write);
        do {
            abstractcs.u32 = ijtag_->scanDmi(IJtag::DMI_ABSTRACTCS, 0, IJtag::DmiOp_Read);
        } while (abstractcs.bits.busy == 1);

        u.buf32[0] = ijtag_->scanDmi(IJtag::DMI_ABSTRACT_DATA0, 0, IJtag::DmiOp_Read);
        u.buf32[1] = ijtag_->scanDmi(IJtag::DMI_ABSTRACT_DATA1, 0, IJtag::DmiOp_Read);

        (*res)[preg->name].make_uint64(u.val);
        preg++;
    }
}

uint64_t CmdRegsGeneric::reg2addr(const char *name) {
    const ECpuRegMapping  *preg = getpMappedReg();
    while (preg->name[0]) {
        if (strcmp(name, preg->name) == 0) {
            return preg->offset;
        }
        preg++;
    }
    return REG_ADDR_ERROR;
}

}  // namespace debugger
