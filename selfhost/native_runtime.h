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
    SYGER_FLOAT,
    SYGER_STRING, /* every type from here on is a reference-counted object */
    SYGER_ARRAY,
    SYGER_MAP
} SygerType;

typedef struct SygerString SygerString;
typedef struct SygerArray SygerArray;
typedef struct SygerMap SygerMap;

typedef struct SygerValue {
    SygerType type;
    union {
        long integer;
        int boolean;
        double number;
        SygerString *string;
        SygerArray *array;
        SygerMap *map;
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

typedef struct {
    SygerValue key;
    SygerValue value;
} SygerMapEntry;

/* Insertion-ordered hash map; keys are strings or integers. */
struct SygerMap {
    long refcount;
    long count;
    long capacity;
    SygerMapEntry *entries;
    long *slots;        /* open-addressing index into entries, -1 = empty */
    long slot_capacity; /* power of two */
};

/* ---- errors -------------------------------------------------------------- */

/*
 * Errors print in the same format as the interpreter:
 *
 *   error[E3007]: index 3 is out of bounds for an array of length 3
 *    --> program.sg:4
 *      |
 *    4 |     print(items[i])
 *      |
 *      = help: valid indexes are 0 to 2
 *      = note: in function 'show', called from line 9
 *
 * Generated code keeps syger_line up to date (one store per statement) and
 * records calls with syger_enter/syger_leave for the traceback.
 */

extern long syger_line;
extern const char *syger_source_path;

typedef struct {
    const char *name;
    long call_line;
} SygerFrame;

#define SYGER_MAX_FRAMES 4096
extern SygerFrame syger_frames[SYGER_MAX_FRAMES];
extern int syger_depth;

static inline void syger_enter(const char *name)
{
    if (syger_depth < SYGER_MAX_FRAMES) {
        syger_frames[syger_depth].name = name;
        syger_frames[syger_depth].call_line = syger_line;
    }
    syger_depth++;
}

static inline void syger_leave(void)
{
    syger_depth--;
    if (syger_depth < SYGER_MAX_FRAMES) syger_line = syger_frames[syger_depth].call_line;
}

_Noreturn void syger_error(const char *code, const char *help, const char *format, ...);
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

static inline SygerValue syger_float(double value)
{
    SygerValue v; v.type = SYGER_FLOAT; v.as.number = value; return v;
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
/* pairs holds key, value, key, value, ... (2 * count values, consumed). */
SygerValue syger_map_new(long count, SygerValue *pairs);

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

enum {
    SYGER_OP_ADD, SYGER_OP_SUB, SYGER_OP_MUL, SYGER_OP_DIV, SYGER_OP_MOD,
    SYGER_OP_LT, SYGER_OP_LE, SYGER_OP_GT, SYGER_OP_GE
};

/* Everything except int-with-int: floats, strings, arrays, and errors. */
SygerValue syger_binary_slow(int op, SygerValue left, SygerValue right);
int syger_equal(const SygerValue *left, const SygerValue *right);

#define SYGER_BOTH_INT(l, r) ((l).type == SYGER_INT && (r).type == SYGER_INT)

static inline SygerValue syger_add(SygerValue l, SygerValue r)
{
    if (SYGER_BOTH_INT(l, r)) return syger_int(l.as.integer + r.as.integer);
    return syger_binary_slow(SYGER_OP_ADD, l, r);
}

static inline SygerValue syger_sub(SygerValue l, SygerValue r)
{
    if (SYGER_BOTH_INT(l, r)) return syger_int(l.as.integer - r.as.integer);
    return syger_binary_slow(SYGER_OP_SUB, l, r);
}

static inline SygerValue syger_mul(SygerValue l, SygerValue r)
{
    if (SYGER_BOTH_INT(l, r)) return syger_int(l.as.integer * r.as.integer);
    return syger_binary_slow(SYGER_OP_MUL, l, r);
}

static inline SygerValue syger_div(SygerValue l, SygerValue r)
{
    if (SYGER_BOTH_INT(l, r) && r.as.integer != 0) return syger_int(l.as.integer / r.as.integer);
    return syger_binary_slow(SYGER_OP_DIV, l, r);
}

static inline SygerValue syger_mod(SygerValue l, SygerValue r)
{
    if (SYGER_BOTH_INT(l, r) && r.as.integer != 0) return syger_int(l.as.integer % r.as.integer);
    return syger_binary_slow(SYGER_OP_MOD, l, r);
}

#define SYGER_DEFINE_COMPARE(name, op, code)                             \
    static inline SygerValue name(SygerValue l, SygerValue r)           \
    {                                                                    \
        if (SYGER_BOTH_INT(l, r)) return syger_bool(l.as.integer op r.as.integer); \
        return syger_binary_slow(code, l, r);                            \
    }

SYGER_DEFINE_COMPARE(syger_lt, <, SYGER_OP_LT)
SYGER_DEFINE_COMPARE(syger_le, <=, SYGER_OP_LE)
SYGER_DEFINE_COMPARE(syger_gt, >, SYGER_OP_GT)
SYGER_DEFINE_COMPARE(syger_ge, >=, SYGER_OP_GE)

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

_Noreturn void syger_type_error(const char *code, SygerValue value, const char *message);

static inline int syger_truth(SygerValue v)
{
    if (__builtin_expect(v.type != SYGER_BOOL, 0)) syger_type_error("E3015", v, "condition must be a boolean");
    return v.as.boolean;
}

/* Operand of and/or. */
static inline int syger_logic_operand(SygerValue v, const char *operator)
{
    if (__builtin_expect(v.type != SYGER_BOOL, 0)) {
        syger_type_error("E3016", v, operator[0] == 'a' ? "and requires booleans" : "or requires booleans");
    }
    return v.as.boolean;
}

static inline SygerValue syger_not(SygerValue v)
{
    if (v.type != SYGER_BOOL) syger_type_error("E3016", v, "not requires a boolean");
    return syger_bool(!v.as.boolean);
}

static inline SygerValue syger_neg(SygerValue v)
{
    if (v.type == SYGER_FLOAT) return syger_float(-v.as.number);
    if (v.type != SYGER_INT) syger_type_error("E3017", v, "unary minus requires a number");
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

/* ---- indexing ------------------------------------------------------------ */
/* Arrays take the inline fast path; maps, strings and errors go out of line. */

void syger_make_unique(SygerValue *array);
SygerValue syger_index_slow(SygerValue container, SygerValue index);
SygerValue *syger_place_slow(SygerValue *container, SygerValue index, int mode);

enum { SYGER_PLACE_READ, SYGER_PLACE_CHANGE, SYGER_PLACE_ASSIGN };

#define SYGER_ARRAY_INDEX_OK(c, i) \
    ((c).type == SYGER_ARRAY && (i).type == SYGER_INT && (i).as.integer >= 0 && (i).as.integer < (c).as.array->count)

/* container[index] as an owned value; consumes both. */
static inline SygerValue syger_index(SygerValue container, SygerValue index)
{
    if (__builtin_expect(!SYGER_ARRAY_INDEX_OK(container, index), 0)) return syger_index_slow(container, index);
    SygerValue result = syger_retain(container.as.array->items[index.as.integer]);
    syger_release(container);
    return result;
}

/* container[index] read through a variable, without copying the container. */
static inline SygerValue syger_index_at(SygerValue *container, SygerValue index)
{
    if (__builtin_expect(!SYGER_ARRAY_INDEX_OK(*container, index), 0)) return syger_index_slow(syger_retain(*container), index);
    return syger_retain(container->as.array->items[index.as.integer]);
}

/* Borrowed pointer to an element, for reading deeper (a[i][j]). */
static inline SygerValue *syger_elem_ptr(SygerValue *container, SygerValue index)
{
    if (__builtin_expect(!SYGER_ARRAY_INDEX_OK(*container, index), 0)) return syger_place_slow(container, index, SYGER_PLACE_READ);
    return &container->as.array->items[index.as.integer];
}

/* Pointer to an element that will be changed; makes the container unique. */
static inline SygerValue *syger_index_ptr(SygerValue *container, SygerValue index)
{
    if (__builtin_expect(!SYGER_ARRAY_INDEX_OK(*container, index), 0)) return syger_place_slow(container, index, SYGER_PLACE_CHANGE);
    if (container->as.array->refcount != 1) syger_make_unique(container);
    return &container->as.array->items[index.as.integer];
}

/* Like syger_index_ptr, but a missing map key is added (for m[key] = value). */
static inline SygerValue *syger_index_slot(SygerValue *container, SygerValue index)
{
    if (__builtin_expect(!SYGER_ARRAY_INDEX_OK(*container, index), 0)) return syger_place_slow(container, index, SYGER_PLACE_ASSIGN);
    if (container->as.array->refcount != 1) syger_make_unique(container);
    return &container->as.array->items[index.as.integer];
}

/* ---- for loops ------------------------------------------------------------- */

long syger_iter_count(const SygerValue *sequence);
SygerValue syger_iter_item(const SygerValue *sequence, long index);
long syger_range_argument(SygerValue value);
void syger_range_check_step(long step);

/* ---- import ---------------------------------------------------------------- */

/* The contents of a data file that was embedded at compile time. */
SygerValue syger_import_embedded(const char *path, const char *text, long length);

/* ---- builtins ------------------------------------------------------------ */
/* Value arguments are consumed. SygerValue * arguments are borrowed lvalues. */

SygerValue syger_b_print(SygerValue value);
SygerValue syger_b_eprint(SygerValue value);
SygerValue syger_b_exit(SygerValue code);
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

SygerValue syger_b_keys(SygerValue map);
SygerValue syger_b_values(SygerValue map);
SygerValue syger_b_get(SygerValue container, SygerValue key, SygerValue fallback);
SygerValue syger_b_range1(SygerValue end);
SygerValue syger_b_range2(SygerValue start, SygerValue end);
SygerValue syger_b_range3(SygerValue start, SygerValue end, SygerValue step);
SygerValue syger_b_to_int(SygerValue value);
SygerValue syger_b_to_float(SygerValue value);
SygerValue syger_b_round1(SygerValue number);
SygerValue syger_b_round2(SygerValue number, SygerValue digits);
SygerValue syger_b_floor(SygerValue number);
SygerValue syger_b_ceil(SygerValue number);
SygerValue syger_b_abs(SygerValue number);
SygerValue syger_b_sqrt(SygerValue number);
SygerValue syger_b_split1(SygerValue text);
SygerValue syger_b_split2(SygerValue text, SygerValue separator);
SygerValue syger_b_trim(SygerValue text);
SygerValue syger_b_upper(SygerValue text);
SygerValue syger_b_lower(SygerValue text);
SygerValue syger_b_replace(SygerValue text, SygerValue old_text, SygerValue new_text);
SygerValue syger_b_starts_with(SygerValue text, SygerValue prefix);
SygerValue syger_b_ends_with(SygerValue text, SygerValue suffix);
SygerValue syger_b_load(SygerValue path);
SygerValue syger_b_load_error(SygerValue path);

#endif
