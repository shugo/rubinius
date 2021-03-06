#include "assembler.hpp"
#include "udis86.h"

namespace assembler_x8664 {
  struct Register {
  public:
    int code_;

  public:
    void set_code(int code) {
      code_ = code;
    }

    int code() {
      return code_;
    }

    int base_code() {
      return code_ & 0x7;
    }

    bool extended_p() {
      return code_ >  7;
    }

    bool operator==(Register& other) {
      return code() == other.code();
    }
  };

  Register rax = { 0 };
  Register rcx = { 1 };
  Register rdx = { 2 };
  Register rbx = { 3 };
  Register rsp = { 4 };
  Register rbp = { 5 };
  Register rsi = { 6 };
  Register rdi = { 7 };
  Register r8  = { 8 };
  Register r9  = { 9 };
  Register r10 = { 10 };
  Register r11 = { 11 };
  Register r12 = { 12 };
  Register r13 = { 13 };
  Register r14 = { 14 };
  Register r15 = { 15 };
  Register rip = { 16 };
  Register no_reg = { -1 };
  Register no_base = rbp;

  class AssemblerX8664 : public assembler::Assembler {
  public:
    enum ModType {
      ModAddr2Reg = 1,
      ModReg2Addr = 2,
      ModReg2Reg = 3
    };

  private:
    void rex(Register &reg, bool imm64=false) {
      if(reg.extended_p()) {
        emit(0x49);
      } else if(imm64) {
        emit(0x48);
      }
    }

    void rex_r(Register &reg) {
      if(reg.extended_p()) {
        emit(0x4c);
      }
    }

    void rex_rb(Register &reg, Register &modrm) {
      int prefix = 0x40;
      if(reg.extended_p())   prefix |= 0xc;
      if(modrm.extended_p()) prefix |= 0x9;

      if(prefix != 0x40) emit(prefix);
    }

    void emit_modrm(ModType mod, int reg, int rm) {
      emit(((int)mod << 6) | (reg & 0x7) << 3 | (rm & 0x7));
    }

  public:
    AssemblerX8664() : Assembler() { }

    class Address {
      Register& base_;
      int offset_;

    public:

      Address(Register& r, int o) : base_(r), offset_(o) { }

      Register& base() const {
        return base_;
      }

      int offset() const {
        return offset_;
      }
    };

    Address address(Register &r, int o = 0) {
      return Address(r, o);
    }

    void mov(Register &reg, uint64_t val) {
      rex(reg, true);
      emit(0xb8 | reg.base_code());
      emit_dw(val);
    }

    void mov(const Address addr, int val) {
      rex(addr.base());
      emit(0xc7);

      // the low bits of r12 are 4, which is the same as the flag to use
      // SIB. Therefore to address via r12, we have to use SIB.
      if(addr.base() == r12) {
        // 4 means use SIB, so we have to use it.
        emit_modrm(ModReg2Addr, 0, 4);
        // rsp.code() means ignore the index.
        emit(0 << 6 | rsp.code() << 3 | addr.base().base_code());
      } else {
        emit_modrm(ModReg2Addr, 0, addr.base().base_code());
      }
      emit_w(addr.offset());
      emit_w(val);
    }

    void mov(Register &dst, Register &src) {
      rex_rb(dst, src);
      emit(0x8b);
      emit_modrm(ModReg2Reg, dst.code(), src.code());
    }

    void mov(Register &dst, const Address addr) {
      rex_rb(dst, addr.base());
      emit(0x8b);
      emit_modrm(ModAddr2Reg, dst.base_code(), addr.base().base_code());
      emit(addr.offset());
    }

    void mov(const Address addr, Register &src) {
      rex_rb(src, addr.base());
      emit(0x89);
      emit_modrm(ModReg2Addr, src.base_code(), addr.base().base_code());
      emit_w(addr.offset());
    }

    void push(Register &reg) {
      rex(reg, true);
      emit(0x50 | reg.base_code());
    }

    void push(uint32_t val) {
      emit(0x68);
      emit_w(val);
    }

    void push(const Address addr) {
      rex(addr.base());
      emit(0xff);

      emit_modrm(ModReg2Addr, 6, addr.base().base_code());
      emit_w(addr.offset());
    }

    void pop(Register &reg) {
      rex(reg);
      emit(0x58 | reg.base_code());
    }

    void lea(Register &dest, Register &base, int offset) {
      rex_rb(dest, base);
      emit(0x8d);
      emit_modrm(ModReg2Addr, dest.base_code(), base.base_code());
      emit_w(offset);
    }

    // Function setup/teardown

    void leave() {
      emit(0xc9);
    }

    void ret() {
      emit(0xc3);
    }

    void sub(Register &reg, int val) {
      rex(reg);
      emit(0x81);
      emit_modrm(ModReg2Reg, 5, reg.base_code());
      emit_w(val);
    }

    void add(Register &reg, int val) {
      rex(reg);
      emit(0x81);
      emit_modrm(ModReg2Reg, 0, reg.base_code());
      emit_w(val);
    }

    void add(Register &dst, Register &src) {
      rex_rb(dst, src);
      emit(0x03);
      emit_modrm(ModReg2Reg, dst.base_code(), src.base_code());
    }

    ud_t* disassemble() {
      ud_t *ud = new ud_t();
      ud_init(ud);
      ud_set_mode(ud, 64);
      ud_set_syntax(ud, UD_SYN_ATT);
      ud_set_input_buffer(ud, (uint8_t*)buffer_, pc_ - buffer_);
      ud_disassemble(ud);
      return ud;
    }
  };
}
