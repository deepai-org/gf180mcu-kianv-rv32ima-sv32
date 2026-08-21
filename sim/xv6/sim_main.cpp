#include "Vsoc.h"
#include "Vsoc___024root.h"
#include "verilated.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

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
      miso_ = (tx_ >> 7) & 1;
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
  bool active_ = false;
  bool prev_clk_ = false;
  uint8_t bit_ = 0, rx_ = 0, tx_ = 0xff, miso_ = 1, phase_ = 0;
  uint32_t address_ = 0;
};

class SdCard {
 public:
  explicit SdCard(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}

  uint8_t tick(bool csn, bool clk, bool mosi) {
    if (csn) {
      active_ = false;
      prev_clk_ = clk;
      miso_ = 1;
      command_.clear();
      return miso_;
    }
    if (!active_) {
      active_ = true;
      bit_ = 0;
      rx_ = 0;
      tx_ = next_byte();
      miso_ = (tx_ >> 7) & 1;
    }
    if (!prev_clk_ && clk) {
      rx_ = static_cast<uint8_t>((rx_ << 1) | mosi);
      if (++bit_ == 8) {
        consume(rx_);
        rx_ = 0;
        bit_ = 0;
        tx_ = next_byte();
      }
    } else if (prev_clk_ && !clk) {
      miso_ = (tx_ >> (7 - bit_)) & 1;
    }
    prev_clk_ = clk;
    return miso_;
  }

 private:
  uint8_t next_byte() {
    if (out_.empty()) return write_busy_ ? 0x00 : 0xff;
    uint8_t value = out_.front();
    out_.pop_front();
    return value;
  }

  void consume(uint8_t value) {
    if (write_state_ != 0) {
      consume_write(value);
      return;
    }
    if (command_.empty()) {
      if ((value & 0xc0) != 0x40) return;
      command_.push_back(value);
      return;
    }
    command_.push_back(value);
    if (command_.size() != 6) return;

    const uint8_t cmd = command_[0] & 0x3f;
    const uint32_t arg = (uint32_t(command_[1]) << 24) |
                         (uint32_t(command_[2]) << 16) |
                         (uint32_t(command_[3]) << 8) | command_[4];
    command_.clear();
    switch (cmd) {
      case 0:
        initialized_ = false;
        out_.push_back(0x01);
        break;
      case 8:
        out_.insert(out_.end(), {uint8_t(initialized_ ? 0 : 1), 0x00, 0x00, 0x01, 0xaa});
        break;
      case 16:
        out_.push_back(0x00);
        break;
      case 55:
        app_command_ = true;
        out_.push_back(initialized_ ? 0x00 : 0x01);
        break;
      case 41:
        if (app_command_) initialized_ = true;
        app_command_ = false;
        out_.push_back(initialized_ ? 0x00 : 0x05);
        break;
      case 58:
        out_.insert(out_.end(), {uint8_t(initialized_ ? 0 : 1), 0x40, 0x00, 0x00, 0x00});
        break;
      case 17:
        read_block(arg);
        break;
      case 24:
        write_block_ = arg;
        write_state_ = 1;
        out_.push_back(0x00);
        break;
      default:
        out_.push_back(0x04);
        break;
    }
  }

  void read_block(uint32_t block) {
    out_.push_back(0x00);
    out_.push_back(0xff);
    out_.push_back(0xfe);
    const uint64_t base = uint64_t(block) * 512;
    for (unsigned i = 0; i < 512; ++i)
      out_.push_back(base + i < bytes_.size() ? bytes_[base + i] : 0);
    out_.push_back(0xff);
    out_.push_back(0xff);
  }

  void consume_write(uint8_t value) {
    if (write_state_ == 1) {
      if (value == 0xfe) {
        write_data_.clear();
        write_state_ = 2;
      }
      return;
    }
    if (write_state_ == 2) {
      write_data_.push_back(value);
      if (write_data_.size() == 514) {
        const uint64_t base = uint64_t(write_block_) * 512;
        if (bytes_.size() < base + 512) bytes_.resize(base + 512);
        for (unsigned i = 0; i < 512; ++i) bytes_[base + i] = write_data_[i];
        write_state_ = 0;
        out_.push_back(0x05);
        write_busy_ = false;
      }
    }
  }

  std::vector<uint8_t> bytes_;
  std::deque<uint8_t> out_;
  std::vector<uint8_t> command_, write_data_;
  bool active_ = false, prev_clk_ = false, initialized_ = false;
  bool app_command_ = false, write_busy_ = false;
  uint8_t bit_ = 0, rx_ = 0, tx_ = 0xff, miso_ = 1, write_state_ = 0;
  uint32_t write_block_ = 0;
};

class Sdram {
 public:
  static constexpr size_t kSize = 32 * 1024 * 1024;

