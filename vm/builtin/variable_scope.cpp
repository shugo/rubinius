#include "vm.hpp"
#include "objectmemory.hpp"
#include "call_frame.hpp"
#include "gc/gc.hpp"

#include "builtin/object.hpp"
#include "builtin/variable_scope.hpp"
#include "builtin/class.hpp"
#include "builtin/system.hpp"
#include "builtin/tuple.hpp"

namespace rubinius {
  void VariableScope::init(STATE) {
    GO(variable_scope).set(state->new_class("VariableScope", G(object), G(rubinius)));
    G(variable_scope)->set_object_type(state, VariableScopeType);
    G(variable_scope)->name(state, state->symbol("Rubinius::VariableScope"));
  }

  void VariableScope::bootstrap_methods(STATE) {
    System::attach_primitive(state,
                             G(variable_scope), false,
                             state->symbol("method_visibility"),
                             state->symbol("variable_scope_method_visibility"));
  }

  VariableScope* VariableScope::promote(STATE) {
    VariableScope* scope = state->new_struct<VariableScope>(
        G(variable_scope), number_of_locals_ * sizeof(Object*));

    scope->block(state, block_);
    scope->method(state, method_);
    scope->module(state, module_);
    if(parent_) {
      scope->parent(state, parent_);
    } else {
      scope->parent(state, (VariableScope*)Qnil);
    }
    scope->self(state, self_);

    scope->number_of_locals_ = number_of_locals_;

    for(int i = 0; i < number_of_locals_; i++) {
      scope->set_local(state, i, locals_[i]);
    }

    return scope;
  }

  void VariableScope::setup_as_block(VariableScope* top, VariableScope* parent, CompiledMethod* cm, int num, Object* self) {
    init_header(UnspecifiedZone, InvalidType);

    parent_ = parent;
    self_ = self;

    method_ = cm;
    module_ = top->module();
    block_ =  top->block();
    number_of_locals_ = num;

    for(int i = 0; i < num; i++) {
      locals_[i] = Qnil;
    }
  }

  VariableScope* VariableScope::of_sender(STATE, CallFrame* call_frame) {
    CallFrame* dest = static_cast<CallFrame*>(call_frame->previous);
    return dest->promote_scope(state);
  }

  VariableScope* VariableScope::current(STATE, CallFrame* call_frame) {
    return call_frame->promote_scope(state);
  }

  Tuple* VariableScope::locals(STATE) {
    Tuple* tup = Tuple::create(state, number_of_locals_);
    for(int i = 0; i < number_of_locals_; i++) {
      tup->put(state, i, locals_[i]);
    }

    return tup;
  }

  // bootstrap method, replaced with an attr_accessor in kernel.
  Object* VariableScope::method_visibility(STATE) {
    return Qnil;
  }

  size_t VariableScope::Info::object_size(const ObjectHeader* obj) {
    const VariableScope* scope = reinterpret_cast<const VariableScope*>(obj);

    return sizeof(VariableScope) + (scope->number_of_locals_ * sizeof(Object*));
  }

  void VariableScope::Info::mark(Object* obj, ObjectMark& mark) {
    auto_mark(obj, mark);

    VariableScope* vs = as<VariableScope>(obj);

    vs->fixup();


    Object* tmp;

    size_t locals = vs->number_of_locals();
    for(size_t i = 0; i < locals; i++) {
      tmp = mark.call(vs->get_local(i));
      if(tmp) vs->set_local(mark.state(), i, tmp);
    }
  }

  void VariableScope::Info::visit(Object* obj, ObjectVisitor& visit) {
    auto_visit(obj, visit);

    VariableScope* vs = as<VariableScope>(obj);

    size_t locals = vs->number_of_locals();
    for(size_t i = 0; i < locals; i++) {
      visit.call(vs->get_local(i));
    }
  }


}
