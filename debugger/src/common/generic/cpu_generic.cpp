/**
 * @file
 * @copyright  Copyright 2016 GNSS Sensor Ltd. All right reserved.
 * @author     Sergey Khabarov - sergeykhbr@gmail.com
 * @brief      CPU generic functional model common methods.
 */

#include <api_core.h>
#include "cpu_generic.h"
#include "coreservices/isocinfo.h"

namespace debugger {

CpuGeneric::CpuGeneric(const char *name)  
    : IService(name), IHap(HAP_ConfigDone),
    pc_(this, "pc", DSUREG(ureg.v.pc)),
    npc_(this, "npc", DSUREG(ureg.v.npc)),
    status_(this, "status", DSUREG(udbg.v.control)),
    stepping_cnt_(this, "stepping_cnt", DSUREG(udbg.v.stepping_mode_steps)),
    clock_cnt_(this, "clock_cnt", DSUREG(udbg.v.clock_cnt)),
    executed_cnt_(this, "executed_cnt", DSUREG(udbg.v.executed_cnt)),
    stackTraceCnt_(this, "stack_trace_cnt", DSUREG(ureg.v.stack_trace_cnt)),
    stackTraceBuf_(this, "stack_trace_buf", DSUREG(ureg.v.stack_trace_buf), 0),
    br_control_(this, "br_control", DSUREG(udbg.v.br_ctrl)),
    br_fetch_addr_(this, "br_fetch_addr", DSUREG(udbg.v.br_address_fetch)),
    br_fetch_instr_(this, "br_fetch_instr", DSUREG(udbg.v.br_instr_fetch)),
    br_hw_add_(this, "br_hw_add", DSUREG(udbg.v.add_breakpoint)),
    br_hw_remove_(this, "br_hw_remove", DSUREG(udbg.v.remove_breakpoint)) {
    registerInterface(static_cast<IThread *>(this));
    registerInterface(static_cast<IClock *>(this));
    registerInterface(static_cast<ICpuGeneric *>(this));
    registerInterface(static_cast<ICpuFunctional *>(this));
    registerInterface(static_cast<IResetListener *>(this));
    registerInterface(static_cast<IHap *>(this));
    registerAttribute("Enable", &isEnable_);
    registerAttribute("SysBus", &sysBus_);
    registerAttribute("DbgBus", &dbgBus_);
    registerAttribute("SysBusWidthBytes", &sysBusWidthBytes_);
    registerAttribute("SourceCode", &sourceCode_);
    registerAttribute("StackTraceSize", &stackTraceSize_);
    registerAttribute("FreqHz", &freqHz_);
    registerAttribute("GenerateRegTraceFile", &generateRegTraceFile_);
    registerAttribute("GenerateMemTraceFile", &generateMemTraceFile_);
    registerAttribute("ResetVector", &resetVector_);
    registerAttribute("SysBusMasterID", &sysBusMasterID_);

    char tstr[256];
    RISCV_sprintf(tstr, sizeof(tstr), "eventConfigDone_%s", name);
    RISCV_event_create(&eventConfigDone_, tstr);
    RISCV_register_hap(static_cast<IHap *>(this));

    isysbus_ = 0;
    estate_ = CORE_OFF;
    step_cnt_ = 0;
    pc_z_.val = 0;
    hw_stepping_break_ = 0;
    interrupt_pending_ = 0;
    sw_breakpoint_ = false;
    hw_breakpoint_ = false;
    hwBreakpoints_.make_list(0);

    dport_.valid = 0;
    reg_trace_file = 0;
    mem_trace_file = 0;
}

CpuGeneric::~CpuGeneric() {
    RISCV_event_close(&eventConfigDone_);
    if (reg_trace_file) {
        reg_trace_file->close();
        delete reg_trace_file;
    }
    if (mem_trace_file) {
        mem_trace_file->close();
        delete mem_trace_file;
    }
}

void CpuGeneric::postinitService() {
    isysbus_ = static_cast<IMemoryOperation *>(
        RISCV_get_service_iface(sysBus_.to_string(), IFACE_MEMORY_OPERATION));
    if (!isysbus_) {
        RISCV_error("System Bus interface '%s' not found",
                    sysBus_.to_string());
        return;
    }

    idbgbus_ = static_cast<IMemoryOperation *>(
        RISCV_get_service_iface(dbgBus_.to_string(), IFACE_MEMORY_OPERATION));
    if (!idbgbus_) {
        RISCV_error("Debug Bus interface '%s' not found",
                    dbgBus_.to_string());
        return;
    }

    isrc_ = static_cast<ISourceCode *>(
       RISCV_get_service_iface(sourceCode_.to_string(), IFACE_SOURCE_CODE));
    if (!isrc_) {
        RISCV_error("Source code interface '%s' not found", 
                    sourceCode_.to_string());
        return;
    }

    stackTraceBuf_.setRegTotal(2 * stackTraceSize_.to_int());

    // Get global settings:
    const AttributeType *glb = RISCV_get_global_settings();
    if ((*glb)["SimEnable"].to_bool() && isEnable_.to_bool()) {
        if (!run()) {
            RISCV_error("Can't create thread.", NULL);
            return;
        }
        if (generateRegTraceFile_.to_bool()) {
            reg_trace_file = new std::ofstream("river_func_regs.log");
        }
        if (generateMemTraceFile_.to_bool()) {
            mem_trace_file = new std::ofstream("river_func_mem.log");
        }
    }
}

void CpuGeneric::hapTriggered(IFace *isrc, EHapType type,
                                       const char *descr) {
    RISCV_event_set(&eventConfigDone_);
}

void CpuGeneric::busyLoop() {
    RISCV_event_wait(&eventConfigDone_);

    while (isEnabled()) {
        updatePipeline();
    }
}

void CpuGeneric::updatePipeline() {
    if (dport_.valid) {
        dport_.valid = 0;
        updateDebugPort();
    }

    if (!updateState()) {
        return;
    }

    pc_.setValue(npc_.getValue());
    branch_ = false;
    oplen_ = 0;

    if (!checkHwBreakpoint()) {
        fetchILine();
        instr_ = decodeInstruction(cacheline_);

        trackContextStart();
        if (instr_) {
            oplen_ = instr_->exec(cacheline_);
        } else {
            generateIllegalOpcode();
        }
        trackContextEnd();

        pc_z_ = pc_.getValue();
    }

    if (!branch_) {
        npc_.setValue(pc_.getValue().val + oplen_);
    }

    updateQueue();

    handleTrap();
}

bool CpuGeneric::updateState() {
    bool upd = true;
    switch (estate_) {
    case CORE_OFF:
    case CORE_Halted:
        updateQueue();
        upd = false;
        break;
    case CORE_Stepping:
        if (hw_stepping_break_ <= step_cnt_) {
            halt("Stepping breakpoint");
            upd = false;
        }
        break;
    default:;
    }
    if (upd) {
        step_cnt_++;
    }
    return upd;
}

void CpuGeneric::updateQueue() {
    IFace *cb;
    queue_.initProc();
    queue_.pushPreQueued();

    while ((cb = queue_.getNext(step_cnt_)) != 0) {
        static_cast<IClockListener *>(cb)->stepCallback(step_cnt_);
    }
}

void CpuGeneric::fetchILine() {
    trans_.action = MemAction_Read;
    trans_.addr = pc_.getValue().val;
    trans_.xsize = 4;
    trans_.wstrb = 0;
    trans_.source_idx = sysBusMasterID_.to_int();
    isysbus_->b_transport(&trans_);
    cacheline_[0].val = trans_.rpayload.b64[0];
    if (skip_sw_breakpoint_ && trans_.addr == br_fetch_addr_.getValue().val) {
        skip_sw_breakpoint_ = false;
        cacheline_[0].buf32[0] = br_fetch_instr_.getValue().buf32[0];
    }
}

void CpuGeneric::registerStepCallback(IClockListener *cb,
                                               uint64_t t) {
    if (!isEnabled() && t <= step_cnt_) {
        cb->stepCallback(t);
        return;
    }
    queue_.put(t, cb);
}

void CpuGeneric::setBranch(uint64_t npc) {
    branch_ = true;
    npc_.setValue(npc);
}

void CpuGeneric::pushStackTrace() {
    int cnt = static_cast<int>(stackTraceCnt_.getValue().val);
    if (cnt >= stackTraceSize_.to_int()) {
        return;
    }
    stackTraceBuf_.write(2*cnt, pc_.getValue().val);
    stackTraceBuf_.write(2*cnt + 1,  npc_.getValue().val);
    stackTraceCnt_.setValue(cnt + 1);
}

void CpuGeneric::popStackTrace() {
    uint64_t cnt = stackTraceCnt_.getValue().val;
    if (cnt) {
        stackTraceCnt_.setValue(cnt - 1);
    }
}

void CpuGeneric::dma_memop(Axi4TransactionType *tr) {
    tr->source_idx = sysBusMasterID_.to_int();
    if (tr->xsize <= sysBusWidthBytes_.to_uint32()) {
        isysbus_->b_transport(tr);
    } else {
        // 1-byte access for HC08
        Axi4TransactionType tr1 = *tr;
        tr1.xsize = 1;
        tr1.wstrb = 1;
        for (unsigned i = 0; i < tr->xsize; i++) {
            tr1.addr = tr->addr + i;
            if (tr->action == MemAction_Write) {
                tr1.wpayload.b8[0] = tr->wpayload.b8[i];
            }
            isysbus_->b_transport(&tr1);
            if (tr->action == MemAction_Read) {
                tr->rpayload.b8[i] = tr1.rpayload.b8[0];
            }
        }
    }
    if (!mem_trace_file) {
    //if (!reg_trace_file) {
        return;
    }

    char tstr[512];
    Reg64Type pload = {0};
    if (tr->action == MemAction_Read) {
        if (tr->xsize == 4) {
            pload.buf32[0] = tr->rpayload.b32[0];
        } else {
            pload.val = tr->rpayload.b64[0];
        }
        RISCV_sprintf(tstr, sizeof(tstr),
                    "%08x: [%08x] => %016" RV_PRI64 "x\n",
                    pc_.getValue().buf32[0],
                    static_cast<int>(tr->addr),
                    pload.val);
    } else {
        if (tr->xsize == 4) {
            pload.buf32[0] = tr->wpayload.b32[0];
        } else {
            pload.val = tr->wpayload.b64[0];
        }
        RISCV_sprintf(tstr, sizeof(tstr),
                    "%08x: [%08x] <= %016" RV_PRI64 "x\n",
                    pc_.getValue().buf32[0],
                    static_cast<int>(tr->addr),
                    pload.val);
    }
    (*mem_trace_file) << tstr;
    mem_trace_file->flush();
}

void CpuGeneric::go() {
    if (estate_ == CORE_OFF) {
        RISCV_error("CPU is turned-off", 0);
        return;
    }
    estate_ = CORE_Normal;
}

void CpuGeneric::step() {
    if (estate_ == CORE_OFF) {
        RISCV_error("CPU is turned-off", 0);
        return;
    }
    hw_stepping_break_ = step_cnt_ + stepping_cnt_.getValue().val;
    estate_ = CORE_Stepping;
}

void CpuGeneric::halt(const char *descr) {
    if (estate_ == CORE_OFF) {
        RISCV_error("CPU is turned-off", 0);
        return;
    }
    char strop[32];
    uint8_t tbyte;
    unsigned bytetot = oplen_;
    if (!bytetot) {
        bytetot = 1;
    }
    for (unsigned i = 0; i < bytetot; i++) {
        if (endianess() == LittleEndian) {
            tbyte = cacheline_[0].buf[bytetot-i-1];
        } else {
            tbyte = cacheline_[0].buf[i];
        }
        RISCV_sprintf(&strop[2*i], sizeof(strop)-2*i, "%02x", tbyte);
    }
    
    if (descr == NULL) {
        RISCV_info("[%6" RV_PRI64 "d] pc:%04" RV_PRI64 "x: %s \t CPU halted",
                       step_cnt_, pc_.getValue().val, strop);
    } else {
        RISCV_info("[%6" RV_PRI64 "d] pc:%04" RV_PRI64 "x: %s\t %s",
                       step_cnt_, pc_.getValue().val, strop, descr);
    }
    estate_ = CORE_Halted;
    RISCV_trigger_hap(getInterface(IFACE_SERVICE), HAP_Halt, "Descr");
}

void CpuGeneric::reset(bool active) {
    interrupt_pending_ = 0;
    status_.reset(active);
    stackTraceCnt_.reset(active);
    pc_.setValue(getResetAddress());
    npc_.setValue(getResetAddress());
    if (!active && estate_ == CORE_OFF) {
        // Turn ON:
        estate_ = CORE_Halted;//CORE_Normal;
        RISCV_trigger_hap(static_cast<IService *>(this),
                            HAP_CpuTurnON, "CPU Turned ON");
    } else if (active) {
        // Turn OFF:
        estate_ = CORE_OFF;
        RISCV_trigger_hap(static_cast<IService *>(this),
                            HAP_CpuTurnOFF, "CPU Turned OFF");
    }
    hw_breakpoint_ = false;
    sw_breakpoint_ = false;
}

void CpuGeneric::updateDebugPort() {
    DebugPortTransactionType *trans = dport_.trans;
    Axi4TransactionType tr;
    tr.xsize = 8;
    tr.source_idx = 0;
    if (trans->write) {
        tr.action = MemAction_Write;
        tr.wpayload.b64[0] = trans->wdata;
        tr.wstrb = 0xFF;
    } else {
        tr.action = MemAction_Read;
        tr.rpayload.b64[0] = 0;
    }
    tr.addr = (static_cast<uint64_t>(trans->region) << 15) | trans->addr;
    idbgbus_->b_transport(&tr);

    trans->rdata = tr.rpayload.b64[0];;
    dport_.cb->nb_response_debug_port(trans);
}

void CpuGeneric::nb_transport_debug_port(DebugPortTransactionType *trans,
                                         IDbgNbResponse *cb) {
    dport_.trans = trans;
    dport_.cb = cb;
    dport_.valid = true;
}

void CpuGeneric::addHwBreakpoint(uint64_t addr) {
    AttributeType item;
    item.make_uint64(addr);
    hwBreakpoints_.add_to_list(&item);
    hwBreakpoints_.sort();
    for (unsigned i = 0; i < hwBreakpoints_.size(); i++) {
        RISCV_debug("Breakpoint[%d]: 0x%04" RV_PRI64 "x",
                    i, hwBreakpoints_[i].to_uint64());
    }
}

void CpuGeneric::removeHwBreakpoint(uint64_t addr) {
    for (unsigned i = 0; i < hwBreakpoints_.size(); i++) {
        if (addr == hwBreakpoints_[i].to_uint64()) {
            hwBreakpoints_.remove_from_list(i);
            hwBreakpoints_.sort();
            return;
        }
    }
}

bool CpuGeneric::checkHwBreakpoint() {
    uint64_t pc = pc_.getValue().val;
    if (hw_breakpoint_ && pc == hw_break_addr_) {
        hw_breakpoint_ = false;
        return false;
    }
    hw_breakpoint_ = false;

    for (unsigned i = 0; i < hwBreakpoints_.size(); i++) {
        uint64_t bradr = hwBreakpoints_[i].to_uint64();
        if (pc < bradr) {
            // Sorted list
            break;
        }
        if (pc == bradr) {
            hw_break_addr_ = pc;
            hw_breakpoint_ = true;
            halt("Hw breakpoint");
            return true;
        }
    }
    return false;
}

void CpuGeneric::skipBreakpoint() {
    skip_sw_breakpoint_ = true;
    sw_breakpoint_ = false;
}


uint64_t GenericStatusType::aboutToRead(uint64_t cur_val) {
    GenericCpuControlType ctrl;
    CpuGeneric *pcpu = static_cast<CpuGeneric *>(parent_);
    ctrl.val = 0;
    ctrl.bits.halt = pcpu->isHalt() || !pcpu->isOn() ? 1 : 0;
    ctrl.bits.sw_breakpoint = pcpu->isSwBreakpoint() ? 1 : 0;
    ctrl.bits.hw_breakpoint = pcpu->isHwBreakpoint() ? 1 : 0;
    return ctrl.val;
}

uint64_t GenericStatusType::aboutToWrite(uint64_t new_val) {
    GenericCpuControlType ctrl;
    CpuGeneric *pcpu = static_cast<CpuGeneric *>(parent_);
    ctrl.val = new_val;
    if (ctrl.bits.halt) {
        pcpu->halt("halted from DSU");
    } else if (ctrl.bits.stepping) {
        pcpu->step();
    } else {
        pcpu->go();
    }
    return new_val;
}

uint64_t FetchedBreakpointType::aboutToWrite(uint64_t new_val) {
    CpuGeneric *pcpu = static_cast<CpuGeneric *>(parent_);
    pcpu->skipBreakpoint();
    return new_val;
}

uint64_t AddBreakpointType::aboutToWrite(uint64_t new_val) {
    CpuGeneric *pcpu = static_cast<CpuGeneric *>(parent_);
    pcpu->addHwBreakpoint(new_val);
    return new_val;
}

uint64_t RemoveBreakpointType::aboutToWrite(uint64_t new_val) {
    CpuGeneric *pcpu = static_cast<CpuGeneric *>(parent_);
    pcpu->removeHwBreakpoint(new_val);
    return new_val;
}

uint64_t StepCounterType::aboutToRead(uint64_t cur_val) {
    CpuGeneric *pcpu = static_cast<CpuGeneric *>(parent_);
    return pcpu->getStepCounter();
}

}  // namespace debugger

