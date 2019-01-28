/* Python runtime. */
#include <Python.h>

#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "fpga.h"
#include "pychronos.h"

#if (PY_MAJOR_VERSION < 3)
#error "Required python major version must be >=3"
#endif

#define SEQCOMMAND_BLK_TERM_RISING  (1 << 7)
#define SEQCOMMAND_BLK_TERM_FALLING (1 << 6)
#define SEQCOMMAND_BLK_TERM_HIGH    (1 << 5)
#define SEQCOMMAND_BLK_TERM_LOW     (1 << 4)
#define SEQCOMMAND_BLK_TERM_FULL    (1 << 3)
#define SEQCOMMAND_REC_TERM_BLOCK   (1 << 2)
#define SEQCOMMAND_REC_TERM_MEMORY  (1 << 1)
#define SEQCOMMAND_REC_TERM_TRIGGER (1 << 0)
#define SEQCOMMAND_NEXT_SHIFT       8
#define SEQCOMMAND_NEXT_MASK        (0xf << SEQCOMMAND_NEXT_SHIFT)
#define SEQCOMMAND_FLAGS_MASK       (0xfff << 0)

#define SEQCOMMAND_SIZE_SHIFT       12
#define SEQCOMMAND_SIZE_MASK        (0xffffffffULL << SEQCOMMAND_SIZE_SHIFT)

struct seqcommand_object {
    PyObject_HEAD
    unsigned long bsize;
    uint8_t nstate;
    uint8_t flags;
};

static int
seqcommand_init(PyObject *self, PyObject *args, PyObject *kwargs)
{
    struct seqcommand_object *cmd = (struct seqcommand_object *)self;
    char *keywords[] = {
        /* Optional and positional */
        "command",
        /* Unsigned Long */
        "blockSize",
        "nextState",
        /* Booleans */
        "blkTermRising",
        "blkTermFalling",
        "blkTermHigh",
        "blkTermLow",
        "blkTermFull",
        "recTermBlockEnd",
        "recTermMemory",
        "recTermTrigger",
        NULL,
    };
    /* Parsed values */
    PyObject *command = NULL;
    unsigned long blockSize = 0;
    int nextState = -1;
    int blkTermRising = -1;
    int blkTermFalling = -1;
    int blkTermHigh = -1;
    int blkTermLow = -1;
    int blkTermFull = -1;
    int recTermBlockEnd = -1;
    int recTermMemory = -1;
    int recTermTrigger = -1;

    /* Parse the constructor arguments. */
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O$kipppppppp", keywords, &command, &nextState, &blockSize,
            &blkTermRising, &blkTermFalling, &blkTermHigh, &blkTermLow, &recTermBlockEnd, &recTermMemory, &recTermTrigger)) {
        return -1;
    }

    /* Handle copy contructors */
    if (command) {
        unsigned long long value;
        PyObject *vobject = PyNumber_Long(command);
        if (!vobject) {
            return -1;
        }
        value = PyLong_AsUnsignedLongLong(vobject);
        Py_DECREF(vobject);
        cmd->flags = (value & SEQCOMMAND_FLAGS_MASK);
        cmd->bsize = (value & SEQCOMMAND_SIZE_MASK) >> SEQCOMMAND_SIZE_SHIFT;
        cmd->nstate = (value & SEQCOMMAND_NEXT_MASK) >> SEQCOMMAND_NEXT_SHIFT;
    }

    /* Modify the flags according to the constructor arguments. */
    if (blockSize != 0) {
        cmd->bsize = blockSize;
    }
    if (nextState >= 0) {
        if ((nextState << SEQCOMMAND_NEXT_SHIFT) & ~SEQCOMMAND_NEXT_MASK) {
            PyErr_SetString(PyExc_OverflowError, "Next state index overflow");
            return -1;
        }
        cmd->nstate = nextState;
    }
    if (blkTermRising == 0) cmd->flags &= ~SEQCOMMAND_BLK_TERM_RISING;
    if (blkTermRising == 1) cmd->flags |=  SEQCOMMAND_BLK_TERM_RISING;
    if (blkTermFalling == 0) cmd->flags &= ~SEQCOMMAND_BLK_TERM_FALLING;
    if (blkTermFalling == 1) cmd->flags |=  SEQCOMMAND_BLK_TERM_FALLING;
    if (blkTermHigh == 0) cmd->flags &= ~SEQCOMMAND_BLK_TERM_HIGH;
    if (blkTermHigh == 1) cmd->flags |=  SEQCOMMAND_BLK_TERM_HIGH;
    if (blkTermLow == 0) cmd->flags &= ~SEQCOMMAND_BLK_TERM_LOW;
    if (blkTermLow == 1) cmd->flags |=  SEQCOMMAND_BLK_TERM_LOW;
    if (blkTermFull == 0) cmd->flags &= ~SEQCOMMAND_BLK_TERM_FULL;
    if (blkTermFull == 1) cmd->flags |=  SEQCOMMAND_BLK_TERM_FULL;
    if (recTermBlockEnd == 0) cmd->flags &= ~SEQCOMMAND_REC_TERM_BLOCK;
    if (recTermBlockEnd == 1) cmd->flags |=  SEQCOMMAND_REC_TERM_BLOCK;
    if (recTermMemory == 0) cmd->flags &= ~SEQCOMMAND_REC_TERM_MEMORY;
    if (recTermMemory == 1) cmd->flags |=  SEQCOMMAND_REC_TERM_MEMORY;
    if (recTermTrigger == 0) cmd->flags &= ~SEQCOMMAND_REC_TERM_TRIGGER;
    if (recTermTrigger == 1) cmd->flags |=  SEQCOMMAND_REC_TERM_TRIGGER;
    return 0;
}

