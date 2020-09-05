--!
--! Copyright 2018 Sergey Khabarov, sergeykhbr@gmail.com
--!
--! Licensed under the Apache License, Version 2.0 (the "License");
--! you may not use this file except in compliance with the License.
--! You may obtain a copy of the License at
--!
--!     http://www.apache.org/licenses/LICENSE-2.0
--!
--! Unless required by applicable law or agreed to in writing, software
--! distributed under the License is distributed on an "AS IS" BASIS,
--! WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
--! See the License for the specific language governing permissions and
--! limitations under the License.
--!
--! @brief     Access to debug port of CPUs through the DMI registers.
-----------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_misc.all;  -- or_reduce()
use ieee.numeric_std.all;
library commonlib;
use commonlib.types_common.all;
library ambalib;
use ambalib.types_amba4.all;
library riverlib;
use riverlib.river_cfg.all;
use riverlib.types_river.all;

entity dmi_regs is
  generic (
    async_reset : boolean := false
  );
  port 
  (
    clk    : in std_logic;
    nrst   : in std_logic;
    -- port[0] connected to JTAG TAP has access to AXI master interface (SBA registers)
    i_dmi_jtag_req_valid : in std_logic;
    o_dmi_jtag_req_ready : out std_logic;
    i_dmi_jtag_write : in std_logic;
    i_dmi_jtag_addr : in std_logic_vector(6 downto 0);
    i_dmi_jtag_wdata : in std_logic_vector(31 downto 0);
    o_dmi_jtag_resp_valid : out std_logic;
    i_dmi_jtag_resp_ready : in std_logic;
    o_dmi_jtag_rdata : out std_logic_vector(31 downto 0);
    -- port[1] connected to DSU doesn't have access to AXI master interface
    i_dmi_dsu_req_valid : in std_logic;
    o_dmi_dsu_req_ready : out std_logic;
    i_dmi_dsu_write : in std_logic;
    i_dmi_dsu_addr : in std_logic_vector(6 downto 0);
    i_dmi_dsu_wdata : in std_logic_vector(31 downto 0);
    o_dmi_dsu_resp_valid : out std_logic;
    i_dmi_dsu_resp_ready : in std_logic;
    o_dmi_dsu_rdata : out std_logic_vector(31 downto 0);
    -- Common signals
    o_hartsel : out std_logic_vector(CFG_LOG2_CPU_MAX-1 downto 0);
    o_dmstat : out std_logic_vector(1 downto 0);
    o_ndmreset : out std_logic;        -- non-debug module reset

    o_cfg  : out axi4_master_config_type;
    i_xmsti  : in axi4_master_in_type;
    o_xmsto  : out axi4_master_out_type;
    o_dporti : out dport_in_vector;
    i_dporto : in dport_out_vector
  );
end;

architecture arch_dmi_regs of dmi_regs is

  constant xconfig : axi4_master_config_type := (
     descrtype => PNP_CFG_TYPE_MASTER,
     descrsize => PNP_CFG_MASTER_DESCR_BYTES,
     vid => VENDOR_GNSSSENSOR,
     did => RISCV_RIVER_DMI
  );

  constant HARTSELLEN : integer := log2x(CFG_TOTAL_CPU_MAX);
  
  type state_type is (
      Idle,
      DmiRequest,
      DportRequest,
      DportResponse,
      DportPostexec,
      DportBroadbandRequest,
      DportBroadbandResponse,
      DmiResponse
   );
  

  type registers is record
    state : state_type;
    dmstat : std_logic_vector(1 downto 0);
    hartsel : std_logic_vector(HARTSELLEN-1 downto 0);
    ndmreset : std_logic;   -- non-debug module reset
    resumeack : std_logic;
    
    addr : std_logic_vector(CFG_DPORT_ADDR_BITS-1 downto 0);
    rdata : std_logic_vector(63 downto 0);
    wdata : std_logic_vector(63 downto 0);
    arg0 : std_logic_vector(63 downto 0);
    command : std_logic_vector(31 downto 0);
    autoexecdata : std_logic_vector(CFG_DATA_REG_TOTAL-1 downto 0);
    autoexecprogbuf : std_logic_vector(CFG_PROGBUF_REG_TOTAL-1 downto 0);
    transfer : std_logic;
    write : std_logic;
    postexec : std_logic;
    jtag_dsu : std_logic;
    broadband_req : std_logic_vector(CFG_TOTAL_CPU_MAX-1 downto 0);
  end record;

  constant R_RESET : registers := (
     Idle, -- state
     "00", -- dmstat
     (others => '0'), -- hartsel
     '0', -- ndmreset
     '0', -- resumeack
     (others => '0'), -- addr
     (others => '0'), -- rdata
     (others => '0'), -- wdata
     (others => '0'),  -- arg0
     (others => '0'),  -- command
     (others => '0'),  -- autoexecdata
     (others => '0'),  -- autoexecprogbuf
     '0', -- transfer
     '0', -- write
     '0',  -- postexec
     '0',  -- jtag_dsu
     (others => '0')  -- broadband_req
  );

  signal r, rin: registers;

