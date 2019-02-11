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

/*=====================================*
 * FPGA Memory Mapping Base Type
 *=====================================*/
struct fpgamap {
    PyObject_HEAD
    void            *regs;
    unsigned long   roffset;
    unsigned long   rsize;
};

struct fpgamap_reginfo {
    unsigned long   offset; /* Starting offset of the register. */
    unsigned long   size;   /* Length of the scalar (or of a single element in the array). */
    unsigned long   count;  /* Zero for scalar types, length of the array for array types. */
};
#define FPGA_REG_INFO(_offset_, _size_, _count_) (struct fpgamap_reginfo []){{_offset_, _size_, _count_}}

#define FPGA_REG_TYPED(_container_, _member_, _type_) \
    FPGA_REG_INFO(offsetof(_container_, _member_), sizeof(_type_), 0)

#define FPGA_REG_SCALAR(_container_, _member_) \
    FPGA_REG_INFO(offsetof(_container_, _member_), sizeof(((_container_ *)0)->_member_), 0)

#define FPGA_REG_ARRAY(_container_, _member_) \
    FPGA_REG_INFO(offsetof(_container_, _member_), sizeof(((_container_ *)0)->_member_[0]), arraysize(((_container_ *)0)->_member_))


#define fpgaptr(_self_, _offset_, _type_) \
    (_type_ *)((unsigned char *)(((struct fpgamap *)_self_)->regs) + (uintptr_t)(_offset_))

static int
fpgamap_init(PyObject *self, PyObject *args, PyObject *kwargs)
{
    struct fpgamap *fmap = (struct fpgamap *)self;
    char *keywords[] = {
        "offset",
        "size",
        NULL,
    };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|kk", keywords, &fmap->roffset, &fmap->rsize)) {
        return -1;
    }
    fmap->regs = (uint8_t *)fpga_regbuffer.buf + fmap->roffset;
    return 0;
}

static PyObject *
fpgamap_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    struct fpgamap *self = (struct fpgamap *)type->tp_alloc(type, 0);
    if (pychronos_init_maps() != 0) {
        Py_DECREF(self);
        return NULL;
    }
    self->roffset = 0;
    self->rsize = FPGA_MAP_SIZE;
    self->regs = NULL;
    return (PyObject *)self;
}

/*=====================================*
 * Array and Memory View Methods
 *=====================================*/
struct fpgamap_arrayview {
    PyObject_HEAD
    union {
        void        *raw;
        uint32_t    *u32;
        uint16_t    *u16;
        uint8_t     *u8;
    } u;
    unsigned int    itemsize;
    size_t          itemcount;
};

static Py_ssize_t
arrayview_length(PyObject *self)
{
    struct fpgamap_arrayview *view = (struct fpgamap_arrayview *)self;
    return view->itemcount;
}

static PyObject *
arrayview_getval(PyObject *self, PyObject *key)
{
    struct fpgamap_arrayview *view = (struct fpgamap_arrayview *)self;
    unsigned long index = PyLong_AsUnsignedLong(key);
    if (PyErr_Occurred()) {
        return NULL;
    }
    if (index >= view->itemcount) {
        PyErr_SetString(PyExc_ValueError, "Register value out of range"); /* TODO: Index out of range probably makes more sense. */
        return NULL;
    }

    /* TODO: We should just call fpga_get_uint from here. */
    switch (view->itemsize) {
        case 1: return PyLong_FromUnsignedLong(view->u.u8[index]);
        case 2: return PyLong_FromUnsignedLong(view->u.u16[index]);
        case 4: return PyLong_FromUnsignedLong(view->u.u32[index]);
        default:
            PyErr_SetString(PyExc_RuntimeError, "Invalid register access size");
            return NULL;
    }
}

static int
arrayview_setval(PyObject *self, PyObject *key, PyObject *value)
{
    struct fpgamap_arrayview *view = (struct fpgamap_arrayview *)self;
    unsigned long val, index;

    index = PyLong_AsUnsignedLong(key);
    if (PyErr_Occurred()) {
        return -1;
    }
    if (index >= view->itemcount) {
        PyErr_SetString(PyExc_IndexError, "Array index out of range");
        return -1;
    }

    /* TODO: We should just call fpga_set_uint from here. */
    val = PyLong_AsUnsignedLong(value);
    if (PyErr_Occurred()) {
        return -1;
    }
    if ((val >> (view->itemsize * 8)) != 0) {
        PyErr_SetString(PyExc_ValueError, "Register value out of range");
        return -1;
    }
    switch (view->itemsize) {
        case 1:
            view->u.u8[index] = val;
            break;
        case 2:
            view->u.u16[index] = val;
            break;
        case 4:
            view->u.u32[index] = val;
            break;
        default:
            PyErr_SetString(PyExc_RuntimeError, "Invalid register access size");
            return -1;
    }
    return 0;
}

static PyMappingMethods fpgamap_as_array = {
    .mp_length = arrayview_length,
    .mp_subscript = arrayview_getval,
    .mp_ass_subscript = arrayview_setval
};

static PyTypeObject arrayview_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.arrayview",
    .tp_doc = "Memory mapped array view",
    .tp_basicsize = sizeof(struct fpgamap_arrayview),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_as_mapping = &fpgamap_as_array,
    .tp_iter = pychronos_array_getiter,
};

/* Getter to return an arrayview object. */
static PyObject *
fpga_get_arrayview(PyObject *self, void *closure)
{
    struct fpgamap *fmap = (struct fpgamap *)self;
    struct fpgamap_reginfo *info = closure;
    struct fpgamap_arrayview *view = PyObject_New(struct fpgamap_arrayview, &arrayview_type);
    if (view) {
        unsigned long maxcount = (fmap->rsize - info->offset) / info->size;
        view->itemsize = info->size;
        view->itemcount = (info->count < maxcount) ? info->count : maxcount;
        view->u.raw = (unsigned char *)fmap->regs + info->offset;
    }
    return (PyObject *)view;
}

/* TODO: Should we add a setter to allow something like foo->arrayview = [0x11, 0x22, 0x33, 0x44] */

/* MemoryView Protocol. */
static int
fpgamap_getbuffer(PyObject *self, Py_buffer *view, int flags)
{
    struct fpgamap *fmap = (struct fpgamap *)self;
    view->format = (flags & PyBUF_FORMAT) ? "<H" : NULL;
    return PyBuffer_FillInfo(view, self, fmap->regs, fmap->rsize, 0, flags);
}
static PyBufferProcs fpgamap_as_buffer = { .bf_getbuffer = fpgamap_getbuffer };

static PyGetSetDef fpgamap_getset[] = {
    {"mem8",  fpga_get_arrayview, NULL, "8-bit memory array view",  FPGA_REG_INFO(0, sizeof(uint8_t), ULONG_MAX) },
    {"mem16", fpga_get_arrayview, NULL, "16-bit memory array view", FPGA_REG_INFO(0, sizeof(uint16_t), ULONG_MAX) },
    {"mem32", fpga_get_arrayview, NULL, "32-bit memory array view", FPGA_REG_INFO(0, sizeof(uint32_t), ULONG_MAX) },
    { NULL },
};