static PyObject *
seqcommand_repr(PyObject *self)
{
    struct seqcommand_object *cmd = (struct seqcommand_object *)self;
    return PyUnicode_FromFormat("seqcommand(blockSize=%lu, nextState=%u,\n"
                                "\tblkTermRising=%s,\n"
                                "\tblkTermFalling=%s,\n"
                                "\tblkTermHigh=%s,\n"
                                "\tblkTermLow=%s,\n"
                                "\tblkTermFull=%s,\n"
                                "\trecTermBlockEnd=%s,\n"
                                "\trecTermMemory=%s,\n"
                                "\trecTermTrigger=%s)",
            cmd->bsize+1, cmd->nstate,
            cmd->flags & SEQCOMMAND_BLK_TERM_RISING ? "True" : "False",
            cmd->flags & SEQCOMMAND_BLK_TERM_FALLING ? "True" : "False",
            cmd->flags & SEQCOMMAND_BLK_TERM_HIGH ? "True" : "False",
            cmd->flags & SEQCOMMAND_BLK_TERM_LOW ? "True" : "False",
            cmd->flags & SEQCOMMAND_BLK_TERM_FULL ? "True" : "False",
            cmd->flags & SEQCOMMAND_REC_TERM_BLOCK ? "True" : "False",
            cmd->flags & SEQCOMMAND_REC_TERM_MEMORY ? "True" : "False",
            cmd->flags & SEQCOMMAND_REC_TERM_TRIGGER ? "True" : "False");
}

static PyObject *
seqcommand_get_flag(PyObject *self, void *closure)
{
    struct seqcommand_object *cmd = (struct seqcommand_object *)self;
    unsigned long mask = (unsigned long)(uintptr_t)closure;
    return PyBool_FromLong(cmd->flags & mask);
}

static int
seqcommand_set_flag(PyObject *self, PyObject *value, void *closure)
{
    struct seqcommand_object *cmd = (struct seqcommand_object *)self;
    unsigned long mask = (unsigned long)(uintptr_t)closure;
    if (PyObject_IsTrue(value)) {
        cmd->flags |= mask;
    }
    else {
        cmd->flags &= ~mask;
    }
    return 0;
}

