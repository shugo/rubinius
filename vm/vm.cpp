#include "vm.hpp"
#include "objectmemory.hpp"
#include "event.hpp"
#include "global_cache.hpp"
#include "gc/gc.hpp"

#include "vm/object_utils.hpp"

#include "builtin/class.hpp"
#include "builtin/fixnum.hpp"
#include "builtin/list.hpp"
#include "builtin/lookuptable.hpp"
#include "builtin/symbol.hpp"
#include "builtin/thread.hpp"
#include "builtin/tuple.hpp"
#include "builtin/string.hpp"
#include "builtin/taskprobe.hpp"
#include "builtin/system.hpp"

#include "instruments/profiler.hpp"

#include "config_parser.hpp"
#include "config.h"

#include "native_thread.hpp"
#include "call_frame.hpp"
#include "signal.hpp"
#include "configuration.hpp"

#include <iostream>
#include <iomanip>
#include <signal.h>
#include <sys/resource.h>

// Reset macros since we're inside state
#undef G
#undef GO
#define G(whatever) globals.whatever.get()
#define GO(whatever) globals.whatever

namespace rubinius {

  bool GlobalLock::debug_locking = false;
  int VM::cStackDepthMax = 655300;

  VM::VM(SharedState& shared)
    : saved_call_frame_(0)
    , alive_(true)
    , profiler_(0)
    , run_signals_(false)
    , shared(shared)
    , waiter_(NULL)
    , globals(shared.globals)
    , om(shared.om)
    , global_cache(shared.global_cache)
    , interrupts(shared.interrupts)
    , symbols(shared.symbols)
    , check_local_interrupts(false)
    , thread_state_(this)
    , thread(this, (Thread*)Qnil)
    , current_mark(NULL)
    , reuse_llvm(true)
  {
    probe.set(Qnil, &globals.roots);
  }


  void VM::discard(VM* vm) {
    vm->alive_ = false;
    vm->saved_call_frame_ = 0;
    if(vm->profiler_) {
      vm->shared.remove_profiler(vm, vm->profiler_);
    }

    vm->shared.remove_vm(vm);
    delete vm;
  }

  void VM::initialize() {
    VM::register_state(this);

    om = new ObjectMemory(this, shared.config);
    shared.om = om;

    global_cache = new GlobalCache;
    shared.global_cache = global_cache;

    /** @todo Done by Environment::boot_vm(), and Thread::s_new()
     *        does not boot at all. Should this be removed? --rue */
//    this->boot();

    shared.set_initialized();

    // This seems like we should do this in VM(), ie, for every VM and
    // therefore every Thread object in the process. But in fact, because
    // we're using the GIL atm, we only do it once. When the GIL goes
    // away, this needs to be moved to VM().

    shared.gc_dependent();
  }

  void VM::boot() {
    TypeInfo::auto_learn_fields(this);

    bootstrap_ontology();

    /* @todo Using a single default loop, revisit when many loops.
     * @todo This needs to be handled through the environment.
     * (disabled epoll backend as it frequently caused hangs on epoll_wait)
     */
    // signal_events = new event::Loop(EVFLAG_FORKCHECK | EVBACKEND_SELECT | EVBACKEND_POLL);
    // events = signal_events;

    // signal_events->start(new event::Child::Event(this));

    events = 0;
    signal_events = 0;

    VMMethod::init(this);

    /** @todo Should a thread be starting a VM or is it the other way around? */
    boot_threads();

    // Force these back to false because creating the default Thread
    // sets them to true.
    interrupts.enable_preempt = false;

    GlobalLock::debug_locking = shared.config.gil_debug;
  }

  // HACK so not thread safe or anything!
  static VM* __state = NULL;

  VM* VM::current_state() {
    return __state;
  }

  void VM::register_state(VM *vm) {
    __state = vm;
  }

  void VM::boot_threads() {
    thread.set(Thread::create(this, this, pthread_self()), &globals.roots);
    thread->sleep(this, Qfalse);
  }

  Object* VM::new_object_typed(Class* cls, size_t bytes, object_type type) {
    return om->new_object_typed(cls, bytes, type);
  }