PyDoc_STRVAR(fpgamap_docstring,
"fpgamap(offset=0, size=FPGA_MAP_SIZE)\n\
--\n\
\n\
Return a new map of the FPGA register space starting at offset, and\n\
covering size bytes of the address space. These maps form the base\n\
class for all other FPGA register blocks and provide raw access to\n\
memory.\n\
\n\
Parameters\n\
----------\n\
offset : `int`, optional\n\
    Starting offset of the register mapping (zero, by default)\n\
size : `int`, optional\n\
    Length of the register mapping (FPGA_MAP_SIZE, by default)");

static PyTypeObject fpgamap_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.fpgamap",
    .tp_doc = fpgamap_docstring,
    .tp_basicsize = sizeof(struct fpgamap),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_init = fpgamap_init,
    .tp_new = fpgamap_new,
    .tp_getset = fpgamap_getset,
    .tp_as_buffer = &fpgamap_as_buffer,
};

/*=====================================*
 * Register Accessor Methods
 *=====================================*/
static PyObject *
fpga_get_uint(PyObject *self, void *closure)
{
    struct fpgamap_reginfo *info = closure;
    switch (info->size) {
        case sizeof(uint8_t):   return PyLong_FromUnsignedLong(*fpgaptr(self, info->offset, uint8_t));
        case sizeof(uint16_t):  return PyLong_FromUnsignedLong(*fpgaptr(self, info->offset, uint16_t));
        case sizeof(uint32_t):  return PyLong_FromUnsignedLong(*fpgaptr(self, info->offset, uint32_t));
        default:
            PyErr_SetString(PyExc_RuntimeError, "Invalid register access size");
            return NULL;
    }
}

static int
fpga_set_uint(PyObject *self, PyObject *value, void *closure)
{
    struct fpgamap_reginfo *info = closure;
    unsigned long val = PyLong_AsUnsignedLong(value);
    if (PyErr_Occurred()) {
        return -1;
    }
    if ((val >> (info->size * 8)) != 0) {
        PyErr_SetString(PyExc_ValueError, "Register value out of range");
        return -1;
    }
    switch (info->size) {
        case sizeof(uint8_t):
            *fpgaptr(self, info->offset, uint8_t) = val;
            break;
        case sizeof(uint16_t):
            *fpgaptr(self, info->offset, uint16_t) = val;
            break;
        case sizeof(uint32_t):
            *fpgaptr(self, info->offset, uint32_t) = val;
            break;
        default:
            PyErr_SetString(PyExc_RuntimeError, "Invalid register access size");
            return -1;
    }
    return 0;
}

static PyObject *
fpga_get_const(PyObject *self, void *closure)
{
    return PyLong_FromLong((uintptr_t)closure);
}

static PyObject *
fpgamap_get_buf(PyObject *self, void *closure)
{
    struct fpgamap *fmap = closure;
    struct fpgamap_reginfo *info = closure;
    PyObject *args = Py_BuildValue("()");
    PyObject *kwds = Py_BuildValue("{s:i,s:i}", "offset", fmap->roffset + info->offset, "size", info->size);
    PyObject *buf = PyObject_Call((PyObject *)&fpgamap_type, args, kwds);
    Py_DECREF(args);
    Py_DECREF(kwds);
    return buf;
}

/*=====================================*
 * Image Sensor Register Space
 *=====================================*/
static PyGetSetDef sensor_getset[] = {
    /* Sensor Register Definitions */
    {"control",     fpga_get_uint, fpga_set_uint, "Control Register",               FPGA_REG_TYPED(struct fpga_sensor, control, uint16_t)},
    {"clkPhase",    fpga_get_uint, fpga_set_uint, "Clock Phase Register",           FPGA_REG_TYPED(struct fpga_sensor, clk_phase, uint16_t)},
    {"syncToken",   fpga_get_uint, fpga_set_uint, "Sync Token Register",            FPGA_REG_TYPED(struct fpga_sensor, sync_token, uint16_t)},
    {"dataCorrect", fpga_get_uint, NULL,          "Data Correct Status Register",   FPGA_REG_SCALAR(struct fpga_sensor, data_correct)},
    {"fifoStart",   fpga_get_uint, fpga_set_uint, "FIFO Starting Address Register", FPGA_REG_TYPED(struct fpga_sensor, fifo_start, uint16_t)},
    {"fifoStop",    fpga_get_uint, fpga_set_uint, "FIFO Ending Address Register",   FPGA_REG_TYPED(struct fpga_sensor, fifo_stop, uint16_t)},
    {"framePeriod", fpga_get_uint, fpga_set_uint, "Frame Period Register",          FPGA_REG_TYPED(struct fpga_sensor, frame_period, uint32_t)},
    {"intTime",     fpga_get_uint, fpga_set_uint, "Integration Time Register",      FPGA_REG_TYPED(struct fpga_sensor, int_time, uint32_t)},
    {"sciControl",  fpga_get_uint, fpga_set_uint, "SCI Control Register",           FPGA_REG_TYPED(struct fpga_sensor, sci_control, uint16_t)},
    {"sciAddress",  fpga_get_uint, fpga_set_uint, "SCI Address Register",           FPGA_REG_TYPED(struct fpga_sensor, sci_address, uint16_t)},
    {"sciDataLen",  fpga_get_uint, fpga_set_uint, "SCI Data Length Register",       FPGA_REG_TYPED(struct fpga_sensor, sci_datalen, uint16_t)},
    {"sciFifoWrite", fpga_get_uint, fpga_set_uint, "SCI Write FIFO Register",       FPGA_REG_TYPED(struct fpga_sensor, sci_fifo_write, uint16_t)},
    {"sciFifoRead", fpga_get_uint, fpga_set_uint, "SCI Read FIFO Register",         FPGA_REG_TYPED(struct fpga_sensor, sci_fifo_read, uint16_t)},
    {"startDelay",  fpga_get_uint, fpga_set_uint, "ABN/exposure start delay",       FPGA_REG_TYPED(struct fpga_sensor, start_delay, uint16_t)},
    {"linePeriod",  fpga_get_uint, fpga_set_uint, "Horizontal line period",         FPGA_REG_TYPED(struct fpga_sensor, line_period, uint16_t)},
    /* Sensor Constant Definitions */
    {"RESET",           fpga_get_const, NULL, "Control Reset Flag", (void *)SENSOR_CTL_RESET_MASK},
    {"EVEN_TIMESLOT",   fpga_get_const, NULL, "Even Timeslot Flag", (void *)SENSOR_CTL_EVEN_SLOT_MASK},
    {"SCI_RUN",         fpga_get_const, NULL, "SCI Run Mask",       (void *)SENSOR_SCI_CONTROL_RUN_MASK},
    {"SCI_RW",          fpga_get_const, NULL, "SCI Read/Write",     (void *)SENSOR_SCI_CONTROL_RW_MASK},
    {"SCI_FULL",        fpga_get_const, NULL, "SCI FIFO Full",      (void *)SENSOR_SCI_CONTROL_FULL_MASK},
    {"SCI_RESET",       fpga_get_const, NULL, "SCI FIFO Reset",     (void *)SENSOR_SCI_CONTROL_RESET_MASK},
    {NULL}
};

