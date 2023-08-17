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
#pragma once

#include <systemc.h>
#include "../../../../rtl/techmap/bufg/iobuf_tech.h"

namespace debugger {

SC_MODULE(vip_sdcard_top) {
 public:
    sc_in<bool> i_nrst;                                     // To avoid undefined states of registers (xxx)
    sc_in<bool> i_sclk;
    sc_inout<bool> io_cmd;
    sc_inout<bool> io_dat0;
    sc_inout<bool> io_dat1;
    sc_inout<bool> io_dat2;
    sc_inout<bool> io_cd_dat3;

    void comb();
    void registers();

    SC_HAS_PROCESS(vip_sdcard_top);

    vip_sdcard_top(sc_module_name name,
                   bool async_reset);
    virtual ~vip_sdcard_top();

    void generateVCD(sc_trace_file *i_vcd, sc_trace_file *o_vcd);

 private:
    bool async_reset_;

    // Receiver CMD state:
    static const uint8_t CMDSTATE_IDLE = 0;
    static const uint8_t CMDSTATE_REQ_ARG = 1;
    static const uint8_t CMDSTATE_REQ_CRC7 = 2;
    static const uint8_t CMDSTATE_REQ_STOPBIT = 3;
    static const uint8_t CMDSTATE_WAIT_RESP = 4;
    static const uint8_t CMDSTATE_RESP = 5;

    struct vip_sdcard_top_registers {
        sc_signal<bool> cmd_dir;
        sc_signal<sc_uint<48>> cmd_rxshift;
        sc_signal<sc_uint<48>> cmd_txshift;
        sc_signal<sc_uint<3>> cmd_state;
        sc_signal<sc_uint<6>> bitcnt;
    } v, r;

    void vip_sdcard_top_r_reset(vip_sdcard_top_registers &iv) {
        iv.cmd_dir = 1;
        iv.cmd_rxshift = ~0ull;
        iv.cmd_txshift = ~0ull;
        iv.cmd_state = CMDSTATE_IDLE;
        iv.bitcnt = 0;
    }

    sc_signal<bool> w_clk;
    sc_signal<sc_uint<8>> wb_rdata;
    sc_signal<bool> w_cmd_in;
    sc_signal<bool> w_cmd_out;

    iobuf_tech *iobufcmd0;

};

}  // namespace debugger

