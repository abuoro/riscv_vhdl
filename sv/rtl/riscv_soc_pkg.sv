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
package riscv_soc_pkg;

import config_target_pkg::*;
import types_amba_pkg::*;
import types_bus0_pkg::*;
import types_bus1_pkg::*;
import river_cfg_pkg::*;
import types_river_pkg::*;
import workgroup_pkg::*;

localparam bit async_reset = CFG_ASYNC_RESET;


localparam int SOC_PNP_XCTRL0 = 0;
localparam int SOC_PNP_GROUP0 = 1;
localparam int SOC_PNP_BOOTROM = 2;
localparam int SOC_PNP_SRAM = 3;
localparam int SOC_PNP_DDR_AXI = 4;
localparam int SOC_PNP_DDR_APB = 5;
localparam int SOC_PNP_PRCI = 6;
localparam int SOC_PNP_GPIO = 7;
localparam int SOC_PNP_CLINT = 8;
localparam int SOC_PNP_PLIC = 9;
localparam int SOC_PNP_PNP = 10;
localparam int SOC_PNP_PBRIDGE0 = 11;
localparam int SOC_PNP_DMI = 12;
localparam int SOC_PNP_UART1 = 13;
localparam int SOC_PNP_SPI = 14;
localparam int SOC_PNP_TOTAL = 15;

localparam int SOC_UART1_LOG2_FIFOSZ = 4;

localparam int SOC_GPIO0_WIDTH = 12;

localparam int SOC_SPI0_LOG2_FIFOSZ = 9;

// Number of contexts in PLIC controller.
// Example FU740: S7 Core0 (M) + 4xU74 Cores (M+S).
localparam int SOC_PLIC_CONTEXT_TOTAL = 9;
// Any number up to 1024. Zero interrupt must be 0.
localparam int SOC_PLIC_IRQ_TOTAL = 73;

// HEX-image for the initialization of the Boot ROM.
localparam string SOC_BOOTROM_FILE_HEX = "../../../../examples/bootrom_tests/linuxbuild/bin/bootrom_tests";

// Hardware SoC Identificator.
// Read Only unique platform identificator that could be read by FW
localparam bit [31:0] SOC_HW_ID = 32'h20220903;

typedef dev_config_type soc_pnp_vector[0:SOC_PNP_TOTAL - 1];

endpackage: riscv_soc_pkg
