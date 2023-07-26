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

#pragma once

#include "api_core.h"
#include "iservice.h"
#include "coreservices/icommand.h"
#include "coreservices/isrccode.h"

namespace debugger {

class CmdDisas : public ICommandRiscv {
 public:
    explicit CmdDisas(IService *parent, IJtag *ijtag);

    /** ICommand */
    virtual int isValid(AttributeType *args);
    virtual void exec(AttributeType *args, AttributeType *res);

 private:
    int getMode() { return 0; }
    void format(AttributeType *asmbuf, AttributeType *fmtstr);

 private:
    ISourceCode *isrc_;
};

}  // namespace debugger