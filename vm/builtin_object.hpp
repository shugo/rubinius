#ifndef RBX_VM_BUILTIN_OBJECT_HPP
#define RBX_VM_BUILTIN_OBJECT_HPP

#include "oop.hpp"

namespace rubinius {
  class MetaClass;
  class Integer;

  class Object : public ObjectHeader {
  public:

    // Ruby.primitive :object_equal
    OBJECT equal(STATE, OBJECT other);

    // Ruby.primitive :object_show
    OBJECT show(STATE);

    /* body access */
    union {
      OBJECT field[];
      uint8_t bytes[];
    };

    /* WARNING. Do not use this version if +num+ has the chance of being
     * greater than FIXNUM_MAX. */
    static FIXNUM i2n(native_int num);

    static Integer* i2n(STATE, native_int num);
    static Integer* ui2n(STATE, unsigned int num);
    static Integer* ll2n(STATE, long long num);
    static Integer* ull2n(STATE, unsigned long long num);

    bool fixnum_p();
    bool symbol_p();

    /* Initialize the objects data with the most basic info. This is done
     * right after an object is created. */
    void init(gc_zone loc, size_t fields);

    /* Clear the body of the object, by setting each field to Qnil */
    void clear_fields();

    /* Initialize the object as storing bytes, by setting the flag then
     * clearing the body of the object, by setting the entire body as bytes to
     * 0 */
    void init_bytes();

    size_t size_in_bytes();
    size_t body_in_bytes();

    bool reference_p();
    bool stores_bytes_p();
    bool stores_references_p();
    bool young_object_p();
    bool mature_object_p();
    bool forwarded_p();

    void set_forward(OBJECT fwd);
    OBJECT forward();

    bool marked_p();
    void mark();
    void clear_mark();

    bool nil_p();
    bool undef_p();
    bool true_p();
    bool false_p();

    bool has_ivars_p();

    bool check_type(object_type type);

    OBJECT get_field(STATE, size_t index);
    void   set_field(STATE, size_t index, OBJECT val);
    void cleanup(STATE);

    bool kind_of_p(STATE, OBJECT cls);
    Class* lookup_begin(STATE);
    Class* class_object(STATE);
    OBJECT dup(STATE);
    hashval hash(STATE);
    uintptr_t id(STATE);
    Class* metaclass(STATE);

    OBJECT get_ivar(STATE, OBJECT sym);
    OBJECT set_ivar(STATE, OBJECT sym, OBJECT val);

    void copy_flags(STATE, OBJECT other);
    void copy_ivars(STATE, OBJECT other);
    void copy_metaclass(STATE, OBJECT other);

    static const char* type_to_name(object_type type);
  };

}

#endif