static int
sensor_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    struct fpgamap *fmap = (struct fpgamap *)self;
    fmap->roffset = FPGA_SENSOR_BASE;
    fmap->rsize = sizeof(struct fpga_sensor);
    return fpgamap_init(self, args, kwds);
}

PyDoc_STRVAR(sensor_docstring,
"sensor(offset=FPGA_SENSOR_BASE, size=FPGA_MAP_SIZE)\n\
--\n\
\n\
Return a new map of the FPGA sensor register space. This map provides\n\
structured read and write access to the registers which control SCI\n\
communication to the image sensor, and timing parameters for the\n\
frame control signals.\n\
\n\
Parameters\n\
----------\n\
offset : `int`, optional\n\
    Starting offset of the register map (FPGA_SENSOR_BASE, by default)\n\
size : `int`, optional\n\
    Length of the register map (FPGA_MAP_SIZE, by default)");

static PyTypeObject sensor_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.sensor",
    .tp_doc = sensor_docstring,
    .tp_basicsize = sizeof(struct fpgamap),
    .tp_itemsize = 0,
    .tp_base = &fpgamap_type,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_init = sensor_init,
    .tp_getset = sensor_getset,
};

/*=====================================*
 * Sequencer Register Space
 *=====================================*/
struct seqprogram_array {
    PyObject_HEAD
    uint64_t *program;
};

static Py_ssize_t
seqprogram_array_length(PyObject *self)
{
    return 16; /* Some ambiguity as to its length... */
}

static PyObject *
seqprogram_array_getval(PyObject *self, PyObject *key)
{
    struct seqprogram_array *view = (struct seqprogram_array *)self;
    unsigned long index = PyLong_AsUnsignedLong(key);

    if (PyErr_Occurred()) {
        return NULL;
    }
    if (index >= seqprogram_array_length(self)) {
        PyErr_SetString(PyExc_IndexError, "Array index out of range");
        return NULL;
    }
    return PyLong_FromUnsignedLongLong(view->program[index]);
}

static int
seqprogram_array_setval(PyObject *self, PyObject *key, PyObject *val)
{
    struct seqprogram_array *view = (struct seqprogram_array *)self;
    unsigned long long pgmcommand;
    unsigned long index;
    PyObject *vobject;

    /* Parse the program index. */
    index = PyLong_AsUnsignedLong(key);
    if (PyErr_Occurred()) {
        return -1;
    }
    if (index >= seqprogram_array_length(self)) {
        PyErr_SetString(PyExc_IndexError, "Array index out of range");
        return -1;
    }

    /* Convert whatever we got to an integer and write it. */
    vobject = PyNumber_Long(val);
    if (!vobject) {
        return -1;
    }
    pgmcommand = PyLong_AsUnsignedLongLong(vobject);
    Py_DECREF(vobject);
    if (PyErr_Occurred()) {
        return -1;
    }
    view->program[index] = pgmcommand;
    return 0;
}

static PyMappingMethods seqprogram_as_array = {
    .mp_length = seqprogram_array_length,
    .mp_subscript = seqprogram_array_getval,
    .mp_ass_subscript = seqprogram_array_setval
};

static PyTypeObject seqprogram_arrayview_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.seqprogram",
    .tp_doc = "Sequencer Program Array View",
    .tp_basicsize = sizeof(struct seqprogram_array),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_as_mapping = &seqprogram_as_array,
    .tp_iter = pychronos_array_getiter,
};

static PyObject *
seqprogram_get_arrayview(PyObject *self, void *closure)
{
    struct fpgamap *fmap = (struct fpgamap *)self;
    struct seqprogram_array *view = PyObject_New(struct seqprogram_array, &seqprogram_arrayview_type);
    if (view) {
        view->program = (uint64_t *)fmap->regs + ((FPGA_SEQPROGRAM_BASE - FPGA_SEQUENCER_BASE) / sizeof(uint64_t));
    }
    return (PyObject *)view;
}

static PyGetSetDef sequencer_getset[] = {
    {"control",     fpga_get_uint, fpga_set_uint, "Control Register",             FPGA_REG_TYPED(struct fpga_seq, control, uint16_t)},
    {"status",      fpga_get_uint, fpga_set_uint, "Status Register",              FPGA_REG_TYPED(struct fpga_seq, status, uint16_t)},
    {"frameSize",   fpga_get_uint, fpga_set_uint, "Frame Size Register",          FPGA_REG_SCALAR(struct fpga_seq, frame_size)},
    {"regionStart", fpga_get_uint, fpga_set_uint, "Recording Region Start Address", FPGA_REG_SCALAR(struct fpga_seq, region_start)},
    {"regionStop",  fpga_get_uint, fpga_set_uint, "Recording Region End Address", FPGA_REG_SCALAR(struct fpga_seq, region_stop)},
    {"liveAddr",    fpga_get_arrayview, NULL,     "Live Display Addresses",       FPGA_REG_ARRAY(struct fpga_seq, live_addr)},
    {"trigDelay",   fpga_get_uint, fpga_set_uint, "Trigger Delay Register",       FPGA_REG_SCALAR(struct fpga_seq, trig_delay)},
    {"mdFifo",      fpga_get_uint, NULL,          "MD FIFO Read Register",        FPGA_REG_SCALAR(struct fpga_seq, md_fifo_read)},
    {"writeAddr",   fpga_get_uint, NULL,          "Current Frame Write Address",  FPGA_REG_SCALAR(struct fpga_seq, write_addr)},
    {"lastAddr",    fpga_get_uint, NULL,          "Last Written Frame Address",   FPGA_REG_SCALAR(struct fpga_seq, last_addr)},
    {"seqprogram",  seqprogram_get_arrayview, NULL, "Sequencer Program Registers", NULL},
    /* Sequencer Constant Definitions */
    {"SW_TRIG",     fpga_get_const, NULL,    "Software Trigger Request", (void *)SEQ_CTL_SOFTWARE_TRIG},
    {"START_REC",   fpga_get_const, NULL,    "Start Recording Request",  (void *)SEQ_CTL_START_RECORDING},
    {"STOP_REC",    fpga_get_const, NULL,    "Stop Recording Request",   (void *)SEQ_CTL_STOP_RECORDING},
    {"TRIG_DELAY",  fpga_get_const, NULL,    "Trigger Delay Mode",       (void *)SEQ_CTL_TRIG_DELAY_MODE},
    {"ACTIVE_REC",  fpga_get_const, NULL,    "Active Recording Flag",    (void *)SEQ_STATUS_RECORDING},
    {"FIFO_EMPTY",  fpga_get_const, NULL,    "Sequencer FIFO Empty Flag",(void *)SEQ_STATUS_FIFO_EMPTY},
    {NULL}
};

static int
sequencer_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    struct fpgamap *fmap = (struct fpgamap *)self;
    fmap->roffset = FPGA_SEQUENCER_BASE;
    fmap->rsize = sizeof(struct fpga_seq);
    return fpgamap_init(self, args, kwds);
}

