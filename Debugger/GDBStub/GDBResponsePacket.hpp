//
// Created by Spencer Michaels on 9/5/18.
//

#ifndef XENDBG_GDBRESPONSEPACKET_HPP
#define XENDBG_GDBRESPONSEPACKET_HPP

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "GDBRegisters.hpp"
#include "../../Util/overloaded.hpp"

namespace xd::dbg::gdbstub::pkt {

  namespace {
    // Writes the bytes of a value of arbitrary size in guest order
    template <typename Value_t>
    void write_bytes(std::stringstream &ss, Value_t value) {
      unsigned char *p = (unsigned char*)&value;
      unsigned char *end = p + sizeof(Value_t);

      ss << std::hex << std::setfill('0');
      while (p != end)
        ss << std::setw(2) << (unsigned)(*p++);
    }

    template <typename Key_t, typename Value_t>
    void add_list_entry(std::stringstream &ss, Key_t key, Value_t value) {
      ss << key;
      ss << ":";
      ss << value;
      ss << ";";
    }
  }

  class GDBResponsePacket {
  public:
    virtual ~GDBResponsePacket() = default;
    virtual std::string to_string() const = 0;
  };

  class OKResponse : public GDBResponsePacket {
  public:
    std::string to_string() const override { return "OK"; };
  };

  class NotSupportedResponse : public GDBResponsePacket {
  public:
    std::string to_string() const override { return ""; };
  };

  class ErrorResponse : public GDBResponsePacket {
  public:
    ErrorResponse(uint8_t error_code)
      : _error_code(error_code) {};

    std::string to_string() const override {
      std::stringstream ss;
      ss << "E" << std::hex << std::setfill('0') << std::setw(2) << (unsigned)_error_code;
      return ss.str();
    };

  private:
    uint8_t _error_code;
  };

  class QuerySupportedResponse : public GDBResponsePacket {
  public:
    QuerySupportedResponse(std::vector<std::string> features)
      : _features(features) {};

    std::string to_string() const override {
      if (_features.empty())
        return "";

      std::stringstream ss;
      ss << _features.front();
      std::for_each(_features.begin()+1, _features.end(),
        [&ss](const auto& feature) {
          ss << ";" << feature;
        }
      );
      return ss.str();
    }

  private:
    std::vector<std::string> _features;
  };

  // NOTE: thread ID 0 = any thread, ID -1 = all threads
  // so these have to be zero-indexed.
  class QueryCurrentThreadIDResponse : public GDBResponsePacket {
  public:
    QueryCurrentThreadIDResponse(size_t thread_id)
      : _thread_id(thread_id) {}

    std::string to_string() const override {
      std::stringstream ss;
      ss << "QC";
      if (_thread_id == (size_t)-1) {
        ss << "-1";
      } else {
        ss << std::hex;
        ss << _thread_id;
      }
      return ss.str();
    }

  private:
    size_t _thread_id;
  };

  class QueryThreadInfoResponse : public GDBResponsePacket {
  public:
    QueryThreadInfoResponse(std::vector<size_t> thread_ids)
      : _thread_ids(thread_ids)
    {
      if (thread_ids.empty())
        throw std::runtime_error("Must provide at least one thread ID!");
    };

    std::string to_string() const override {
      std::stringstream ss;

      ss << "m";
      ss << std::hex;
      ss << _thread_ids.front();
      std::for_each(_thread_ids.begin()+1, _thread_ids.end(),
        [&ss](const auto& tid) {
          ss << "," << tid;
        });
      ss << "l";

      return ss.str();
    };

  private:
    const std::vector<size_t> _thread_ids;
  };

  class QueryThreadInfoEndResponse : public GDBResponsePacket {
  public:
    std::string to_string() const override {
      return "l";
    };
  };

  class RegisterReadResponse : public GDBResponsePacket {
  public:
    RegisterReadResponse(uint64_t value, int width = sizeof(uint64_t))
      : _value(value), _width(width) {};

    std::string to_string() const override {
      std::stringstream ss;
      write_bytes(ss, _value);
      return ss.str();
    };

  private:
    uint64_t _value;
    int _width;
  };

  class GeneralRegistersBatchReadResponse : public GDBResponsePacket {
  public:
    GeneralRegistersBatchReadResponse(GDBRegisters registers)
      : _registers(registers) {}

