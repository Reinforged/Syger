#ifndef SYGER_NATIVE_RUNTIME_H
#define SYGER_NATIVE_RUNTIME_H

/*
 * Syger native runtime.
 *
 * Values are 16 bytes: a type tag plus an integer, boolean or object pointer.
 * Strings and arrays live on the heap and are reference counted. Syger has
 * value semantics for arrays (assigning or passing an array behaves like a
 * copy), which is implemented with copy-on-write: copying a value only bumps
 * a reference count, and an array is cloned only when it is mutated while
 * shared. Because a shared array is always cloned before mutation, reference
 * cycles cannot form and plain reference counting frees everything.
 *
 * Ownership convention used by generated code: every expression produces an
 * owned value, and every function that receives a SygerValue by value
 * consumes it (releases it or stores it). Pointers (SygerValue *) are
 * borrowed lvalues used for assignment and in-place mutation.
 */

#include <stddef.h>

typedef enum {
    SYGER_UNDEFINED = 0,
    SYGER_NONE,
    SYGER_INT,
    SYGER_BOOL,
    SYGER_STRING,
    SYGER_ARRAY
} SygerType;

typedef struct SygerString SygerString;
typedef struct SygerArray SygerArray;

typedef struct SygerValue {
    SygerType type;
    union {
        long integer;
        int boolean;
        SygerString *string;
        SygerArray *array;
        long *refcount;
    } as;
} SygerValue;

struct SygerString {
    long refcount; /* negative: immortal (string literals) */
    long length;
    long capacity;
    char data[];
};

struct SygerArray {
    long refcount;
    long count;
    long capacity;
    SygerValue *items;
};

/* ---- errors -------------------------------------------------------------- */

_Noreturn void syger_fail(const char *message);
_Noreturn void syger_failf(const char *format, ...);

/* ---- program lifetime ---------------------------------------------------- */

void syger_runtime_set_arguments(int argc, char **argv);
void syger_runtime_shutdown(void);

/* ---- constructors -------------------------------------------------------- */

static inline SygerValue syger_int(long value)
{
    SygerValue v; v.type = SYGER_INT; v.as.integer = value; return v;
}

static inline SygerValue syger_bool(int value)
{
    SygerValue v; v.type = SYGER_BOOL; v.as.integer = 0; v.as.boolean = value != 0; return v;
}

static inline SygerValue syger_none(void)
{
    SygerValue v; v.type = SYGER_NONE; v.as.integer = 0; return v;
}

static inline SygerValue syger_undefined(void)
{
    SygerValue v; v.type = SYGER_UNDEFINED; v.as.integer = 0; return v;
}

SygerValue syger_string_n(const char *text, long length);
SygerValue syger_string(const char *text);
SygerValue syger_string_literal(const char *text, long length);
SygerValue syger_array_new(long count, SygerValue *items);

/* ---- reference counting -------------------------------------------------- */

void syger_free_object(SygerValue value);

static inline SygerValue syger_retain(SygerValue value)
{
    if (value.type >= SYGER_STRING && *value.as.refcount >= 0) (*value.as.refcount)++;
    return value;
}

static inline void syger_release(SygerValue value)
{
    if (value.type >= SYGER_STRING && *value.as.refcount > 0 && --(*value.as.refcount) == 0) {
        syger_free_object(value);
    }
}

/* Replace *slot with value (consumed), releasing the previous contents. */
static inline void syger_set(SygerValue *slot, SygerValue value)
{
    SygerValue old = *slot;
    *slot = value;
    syger_release(old);
}

/* ---- variables ----------------------------------------------------------- */

_Noreturn void syger_undefined_variable(const char *name);

static inline SygerValue *syger_global_ptr(SygerValue *slot, const char *name)
{
    if (__builtin_expect(slot->type == SYGER_UNDEFINED, 0)) syger_undefined_variable(name);
    return slot;
}

static inline SygerValue syger_global_get(SygerValue *slot, const char *name)
{
    return syger_retain(*syger_global_ptr(slot, name));
}

/* ---- operators ----------------------------------------------------------- */

SygerValue syger_add_slow(SygerValue left, SygerValue right);
_Noreturn void syger_arith_error(SygerValue left, SygerValue right);
int syger_equal(const SygerValue *left, const SygerValue *right);

#define SYGER_BOTH_INT(l, r) ((l).type == SYGER_INT && (r).type == SYGER_INT)

static inline SygerValue syger_add(SygerValue l, SygerValue r)
{
    if (SYGER_BOTH_INT(l, r)) return syger_int(l.as.integer + r.as.integer);
    return syger_add_slow(l, r);
}

static inline SygerValue syger_sub(SygerValue l, SygerValue r)
{
    if (SYGER_BOTH_INT(l, r)) return syger_int(l.as.integer - r.as.integer);
    syger_arith_error(l, r);
}

static inline SygerValue syger_mul(SygerValue l, SygerValue r)
{
    if (SYGER_BOTH_INT(l, r)) return syger_int(l.as.integer * r.as.integer);
    syger_arith_error(l, r);
}

static inline SygerValue syger_div(SygerValue l, SygerValue r)
{
    if (!SYGER_BOTH_INT(l, r)) syger_arith_error(l, r);
    if (r.as.integer == 0) syger_fail("division by zero");
    return syger_int(l.as.integer / r.as.integer);
}

static inline SygerValue syger_mod(SygerValue l, SygerValue r)
{
    if (!SYGER_BOTH_INT(l, r)) syger_arith_error(l, r);
    if (r.as.integer == 0) syger_fail("modulo by zero");
    return syger_int(l.as.integer % r.as.integer);
}