PyDoc_STRVAR(sequencer_docstring,
"sequencer(offset=FPGA_SEQUENCER_BASE, size=FPGA_MAP_SIZE)\n\
--\n\
\n\
Return a new map of the FPGA sequencer register space. This map provides\n\
structured read and write access to the registers which control the\n\
acquisition and storage of frames from the image sensor.\n\
\n\
Parameters\n\
----------\n\
offset : `int`, optional\n\
    Starting offset of the register map (FPGA_SEQUENCER_BASE, by default)\n\
size : `int`, optional\n\
    Length of the register map (FPGA_MAP_SIZE, by default)");

static PyTypeObject sequencer_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.sequencer",
    .tp_doc = sequencer_docstring,
    .tp_basicsize = sizeof(struct fpgamap),
    .tp_itemsize = 0,
    .tp_base = &fpgamap_type,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_init = sequencer_init,
    .tp_getset = sequencer_getset,
};

/*=====================================*
 * Trigger and IO Register Space
 *=====================================*/
static PyGetSetDef trigger_getset[] = {
    {"enable",      fpga_get_uint, fpga_set_uint, "Trigger Enable Mask",        FPGA_REG_TYPED(struct fpga_trigger, enable, uint16_t)},
    {"invert",      fpga_get_uint, fpga_set_uint, "Trigger Input Invert Mask",  FPGA_REG_TYPED(struct fpga_trigger, invert, uint16_t)},
    {"debounce",    fpga_get_uint, fpga_set_uint, "Trigger Debounce Enable",    FPGA_REG_TYPED(struct fpga_trigger, debounce, uint16_t)},
    {"ioOutput",    fpga_get_uint, fpga_set_uint, "IO Output Enable Register",  FPGA_REG_TYPED(struct fpga_trigger, io_output, uint16_t)},
    {"ioSource",    fpga_get_uint, fpga_set_uint, "IO Pullup Enable Register",  FPGA_REG_TYPED(struct fpga_trigger, io_source, uint16_t)},
    {"ioInvert",    fpga_get_uint, fpga_set_uint, "IO Output Invert Mask",      FPGA_REG_TYPED(struct fpga_trigger, io_invert, uint16_t)},
    {"ioInput",     fpga_get_uint, NULL,          "IO Input Status Mask",       FPGA_REG_TYPED(struct fpga_trigger, io_input, uint16_t)},
    {"extShutter",  fpga_get_uint, fpga_set_uint, "External Shutter Control",   FPGA_REG_TYPED(struct fpga_trigger, ext_shutter, uint16_t)},
    /* Sequencer Constant Definitions */
    {"EXT_EXPOSURE", fpga_get_const, NULL,        "Exposure Trigger Enable",   (void *)EXT_SHUTTER_EXPOSURE_ENABLE},
    {"EXT_GATING",  fpga_get_const, NULL,         "Exposure Gating Enable",    (void *)EXT_SHUTTER_GATING_ENABLE},
    {NULL}
};

static int
trigger_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    struct fpgamap *fmap = (struct fpgamap *)self;
    fmap->roffset = FPGA_TRIGGER_BASE;
    fmap->rsize = sizeof(struct fpga_trigger);
    return fpgamap_init(self, args, kwds);
}

PyDoc_STRVAR(trigger_docstring,
"trigger(offset=FPGA_TRIGGER_BASE, size=FPGA_MAP_SIZE)\n\
--\n\
\n\
Return a new map of the FPGA trigger and IO register space. This map\n\
provides structured read and write access to the registers which control\n\
the external trigger and control signals to the FPGA.\n\
\n\
Parameters\n\
----------\n\
offset : `int`, optional\n\
    Starting offset of the register map (FPGA_TRIGGER_BASE, by default)\n\
size : `int`, optional\n\
    Length of the register map (FPGA_MAP_SIZE, by default)");

static PyTypeObject trigger_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.trigger",
    .tp_doc = trigger_docstring,
    .tp_basicsize = sizeof(struct fpgamap),
    .tp_itemsize = 0,
    .tp_base = &fpgamap_type,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_init = trigger_init,
    .tp_getset = trigger_getset,
};

/*=====================================*
 * Color Math Array Helpers
 *=====================================*/
struct color_array {
    PyObject_HEAD
    volatile struct fpga_display *display;
    /* Helpers for buffer access */
    Py_ssize_t ndim;
    Py_ssize_t shape[2];
};

static int
color_array_getbuffer(PyObject *self, Py_buffer *view, int flags)
{
    struct color_array *array = (struct color_array *)self;
    PyTypeObject *type = Py_TYPE(self);
    Py_ssize_t i, length = type->tp_as_mapping->mp_length(self);
    double *dbuffer;

    dbuffer = malloc(length * sizeof(double));
    if (!dbuffer) {
        PyErr_NoMemory();
        return -1;
    }
    /* Refresh the array contents */
    for (i = 0; i < length; i++) {
        PyObject *obj = type->tp_as_mapping->mp_subscript(self, PyLong_FromLong(i));
        if (!obj) {
            free(dbuffer);
            return -1;
        }
        dbuffer[i] = PyFloat_AsDouble(obj);
        Py_DECREF(obj);
    }
    view->buf = dbuffer;
    view->obj = self;
    view->len = length * sizeof(double);
    view->readonly = 1;
    view->itemsize = sizeof(double);
    view->format = (flags & PyBUF_FORMAT) ? "d" : NULL;
    if (flags & PyBUF_ND) {
        view->ndim = array->ndim;
        view->shape = array->shape;
    } else {
        view->ndim = 1;
        view->shape = NULL;
    }
    /* Simple n-dimensional array */
    view->strides = NULL;
    view->suboffsets = NULL;
    view->internal = NULL;

    Py_INCREF(self);
    return 0;
}

static void
color_array_releasebuffer(PyObject *self, Py_buffer *view)
{
    if (view->buf) {
        free(view->buf);
        view->buf = NULL;
    }
}

static Py_ssize_t
wbal_array_length(PyObject *self)
{
    return 3;
}

static PyObject *
wbal_array_getval(PyObject *self, PyObject *key)
{
    struct color_array *view = (struct color_array *)self;
    unsigned long index = PyLong_AsUnsignedLong(key);

    if (PyErr_Occurred()) {
        return NULL;
    }
    if (index >= wbal_array_length(self)) {
        PyErr_SetString(PyExc_IndexError, "Array index out of range");
        return NULL;
    }
    return PyFloat_FromDouble((double)(view->display->wbal[index]) / (1 << COLOR_MATRIX_FRAC_BITS));
}

static int
wbal_array_setval(PyObject *self, PyObject *key, PyObject *val)
{
    struct color_array *view = (struct color_array *)self;
    unsigned long index;
    unsigned long ivalue;

    /* Parse the register address. */
    index = PyLong_AsUnsignedLong(key);
    if (PyErr_Occurred()) {
        return -1;
    }
    if (index >= wbal_array_length(self)) {
        PyErr_SetString(PyExc_IndexError, "Array index out of range");
        return -1;
    }

    /* Parse the register value. */
    ivalue = PyFloat_AsDouble(val) * (1 << COLOR_MATRIX_FRAC_BITS);
    if (PyErr_Occurred()) {
        return -1;
    }
    /* Clamp to min and max limits of the color matrix. */
    if (ivalue > 0xffff) ivalue = 0xffff;
    else if (ivalue < 0) ivalue = 0;
    view->display->wbal[index] = (ivalue & 0xffff);
    return 0;
}

