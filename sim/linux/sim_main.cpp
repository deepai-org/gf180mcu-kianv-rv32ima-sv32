#include "Vsoc.h"
#include "Vsoc___024root.h"
#include "verilated.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kUartDiv = 174;  // 20 MHz / 115200, SIM reset default
// Linux's SBI HVC console polls the 16-byte hardware RX FIFO. A real person or
// terminal program does not paste an entire test script with zero think time,
// so leave 2.5 ms between bytes and wait for each command's output before
// sending the next one. This keeps the test pin-accurate without overrunning
// the RTL FIFO merely because the host simulator can enqueue bytes instantly.
constexpr int kUartInterByteGap = 50000;  // 2.5 ms at the modeled 20 MHz

std::vector<uint8_t> read_file(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot open " + path);
  return {std::istreambuf_iterator<char>(input), {}};
}

class SpiFlash {
 public:
  explicit SpiFlash(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}

  uint8_t tick(bool csn, bool clk, bool mosi) {
    if (csn) {
      active_ = false;
      prev_clk_ = clk;
      miso_ = 1;
      return miso_;
    }
    if (!active_) {
      active_ = true;
      bit_ = 0;
      rx_ = 0;
      phase_ = 0;
      address_ = 0;
      tx_ = 0xff;
      miso_ = 1;
    }
    if (!prev_clk_ && clk) {
      rx_ = static_cast<uint8_t>((rx_ << 1) | mosi);
      if (++bit_ == 8) {
        consume(rx_);
        rx_ = 0;
        bit_ = 0;
      }
    } else if (prev_clk_ && !clk) {
      miso_ = (tx_ >> (7 - bit_)) & 1;
    }
    prev_clk_ = clk;
    return miso_;
  }

 private:
  void consume(uint8_t value) {
    if (phase_ == 0) {
      phase_ = value == 0x03 ? 1 : 0;
    } else if (phase_ <= 3) {
      address_ = (address_ << 8) | value;
      ++phase_;
      if (phase_ == 4) tx_ = byte_at(address_++);
    } else {
      tx_ = byte_at(address_++);
    }
  }

  uint8_t byte_at(uint32_t address) const {
    return address < bytes_.size() ? bytes_[address] : 0xff;
  }

  std::vector<uint8_t> bytes_;
  bool active_ = false, prev_clk_ = false;
  uint8_t bit_ = 0, rx_ = 0, tx_ = 0xff, miso_ = 1, phase_ = 0;
  uint32_t address_ = 0;
};

class Sdram {
 public:
  static constexpr size_t kSize = 32 * 1024 * 1024;

  explicit Sdram(const std::vector<uint8_t> &payload) : bytes_(kSize, 0) {
    if (payload.size() > bytes_.size())
      throw std::runtime_error("firmware payload exceeds 32 MiB SDRAM");
    std::copy(payload.begin(), payload.end(), bytes_.begin());
  }

  uint16_t rising(const Vsoc &top) {
    if (read_delay_ > 0) {
      --read_delay_;
      if (read_delay_ == 0) {
        read_phase_ = 1;
        drive_ = load16(read_address_);
      }
    } else if (read_phase_ == 1) {
      drive_ = load16(read_address_ + 2);
      read_phase_ = 2;
    } else if (read_phase_ == 2) {
      read_phase_ = 0;
      drive_ = 0;
    }

    const uint8_t command = (top.sdram_csn << 3) | (top.sdram_rasn << 2) |
                            (top.sdram_casn << 1) | top.sdram_wen;
    if (!top.sdram_cke || top.sdram_csn) return drive_;
    switch (command) {
      case 0x3:
        open_row_[top.sdram_ba & 3] = top.sdram_addr;
        break;
      case 0x5:
        read_address_ = address(top.sdram_ba, top.sdram_addr);
        read_delay_ = 2;
        read_phase_ = 0;
        break;
      case 0x4:
        write_address_ = address(top.sdram_ba, top.sdram_addr);
        store16(write_address_, top.sdram_dq_out, top.sdram_dqm);
        write_second_ = true;
        break;
      case 0x7:
        if (write_second_ && top.sdram_dq_oe) {
          store16(write_address_ + 2, top.sdram_dq_out, top.sdram_dqm);
          write_second_ = false;
        }
        break;
      default:
        write_second_ = false;
        break;
    }
    return drive_;
  }

 private:
  uint32_t address(uint8_t bank, uint16_t column) const {
    const uint32_t row = open_row_[bank & 3];
    return ((row >> 11) << 23) | (uint32_t(bank & 3) << 21) |
           ((row & 0x7ff) << 10) | (uint32_t(column & 0x3ff) << 1);
  }

  uint16_t load16(uint32_t address) const {
    if (address + 1 >= bytes_.size()) return 0;
    return uint16_t(bytes_[address]) | (uint16_t(bytes_[address + 1]) << 8);
  }