  std::string to_string() const override {
    std::stringstream ss;

    ss << std::hex << std::setfill('0');
    std::visit(util::overloaded {
      [this, &ss](const GDBRegisters64& regs) {
        write_register(ss, regs.values.rax);
        write_register(ss, regs.values.rbx);
        write_register(ss, regs.values.rcx);
        write_register(ss, regs.values.rdx);
        write_register(ss, regs.values.rsi);
        write_register(ss, regs.values.rdi);
        write_register(ss, regs.values.rbp);
        write_register(ss, regs.values.rsp);

        write_register(ss, regs.values.r8);
        write_register(ss, regs.values.r9);
        write_register(ss, regs.values.r10);
        write_register(ss, regs.values.r11);
        write_register(ss, regs.values.r12);
        write_register(ss, regs.values.r13);
        write_register(ss, regs.values.r14);
        write_register(ss, regs.values.r15);

        write_register(ss, regs.values.rip);

        // GDB wants this to be 32-bit, for some reason...
        // Likely because the upper 32 bytes aren't used
        // TODO: only do this for GDB --- maybe LLDB accepts 64-bit rflags?
        uint32_t eflags = regs.values.rflags & 0xFFFFFFFF;
        write_register(ss, eflags);

        write_register(ss, regs.values.cs);
        write_register(ss, regs.values.ss);
        write_register(ss, regs.values.ds);
        write_register(ss, regs.values.es);
        write_register(ss, regs.values.fs);
        write_register(ss, regs.values.gs);
      },
      [this, &ss](const GDBRegisters32& regs) {
        write_register(ss, regs.values.eax);
        write_register(ss, regs.values.ecx);
        write_register(ss, regs.values.edx);
        write_register(ss, regs.values.ebx);
        write_register(ss, regs.values.esp);
        write_register(ss, regs.values.ebp);
        write_register(ss, regs.values.esi);
        write_register(ss, regs.values.edi);

        write_register(ss, regs.values.eip);

        write_register(ss, regs.values.eflags);

        write_register(ss, regs.values.cs);
        write_register(ss, regs.values.ss);
        write_register(ss, regs.values.ds);
        write_register(ss, regs.values.es);
        write_register(ss, regs.values.fs);
        write_register(ss, regs.values.gs);
      },
    }, _registers);

    return ss.str();
  }

  template <typename Reg_t>
  void write_register(std::stringstream &ss, const Reg_t&reg) const {
    ss << std::setw(2*sizeof(Reg_t)) << reg;
  }

  private:
      GDBRegisters _registers;
  };

  class MemoryReadResponse : public GDBResponsePacket {
  public:
    MemoryReadResponse(const char * const data, size_t length)
      : _data(data, data + length) {};

    std::string to_string() const override {
      std::stringstream ss;

      ss << std::hex << std::setfill('0');
      std::for_each(_data.begin(), _data.end(),
        [&ss](const auto &ch) {
          ss << std::setw(2) << ch;
        });

      return ss.str();
    };

  private:
    std::vector<char> _data;
  };

  class StopReasonSignalResponse : public GDBResponsePacket {
  public:
    StopReasonSignalResponse(uint8_t signal)
      : _signal(signal) {};

    std::string to_string() const override {
      std::stringstream ss;
      ss << "T "; // NOTE: requires a space IFF working in ACK mode
      ss << std::hex << std::setfill('0') << std::setw(2);
      ss << (unsigned)_signal;
      // ss << "thread:1"; // TODO
      return ss.str();
    };

  private:
    uint8_t _signal;
  };

  // See https://github.com/llvm-mirror/lldb/blob/master/docs/lldb-gdb-remote.txt#L756
  class QueryHostInfoResponse : public GDBResponsePacket {
  public:
    QueryHostInfoResponse(unsigned word_size, std::string hostname)
      : _word_size(word_size), _hostname(std::move(hostname)) 
    {};

    std::string to_string() const override {
      std::stringstream ss;
      add_list_entry(ss, "ostype", "linux");   // TODO
      add_list_entry(ss, "endian", "little");     // TODO
      add_list_entry(ss, "ptrsize", _word_size);
      add_list_entry(ss, "hostname", _hostname);
      return ss.str();
    };

  private:
    unsigned _word_size;
    std::string _hostname;
  };

  class QueryProcessInfoResponse : public GDBResponsePacket {
  public:
    QueryProcessInfoResponse(size_t pid)
      : _pid(pid) {};

    std::string to_string() const override {
      std::stringstream ss;
      add_list_entry(ss, "pid", _pid);
      return ss.str();
    };

  private:
    size_t _pid;
  };

  class QueryRegisterInfoResponse : public GDBResponsePacket {
  public:
    QueryRegisterInfoResponse(
        std::string name, unsigned width, unsigned offset,
          unsigned gcc_register_id)
      : _name(std::move(name)), _width(width), _offset(offset),
        _gcc_register_id(gcc_register_id)
    {};

    std::string to_string() const override {
      std::stringstream ss;
      add_list_entry(ss, "name", _name);
      add_list_entry(ss, "bitsize", _width);
      add_list_entry(ss, "offset", _offset);
      add_list_entry(ss, "encoding", "uint");
      add_list_entry(ss, "format", "hex");
      add_list_entry(ss, "set", "General Purpose Registers");
      add_list_entry(ss, "gcc", _gcc_register_id);
      add_list_entry(ss, "dwarf", _gcc_register_id); // TODO
      return ss.str();
    };

  private:
    std::string _name;
    unsigned _width;
    unsigned _offset;
    unsigned _gcc_register_id;
  };

}

#endif //XENDBG_GDBRESPONSEPACKET_HPP