static Py_ssize_t
ccm_array_length(PyObject *self)
{
    return 9;
}

static PyObject *
ccm_array_getval(PyObject *self, PyObject *key)
{
    struct color_array *view = (struct color_array *)self;
    unsigned long index = PyLong_AsUnsignedLong(key);
    int16_t rawval;

    if (PyErr_Occurred()) {
        return NULL;
    }
    if (index >= ccm_array_length(self)) {
        PyErr_SetString(PyExc_IndexError, "Array index out of range");
        return NULL;
    }

    /* Get the raw value... */
    if (index < 3) {
        rawval = (view->display->ccm_red[index % 3] & 0xffff);
    }
    else if (index < 6) {
        rawval = (view->display->ccm_green[index % 3] & 0xffff);
    }
    else {
        rawval = (view->display->ccm_blue[index % 3] & 0xffff);
    }
    return PyFloat_FromDouble((double)rawval / (1 << COLOR_MATRIX_FRAC_BITS));
}

static int
ccm_array_setval(PyObject *self, PyObject *key, PyObject *val)
{
    struct color_array *view = (struct color_array *)self;
    unsigned long index;
    long ivalue;

    /* Parse the register address. */
    index = PyLong_AsUnsignedLong(key);
    if (PyErr_Occurred()) {
        return -1;
    }
    if (index >= ccm_array_length(self)) {
        PyErr_SetString(PyExc_IndexError, "Array index out of range");
        return -1;
    }

    /* Parse the register value. */
    ivalue = PyFloat_AsDouble(val) * (1 << COLOR_MATRIX_FRAC_BITS);
    if (PyErr_Occurred()) {
        return -1;
    }
    /* Clamp to min and max limits of the color matrix. */
    if (ivalue > COLOR_MATRIX_MAXVAL) ivalue = COLOR_MATRIX_MAXVAL;
    if (ivalue < COLOR_MATRIX_MINVAL) ivalue = COLOR_MATRIX_MINVAL;

    /* Write the raw value */
    if (index < 3) {
        view->display->ccm_red[index - 0] = (ivalue & 0xffff);
    }
    else if (index < 6) {
        view->display->ccm_green[index - 3] = (ivalue & 0xffff);
    }
    else {
        view->display->ccm_blue[index - 6] = (ivalue & 0xffff);
    }
    return 0;
}

static PyMappingMethods wbal_as_array = {
    .mp_length = wbal_array_length,
    .mp_subscript = wbal_array_getval,
    .mp_ass_subscript = wbal_array_setval
};

static PyMappingMethods ccm_as_array = {
    .mp_length = ccm_array_length,
    .mp_subscript = ccm_array_getval,
    .mp_ass_subscript = ccm_array_setval
};

static PyBufferProcs color_array_as_buffer = {
    .bf_getbuffer = color_array_getbuffer,
    .bf_releasebuffer = color_array_releasebuffer,
};

static PyTypeObject wbal_arrayview_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.wbarray",
    .tp_doc = "White Balance Matrix Array View",
    .tp_basicsize = sizeof(struct color_array),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_as_buffer = &color_array_as_buffer,
    .tp_as_mapping = &wbal_as_array,
    .tp_iter = pychronos_array_getiter,
};

static PyTypeObject ccm_arrayview_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.ccmarray",
    .tp_doc = "Color Correction Matrix Array View",
    .tp_basicsize = sizeof(struct color_array),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_as_buffer = &color_array_as_buffer,
    .tp_as_mapping = &ccm_as_array,
    .tp_iter = pychronos_array_getiter,
};

static PyObject *
wbal_get_arrayview(PyObject *self, void *closure)
{
    struct fpgamap *fmap = (struct fpgamap *)self;
    struct color_array *view = PyObject_New(struct color_array, &wbal_arrayview_type);
    if (view) {
        view->ndim = 1;
        view->shape[0] = 3;
        view->shape[1] = 0;
        view->display = (struct fpga_display *)fmap->regs;
    }
    return (PyObject *)view;
}

static PyObject *
ccm_get_arrayview(PyObject *self, void *closure)
{
    struct fpgamap *fmap = (struct fpgamap *)self;
    struct color_array *view = PyObject_New(struct color_array, &ccm_arrayview_type);
    if (view) {
        view->ndim = 2;
        view->shape[0] = 3;
        view->shape[1] = 3;
        view->display = (struct fpga_display *)fmap->regs;
    }
    return (PyObject *)view;
}

/*=====================================*
 * Video Display Register Space
 *=====================================*/