  void store16(uint32_t address, uint16_t value, uint8_t dqm) {
    if (address + 1 >= bytes_.size()) return;
    if (!(dqm & 1)) bytes_[address] = value & 0xff;
    if (!(dqm & 2)) bytes_[address + 1] = value >> 8;
  }

  std::vector<uint8_t> bytes_;
  std::array<uint16_t, 4> open_row_{};
  uint32_t read_address_ = 0, write_address_ = 0;
  int read_delay_ = 0, read_phase_ = 0;
  uint16_t drive_ = 0;
  bool write_second_ = false;
};

class UartOutput {
 public:
  void sample(bool level) {
    if (state_ == 0) {
      if (previous_ && !level) {
        state_ = 1;
        countdown_ = kUartDiv + kUartDiv / 2;
        bit_ = 0;
        value_ = 0;
      }
    } else if (--countdown_ == 0) {
      if (state_ == 1) {
        value_ |= uint8_t(level) << bit_;
        if (++bit_ == 8) state_ = 2;
        countdown_ = kUartDiv;
      } else {
        std::cout.put(static_cast<char>(value_));
        std::cout.flush();
        text_.push_back(static_cast<char>(value_));
        state_ = 0;
      }
    }
    previous_ = level;
  }

  bool contains(const std::string &needle) const {
    return text_.find(needle) != std::string::npos;
  }

  bool appears_before(const std::string &first,
                      const std::string &second) const {
    const auto first_pos = text_.find(first);
    const auto second_pos = text_.find(second);
    return first_pos != std::string::npos && second_pos != std::string::npos &&
           first_pos < second_pos;
  }

  bool section_contains(const std::string &begin, const std::string &end,
                        const std::string &needle) const {
    const auto begin_pos = text_.find(begin);
    const auto end_pos = text_.find(end, begin_pos);
    const auto needle_pos = text_.find(needle, begin_pos);
    return begin_pos != std::string::npos && end_pos != std::string::npos &&
           needle_pos != std::string::npos && needle_pos < end_pos;
  }

 private:
  bool previous_ = true;
  int state_ = 0, countdown_ = 0, bit_ = 0;
  uint8_t value_ = 0;
  std::string text_;
};

class UartInput {
 public:
  void enqueue(const std::string &text) {
    bytes_.insert(bytes_.end(), text.begin(), text.end());
  }

  bool drive() {
    if (cycles_left_ == 0) advance();
    if (cycles_left_ > 0) --cycles_left_;
    return level_;
  }

  bool empty() const { return bytes_.empty() && frame_bit_ < 0; }

 private:
  void advance() {
    if (frame_bit_ < 0) {
      if (bytes_.empty()) {
        level_ = true;
        return;
      }
      byte_ = static_cast<uint8_t>(bytes_.front());
      bytes_.pop_front();
      frame_bit_ = 0;
    } else if (++frame_bit_ == 10) {
      frame_bit_ = -1;
      level_ = true;
      cycles_left_ = kUartInterByteGap;
      return;
    }

    if (frame_bit_ == 0) level_ = false;
    else if (frame_bit_ <= 8) level_ = (byte_ >> (frame_bit_ - 1)) & 1;
    else level_ = true;
    cycles_left_ = kUartDiv;
  }

  std::deque<char> bytes_;
  uint8_t byte_ = 0;
  int frame_bit_ = -1, cycles_left_ = 0;
  bool level_ = true;
};

}  // namespace