  explicit Sdram(const std::vector<uint8_t> &kernel) : bytes_(kSize, 0) {
    if (kernel.size() > bytes_.size()) throw std::runtime_error("kernel exceeds SDRAM");
    std::copy(kernel.begin(), kernel.end(), bytes_.begin());
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
      case 0x3:  // ACTIVATE
        open_row_[top.sdram_ba & 3] = top.sdram_addr;
        break;
      case 0x5:  // READ
        read_address_ = address(top.sdram_ba, top.sdram_addr);
        read_delay_ = 2;
        read_phase_ = 0;
        break;
      case 0x4:  // WRITE, first half of BL=2
        write_address_ = address(top.sdram_ba, top.sdram_addr);
        store16(write_address_, top.sdram_dq_out, top.sdram_dqm);
        write_second_ = true;
        break;
      case 0x7:  // NOP; controller presents second write beat here
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

class Uart {
 public:
  void sample(bool level) {
    if (state_ == 0) {
      if (previous_ && !level) {
        state_ = 1;
        countdown_ = kDiv + kDiv / 2;
        bit_ = 0;
        value_ = 0;
      }
    } else if (--countdown_ == 0) {
      if (state_ == 1) {
        value_ |= uint8_t(level) << bit_;
        if (++bit_ == 8) state_ = 2;
        countdown_ = kDiv;
      } else {
        std::cout.put(static_cast<char>(value_));
        std::cout.flush();
        text_.push_back(static_cast<char>(value_));
        state_ = 0;
      }
    }
    previous_ = level;
  }

  bool booted() const {
    return text_.find("xv6 kernel is booting") != std::string::npos &&
           text_.find("init: starting sh") != std::string::npos &&
           text_.find("$ ") != std::string::npos;
  }

 private:
  static constexpr int kDiv = 174;  // 20 MHz / 115200, SIM reset default
  bool previous_ = true;
  int state_ = 0, countdown_ = 0, bit_ = 0;
  uint8_t value_ = 0;
  std::string text_;
};

}  // namespace

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  std::string kernel_path, fs_path;
  uint64_t max_cycles = 2000000000ULL;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--kernel" && i + 1 < argc) kernel_path = argv[++i];
    else if (arg == "--fs" && i + 1 < argc) fs_path = argv[++i];
    else if (arg == "--max-cycles" && i + 1 < argc) max_cycles = std::stoull(argv[++i]);
  }
  if (kernel_path.empty() || fs_path.empty()) {
    std::cerr << "usage: sim --kernel xv6.bin --fs fs.img [--max-cycles N]\n";
    return 2;
  }

  auto kernel = read_file(kernel_path);
  auto fs = read_file(fs_path);
  std::vector<uint8_t> flash(0x100008, 0xff);
  const std::array<uint8_t, 8> jump_rom = {
      0xb7, 0x02, 0x00, 0x80,  // lui t0,0x80000
      0x67, 0x80, 0x02, 0x00   // jalr zero,0(t0)
  };
  std::copy(jump_rom.begin(), jump_rom.end(), flash.begin() + 0x100000);
  std::vector<uint8_t> sd(2 * 1024 * 1024 + fs.size(), 0);
  std::copy(fs.begin(), fs.end(), sd.begin() + 2 * 1024 * 1024);

  Vsoc top;
  SpiFlash spi_flash(std::move(flash));
  SdCard sd_card(std::move(sd));
  Sdram sdram(kernel);
  Uart uart;

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

    top.sdram_dq_in = sdram.rising(top);
    top.clk_osc = 0;
    top.eval();
    top.flash_miso = spi_flash.tick(top.flash_csn, top.flash_sclk, top.flash_mosi);
    top.spi_sio1_so_miso0 = sd_card.tick(top.spi_cen0, top.spi_sclk0,
                                        top.spi_sio0_si_mosi0);

    top.clk_osc = 1;
    top.eval();
    top.flash_miso = spi_flash.tick(top.flash_csn, top.flash_sclk, top.flash_mosi);
    top.spi_sio1_so_miso0 = sd_card.tick(top.spi_cen0, top.spi_sclk0,
                                        top.spi_sio0_si_mosi0);
    // Ignore power-up/reset transients before the SoC's long reset releases.
    if (cycle > 350000) uart.sample(top.uart_tx);
    if (cycle != 0 && cycle % 10000000 == 0) {
#ifdef LOOM_IMPORTED_RTL
      std::cerr << "[sim] cycle=" << cycle << "\n";
#else
      std::cerr << "[sim] cycle=" << cycle << " pc=0x" << std::hex
                << top.rootp->soc__DOT__kianv_I__DOT__datapath_unit_I__DOT__PC
                << " mem=0x" << top.rootp->soc__DOT__kianv_I__DOT__cpu_mem_addr
                << std::dec << "\n";
#endif
    }
    if (uart.booted()) {
      std::cerr << "\n[sim] xv6 shell reached after " << cycle << " cycles\n";
      top.final();
      return 0;
    }
  }

  top.final();
  std::cerr << "\n[sim] timeout before xv6 shell\n";
  return 1;
}