static PyGetSetDef display_getset[] = {
    /* Display Registers */
    {"control",     fpga_get_uint, fpga_set_uint, "Control Register",                   FPGA_REG_TYPED(struct fpga_display, control, uint16_t)},
    {"frameAddr",   fpga_get_uint, fpga_set_uint, "Frame Address Register",             FPGA_REG_SCALAR(struct fpga_display, frame_address)},
    {"fpnAddr",     fpga_get_uint, fpga_set_uint, "FPN Address Register",               FPGA_REG_SCALAR(struct fpga_display, fpn_address)},
    {"gain",        fpga_get_uint, fpga_set_uint, "Gain Control Register",              FPGA_REG_TYPED(struct fpga_display, gain, uint16_t)},
    {"hPeriod",     fpga_get_uint, fpga_set_uint, "Horizontal Period Register",         FPGA_REG_TYPED(struct fpga_display, h_period, uint16_t)},
    {"vPeriod",     fpga_get_uint, fpga_set_uint, "Vertical Period Register",           FPGA_REG_TYPED(struct fpga_display, v_period, uint16_t)},
    {"hSyncLen",    fpga_get_uint, fpga_set_uint, "Horizontal Sync Length Register",    FPGA_REG_TYPED(struct fpga_display, h_sync_len, uint16_t)},
    {"vSyncLen",    fpga_get_uint, fpga_set_uint, "Vertical Sync Length Register",      FPGA_REG_TYPED(struct fpga_display, v_sync_len, uint16_t)},
    {"hBackPorch",  fpga_get_uint, fpga_set_uint, "Horizontal Back Porch Register",     FPGA_REG_TYPED(struct fpga_display, h_back_porch, uint16_t)},
    {"vBackPorch",  fpga_get_uint, fpga_set_uint, "Vertical Back Porch Register",       FPGA_REG_TYPED(struct fpga_display, v_back_porch, uint16_t)},
    {"hRes",        fpga_get_uint, fpga_set_uint, "Horizontal Resolution Register",     FPGA_REG_TYPED(struct fpga_display, h_res, uint16_t)},
    {"vRes",        fpga_get_uint, fpga_set_uint, "Vertical Resolution Register",       FPGA_REG_TYPED(struct fpga_display, v_res, uint16_t)},
    {"hOutRes",     fpga_get_uint, fpga_set_uint, "Horizontal Output Register",         FPGA_REG_TYPED(struct fpga_display, h_out_res, uint16_t)},
    {"vOutRes",     fpga_get_uint, fpga_set_uint, "Vertical Output Register",           FPGA_REG_TYPED(struct fpga_display, v_out_res, uint16_t)},
    {"peakThresh",  fpga_get_uint, fpga_set_uint, "Focus Peaking Threshold Register",   FPGA_REG_TYPED(struct fpga_display, peaking_thresh, uint16_t)},
    {"pipeline",    fpga_get_uint, fpga_set_uint, "Display Pipeline Register",          FPGA_REG_TYPED(struct fpga_display, pipeline, uint16_t)},
    {"manualSync",  fpga_get_uint, fpga_set_uint, "Manual Frame Sync Register",         FPGA_REG_TYPED(struct fpga_display, manual_sync, uint16_t)},
    {"gainControl", fpga_get_uint, fpga_set_uint, "Gain Calibration Control Register",  FPGA_REG_TYPED(struct fpga_display, gainctl, uint16_t)},
    {"colorMatrix", ccm_get_arrayview, NULL,      "Color Correction Matrix",            NULL},
    {"whiteBalance", wbal_get_arrayview, NULL,    "White Balance Matrix",               NULL},
    /* Display Constrol Constants */
    {"ADDR_SELECT",     fpga_get_const, NULL, "Address Select",                         (void *)DISPLAY_CTL_ADDRESS_SELECT},
    {"SCALER_NN",       fpga_get_const, NULL, "Scaler NN",                              (void *)DISPLAY_CTL_SCALER_NN},
    {"SYNC_INHIBIT",    fpga_get_const, NULL, "Frame Sync Inhibit",                     (void *)DISPLAY_CTL_SYNC_INHIBIT},
    {"READOUT_INHIBIT", fpga_get_const, NULL, "Readout Inhibit",                        (void *)DISPLAY_CTL_READOUT_INHIBIT},
    {"COLOR_MODE",      fpga_get_const, NULL, "Color Mode",                             (void *)DISPLAY_CTL_COLOR_MODE},
    {"FOCUS_PEAK",      fpga_get_const, NULL, "Focus Peak Enable",                      (void *)DISPLAY_CTL_FOCUS_PEAK_ENABLE},
    {"FOCUS_COLOR",     fpga_get_const, NULL, "Focus Peak Color Mask",                  (void *)DISPLAY_CTL_FOCUS_PEAK_COLOR},
    {"ZEBRA_ENABLE",    fpga_get_const, NULL, "Zebra Stripes Enable",                   (void *)DISPLAY_CTL_ZEBRA_ENABLE},
    {"BLACK_CAL_MODE",  fpga_get_const, NULL, "Black Calibration Mode",                 (void *)DISPLAY_CTL_BLACK_CAL_MODE},
    /* Display Pipeline Constants */
    {"BYPASS_FPN",      fpga_get_const, NULL, "Bypass FPN",                             (void *)DISPLAY_PIPELINE_BYPASS_FPN},
    {"BYPASS_GAIN",     fpga_get_const, NULL, "Bypass Gain",                            (void *)DISPLAY_PIPELINE_BYPASS_GAIN},
    {"BYPASS_DEMOSAIC", fpga_get_const, NULL, "Bypass Demosaic",                        (void *)DISPLAY_PIPELINE_BYPASS_DEMOSAIC},
    {"BYPASS_COLOR_MATRIX", fpga_get_const, NULL, "Bypass Color Matrix",                (void *)DISPLAY_PIPELINE_BYPASS_COLOR_MATRIX},
    {"BYPASS_GAMMA_TABLE", fpga_get_const, NULL, "Bypass FPN",                          (void *)DISPLAY_PIPELINE_BYPASS_GAMMA_TABLE},
    {"RAW_12BPP",       fpga_get_const, NULL, "Enable Raw 12-bit",                      (void *)DISPLAY_PIPELINE_RAW_12BPP},
    {"RAW_16BPP",       fpga_get_const, NULL, "Enable Raw 16-bit",                      (void *)DISPLAY_PIPELINE_RAW_16BPP},
    {"RAW_16PAD",       fpga_get_const, NULL, "Enable 16-bit LSB pad",                  (void *)DISPLAY_PIPELINE_RAW_16PAD},
    {"TEST_PATTERN",    fpga_get_const, NULL, "Enable Test Pattern",                    (void *)DISPLAY_PIPELINE_TEST_PATTERN},
    /* Display Gain Control Constants */
    {"GAINCTL_3POINT",  fpga_get_const, NULL, "Enable 3-point Calibration",             (void *)DISPLAY_GAINCTL_3POINT},
    {NULL}
};

static int
display_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    struct fpgamap *fmap = (struct fpgamap *)self;
    fmap->roffset = FPGA_DISPLAY_BASE;
    fmap->rsize = sizeof(struct fpga_display);
    return fpgamap_init(self, args, kwds);
}

PyDoc_STRVAR(display_docstring,
"display(offset=FPGA_DISPLAY_BASE, size=FPGA_MAP_SIZE)\n\
--\n\
\n\
Return a new map of the FPGA display register space. This map provides\n\
structured read and write access to the registers which control the\n\
playback and display of frames out of video RAM to the primary video\n\
port.\n\
\n\
Parameters\n\
----------\n\
offset : `int`, optional\n\
    Starting offset of the register map (FPGA_DISPLAY_BASE, by default)\n\
size : `int`, optional\n\
    Length of the register map (FPGA_MAP_SIZE, by default)");

static PyTypeObject display_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.display",
    .tp_doc = display_docstring,
    .tp_basicsize = sizeof(struct fpgamap),
    .tp_itemsize = 0,
    .tp_base = &fpgamap_type,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_init = display_init,
    .tp_getset = display_getset,
};

/*=====================================*
 * Misc/Configuration Register Block
 *=====================================*/
static PyGetSetDef config_getset[] = {
    {"wlDelay",     fpga_get_arrayview, NULL,     "Write Leveling Delay",   FPGA_REG_ARRAY(struct fpga_config, wl_delay)},
    {"mmuConfig",   fpga_get_uint, fpga_set_uint, "MMU Configuration",      FPGA_REG_TYPED(struct fpga_config, mmu_config, uint16_t)},
    {"sysReset",    fpga_get_uint, fpga_set_uint, "FPGA Soft Reset Control", FPGA_REG_TYPED(struct fpga_config, sys_reset, uint16_t)},
    {"version",     fpga_get_uint, NULL,          "FPGA Version",           FPGA_REG_SCALAR(struct fpga_config, version)},
    {"subver",      fpga_get_uint, NULL,          "FPGA Sub Version",       FPGA_REG_SCALAR(struct fpga_config, subver)},
    /* MMU Configuration Constants  */
    {"MMU_INVERT_CS",       fpga_get_const, NULL, "Invert SODIMM chip select", (void *)MMU_INVERT_CS},
    {"MMU_SWITCH_STUFFED",  fpga_get_const, NULL, "Stuff 8GB SODIMMs to 16GB space", (void *)MMU_SWITCH_STUFFED},
    {NULL},
};

static int
config_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    struct fpgamap *fmap = (struct fpgamap *)self;
    fmap->roffset = FPGA_CONFIG_BASE;
    fmap->rsize = sizeof(struct fpga_config);
    return fpgamap_init(self, args, kwds);
}