#define SYGER_DEFINE_COMPARE(name, op)                                   \
    static inline SygerValue name(SygerValue l, SygerValue r)           \
    {                                                                    \
        if (SYGER_BOTH_INT(l, r)) return syger_bool(l.as.integer op r.as.integer); \
        syger_arith_error(l, r);                                         \
    }

SYGER_DEFINE_COMPARE(syger_lt, <)
SYGER_DEFINE_COMPARE(syger_le, <=)
SYGER_DEFINE_COMPARE(syger_gt, >)
SYGER_DEFINE_COMPARE(syger_ge, >=)

static inline SygerValue syger_eq(SygerValue l, SygerValue r)
{
    if (SYGER_BOTH_INT(l, r)) return syger_bool(l.as.integer == r.as.integer);
    int equal = syger_equal(&l, &r);
    syger_release(l); syger_release(r);
    return syger_bool(equal);
}

static inline SygerValue syger_ne(SygerValue l, SygerValue r)
{
    SygerValue result = syger_eq(l, r);
    result.as.boolean = !result.as.boolean;
    return result;
}

_Noreturn void syger_type_error(SygerValue value, const char *message);

static inline int syger_truth(SygerValue v)
{
    if (__builtin_expect(v.type != SYGER_BOOL, 0)) syger_type_error(v, "condition must be a boolean");
    return v.as.boolean;
}

static inline SygerValue syger_not(SygerValue v)
{
    if (v.type != SYGER_BOOL) syger_type_error(v, "not requires a boolean");
    return syger_bool(!v.as.boolean);
}

static inline SygerValue syger_neg(SygerValue v)
{
    if (v.type != SYGER_INT) syger_type_error(v, "unary minus requires an integer");
    return syger_int(-v.as.integer);
}

/* text = text + value, growing text in place when it is not shared. */
void syger_append_assign(SygerValue *slot, SygerValue value);

/* x = x + value */
static inline void syger_add_assign(SygerValue *slot, SygerValue value)
{
    if (SYGER_BOTH_INT(*slot, value)) { slot->as.integer += value.as.integer; return; }
    syger_append_assign(slot, value);
}

/* ---- arrays -------------------------------------------------------------- */

void syger_make_unique(SygerValue *array);
_Noreturn void syger_index_error(SygerValue container, SygerValue index);

static inline SygerValue syger_index(SygerValue array, SygerValue index)
{
    if (__builtin_expect(array.type != SYGER_ARRAY || index.type != SYGER_INT ||
                         index.as.integer < 0 || index.as.integer >= array.as.array->count, 0)) {
        syger_index_error(array, index);
    }
    SygerValue result = syger_retain(array.as.array->items[index.as.integer]);
    syger_release(array);
    return result;
}

/* Borrowed pointer to an element for reading (no copy, no uniqueness). */
static inline SygerValue *syger_elem_ptr(SygerValue *array, SygerValue index)
{
    if (__builtin_expect(array->type != SYGER_ARRAY || index.type != SYGER_INT ||
                         index.as.integer < 0 || index.as.integer >= array->as.array->count, 0)) {
        syger_index_error(syger_retain(*array), index);
    }
    return &array->as.array->items[index.as.integer];
}

/* Pointer to an element for assignment; makes the array unique first. */
static inline SygerValue *syger_index_ptr(SygerValue *array, SygerValue index)
{
    if (__builtin_expect(array->type != SYGER_ARRAY || index.type != SYGER_INT ||
                         index.as.integer < 0 || index.as.integer >= array->as.array->count, 0)) {
        syger_index_error(syger_retain(*array), index);
    }
    if (array->as.array->refcount != 1) syger_make_unique(array);
    return &array->as.array->items[index.as.integer];
}

/* ---- builtins ------------------------------------------------------------ */
/* Value arguments are consumed. SygerValue * arguments are borrowed lvalues. */

SygerValue syger_b_print(SygerValue value);
SygerValue syger_b_type_of(SygerValue value);
SygerValue syger_b_fail(SygerValue message);
SygerValue syger_b_args(void);
SygerValue syger_b_read_file(SygerValue path);
SygerValue syger_b_write_file(SygerValue path, SygerValue text);
SygerValue syger_b_char_at(SygerValue text, SygerValue index);
SygerValue syger_b_substring(SygerValue text, SygerValue start, SygerValue end);
SygerValue syger_b_chr(SygerValue code);
SygerValue syger_b_to_string(SygerValue value);
SygerValue syger_b_length(SygerValue value);
SygerValue syger_b_token_at(SygerValue array, SygerValue index);
SygerValue syger_b_contains(SygerValue array, SygerValue item);
SygerValue syger_b_index_of(SygerValue array, SygerValue item);
SygerValue syger_b_slice(SygerValue array, SygerValue start, SygerValue end);
SygerValue syger_b_join(SygerValue array, SygerValue separator);
SygerValue syger_b_sum(SygerValue array);
SygerValue syger_b_min(SygerValue array);
SygerValue syger_b_max(SygerValue array);
SygerValue syger_b_first(SygerValue array);
SygerValue syger_b_last(SygerValue array);

SygerValue syger_b_append(SygerValue *array, SygerValue item);
SygerValue syger_b_pop(SygerValue *array);
SygerValue syger_b_insert(SygerValue *array, SygerValue index, SygerValue item);
SygerValue syger_b_remove(SygerValue *array, SygerValue index);
SygerValue syger_b_clear(SygerValue *array);
SygerValue syger_b_reverse(SygerValue *array);
SygerValue syger_b_sort(SygerValue *array);

#endif