static PyObject *
seqcommand_get_blocksize(PyObject *self, void *closure)
{
    struct seqcommand_object *cmd = (struct seqcommand_object *)self;
    return PyLong_FromUnsignedLong(cmd->bsize + 1);
}

static int
seqcommand_set_blocksize(PyObject *self, PyObject *value, void *closure)
{
    struct seqcommand_object *cmd = (struct seqcommand_object *)self;
    unsigned long long bsize = PyLong_AsUnsignedLong(value);
    if (PyErr_Occurred()) {
        return -1;
    }
    if (bsize == 0) {
        PyErr_SetString(PyExc_ValueError, "Invalid block size");
        return -1;
    }
    cmd->bsize = bsize - 1;
    return 0;
}

static PyObject *
seqcommand_get_nextstate(PyObject *self, void *closure)
{
    struct seqcommand_object *cmd = (struct seqcommand_object *)self;
    return PyLong_FromUnsignedLong(cmd->nstate);
}

static int
seqcommand_set_nextstate(PyObject *self, PyObject *value, void *closure)
{
    struct seqcommand_object *cmd = (struct seqcommand_object *)self;
    unsigned long next = PyLong_AsUnsignedLong(value);
    if (PyErr_Occurred()) {
        return -1;
    }
    if ((next << SEQCOMMAND_NEXT_SHIFT) & ~SEQCOMMAND_NEXT_MASK) {
        PyErr_SetString(PyExc_OverflowError, "Next state index overflow");
        return -1;
    }
    cmd->nstate = next;
    return 0;
}

static PyGetSetDef seqcommand_getset[] = {
    /* Integers */
    {"blockSize", seqcommand_get_blocksize, seqcommand_set_blocksize, "Number of frames in a block"},
    {"nextState", seqcommand_get_nextstate, seqcommand_set_nextstate, "Jump to program index on block termination"},
    /* Bitmasks */
    {"blkTermRising", seqcommand_get_flag, seqcommand_set_flag,
        "Enable block terimation on rising edge",               (void *)SEQCOMMAND_BLK_TERM_RISING},
    {"blkTermFalling", seqcommand_get_flag, seqcommand_set_flag,
        "Enable block terimation on falling edge",              (void *)SEQCOMMAND_BLK_TERM_FALLING},
    {"blkTermHigh",   seqcommand_get_flag, seqcommand_set_flag,
        "Enable block terimation on high trigger",              (void *)SEQCOMMAND_BLK_TERM_HIGH},
    {"blkTermLow",    seqcommand_get_flag, seqcommand_set_flag,
        "Enable block terimation on low trigger",               (void *)SEQCOMMAND_BLK_TERM_LOW},
    {"blkTermFull",   seqcommand_get_flag, seqcommand_set_flag,
        "Enable block terminatio when the block becomes full",  (void *)SEQCOMMAND_BLK_TERM_FULL},
    {"recTermBlockEnd", seqcommand_get_flag, seqcommand_set_flag,
        "Enable recording termination when the block terminates", (void *)SEQCOMMAND_REC_TERM_BLOCK},
    {"recTermMemory", seqcommand_get_flag, seqcommand_set_flag,
        "Enable recording termination when memory becomes full", (void *)SEQCOMMAND_REC_TERM_MEMORY},
    {"recTermTrigger", seqcommand_get_flag, seqcommand_set_flag,
        "Enable recording termination on block termination",    (void *)SEQCOMMAND_REC_TERM_TRIGGER},
    {NULL}
};

/*=====================================*
 * Misc/Configuration Register Block
 *=====================================*/
struct seqcommand_object *
seqcommand_dup_sanity(PyObject *orig)
{
    struct seqcommand_object *cmd = (struct seqcommand_object *)orig;
    struct seqcommand_object *copy;
    if (PyErr_Occurred()) return NULL;
    copy = PyObject_New(struct seqcommand_object, &seqcommand_type);
    if (copy) {
        copy->flags = cmd->flags;
        copy->nstate = cmd->nstate;
        copy->bsize = cmd->bsize;
    }
    return copy;
}

