//
// Created by Spencer Michaels on 9/20/18.
//

#define X86_MAX_INSTRUCTION_SIZE 0x10

#include <Debugger/Debugger.hpp>

#include <sys/mman.h>

using xd::dbg::Debugger;
using xd::xen::Address;
using xd::xen::DomID;

Debugger::Debugger(uvw::Loop &loop, xen::Domain &domain)
  : _domain(domain), _timer(loop.resource<uvw::TimerHandle>()), _vcpu_id(0)
{
  const auto mode =
      (_domain.get_word_size() == sizeof(uint64_t)) ? CS_MODE_64 : CS_MODE_32;

  if (cs_open(CS_ARCH_X86, mode, &_capstone) != CS_ERR_OK)
    throw CapstoneException("Failed to open Capstone handle!");

  cs_option(_capstone, CS_OPT_DETAIL, CS_OPT_ON);
}

Debugger::~Debugger() {
  cs_close(&_capstone);
}

void Debugger::attach() {
  _domain.pause();
}

void Debugger::detach() {
  cleanup();
  _domain.unpause();
}

void Debugger::notify_breakpoint_hit(OnBreakpointHitFn on_breakpoint_hit) {
  _timer->data(shared_from_this()); // TODO
  _timer->on<uvw::TimerEvent>([on_breakpoint_hit](const auto &event, auto &handle) {
    auto self = handle.template data<Debugger>();
    auto address = self->check_breakpoint_hit();
    if (address) {
      handle.stop();
      on_breakpoint_hit(*address);
    }
    return address.has_value();
  });

  // TODO: is this 100 ms?
  _timer->start(uvw::TimerHandle::Time(100), uvw::TimerHandle::Time(100));
}

std::pair<Address, std::optional<Address>>
Debugger::get_address_of_next_instruction() {
  const auto read_word = [this](Address addr) {
    const auto mem_handle = _domain.map_memory<uint64_t>(addr, sizeof(uint64_t), PROT_READ);
    if (_domain.get_word_size() == sizeof(uint64_t)) {
      return *mem_handle;
    } else {
      return (uint64_t)(*((uint32_t*)mem_handle.get()));
    }
  };

  // TODO: need functionality to get register by name
  const auto read_reg_cs  = [this](const auto &regs_any, auto cs_reg)
  {
    const auto name = cs_reg_name(_capstone, cs_reg);
    return std::visit(util::overloaded {
      [&](const auto &regs) {
        const auto id = 0;// decltype(regs)::get_id_by_name(name);

        uint64_t value;
        regs.find_by_id(id, [&](const auto&, const auto &reg) {
          value = reg;
        }, [&] {
          throw std::runtime_error(std::string("No such register: ") + name);
        });

        return value;
      }
    }, regs_any);

  };

  const auto context = _domain.get_cpu_context();
  const auto address = reg::read_register<reg::x86_32::eip, reg::x86_64::rip>(context);
  const auto read_size = (2*X86_MAX_INSTRUCTION_SIZE);
  const auto mem_handle = _domain.map_memory<uint8_t>(address, read_size, PROT_READ);

  cs_insn *instrs;
	size_t instrs_size;

  instrs_size = cs_disasm(_capstone, mem_handle.get(), read_size-1, address, 0, &instrs);

  if (instrs_size < 2)
    throw CapstoneException("Failed to read instructions!");

  auto cur_instr = instrs[0];
  const auto next_instr_address = instrs[1].address;

  // JMP and CALL
  if (cs_insn_group(_capstone, &cur_instr, X86_GRP_JUMP) ||
      cs_insn_group(_capstone, &cur_instr, X86_GRP_CALL))
  {
    const auto x86 = cur_instr.detail->x86;
    assert(x86.op_count != 0);
    const auto op = x86.operands[0];

    if (op.type == X86_OP_IMM) {
      const auto dest = op.imm;
      return std::make_pair(next_instr_address, dest);
    } else if (op.type == X86_OP_MEM) {
      const auto base = op.mem.base ? read_reg_cs(context, op.mem.base) : 0;
      const auto index = op.mem.index ? read_reg_cs(context, op.mem.index) : 0;
      const auto addr = base + (op.mem.scale * index) + op.mem.disp;

      uint64_t dest;
      if (_domain.get_word_size() == sizeof(uint64_t))
        dest = *_domain.map_memory<uint64_t>(addr, sizeof(uint64_t), PROT_READ);
      else
        dest = *_domain.map_memory<uint32_t>(addr, sizeof(uint32_t), PROT_READ);

      return std::make_pair(dest, std::nullopt);
    } else if (op.type == X86_OP_REG) {
      const auto reg_value = read_reg_cs(context, op.reg);
      return std::make_pair(reg_value, std::nullopt);
    } else {
      throw std::runtime_error("Invalid JMP/CALL operand type!");
    }
  }

  // RET
  else if (cs_insn_group(_capstone, &cur_instr, X86_GRP_RET) ||
             cs_insn_group(_capstone, &cur_instr, X86_GRP_IRET))
  {
    const auto stack_ptr = reg::read_register<reg::x86_32::esp, reg::x86_64::rsp>(_domain.get_cpu_context());
    const auto ret_dest = read_word(stack_ptr);
    return std::make_pair(ret_dest, std::nullopt);
  }

  // Any other instructions
  else {
    return std::make_pair(next_instr_address, std::nullopt);
  }
}
