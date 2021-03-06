#ifndef RBX_INSTRUCTIONS_UTIL_HPP
#define RBX_INSTRUCTIONS_UTIL_HPP

#define SPECIFIC (static_cast<SubClass*>(this))

namespace rubinius {
  template <class SubClass>
    class VisitInstructions {
    public:
      void visit(opcode code, opcode arg1, opcode arg2) { }

#define HANDLE_INST0(code, name) \
      void visit_ ## name() { SPECIFIC->visit(code, -1, -1); }

#define HANDLE_INST1(code, name) \
      void visit_ ## name(opcode arg1) { SPECIFIC->visit(code, arg1, -1); }

#define HANDLE_INST2(code, name) \
      void visit_ ## name(opcode arg1, opcode arg2) { SPECIFIC->visit(code, arg1, arg2); }

#include "vm/gen/instruction_visitors.hpp"

#undef HANDLE_INST0
#undef HANDLE_INST1
#undef HANDLE_INST2

      void at_ip(int ip) { }
      bool before(opcode op, opcode arg1 = 0, opcode arg2 = 0) { return true; }

      void drive(opcode* stream, int size, int start = 0) {
        int ip = start;
        while(ip < size) {
          SPECIFIC->at_ip(ip);

          switch(stream[ip]) {
#define HANDLE_INST0(code, name) \
          case code: \
                     if(SPECIFIC->before(stream[ip])) { \
                     SPECIFIC->visit_ ## name(); } ip += 1; break;

#define HANDLE_INST1(code, name) \
          case code: \
                     if(SPECIFIC->before(stream[ip], stream[ip + 1])) { \
                     SPECIFIC->visit_ ## name(stream[ip + 1]); } ip += 2; break;

#define HANDLE_INST2(code, name) \
          case code: \
                     if(SPECIFIC->before(stream[ip], stream[ip + 1], stream[ip + 2])) { \
                     SPECIFIC->visit_ ## name(stream[ip + 1], stream[ip + 2]); } ip += 3; break;

#include "vm/gen/instruction_visitors.hpp"

#undef HANDLE_INST0
#undef HANDLE_INST1
#undef HANDLE_INST2
          }
        }
      }

      void drive(VMMethod* vmm) {
        drive(vmm->opcodes, vmm->total);
      }
    };
}

#endif