/*
 * Macro to reduce typing for defining binary math operations on a
 * python seqcommand_type. This will expand into a static function
 * that does the correct sanity-checks, and then applies the
 * expression:
 *
 *      result->member = lvalue->member (operand) rvalue
 */
#define SEQCOMMAND_BINARY_MATHOP(_function_, _member_, _operand_) \
    static PyObject * _function_(PyObject *a, PyObject *b) { \
        unsigned long rvalue = PyLong_AsUnsignedLong(b); \
        struct seqcommand_object *lvalue = (struct seqcommand_object *)a; \
        struct seqcommand_object *result = seqcommand_dup_sanity(a); \
        if (result) { result->_member_ = lvalue->_member_ _operand_ rvalue; } \
        return (PyObject *)result; \
    }

SEQCOMMAND_BINARY_MATHOP(seqcommand_lshift, flags, << )
SEQCOMMAND_BINARY_MATHOP(seqcommand_rshift, flags, >> )
SEQCOMMAND_BINARY_MATHOP(seqcommand_and,    flags, & )
SEQCOMMAND_BINARY_MATHOP(seqcommand_xor,    flags, ^ )
SEQCOMMAND_BINARY_MATHOP(seqcommand_or,     flags, | )

static PyObject *
seqcommand_invert(PyObject *self)
{
    struct seqcommand_object *result = seqcommand_dup_sanity(self);
    if (result) result->flags = ~result->flags;
    return (PyObject *)result;
}

static PyObject *
seqcommand_int(PyObject *self)
{
    struct seqcommand_object *cmd = (struct seqcommand_object *)self;
    unsigned long long value = (unsigned long long)cmd->bsize << SEQCOMMAND_SIZE_SHIFT;
    return PyLong_FromUnsignedLongLong(value | (cmd->nstate << SEQCOMMAND_NEXT_SHIFT) | cmd->flags);
}

static PyNumberMethods seqcommand_as_number = {
    .nb_invert = seqcommand_invert,
    .nb_lshift = seqcommand_lshift,
    .nb_rshift = seqcommand_rshift,
    .nb_and = seqcommand_and,
    .nb_xor = seqcommand_xor,
    .nb_or  = seqcommand_or,
    .nb_int = seqcommand_int,
};

PyDoc_STRVAR(seqcommand_docstring,
"seqcommand(command=0)\n\
--\n\
\n\
Create a program sequencer command for controlling the acquisition of\n\
frames from the image sensor.\n\
\n\
Parameters\n\
----------\n\
command : `int`, optional\n\
    Raw integer command value\n\
blockSize : `int`, optional\n\
    Number of frames in a block\n\
nextState : `int`, optional\n\
    Jump to this address on completion of this block\n\
blkTermRising : `bool`, optional\n\
    Enable block terimation on rising edge\n\
blkTermFalling : `bool`, optional\n\
    Enable block termination on falling edge\n\
blkTermHigh : `bool`, optional\n\
    Enable block termination on high trigger\n\
blkTermLow : `bool`, optional\n\
    Enable block termination on low trigger\n\
blkTermFull : `bool`, optional\n\
    Enable block termination when the block becomes full\n\
recTermBlockEnd : `bool`, optional\n\
    Enable recording termination when the block terminates\n\
recTermMemory : `bool`, optional\n\
    Enable recording termination when recording memory becomes full\n\
recTermTrigger : `bool`, optional\n\
    Enable record termination on block termination trigger");

PyTypeObject seqcommand_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.seqcommand",
    .tp_doc = seqcommand_docstring,
    .tp_basicsize = sizeof(struct seqcommand_object),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = PyType_GenericNew,
    .tp_init = seqcommand_init,
    .tp_repr = seqcommand_repr,
    .tp_getset = seqcommand_getset,
    .tp_as_number = &seqcommand_as_number,
};