PyDoc_STRVAR(config_docstring,
"config(offset=FPGA_CONFIG_BASE, size=FPGA_MAP_SIZE)\n\
--\n\
\n\
Return a new map of the FPGA configuration register space. This map\n\
provides structured read and write access to the registers which setup\n\
DDR3 SODIMM for storage of video data.\n\
\n\
Parameters\n\
----------\n\
offset : `int`, optional\n\
    Starting offset of the register map (FPGA_CONFIG_BASE, by default)\n\
size : `int`, optional\n\
    Length of the register map (FPGA_MAP_SIZE, by default)");

static PyTypeObject config_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.config",
    .tp_doc = config_docstring,
    .tp_basicsize = sizeof(struct fpgamap),
    .tp_itemsize = 0,
    .tp_base = &fpgamap_type,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_init = config_init,
    .tp_getset = config_getset,
};

/*=====================================*
 * Video RAM Readout Register Space
 *=====================================*/
static PyGetSetDef vram_getset[] = {
    {"id",          fpga_get_uint, NULL,          "Identifier",         FPGA_REG_TYPED(struct fpga_vram, identifier, uint16_t)},
    {"version",     fpga_get_uint, NULL,          "Version",            FPGA_REG_TYPED(struct fpga_vram, version, uint16_t)},
    {"subver",      fpga_get_uint, NULL,          "Sub Version",        FPGA_REG_TYPED(struct fpga_vram, subver, uint16_t)},
    {"control",     fpga_get_uint, fpga_set_uint, "Control Register",   FPGA_REG_TYPED(struct fpga_vram, control, uint16_t)},
    {"status",      fpga_get_uint, NULL,          "Status Register",    FPGA_REG_TYPED(struct fpga_vram, status, uint16_t)},
    {"address",     fpga_get_uint, fpga_set_uint, "Video RAM Address",  FPGA_REG_TYPED(struct fpga_vram, address, uint32_t)},
    {"burst",       fpga_get_uint, fpga_set_uint, "Transaction Burst Length", FPGA_REG_TYPED(struct fpga_vram, burst, uint16_t)},
    {"buffer",      fpgamap_get_buf, NULL,        "Video RAM Buffer",   FPGA_REG_SCALAR(struct fpga_vram, buffer)},
    /* Video RAM Reader Constants */
    {"IDENTIFIER",  fpga_get_const, NULL, "Video RAM Block Identifier", (void *)VRAM_IDENTIFIER},
    {"TRIG_READ",   fpga_get_const, NULL, "Trigger Video RAM Readout",  (void *)VRAM_CTL_TRIG_READ},
    {"TRIG_WRITE",  fpga_get_const, NULL, "Trigger Video RAM Writeback", (void *)VRAM_CTL_TRIG_WRITE},
    {NULL}
};

static int
vram_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    struct fpgamap *fmap = (struct fpgamap *)self;
    fmap->roffset = FPGA_VRAM_BASE;
    fmap->rsize = sizeof(struct fpga_vram);
    return fpgamap_init(self, args, kwds);
}

PyDoc_STRVAR(vram_docstring,
"vram(offset=FPGA_VRAM_BASE, size=FPGA_MAP_SIZE)\n\
--\n\
\n\
Return a new map of the FPGA video RAM readout register space. This map\n\
provides structured read and write access to the registers and cache used\n\
for the readout of data from video RAM.\n\
\n\
Parameters\n\
----------\n\
offset : `int`, optional\n\
    Starting offset of the register map (FPGA_VRAM_BASE, by default)\n\
size : `int`, optional\n\
    Length of the register map (FPGA_MAP_SIZE, by default)");

static PyTypeObject vram_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.vram",
    .tp_doc = vram_docstring,
    .tp_basicsize = sizeof(struct fpgamap),
    .tp_itemsize = 0,
    .tp_base = &fpgamap_type,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_init = vram_init,
    .tp_getset = vram_getset,
};

/*=====================================*
 * Video Overlay Register Space
 *=====================================*/
static PyGetSetDef overlay_getset[] = {
    {"id",          fpga_get_uint, NULL,          "Identifier",                     FPGA_REG_SCALAR(struct fpga_overlay, identifier)},
    {"version",     fpga_get_uint, NULL,          "Version",                        FPGA_REG_SCALAR(struct fpga_overlay, version)},
    {"subver",      fpga_get_uint, NULL,          "Sub Version",                    FPGA_REG_SCALAR(struct fpga_overlay, subver)},
    {"control",     fpga_get_uint, fpga_set_uint, "Control Register",               FPGA_REG_SCALAR(struct fpga_overlay, control)},
    {"status",      fpga_get_uint, NULL,          "Status Register",                FPGA_REG_SCALAR(struct fpga_overlay, status)},
    {"text0hpos",   fpga_get_uint, fpga_set_uint, "Textbox 0 Horizontal Position",  FPGA_REG_SCALAR(struct fpga_overlay, text0_xpos)},
    {"text0vpos",   fpga_get_uint, fpga_set_uint, "Textbox 0 Vertical Position",    FPGA_REG_SCALAR(struct fpga_overlay, text0_ypos)},
    {"text0width",  fpga_get_uint, fpga_set_uint, "Textbox 0 Width",                FPGA_REG_SCALAR(struct fpga_overlay, text0_xsize)},
    {"text0height", fpga_get_uint, fpga_set_uint, "Textbox 0 Hight",                FPGA_REG_SCALAR(struct fpga_overlay, text0_ysize)},
    {"text1hpos",   fpga_get_uint, fpga_set_uint, "Textbox 1 Horizontal Position",  FPGA_REG_SCALAR(struct fpga_overlay, text1_xpos)},
    {"text1vpos",   fpga_get_uint, fpga_set_uint, "Textbox 1 Vertical Position",    FPGA_REG_SCALAR(struct fpga_overlay, text1_ypos)},
    {"text1width",  fpga_get_uint, fpga_set_uint, "Textbox 1 Width",                FPGA_REG_SCALAR(struct fpga_overlay, text1_xsize)},
    {"text1height", fpga_get_uint, fpga_set_uint, "Textbox 1 Hight",                FPGA_REG_SCALAR(struct fpga_overlay, text1_ysize)},
    {"wmarkHpos",   fpga_get_uint, fpga_set_uint, "Watermark Horizontal Position",  FPGA_REG_SCALAR(struct fpga_overlay, wmark_xpos)},
    {"wmarkVpos",   fpga_get_uint, fpga_set_uint, "Watermark Vertical Position",    FPGA_REG_SCALAR(struct fpga_overlay, wmark_ypos)},
    {"text0hoff",   fpga_get_uint, fpga_set_uint, "Textbox 0 Horizontal Offset",    FPGA_REG_SCALAR(struct fpga_overlay, text0_xoffset)},
    {"text1hoff",   fpga_get_uint, fpga_set_uint, "Textbox 1 Horizontal Offset",    FPGA_REG_SCALAR(struct fpga_overlay, text1_xoffset)},
    {"text0voff",   fpga_get_uint, fpga_set_uint, "Textbox 0 Vertical Offset",      FPGA_REG_SCALAR(struct fpga_overlay, text0_yoffset)},
    {"text1voff",   fpga_get_uint, fpga_set_uint, "Textbox 1 Vertical Offset",      FPGA_REG_SCALAR(struct fpga_overlay, text1_yoffset)},
    {"logoHpos",    fpga_get_uint, fpga_set_uint, "Logo Horizontal Position",       FPGA_REG_SCALAR(struct fpga_overlay, logo_xpos)},
    {"logoVpos",    fpga_get_uint, fpga_set_uint, "Logo Vertical Position",         FPGA_REG_SCALAR(struct fpga_overlay, logo_ypos)},
    {"logoWidth",   fpga_get_uint, fpga_set_uint, "Logo Width",                     FPGA_REG_SCALAR(struct fpga_overlay, logo_xsize)},
    {"logoHeight",  fpga_get_uint, fpga_set_uint, "Logo Hight",                     FPGA_REG_SCALAR(struct fpga_overlay, logo_ysize)},
    {"text0color",  fpga_get_arrayview, NULL,     "Textbox 0 Color",                FPGA_REG_ARRAY(struct fpga_overlay, text0_abgr)},
    {"text1color",  fpga_get_arrayview, NULL,     "Textbox 1 Color",                FPGA_REG_ARRAY(struct fpga_overlay, text1_abgr)},
    {"wmarkColor",  fpga_get_arrayview, NULL,     "Watermark Color",                FPGA_REG_ARRAY(struct fpga_overlay, wmark_abgr)},
    {"text0buf",    fpgamap_get_buf, NULL,        "Textbox 0 Buffer",               FPGA_REG_SCALAR(struct fpga_overlay, text0_buffer)},
    {"text1buf",    fpgamap_get_buf, NULL,        "Textbox 1 Buffer",               FPGA_REG_SCALAR(struct fpga_overlay, text1_buffer)},
    {"logoRedLut",  fpga_get_arrayview, NULL,     "Logo Red Lookup Table",          FPGA_REG_ARRAY(struct fpga_overlay, logo_red_lut)},
    {"logoGreenLut", fpga_get_arrayview, NULL,    "Logo Green Lookup Table",        FPGA_REG_ARRAY(struct fpga_overlay, logo_green_lut)},
    {"logoBlueLut", fpga_get_arrayview, NULL,     "Logo Blue Lookup Table",         FPGA_REG_ARRAY(struct fpga_overlay, logo_blue_lut)},
    {"text0bitmap", fpgamap_get_buf, NULL,        "Textbox 0 Font Bitmaps",         FPGA_REG_ARRAY(struct fpga_overlay, text0_bitmap)},
    {"text1bitmap", fpgamap_get_buf, NULL,        "Textbox 1 Font Bitmaps",         FPGA_REG_ARRAY(struct fpga_overlay, text1_bitmap)},
    {"logo",        fpgamap_get_buf, NULL,        "Logo Image Buffer",              FPGA_REG_SCALAR(struct fpga_overlay, logo)},
    {NULL}
};