enum class TestStage {
  kWaitForShell,
  kWaitForCommandOk,
  kWaitForUname,
  kWaitForWorkerA,
  kWaitForWorkerB,
  kWaitForProcessTable,
  kWaitForWorkers,
  kWaitForScheduler,
};

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  std::string payload_path;
  uint64_t max_cycles = 3000000000ULL;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--payload" && i + 1 < argc) payload_path = argv[++i];
    else if (arg == "--max-cycles" && i + 1 < argc)
      max_cycles = std::stoull(argv[++i]);
  }
  if (payload_path.empty()) {
    std::cerr << "usage: sim --payload fw_payload.bin [--max-cycles N]\n";
    return 2;
  }

  auto payload = read_file(payload_path);
  std::vector<uint8_t> flash(0x100008, 0xff);
  const std::array<uint8_t, 8> jump_rom = {
      0xb7, 0x02, 0x00, 0x80,  // lui t0,0x80000
      0x67, 0x80, 0x02, 0x00   // jalr zero,0(t0)
  };
  std::copy(jump_rom.begin(), jump_rom.end(), flash.begin() + 0x100000);

  Vsoc top;
  SpiFlash spi_flash(std::move(flash));
  Sdram sdram(payload);
  UartOutput uart_output;
  UartInput uart_input;
  TestStage test_stage = TestStage::kWaitForShell;

  top.clk_osc = 0;
  top.ext_resetn = 0;
  top.uart_rx = 1;
  top.gpio_in = 0;
  top.spi_sio1_so_miso1 = 1;
  top.flash_miso = 1;
  top.spi_sio1_so_miso0 = 1;
  top.sdram_dq_in = 0;
  top.eval();

  for (uint64_t cycle = 0; cycle < max_cycles && !Verilated::gotFinish(); ++cycle) {
    if (cycle == 16) top.ext_resetn = 1;
    switch (test_stage) {
      case TestStage::kWaitForShell:
        if (uart_output.contains("kianv# ")) {
          uart_input.enqueue("echo COMMAND_OK\n");
          test_stage = TestStage::kWaitForCommandOk;
          std::cerr << "\n[sim] Linux shell reached; starting interactive test\n";
        }
        break;
      case TestStage::kWaitForCommandOk:
        if (uart_output.contains("COMMAND_OK")) {
          uart_input.enqueue("uname -a\n");
          test_stage = TestStage::kWaitForUname;
        }
        break;
      case TestStage::kWaitForUname:
        if (uart_output.contains("Linux kianv ")) {
          uart_input.enqueue("/worker-a &\n");
          test_stage = TestStage::kWaitForWorkerA;
        }
        break;
      case TestStage::kWaitForWorkerA:
        if (uart_output.contains("WORKER_A_START")) {
          uart_input.enqueue("/worker-b &\n");
          test_stage = TestStage::kWaitForWorkerB;
        }
        break;
      case TestStage::kWaitForWorkerB:
        if (uart_output.contains("WORKER_B_START")) {
          uart_input.enqueue("/ps-check\n");
          test_stage = TestStage::kWaitForProcessTable;
        }
        break;
      case TestStage::kWaitForProcessTable:
        if (uart_output.section_contains("PROCESS_TABLE_BEGIN",
                                         "PROCESS_TABLE_END", "/worker-a") &&
            uart_output.section_contains("PROCESS_TABLE_BEGIN",
                                         "PROCESS_TABLE_END", "/worker-b")) {
          uart_input.enqueue("wait\n");
          test_stage = TestStage::kWaitForWorkers;
        }
        break;
      case TestStage::kWaitForWorkers:
        if (uart_output.contains("WORKER_A_DONE") &&
            uart_output.contains("WORKER_B_DONE")) {
          uart_input.enqueue("/scheduler-ok\n");
          test_stage = TestStage::kWaitForScheduler;
        }
        break;
      case TestStage::kWaitForScheduler:
        break;
    }

    top.uart_rx = uart_input.drive();
    top.sdram_dq_in = sdram.rising(top);
    top.clk_osc = 0;
    top.eval();
    top.flash_miso = spi_flash.tick(top.flash_csn, top.flash_sclk, top.flash_mosi);

    top.clk_osc = 1;
    top.eval();
    top.flash_miso = spi_flash.tick(top.flash_csn, top.flash_sclk, top.flash_mosi);
    if (cycle > 350000) uart_output.sample(top.uart_tx);

    if (cycle != 0 && cycle % 10000000 == 0) {
      std::cerr << "[sim] cycle=" << cycle << " pc=0x" << std::hex
                << top.rootp->soc__DOT__kianv_I__DOT__datapath_unit_I__DOT__PC
                << " mem=0x" << top.rootp->soc__DOT__kianv_I__DOT__cpu_mem_addr
                << " satp=0x" << top.rootp->soc__DOT__kianv_I__DOT__satp
                << " scause=0x"
                << top.rootp->soc__DOT__kianv_I__DOT__datapath_unit_I__DOT__csr_exception_handler_I__DOT__csr_unit_inst__DOT__scause
                << " sepc=0x"
                << top.rootp->soc__DOT__kianv_I__DOT__datapath_unit_I__DOT__csr_exception_handler_I__DOT__csr_unit_inst__DOT__sepc
                << " stval=0x"
                << top.rootp->soc__DOT__kianv_I__DOT__datapath_unit_I__DOT__csr_exception_handler_I__DOT__csr_unit_inst__DOT__stval
                << std::dec << "\n";
    }
    if (test_stage == TestStage::kWaitForScheduler && uart_input.empty() &&
        uart_output.contains("WORKER_A_DONE") &&
        uart_output.contains("WORKER_B_DONE") &&
        uart_output.contains("SCHEDULER_OK") &&
        uart_output.appears_before("WORKER_B_DONE", "WORKER_A_DONE")) {
      std::cerr << "\n[sim] interactive Linux scheduler test passed after "
                << cycle << " cycles\n";
      top.final();
      return 0;
    }
  }

  top.final();
  std::cerr << "\n[sim] timeout before interactive Linux test completed\n";
  return 1;
}
