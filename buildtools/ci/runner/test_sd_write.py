#!/usr/bin/env python3
"""Exercise the Dolphin patch's actual token and DMA paths with a sector buffer."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).parent / "dolphin/0001-sd2sp2-adapter.patch"
lines = SOURCE.read_text().split("+++ b/Source/Core/Core/HW/EXI/EXI_DeviceSDCard.cpp\n")[1].split("diff --git ")[0]
source = "\n".join(line[1:] for line in lines.splitlines() if line.startswith("+"))
token = source[source.index("  case State::DataWriteToken:"):source.index("  case State::DataWrite:")]
dma = source[source.index("void CEXISDCard::DMAWrite("):source.index("void CEXISDCard::DoState(")]
constants = source[source.index("constexpr u8 DATA_START_TOKEN"):source.index("CEXISDCard::CEXISDCard(")]
stub = """
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>
using u8 = uint8_t;
using u32 = uint32_t;
#define DEBUG_LOG_FMT(...)
#define WARN_LOG_FMT(...)
@CONSTANTS@
struct IEXIDevice { void DMAWrite(u32,u32) { assert(false); } };
struct System {
  std::array<u8, 516> input{};
  System& GetMemory() { return *this; }
  u8 Read_U8(u32 a) { return input.at(a); }
};
struct CEXISDCard : IEXIDevice {
  enum class State { DataWriteToken, DataWrite, Response };
  State m_state = State::DataWriteToken;
  System m_system;
  int m_data_size = 512, m_data_byte_idx = 0, m_current_sector = 0, m_response_idx = 0;
  bool m_multi_block = false, m_pending_write = false;
  std::array<u8, 514> m_data_buf{};
  std::vector<u8> m_response;
  bool WriteSector(u32,const u8*) { return true; }
  void DMAWrite(u32,u32);
  void SendToken(u8 received) { u8 byte; switch(m_state) {
"""
driver = """
int main() {
  for(bool multi : {false, true}) {
    const u8 start = multi ? MULTI_WRITE_TOKEN : DATA_START_TOKEN;
    for(int first=0; first<256; ++first) {
      for(int framing=0; framing<4; ++framing) {
        CEXISDCard sd;
        sd.m_multi_block = multi;
        unsigned offset = framing >= 2;
        if(framing == 1) {
          sd.SendToken(start);
          assert(sd.m_state == CEXISDCard::State::DataWrite);
        }
        sd.m_system.input[0] = start;
        for(unsigned i=0; i<512; ++i) sd.m_system.input[offset+i] = (u8)(first + i);
        sd.DMAWrite(0, offset + 512 + (framing == 3 ? 2 : 0));
        for(unsigned i=0; i<512; ++i) assert(sd.m_data_buf[i] == (u8)(first + i));
        assert(sd.m_data_byte_idx == (framing == 3 ? 514 : 512));
      }
    }
  }
}
"""
class SDWrite(unittest.TestCase):
    def test_all_sector_prefixes(self):
        program = stub.replace("@CONSTANTS@", constants) + token + "default: break; } } };\n" + dma + driver
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "sd-write"
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-x", "c++", "-", "-o", str(binary)],
                           input=program, text=True, check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