  Object* VM::new_object_typed_mature(Class* cls, size_t bytes, object_type type) {
    return om->new_object_typed_mature(cls, bytes, type);
  }

  Object* VM::new_object_from_type(Class* cls, TypeInfo* ti) {
    return om->new_object_typed(cls, ti->instance_size, ti->type);
  }

  Class* VM::new_basic_class(Class* sup) {
    Class *cls = om->new_object_enduring<Class>(G(klass));
    cls->set_class_id(shared.inc_class_count());
    cls->set_packed_size(0);

    if(sup->nil_p()) {
      cls->instance_type(this, Fixnum::from(ObjectType));
      cls->set_type_info(find_type(ObjectType));
    } else {
      cls->instance_type(this, sup->instance_type()); // HACK test that this is always true
      cls->set_type_info(sup->type_info());
    }
    cls->superclass(this, sup);

    return cls;
  }

  Class* VM::new_class(const char* name) {
    return new_class(name, G(object), G(object));
  }

  Class* VM::new_class(const char* name, Class* super_class) {
    return new_class(name, super_class, G(object));
  }

  Class* VM::new_class(const char* name, Class* sup, Module* under) {
    Class* cls = new_basic_class(sup);
    cls->setup(this, name, under);

    // HACK test that we've got the MOP setup properly
    MetaClass::attach(this, cls, sup->metaclass(this));
    return cls;
  }

  Class* VM::new_class_under(const char* name, Module* under) {
    return new_class(name, G(object), under);
  }

  Module* VM::new_module(const char* name, Module* under) {
    Module *mod = new_object<Module>(G(module));
    mod->setup(this, name, under);
    return mod;
  }


  Symbol* VM::symbol(const char* str) {
    return symbols.lookup(this, str);
  }

  Symbol* VM::symbol(String* str) {
    return symbols.lookup(this, str);
  }

  void type_assert(STATE, Object* obj, object_type type, const char* reason) {
    if((obj->reference_p() && obj->type_id() != type)
        || (type == FixnumType && !obj->fixnum_p())) {
      Exception::type_error(state, type, obj, reason);
    }
  }

  void VM::raise_stack_error(CallFrame* call_frame) {
    G(stack_error)->locations(this, System::vm_backtrace(this, Fixnum::from(0), call_frame));
    thread_state()->raise_exception(G(stack_error));
  }

  void VM::init_stack_size() {
    struct rlimit rlim;
    if (getrlimit(RLIMIT_STACK, &rlim) == 0) {
      unsigned int space = rlim.rlim_cur/5;

      if (space > 1024*1024) space = 1024*1024;
      cStackDepthMax = (rlim.rlim_cur - space);
    }
  }

  TypeInfo* VM::find_type(int type) {
    return om->type_info[type];
  }

  Thread *VM::current_thread() {
    return globals.current_thread.get();
  }

  void VM::run_gc_soon() {
    om->collect_young_now = true;
    om->collect_mature_now = true;
    interrupts.set_perform_gc();
  }

  void VM::collect(CallFrame* call_frame) {
    this->set_call_frame(call_frame);

    // Stops all other threads, so we're only here by ourselves.
    StopTheWorld guard(this);

    GCData gc_data(this);

    om->collect_young(gc_data);
    om->collect_mature(gc_data);
  }