begin

  comblogic : process(nrst,
       i_dmi_jtag_req_valid, i_dmi_jtag_write, i_dmi_jtag_addr, i_dmi_jtag_wdata, i_dmi_jtag_resp_ready,
       i_dmi_dsu_req_valid, i_dmi_dsu_write, i_dmi_dsu_addr, i_dmi_dsu_wdata, i_dmi_dsu_resp_ready,
       i_xmsti, i_dporto, r)
    variable v : registers;
    variable v_dmi_jtag_req_ready : std_logic;
    variable v_dmi_dsu_req_ready : std_logic;
    variable v_dmi_jtag_resp_valid : std_logic;
    variable v_dmi_dsu_resp_valid : std_logic;
    variable vdporti : dport_in_vector;
    variable vxmsto : axi4_master_out_type;
    variable hsel : integer range 0 to CFG_TOTAL_CPU_MAX-1;
    variable v_axi_ready : std_logic;
    variable vb_haltsum : std_logic_vector(CFG_TOTAL_CPU_MAX-1 downto 0);
  begin
    v := r;

    v_dmi_jtag_req_ready := '0';
    v_dmi_dsu_req_ready := '0';
    v_dmi_jtag_resp_valid := '0';
    v_dmi_dsu_resp_valid := '0';
    vdporti := (others => dport_in_none);
    vxmsto := axi4_master_out_none;
    hsel := conv_integer(r.hartsel);
    
    for n in 0 to CFG_TOTAL_CPU_MAX-1 loop
      vb_haltsum(n) := i_dporto(n).halted;
    end loop;

    case r.state is
    when Idle =>
        v.addr := (others => '0');
        v.wdata := (others => '0');
        v.rdata := (others => '0');
        v.transfer := '0';
        v.postexec := '0';
        if i_dmi_jtag_req_valid = '1' then
            v.jtag_dsu := '0';
            v_dmi_jtag_req_ready := '1';
            v.write := i_dmi_jtag_write;
            v.addr(6 downto 0) := i_dmi_jtag_addr;
            v.wdata(31 downto 0) := i_dmi_jtag_wdata;
            v.state := DmiRequest;
        elsif i_dmi_dsu_req_valid = '1' then
            v.jtag_dsu := '1';
            v_dmi_dsu_req_ready := '1';
            v.write := i_dmi_dsu_write;
            v.addr(6 downto 0) := i_dmi_dsu_addr;
            v.wdata(31 downto 0) := i_dmi_dsu_wdata;
            v.state := DmiRequest;
        end if;
          
    when DmiRequest =>
        v.state := DmiResponse;       -- default no dport transfer
        if r.addr(11 downto 0) = X"004" then            -- DATA0
            v.rdata(31 downto 0) := r.arg0(31 downto 0);
            if r.write = '1' then
                v.arg0(31 downto 0) := r.wdata(31 downto 0);
            end if;
            if r.autoexecdata(0) = '1' and r.command(31 downto 24) = X"00" then  -- cmdtype: register access
                -- Auto repead the last command on register access
                v.addr(13 downto 0) := r.command(13 downto 0);
                v.write := r.command(16);               -- write
                v.transfer := r.command(17);
                v.postexec := r.command(18);            -- postexec
                v.wdata := r.arg0(63 downto 32) & r.wdata(31 downto 0);
                if r.command(16) = '0' or r.command(17) = '1' then
                    -- read operation or write with transfer
                    v.state := DportRequest;
                end if;
            end if;
        elsif r.addr(11 downto 0) = X"005" then            -- DATA1
            v.rdata(31 downto 0) := r.arg0(63 downto 32);
            if r.write = '1' then
                v.arg0(63 downto 32) := r.wdata(31 downto 0);
            end if;
            if r.autoexecdata(1) = '1' and r.command(31 downto 24) = X"00" then
                -- Auto repead the last command on register access
                v.addr(13 downto 0) := r.command(13 downto 0);
                v.write := r.command(16);               -- write
                v.transfer := r.command(17);
                v.postexec := r.command(18);            -- postexec
                v.wdata := r.wdata(31 downto 0) & r.arg0(31 downto 0);
                if r.command(16) = '0' or r.command(17) = '1' then
                    -- read operation or write with transfer
                    v.state := DportRequest;
                end if;
            end if;
        elsif r.addr(11 downto 0) = X"010" then         -- DMCONTROL
            v.rdata(16+HARTSELLEN-1 downto 16) := r.hartsel;
            v.rdata(1) := r.ndmreset;
            v.rdata(0) := '1';                          -- dmactive: 1=module functional normally 
            if r.write = '1' then
                -- Access to CSR only on writing
                v.hartsel := r.wdata(16+HARTSELLEN-1 downto 16);  -- hartsello
                v.ndmreset := r.wdata(1);               -- ndmreset
                v.resumeack := not r.wdata(31) and r.wdata(30) and i_dporto(conv_integer(v.hartsel)).halted;

                v.state := DportRequest;
                v.addr(13 downto 0) := "00" & CSR_runcontrol;
            end if;
        elsif r.addr(11 downto 0) = X"011" then         -- DMSTATUS
            v.rdata(17) := r.resumeack;                 -- allresumeack
            v.rdata(16) := r.resumeack;                 -- anyresumeack
            v.rdata(15) := not i_dporto(hsel).available; -- allnonexistent
            v.rdata(14) := not i_dporto(hsel).available; -- anynonexistent
            v.rdata(13) := not i_dporto(hsel).available; -- allunavail
            v.rdata(12) := not i_dporto(hsel).available; -- anyunavail
            v.rdata(11) := not i_dporto(hsel).halted and i_dporto(hsel).available; -- allrunning:
            v.rdata(10) := not i_dporto(hsel).halted and i_dporto(hsel).available; -- anyrunning:
            v.rdata(9) := i_dporto(hsel).halted and i_dporto(hsel).available;      -- allhalted:
            v.rdata(8) := i_dporto(hsel).halted and i_dporto(hsel).available;      -- anyhalted:
            v.rdata(7) := '1';                          -- authenticated:
            v.rdata(3 downto 0) := X"2";                -- version: dbg spec v0.13
        elsif r.addr(11 downto 0) = X"016" then         -- ABSTRACTCS
            v.state := DportRequest;
            v.addr(13 downto 0) := "00" & CSR_abstractcs;
        elsif r.addr(11 downto 0) = X"017" then         -- COMMAND
            if r.write = '1' then
                v.command := r.wdata(31 downto 0);          -- original value for auto repeat
                if r.wdata(31 downto 24) = X"00" then       -- cmdtype: 0=register access
                    v.addr(13 downto 0) := r.wdata(13 downto 0);
                    v.write := r.wdata(16);                 -- write
                    v.transfer := r.command(17);
                    v.postexec := r.wdata(18);              -- postexec
                    v.wdata := r.arg0;
                    if r.command(16) = '0' or r.command(17) = '1' then
                        -- read operation or write with transfer
                        v.state := DportRequest;
                    end if;
                end if;
            end if;
        elsif r.addr(11 downto 0) = X"018" then         -- ABSTRACAUTO
            v.rdata(CFG_DATA_REG_TOTAL-1 downto 0) := r.autoexecdata;
            v.rdata(16+CFG_PROGBUF_REG_TOTAL-1 downto 16) := r.autoexecprogbuf;
        elsif r.addr(11 downto 4) = X"02" then          -- PROGBUF0..PROGBUF15
            v.addr(13 downto 0) := "00" & CSR_progbuf;
            v.wdata(35 downto 32) :=  r.addr(3 downto 0);
            v.broadband_req := (others => '1');         -- to all Harts
            v.state := DportBroadbandRequest;
        elsif r.addr(11 downto 0) = X"040" then         -- HALTSUM0
            v.rdata(CFG_TOTAL_CPU_MAX-1 downto 0) := vb_haltsum;
        end if;
          
    when DportRequest =>  
        vdporti(hsel).req_valid := '1';
        vdporti(hsel).addr := r.addr;
        vdporti(hsel).write := r.write;
        vdporti(hsel).wdata := r.wdata;
        if i_dporto(hsel).req_ready = '1' then
            v.state := DportResponse;
        end if;
    when DportResponse =>
        vdporti(hsel).resp_ready := '1';
        if i_dporto(hsel).resp_valid = '1' then
            v.state := DmiResponse;
            v.rdata := i_dporto(hsel).rdata;
            if r.write = '0' and r.transfer = '1' then
                v.arg0 := i_dporto(hsel).rdata;
            end if;
            if r.postexec = '1' then
                v.state := DportPostexec;
            end if;
        end if;
    when DportPostexec =>
        v.write := '1';
        v.postexec := '0';
        v.transfer := '0';
        v.addr(13 downto 0) := "00" & CSR_runcontrol;
        v.wdata := (others => '0');
        v.wdata(18) := '1';             -- req_progbuf: request to execute progbuf
        v.state := DportRequest;

    when DportBroadbandRequest =>
        for i in 0 to CFG_TOTAL_CPU_MAX-1 loop
            vdporti(i).req_valid := r.broadband_req(i);
            vdporti(i).wdata := r.wdata;
            vdporti(i).addr := r.addr;
            vdporti(i).write := r.write;
            if i_dporto(i).req_ready = '1' then
                v.broadband_req(i) := '0';
            end if;
        end loop;
        if or_reduce(r.broadband_req) = '0' then
            v.broadband_req := (others => '1');
            v.state := DportBroadbandResponse;
        end if;
    when DportBroadbandResponse =>
        for i in 0 to CFG_TOTAL_CPU_MAX-1 loop
             vdporti(i).resp_ready := r.broadband_req(i);
             if i_dporto(i).resp_valid = '1' then
                v.broadband_req(i) := '0';
             end if;
        end loop;
        if or_reduce(r.broadband_req) = '0' then
            if r.postexec = '1' then
                v.state := DportPostexec;
            else
                v.state := DportResponse;
            end if;
        end if;

    when DmiResponse =>
        v_dmi_jtag_resp_valid := not r.jtag_dsu;
        v_dmi_dsu_resp_valid := r.jtag_dsu;
        if (not r.jtag_dsu and i_dmi_jtag_resp_ready) = '1' or
           (r.jtag_dsu and i_dmi_dsu_resp_ready) = '1' then
            v.state := Idle;
        end if;
    when others =>
    end case;

    if not async_reset and nrst = '0' then 
        v := R_RESET;
    end if;

    rin <= v;

    o_dmi_jtag_req_ready <= v_dmi_jtag_req_ready;
    o_dmi_jtag_resp_valid <= v_dmi_jtag_resp_valid;
    o_dmi_jtag_rdata <= r.rdata(31 downto 0);

    o_dmi_dsu_req_ready <= v_dmi_dsu_req_ready;
    o_dmi_dsu_resp_valid <= v_dmi_dsu_resp_valid;
    o_dmi_dsu_rdata <= r.rdata(31 downto 0);

    o_dporti <= vdporti;
    o_xmsto <= vxmsto;
  end process;

  o_cfg  <= xconfig;
  o_hartsel <= r.hartsel;
  o_ndmreset <= r.ndmreset;
  o_dmstat <= r.dmstat;


  -- registers:
  regs : process(clk, nrst)
  begin 
      if async_reset and nrst = '0' then
          r <= R_RESET;
      elsif rising_edge(clk) then 
          r <= rin;
      end if; 
  end process;

end;