static int
overlay_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    struct fpgamap *fmap = (struct fpgamap *)self;
    fmap->roffset = FPGA_OVERLAY_BASE;
    fmap->rsize = sizeof(struct fpga_overlay);
    return fpgamap_init(self, args, kwds);
}

PyDoc_STRVAR(overlay_docstring,
"overlay(offset=FPGA_OVERLAY_BASE, size=FPGA_MAP_SIZE)\n\
--\n\
\n\
Return a new map of the FPGA video overlay register space. This map provides\n\
structured read and write access to the registers used to configure text and\n\
and watermarking overlays onto the video stream.\n\
\n\
Parameters\n\
----------\n\
offset : `int`, optional\n\
    Starting offset of the register map (FPGA_OVERLAY_BASE, by default)\n\
size : `int`, optional\n\
    Length of the register map (FPGA_MAP_SIZE, by default)");

static PyTypeObject overlay_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.overlay",
    .tp_doc = overlay_docstring,
    .tp_basicsize = sizeof(struct fpgamap),
    .tp_itemsize = 0,
    .tp_base = &fpgamap_type,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_init = overlay_init,
    .tp_getset = overlay_getset,
};

/*=====================================*
 * Register Types Initialization
 *=====================================*/
int
pychronos_init_regs(PyObject *mod)
{
    int i;
    PyTypeObject *pubtypes[] = {
        &fpgamap_type,
        &sensor_type,
        &sequencer_type,
        &trigger_type,
        &display_type,
        &config_type,
        &vram_type,
        &overlay_type,
    };

    /* Init the arrayview type */
    PyType_Ready(&arrayview_type);
    Py_INCREF(&arrayview_type);
    PyType_Ready(&seqprogram_arrayview_type);
    Py_INCREF(&seqprogram_arrayview_type);
    PyType_Ready(&ccm_arrayview_type);
    Py_INCREF(&ccm_arrayview_type);
    PyType_Ready(&wbal_arrayview_type);
    Py_INCREF(&wbal_arrayview_type);

    /* Load up some macros */
    PyModule_AddIntConstant(mod, "TIMEBASE_HZ", FPGA_TIMEBASE_HZ);
    PyModule_AddIntConstant(mod, "FRAME_WORD_SIZE", FPGA_FRAME_WORD_SIZE);
    PyModule_AddIntMacro(mod, FPGA_MAP_SIZE);
    PyModule_AddIntMacro(mod, FPGA_TIMEBASE_HZ);
    PyModule_AddIntMacro(mod, FPGA_SENSOR_BASE);
    PyModule_AddIntMacro(mod, FPGA_SEQUENCER_BASE);
    PyModule_AddIntMacro(mod, FPGA_TRIGGER_BASE);
    PyModule_AddIntMacro(mod, FPGA_SEQPROGRAM_BASE);
    PyModule_AddIntMacro(mod, FPGA_DISPLAY_BASE);
    PyModule_AddIntMacro(mod, FPGA_CONFIG_BASE);
    PyModule_AddIntMacro(mod, FPGA_COL_GAIN_BASE);
    PyModule_AddIntMacro(mod, FPGA_VRAM_BASE);
    PyModule_AddIntMacro(mod, FPGA_SCI_BASE);
    PyModule_AddIntMacro(mod, FPGA_COL_OFFSET_BASE);
    PyModule_AddIntMacro(mod, FPGA_IO_BASE);
    PyModule_AddIntMacro(mod, FPGA_TIMING_BASE);
    PyModule_AddIntMacro(mod, FPGA_PIPELINE_BASE);
    PyModule_AddIntMacro(mod, FPGA_VIDSRC_BASE);
    PyModule_AddIntMacro(mod, FPGA_CALSRC_BASE);
    PyModule_AddIntMacro(mod, FPGA_OVERLAY_BASE);
    PyModule_AddIntMacro(mod, FPGA_COL_CURVE_BASE);

    /* Register all types. */
    for (i = 0; i < arraysize(pubtypes); i++) {
        PyTypeObject *t = pubtypes[i]; 
        if (PyType_Ready(t) < 0) {
            return -1;
        }
        Py_INCREF(t);
        PyModule_AddObject(mod, strchr(t->tp_name, '.') + 1,  (PyObject *)t);
    }
    return 0;
}