  void VM::collect_maybe(CallFrame* call_frame) {
    this->set_call_frame(call_frame);

    // Stops all other threads, so we're only here by ourselves.
    StopTheWorld guard(this);

    GCData gc_data(this);

    uint64_t start_time = 0;

    if(om->collect_young_now) {
      if(shared.config.gc_show) {
        start_time = get_current_time();
      }

      YoungCollectStats stats;

#ifdef RBX_PROFILER
      if(unlikely(shared.profiling())) {
        profiler::MethodEntry method(this, profiler::kYoungGC);
        om->collect_young(gc_data, &stats);
      } else {
        om->collect_young(gc_data, &stats);
      }
#else
      om->collect_young(gc_data, &stats);
#endif

      if(shared.config.gc_show) {
        uint64_t fin_time = get_current_time();
        int diff = (fin_time - start_time) / 1000000;

        fprintf(stderr, "[GC %0.1f%% %d/%d %d %2dms]\n",
                  stats.percentage_used,
                  stats.promoted_objects,
                  stats.excess_objects,
                  stats.lifetime,
                  diff);
      }
    }

    if(om->collect_mature_now) {
      int before_kb = 0;

      if(shared.config.gc_show) {
        start_time = get_current_time();
        before_kb = om->mature_bytes_allocated() / 1024;
      }

#ifdef RBX_PROFILER
      if(unlikely(shared.profiling())) {
        profiler::MethodEntry method(this, profiler::kMatureGC);
        om->collect_mature(gc_data);
      } else {
        om->collect_mature(gc_data);
      }
#else
      om->collect_mature(gc_data);
#endif

      if(shared.config.gc_show) {
        uint64_t fin_time = get_current_time();
        int diff = (fin_time - start_time) / 1000000;
        int kb = om->mature_bytes_allocated() / 1024;
        fprintf(stderr, "[Full GC %dkB => %dkB %2dms]\n",
            before_kb,
            kb,
            diff);
      }

    }
  }

  void VM::set_const(const char* name, Object* val) {
    globals.object->set_const(this, (char*)name, val);
  }

  void VM::set_const(Module* mod, const char* name, Object* val) {
    mod->set_const(this, (char*)name, val);
  }

  void VM::print_backtrace() {
    abort();
  }

  // Trampoline to call scheduler_loop()
  static void* __thread_tramp__(void* arg) {
    VM* state = static_cast<VM*>(arg);
    state->scheduler_loop();
    return NULL;
  }

  // Create the preemption thread and call scheduler_loop() in the new thread
  void VM::setup_preemption() {
    if(pthread_create(&preemption_thread, NULL, __thread_tramp__, this) != 0) {
      std::cout << "Unable to create preemption thread!\n";
    }
  }

  // Runs forever, telling the VM to reschedule threads ever 10 milliseconds
  void VM::scheduler_loop() {
    // First off, we don't want this thread ever receiving a signal.
    sigset_t mask;
    sigfillset(&mask);
    if(pthread_sigmask(SIG_SETMASK, &mask, NULL) != 0) {
      abort();
    }

    struct timespec requested;
    struct timespec actual;

    requested.tv_sec = 0;
    requested.tv_nsec = 10000000; // 10 milliseconds

    for(;;) {
      nanosleep(&requested, &actual);
      if(interrupts.enable_preempt) {
        interrupts.set_timer();
      }
    }
  }

  void VM::install_waiter(Waiter& waiter) {
    waiter_ = &waiter;
  }

  bool VM::wakeup() {
    if(waiter_) {
      waiter_->run();
      waiter_ = NULL;
      return true;
    }

    return false;
  }

  void VM::clear_waiter() {
    waiter_ = NULL;
  }

  void VM::send_async_signal(int sig) {
    mailbox_.add(ASyncMessage(ASyncMessage::cSignal, sig));
    check_local_interrupts = true;

    // TODO I'm worried there might be a race calling
    // wakeup without the lock held...
    wakeup();
  }

  bool VM::process_async(CallFrame* call_frame) {
    check_local_interrupts = false;

    if(run_signals_) {
      shared.signal_handler()->deliver_signals(call_frame);
    }

    if(thread_state_.raise_reason() != cNone) return false;

    return true;
  }

  void VM::register_raise(Exception* exc) {
    thread_state_.raise_exception(exc);
    check_local_interrupts = true;
  }

  void VM::check_exception(CallFrame* call_frame) {
    if(thread_state()->raise_reason() == cNone) {
      std::cout << "Exception propogating, but none registered!\n";
      call_frame->print_backtrace(this);
      rubinius::abort();
    }
  }

  profiler::Profiler* VM::profiler() {
    if(unlikely(!profiler_)) {
      profiler_ = new profiler::Profiler(this);
      shared.add_profiler(this, profiler_);
    }

    return profiler_;
  }

  void VM::remove_profiler() {
    profiler_ = 0;
  }
};
