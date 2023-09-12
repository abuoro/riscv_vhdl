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

#include <string.h>
#include "axi_maps.h"
#include "encoding.h"
#include "fw_api.h"

//#define PRINT_DDR_IMAGE

void allocate_exception_table(void);
void allocate_interrupt_table(void);
void test_l2coherence(void);
void test_plic(void);
void test_fpu(void);
void test_swirq(void);
void test_mtimer(void);
void test_missaccess(void);
void test_stackprotect(void);
void test_spiflash(uint64_t bar);
void test_gnss_ss(uint64_t bar);
int test_pmp();
int test_mmu();
int test_ddr();
void print_pnp(void);
int hwthread1(void);
int hwthread2(void);
int hwthread3(void);

ESdCardType spi_init();
int spi_sd_card_memcpy(uint64_t src, uint64_t dst, int sz);


int main() {
    uint32_t cfg;
    pnp_map *pnp = (pnp_map *)ADDR_BUS0_XSLV_PNP;
    uart_map *uart = (uart_map *)ADDR_BUS0_XSLV_UART0;
    gpio_map *gpio = (gpio_map *)ADDR_BUS0_XSLV_GPIO;
    uint64_t bar;
    uint32_t cpu_max;

    switch (fw_get_cpuid()) {
    case 0:
        break;
    case 1:
        hwthread1();
        break;
    case 2:
        hwthread2();
        break;
    case 3:
        hwthread3();
        break;
    default:
        while (1) {}
    }

    pnp->fwid = 0x20220116;
    gpio->input_en = 0x000f;
    gpio->output_en = 0xfff0;
    fw_malloc_init();
    
    allocate_exception_table();
    allocate_interrupt_table();

    uart_isr_init();   // enable printf_uart function and Tx irq=1
 
    led_set(0x01);

#if 1
    // wait 1 ms of CMD = HIGH before enabling SD-controller clock
    while (fw_get_rdtime() < (SYS_HZ / 1000)) {}

    *((uint32_t *)(ADDR_BUS1_APB_QSPI2 + 0)) = (0 << 24) | 49;  // [31:24]=20 MHz;  [23:0]=400 KHz
    *((uint32_t *)(ADDR_BUS1_APB_QSPI2 + 4)) = 0x1;  // enable sdctrl sclk
    *((uint32_t *)(ADDR_BUS1_APB_QSPI2 + 8)) = 0x0FFF;  // Increase default watchdog to detect 'no response'
    uint32_t cmd_status;
    uint32_t cmd_err;
    uint32_t sdstate;
    uint32_t t1;
    do {
        t1 = *((uint32_t *)(ADDR_BUS1_APB_QSPI2 + 0x04));
        printf_uart("x04: cmd:%d {cd,dat2,da1,dat0} %d%d%d%d\r\n",
                   (t1 >> 8) & 1,
                   (t1 >> 7) & 1,
                   (t1 >> 6) & 1,
                   (t1 >> 5) & 1,
                   (t1 >> 4) & 1
                   );

        cmd_status = *((uint32_t *)(ADDR_BUS1_APB_QSPI2 + 0x10));
        cmd_err = cmd_status & 0xF;
        sdstate = (cmd_status >> 8) & 0xF;
        printf_uart("x10_status:%04x;err:%1x;cmdstate:%1d;sdstate:%1d;type:%d\r\n",
                   cmd_status,
                   cmd_err,
                   (cmd_status >> 4) & 0xF,
                   sdstate,
                   (cmd_status >> 12) & 0x7);
        t1 = *((uint32_t *)(ADDR_BUS1_APB_QSPI2 + 0x14));
        printf_uart("x14_last_resp:%08x;req_cmd:%d,resp_cmd:%d;crc_rx:%02x,crc_calc:%02x\r\n",
                    t1,
                    (t1 >> 0) & 0x3F,
                    (t1 >> 8) & 0x3F,
                    (t1 >> 16) & 0x7F,
                    (t1 >> 24) & 0x7F
                    );
        t1 = *((uint32_t *)(ADDR_BUS1_APB_QSPI2 + 0x18));
        printf_uart("last_arg: %08x\r\n", t1);
    } while (sdstate != 3);  // SDSTATE=3 (STBY)
    t1 = *((uint32_t *)(ADDR_BUS1_APB_QSPI2 + 0x10));
    t1 = (t1 >> 12) & 0x7; // sdtype
    const char *SDTYPE[8] = {"unknown", "Ver1X", "Ver2X_SC", "Ver2X_HC", "Unusable", 0};
    printf_uart("sdtype: %s\r\n", SDTYPE[t1]);
#endif

#if 1
    cpu_max = pnp->cfg >> 28;

    printf_uart("HARTID . . . . .%d\r\n", fw_get_cpuid());
    printf_uart("HARTS. . . . . .%d\r\n", cpu_max);
    printf_uart("PLIC_IRQS  . . .%d\r\n", (pnp->cfg & 0xFF));
    printf_uart("HWID . . . . . .0x%08x\r\n", pnp->hwid);
    printf_uart("FWID . . . . . .0x%08x\r\n", pnp->fwid);

    led_set(0x02);

    test_plic();
    test_mtimer();
    test_swirq();
    test_l2coherence();
    test_pmp();
    test_mmu();

    led_set(0x03);
    test_fpu();

    led_set(0x04);
    test_missaccess();

    led_set(0x05);
    test_stackprotect();


    led_set(0x55);
    print_pnp();

    led_set(0x1F);

    test_ddr();
#endif

    ESdCardType sdtype = spi_init();

    SD_Unknown,
    printf_uart("%s", "SPI.Init . . . .");
    if (sdtype == SD_Ver1x) {
        printf_uart("%s\r\n", "SD1x");
    } else if (sdtype == SD_Ver2x_StandardCapacity) {
        printf_uart("%s\r\n", "SD2x");
    } else if (sdtype == SD_Ver2x_HighCapacity) {
        printf_uart("%s\r\n", "SDHC");
    } else {
        printf_uart("%s", "Wrong SD-card\r\n");
        while (1) {}
    }

    static const int BBL_IMAGE_SIZE = 10*1024*1024;  // actually ~8MB

    printf_uart("%s", "Copy BBL . . . .");
    int copied = spi_sd_card_memcpy(0, ADDR_BUS0_XSLV_DDR, BBL_IMAGE_SIZE);
    if (copied < BBL_IMAGE_SIZE) {
        printf_uart("%s\r\n", "Failed");
        while (1) {}
    } else {
        printf_uart("%d B copied\r\n", copied);
    }

    // Run BSL and Linux from DDR:
    set_csr(mstatus, MSTATUS_MPP_M);          // run bbl-q and riscv-tests in machine mode
    write_csr(mepc, ADDR_BUS0_XSLV_DDR);      // jump to ddr (bbl-q should be init)

    // a0 = hart id
    // a1 = fdt header
    __asm__("fence.i");

#ifdef PRINT_DDR_IMAGE
   printf_uart("%s\r\n", "Verif:");
   uint64_t *verif = (uint64_t *)ADDR_BUS0_XSLV_DDR;
   for (int i = 0; i < BBL_IMAGE_SIZE/sizeof(uint64_t); i++) {
        printf_uart("%016llx\r\n", verif[i]);
   }
#endif

    __asm__("csrr a0, mhartid");
    __asm__("la a1, dtb_start");
    __asm__("mret");

    // NEVER REACH THIS POINT
    return 0;
}
