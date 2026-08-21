SHELL := /bin/bash
ROOT := ../..
BUILD := build
OBJDIR := $(BUILD)/obj
TOP := soc

VERILATOR ?= verilator
PAYLOAD ?= $(BUILD)/fw_payload.bin
MAX_CYCLES ?= 3000000000

ARTIFACTS := $(BUILD)/fw_payload.bin $(BUILD)/Image $(BUILD)/rootfs.cpio
ARTIFACT_INPUTS := \
	Dockerfile \
	build-in-docker.sh \
	build-linux.sh \
	configs/kianv_rtl_linux_defconfig \
	configs/linux-sim.fragment \
	$(wildcard rootfs-overlay/*)

RTL ?= \
	$(ROOT)/src/cache.v \
	$(ROOT)/src/cache_sram_D$$.v \
	$(ROOT)/src/cache_sram_I$$.v \
	$(ROOT)/src/clint.v \
	$(ROOT)/src/dcache.v \
	$(ROOT)/src/fifo.v \
	$(ROOT)/src/gf180mcu_fd_ip_sram__sram512x8m8wm1_wrapper.v \
	$(ROOT)/src/gpio.v \
	$(ROOT)/src/icache.v \
	$(ROOT)/src/kianv_harris_edition/alu.v \
	$(ROOT)/src/kianv_harris_edition/alu_decoder.v \
	$(ROOT)/src/kianv_harris_edition/associative_cache.v \
	$(ROOT)/src/kianv_harris_edition/control_unit.v \
	$(ROOT)/src/kianv_harris_edition/csr_decoder.v \
	$(ROOT)/src/kianv_harris_edition/csr_exception_handler.v \
	$(ROOT)/src/kianv_harris_edition/csr_unit.v \
	$(ROOT)/src/kianv_harris_edition/datapath_unit.v \
	$(ROOT)/src/kianv_harris_edition/design_elements.v \
	$(ROOT)/src/kianv_harris_edition/design_elements_fpgacpu_ca.v \
	$(ROOT)/src/kianv_harris_edition/divider.v \
	$(ROOT)/src/kianv_harris_edition/divider_decoder.v \
	$(ROOT)/src/kianv_harris_edition/extend.v \
	$(ROOT)/src/kianv_harris_edition/interrupt_controller.v \
	$(ROOT)/src/kianv_harris_edition/kianv_harris_mc_edition.v \
	$(ROOT)/src/kianv_harris_edition/load_alignment.v \
	$(ROOT)/src/kianv_harris_edition/load_decoder.v \
	$(ROOT)/src/kianv_harris_edition/main_fsm.v \
	$(ROOT)/src/kianv_harris_edition/multiplier.v \
	$(ROOT)/src/kianv_harris_edition/multiplier_decoder.v \
	$(ROOT)/src/kianv_harris_edition/multiplier_extension_decoder.v \
	$(ROOT)/src/kianv_harris_edition/register_file.v \
	$(ROOT)/src/kianv_harris_edition/store_alignment.v \
	$(ROOT)/src/kianv_harris_edition/store_decoder.v \
	$(ROOT)/src/kianv_harris_edition/sv32.v \
	$(ROOT)/src/kianv_harris_edition/sv32_table_walk.v \
	$(ROOT)/src/kianv_harris_edition/sv32_translate_data_to_physical.v \
	$(ROOT)/src/kianv_harris_edition/sv32_translate_instruction_to_physical.v \
	$(ROOT)/src/plic.v \
	$(ROOT)/src/rx_uart.v \
	$(ROOT)/src/sdram/mt48lc16m16a2_ctrl.v \
	$(ROOT)/src/soc.v \
	$(ROOT)/src/spi.v \
	$(ROOT)/src/spi_nor_flash.v \
	$(ROOT)/src/sram_sp.v \
	$(ROOT)/src/sram_sp_gf180_512x56.v \
	$(ROOT)/src/tx_uart.v

.PHONY: all artifacts build run verify clean

all: verify

$(ARTIFACTS) &: $(ARTIFACT_INPUTS)
	./build-in-docker.sh

artifacts: $(ARTIFACTS)

$(OBJDIR)/V$(TOP): Makefile sim_main.cpp $(RTL)
	mkdir -p $(BUILD)
	$(VERILATOR) --cc --exe --build -j 0 \
		--top-module $(TOP) --Mdir $(OBJDIR) \
		-MAKEFLAGS "OPT_FAST=-O3 OPT_GLOBAL=-O3" \
		-Wno-fatal -Wno-PINMISSING -Wno-TIMESCALEMOD -Wno-BLKANDNBLK \
		-DSIM -DNUM_ENTRIES_ITLB=32 -DNUM_ENTRIES_DTLB=32 \
		-I$(ROOT)/src -I$(ROOT)/src/kianv_harris_edition \
		-CFLAGS "-O3 -std=c++17 $(EXTRA_CFLAGS)" \
		$(RTL) $(abspath sim_main.cpp)

build: $(OBJDIR)/V$(TOP)

run: artifacts build
	test -f $(PAYLOAD)
	set -o pipefail; $(OBJDIR)/V$(TOP) --payload $(PAYLOAD) \
		--max-cycles $(MAX_CYCLES) | tee $(BUILD)/uart.log

verify: run
	grep -F "KianV RTL Linux ready" $(BUILD)/uart.log
	grep -F "COMMAND_OK" $(BUILD)/uart.log
	grep -F "Linux kianv" $(BUILD)/uart.log
	grep -F "WORKER_A_START" $(BUILD)/uart.log
	grep -F "WORKER_B_START" $(BUILD)/uart.log
	grep -F "PROCESS_TABLE_BEGIN" $(BUILD)/uart.log
	grep -F "PROCESS_TABLE_END" $(BUILD)/uart.log
	grep -F "WORKER_A_DONE" $(BUILD)/uart.log
	grep -F "WORKER_B_DONE" $(BUILD)/uart.log
	grep -F "SCHEDULER_OK" $(BUILD)/uart.log

clean:
	rm -rf $(BUILD)
