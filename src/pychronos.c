/* Python runtime. */
#include <Python.h>

#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "fpga.h"

#if (PY_MAJOR_VERSION < 3)
#error "Required python major version must be >=3"
#endif

#ifndef arraysize
#define arraysize(_array_) (sizeof(_array_)/sizeof((_array_)[0]))
#endif

#ifndef offsetof
#define offsetof(_type_, _member_) (uintptr_t)(&((_type_ *)0)->_member_)
#endif

#define FPGA_MAP_SIZE (16 * 1024 * 1024)

static int pychronos_init_maps(void);

static Py_buffer fpga_regbuffer = {
    .buf = MAP_FAILED,
    .obj = NULL,
    .len = FPGA_MAP_SIZE,
    .format = "<H",
    .readonly = 0,
    .itemsize = sizeof(uint16_t),
    .ndim = 1,
    .shape = (Py_ssize_t[]){FPGA_MAP_SIZE / sizeof(uint16_t)},
    .strides = (Py_ssize_t[]){sizeof(uint16_t)},
};

static Py_buffer fpga_rambuffer = {
    .buf = MAP_FAILED,
    .obj = NULL,
    .len = FPGA_MAP_SIZE,
    .format = "<H",
    .readonly = 0,
    .itemsize = sizeof(uint16_t),
    .ndim = 1,
    .shape = (Py_ssize_t[]){FPGA_MAP_SIZE / sizeof(uint16_t)},
    .strides = (Py_ssize_t[]){sizeof(uint16_t)},
};

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

#define FPGA_REG_TYPED(_container_, _member_, _type_) \
    (struct fpgamap_reginfo []){offsetof(_container_, _member_), sizeof(_type_), 0}

#define FPGA_REG_SCALAR(_container_, _member_) \
    (struct fpgamap_reginfo []){offsetof(_container_, _member_), sizeof(((_container_ *)0)->_member_), 0}

#define FPGA_REG_ARRAY(_container_, _member_) \
    (struct fpgamap_reginfo []){offsetof(_container_, _member_), sizeof(((_container_ *)0)->_member_[0]), arraysize(((_container_ *)0)->_member_)}

#define fpgaptr(_self_, _offset_, _type_) \
    (_type_ *)((unsigned char *)(((struct fpgamap *)_self_)->regs) + (uintptr_t)(_offset_))

static int
pychronos_init_maps(void)
{
    static int fd = -1;
    if (fd >= 0) return 0;

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return -1;
    }

    fpga_regbuffer.buf = mmap(0, FPGA_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, GPMC_RANGE_BASE + GPMC_REGISTER_OFFSET);
    if (fpga_regbuffer.buf == MAP_FAILED) {
        PyErr_SetFromErrno(PyExc_OSError);
        close(fd);
        fd = -1;
        return -1;
    }

    fpga_rambuffer.buf = mmap(0, FPGA_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, GPMC_RANGE_BASE + GPMC_RAM_OFFSET);
    if (fpga_rambuffer.buf == MAP_FAILED) {
        PyErr_SetFromErrno(PyExc_OSError);
        munmap(fpga_regbuffer.buf, FPGA_MAP_SIZE);
        close(fd);
        fd = -1;
        return -1;
    }

    /* Success */
    return 0;
}

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
    fmap->regs = (uint16_t *)fpga_regbuffer.buf + fmap->roffset;
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

    /* TODO: We should just call fpgamap_get_uint from here. */
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

    /* TODO: We should just call fpgamap_set_uint from here. */
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
};

/* Getter to return an arrayview object. */
static PyObject *
fpgamap_get_arrayview(PyObject *self, void *closure)
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
    {"mem8",  fpgamap_get_arrayview, NULL, "8-bit memory array view",  (struct fpgamap_reginfo []){0, sizeof(uint8_t), ULONG_MAX}},
    {"mem16", fpgamap_get_arrayview, NULL, "16-bit memory array view", (struct fpgamap_reginfo []){0, sizeof(uint16_t), ULONG_MAX}},
    {"mem32", fpgamap_get_arrayview, NULL, "32-bit memory array view", (struct fpgamap_reginfo []){0, sizeof(uint32_t), ULONG_MAX}},
    { NULL },
};

static PyTypeObject fpgamap_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.fpgamap",
    .tp_doc = "Abstract FPGA Register Map",
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
fpgamap_get_uint(PyObject *self, void *closure)
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
fpgamap_set_uint(PyObject *self, PyObject *value, void *closure)
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
fpgamap_get_const(PyObject *self, void *closure)
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
    {"control",     fpgamap_get_uint, fpgamap_set_uint, "Control Register",             FPGA_REG_TYPED(struct fpga_sensor, control, uint16_t)},
    {"clkPhase",    fpgamap_get_uint, fpgamap_set_uint, "Clock Phase Register",         FPGA_REG_TYPED(struct fpga_sensor, clk_phase, uint16_t)},
    {"syncToken",   fpgamap_get_uint, fpgamap_set_uint, "Sync Token Register",          FPGA_REG_TYPED(struct fpga_sensor, sync_token, uint16_t)},
    {"dataCorrect", fpgamap_get_uint, NULL,             "Data Correct Status Register", FPGA_REG_TYPED(struct fpga_sensor, data_correct, uint16_t)},
    {"fifoStart",   fpgamap_get_uint, fpgamap_set_uint, "FIFO Starting Address Register", FPGA_REG_TYPED(struct fpga_sensor, fifo_start, uint16_t)},
    {"fifoStop",    fpgamap_get_uint, fpgamap_set_uint, "FIFO Ending Address Register", FPGA_REG_TYPED(struct fpga_sensor, fifo_stop, uint16_t)},
    {"framePeriod", fpgamap_get_uint, fpgamap_set_uint, "Frame Period Register",        FPGA_REG_TYPED(struct fpga_sensor, frame_period, uint32_t)},
    {"intTime",     fpgamap_get_uint, fpgamap_set_uint, "Integration Time Register",    FPGA_REG_TYPED(struct fpga_sensor, int_time, uint32_t)},
    {"sciControl",  fpgamap_get_uint, fpgamap_set_uint, "SCI Control Register",         FPGA_REG_TYPED(struct fpga_sensor, sci_control, uint16_t)},
    {"sciAddress",  fpgamap_get_uint, fpgamap_set_uint, "SCI Address Register",         FPGA_REG_TYPED(struct fpga_sensor, sci_address, uint16_t)},
    {"sciDataLen",  fpgamap_get_uint, fpgamap_set_uint, "SCI Data Length Register",     FPGA_REG_TYPED(struct fpga_sensor, sci_datalen, uint16_t)},
    {"sciFifoAddr", fpgamap_get_uint, fpgamap_set_uint, "SCI Address FIFO Register",    FPGA_REG_TYPED(struct fpga_sensor, sci_fifo_addr, uint16_t)},
    {"sciFifoData", fpgamap_get_uint, fpgamap_set_uint, "SCI Data FIFO Register",       FPGA_REG_TYPED(struct fpga_sensor, sci_fifo_data, uint16_t)},
    /* Sensor Constant Definitions */
    {"RESET",           fpgamap_get_const, NULL, "Control Reset Flag", (void *)IMAGE_SENSOR_RESET_MASK},
    {"EVEN_TIMESLOT",   fpgamap_get_const, NULL, "Even Timeslot Flag", (void *)IMAGE_SENSOR_EVEN_TIMESLOT_MASK},
    {"SCI_RUN",         fpgamap_get_const, NULL, "SCI Run Mask",       (void *)SENSOR_SCI_CONTROL_RUN_MASK},
    {"SCI_RW",          fpgamap_get_const, NULL, "SCI Read/Write",     (void *)SENSOR_SCI_CONTROL_RW_MASK},
    {"SCI_FULL",        fpgamap_get_const, NULL, "SCI FIFO Full",      (void *)SENSOR_SCI_CONTROL_FIFO_FULL_MASK},
    {NULL}
};

static int
sensor_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    struct fpgamap *fmap = (struct fpgamap *)self;
    fmap->roffset = SENSOR_CONTROL;
    fmap->rsize = sizeof(struct fpga_sensor);
    return fpgamap_init(self, args, kwds);
}

static PyTypeObject sensor_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.sensor",
    .tp_doc = "FPGA Sensor Object",
    .tp_basicsize = sizeof(struct fpgamap),
    .tp_itemsize = 0,
    .tp_base = &fpgamap_type,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_init = sensor_init,
    .tp_getset = sensor_getset,
};

/*=====================================*
 * Sequencer Register Space
 *=====================================*/
static PyGetSetDef sequencer_getset[] = {
    {"control",     fpgamap_get_uint, fpgamap_set_uint, "Control Register",             FPGA_REG_TYPED(struct fpga_seq, control, uint16_t)},
    {"status",      fpgamap_get_uint, fpgamap_set_uint, "Status Register",              FPGA_REG_TYPED(struct fpga_seq, status, uint16_t)},
    {"frameSize",   fpgamap_get_uint, fpgamap_set_uint, "Frame Size Register",          FPGA_REG_SCALAR(struct fpga_seq, frame_size)},
    {"regionStart", fpgamap_get_uint, fpgamap_set_uint, "Recording Region Start Address", FPGA_REG_SCALAR(struct fpga_seq, region_start)},
    {"regionStop",  fpgamap_get_uint, fpgamap_set_uint, "Recording Region End Address", FPGA_REG_SCALAR(struct fpga_seq, region_stop)},
    /* TODO: live_addr[3]; How to handle arrays? */
    {"trigDelay",   fpgamap_get_uint, fpgamap_set_uint, "Trigger Delay Register",       FPGA_REG_SCALAR(struct fpga_seq, trig_delay)},
    {"mdFifo",      fpgamap_get_uint, NULL,            "MD FIFO Read Register",         FPGA_REG_SCALAR(struct fpga_seq, md_fifo_read)},
    /* Sequencer Constant Definitions */
    {"SW_TRIG",     fpgamap_get_const, NULL,    "Software Trigger Request", (void *)SEQ_CTL_SOFTWARE_TRIG},
    {"START_REC",   fpgamap_get_const, NULL,    "Start Recording Request",  (void *)SEQ_CTL_START_RECORDING},
    {"STOP_REC",    fpgamap_get_const, NULL,    "Stop Recording Request",   (void *)SEQ_CTL_STOP_RECORDING},
    {"TRIG_DELAY",  fpgamap_get_const, NULL,    "Trigger Delay Mode",       (void *)SEQ_CTL_TRIG_DELAY_MODE},
    {"ACTIVE_REC",  fpgamap_get_const, NULL,    "Active Recording Flag",    (void *)SEQ_STATUS_RECORDING},
    {"FIFO_EMPTY",  fpgamap_get_const, NULL,    "Sequencer FIFO Empty Flag",(void *)SEQ_STATUS_FIFO_EMPTY},
    {NULL}
};

static int
sequencer_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    struct fpgamap *fmap = (struct fpgamap *)self;
    fmap->roffset = SEQ_CONTROL;
    fmap->rsize = sizeof(struct fpga_seq);
    return fpgamap_init(self, args, kwds);
}

static PyTypeObject sequencer_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.sequencer",
    .tp_doc = "FPGA Recording Sequencer Object",
    .tp_basicsize = sizeof(struct fpgamap),
    .tp_itemsize = 0,
    .tp_base = &fpgamap_type,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_init = sequencer_init,
    .tp_getset = sequencer_getset,
};

/*=====================================*
 * Video Display Register Space
 *=====================================*/
static PyGetSetDef display_getset[] = {
    /* Display Registers */
    {"control",     fpgamap_get_uint, fpgamap_set_uint, "Control Register",                 FPGA_REG_TYPED(struct fpga_display, control, uint16_t)},
    {"frameAddr",   fpgamap_get_uint, fpgamap_set_uint, "Frame Address Register",           FPGA_REG_SCALAR(struct fpga_display, frame_address)},
    {"fpnAddr",     fpgamap_get_uint, fpgamap_set_uint, "FPN Address Register",             FPGA_REG_SCALAR(struct fpga_display, fpn_address)},
    {"gain",        fpgamap_get_uint, fpgamap_set_uint, "Gain Control Register",            FPGA_REG_TYPED(struct fpga_display, gain, uint16_t)},
    {"hPeriod",     fpgamap_get_uint, fpgamap_set_uint, "Horizontal Period Register",       FPGA_REG_TYPED(struct fpga_display, h_period, uint16_t)},
    {"vPeriod",     fpgamap_get_uint, fpgamap_set_uint, "Vertical Period Register",         FPGA_REG_TYPED(struct fpga_display, v_period, uint16_t)},
    {"hSyncLen",    fpgamap_get_uint, fpgamap_set_uint, "Horizontal Sync Length Register",  FPGA_REG_TYPED(struct fpga_display, h_sync_len, uint16_t)},
    {"vSyncLen",    fpgamap_get_uint, fpgamap_set_uint, "Vertical Sync Length Register",    FPGA_REG_TYPED(struct fpga_display, v_sync_len, uint16_t)},
    {"hBackPorch",  fpgamap_get_uint, fpgamap_set_uint, "Horizontal Back Porch Register",   FPGA_REG_TYPED(struct fpga_display, h_back_porch, uint16_t)},
    {"vBackPorch",  fpgamap_get_uint, fpgamap_set_uint, "Vertical Back Porch Register",     FPGA_REG_TYPED(struct fpga_display, v_back_porch, uint16_t)},
    {"hRes",        fpgamap_get_uint, fpgamap_set_uint, "Horizontal Resolution Register",   FPGA_REG_TYPED(struct fpga_display, h_res, uint16_t)},
    {"vRes",        fpgamap_get_uint, fpgamap_set_uint, "Vertical Resolution Register",     FPGA_REG_TYPED(struct fpga_display, v_res, uint16_t)},
    {"hOutRes",     fpgamap_get_uint, fpgamap_set_uint, "Horizontal Output Register",       FPGA_REG_TYPED(struct fpga_display, h_out_res, uint16_t)},
    {"vOutRes",     fpgamap_get_uint, fpgamap_set_uint, "Vertical Output Register",         FPGA_REG_TYPED(struct fpga_display, v_out_res, uint16_t)},
    {"peakThresh",  fpgamap_get_uint, fpgamap_set_uint, "Focus Peaking Threshold Register", FPGA_REG_TYPED(struct fpga_display, peaking_thresh, uint16_t)},
    {"pipeline",    fpgamap_get_uint, fpgamap_set_uint, "Display Pipeline Register",        FPGA_REG_TYPED(struct fpga_display, pipeline, uint16_t)},
    {"manualSync",  fpgamap_get_uint, fpgamap_set_uint, "Manual Frame Sync Register",       FPGA_REG_TYPED(struct fpga_display, manual_sync, uint16_t)},
    /* Display Constrol Constants */
    {"ADDR_SELECT",     fpgamap_get_const, NULL, "Address Select",          (void *)DISPLAY_CTL_ADDRESS_SELECT},
    {"SCALER_NN",       fpgamap_get_const, NULL, "Scaler NN",               (void *)DISPLAY_CTL_SCALER_NN},
    {"SYNC_INHIBIT",    fpgamap_get_const, NULL, "Frame Sync Inhibit",      (void *)DISPLAY_CTL_SYNC_INHIBIT},
    {"READOUT_INHIBIT", fpgamap_get_const, NULL, "Readout Inhibit",         (void *)DISPLAY_CTL_READOUT_INHIBIT},
    {"COLOR_MODE",      fpgamap_get_const, NULL, "Color Mode",              (void *)DISPLAY_CTL_COLOR_MODE},
    {"FOCUS_PEAK",      fpgamap_get_const, NULL, "Focus Peak Enable",       (void *)DISPLAY_CTL_FOCUS_PEAK_ENABLE},
    {"FOCUS_COLOR",     fpgamap_get_const, NULL, "Focus Peak Color Mask",   (void *)DISPLAY_CTL_FOCUS_PEAK_COLOR},
    {"ZEBRA_ENABLE",    fpgamap_get_const, NULL, "Zebra Stripes Enable",    (void *)DISPLAY_CTL_ZEBRA_ENABLE},
    {"BLACK_CAL_MODE",  fpgamap_get_const, NULL, "Black Calibration Mode",  (void *)DISPLAY_CTL_BLACK_CAL_MODE},
    /* Display Pipeline Constants */
    {"BYPASS_FPN",      fpgamap_get_const, NULL, "Bypass FPN",              (void *)DISPLAY_PIPELINE_BYPASS_FPN},
    {"BYPASS_GAIN",     fpgamap_get_const, NULL, "Bypass Gain",             (void *)DISPLAY_PIPELINE_BYPASS_GAIN},
    {"BYPASS_DEMOSAIC", fpgamap_get_const, NULL, "Bypass Demosaic",         (void *)DISPLAY_PIPELINE_BYPASS_DEMOSAIC},
    {"BYPASS_COLOR_MATRIX", fpgamap_get_const, NULL, "Bypass Color Matrix", (void *)DISPLAY_PIPELINE_BYPASS_COLOR_MATRIX},
    {"BYPASS_GAMMA_TABLE", fpgamap_get_const, NULL, "Bypass FPN",           (void *)DISPLAY_PIPELINE_BYPASS_GAMMA_TABLE},
    {"RAW_12BPP",       fpgamap_get_const, NULL, "Enable Raw 12-bit",       (void *)DISPLAY_PIPELINE_RAW_12BPP},
    {"RAW_16BPP",       fpgamap_get_const, NULL, "Enable Raw 16-bit",       (void *)DISPLAY_PIPELINE_RAW_16BPP},
    {"RAW_16PAD",       fpgamap_get_const, NULL, "Enable 16-bit LSB pad",   (void *)DISPLAY_PIPELINE_RAW_16PAD},
    {"TEST_PATTERN",    fpgamap_get_const, NULL, "Enable Test Pattern",     (void *)DISPLAY_PIPELINE_TEST_PATTERN},
    {NULL}
};

static int
display_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    struct fpgamap *fmap = (struct fpgamap *)self;
    fmap->roffset = DISPLAY_CTL;
    fmap->rsize = sizeof(struct fpga_display);
    return fpgamap_init(self, args, kwds);
}

static PyTypeObject display_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.display",
    .tp_doc = "FPGA Display Object",
    .tp_basicsize = sizeof(struct fpgamap),
    .tp_itemsize = 0,
    .tp_base = &fpgamap_type,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_init = display_init,
    .tp_getset = display_getset,
};

/*=====================================*
 * Video RAM Readout Register Space
 *=====================================*/
static PyGetSetDef vram_getset[] = {
    {"id",          fpgamap_get_uint, NULL,             "Identifier",         FPGA_REG_TYPED(struct fpga_vram, identifier, uint16_t)},
    {"version",     fpgamap_get_uint, NULL,             "Version",            FPGA_REG_TYPED(struct fpga_vram, version, uint16_t)},
    {"subver",      fpgamap_get_uint, NULL,             "Sub Version",        FPGA_REG_TYPED(struct fpga_vram, subver, uint16_t)},
    {"control",     fpgamap_get_uint, fpgamap_set_uint, "Control Register",   FPGA_REG_TYPED(struct fpga_vram, control, uint16_t)},
    {"status",      fpgamap_get_uint, NULL,             "Status Register",    FPGA_REG_TYPED(struct fpga_vram, status, uint16_t)},
    {"address",     fpgamap_get_uint, fpgamap_set_uint, "Video RAM Address",  FPGA_REG_TYPED(struct fpga_vram, address, uint32_t)},
    {"burst",       fpgamap_get_uint, fpgamap_set_uint, "Transaction Burst Length", FPGA_REG_TYPED(struct fpga_vram, burst, uint16_t)},
    {"buffer",      fpgamap_get_buf, NULL,              "Video RAM Buffer",   FPGA_REG_SCALAR(struct fpga_vram, buffer)},
    /* Video RAM Reader Constants */
    {"IDENTIFIER",  fpgamap_get_const, NULL, "Video RAM Block Identifier",  (void *)VRAM_IDENTIFIER},
    {"TRIG_READ",   fpgamap_get_const, NULL, "Trigger Video RAM Readout",   (void *)VRAM_CTL_TRIG_READ},
    {"TRIG_WRITE",  fpgamap_get_const, NULL, "Trigger Video RAM Writeback", (void *)VRAM_CTL_TRIG_WRITE},
    {NULL}
};

static int
vram_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    struct fpgamap *fmap = (struct fpgamap *)self;
    fmap->roffset = VRAM_OFFSET;
    fmap->rsize = sizeof(struct fpga_vram);
    return fpgamap_init(self, args, kwds);
}

static PyTypeObject vram_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.vram",
    .tp_doc = "FPGA Video RAM Readout Object",
    .tp_basicsize = sizeof(struct fpgamap),
    .tp_itemsize = 0,
    .tp_base = &fpgamap_type,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_init = vram_init,
    .tp_getset = vram_getset,
};

/*=====================================*
 * Video Overlay Register Space
 *=====================================*/
static PyGetSetDef overlay_getset[] = {
    {"id",          fpgamap_get_uint, NULL,             "Identifier",                   FPGA_REG_SCALAR(struct fpga_overlay, identifier)},
    {"version",     fpgamap_get_uint, NULL,             "Version",                      FPGA_REG_SCALAR(struct fpga_overlay, version)},
    {"subver",      fpgamap_get_uint, NULL,             "Sub Version",                  FPGA_REG_SCALAR(struct fpga_overlay, subver)},
    {"control",     fpgamap_get_uint, fpgamap_set_uint, "Control Register",             FPGA_REG_SCALAR(struct fpga_overlay, control)},
    {"status",      fpgamap_get_uint, NULL,             "Status Register",              FPGA_REG_SCALAR(struct fpga_overlay, status)},
    {"text0hpos",   fpgamap_get_uint, fpgamap_set_uint, "Textbox 0 Horizontal Position", FPGA_REG_SCALAR(struct fpga_overlay, text0_xpos)},
    {"text0vpos",   fpgamap_get_uint, fpgamap_set_uint, "Textbox 0 Vertical Position",  FPGA_REG_SCALAR(struct fpga_overlay, text0_ypos)},
    {"text0width",  fpgamap_get_uint, fpgamap_set_uint, "Textbox 0 Width",              FPGA_REG_SCALAR(struct fpga_overlay, text0_xsize)},
    {"text0height", fpgamap_get_uint, fpgamap_set_uint, "Textbox 0 Hight",              FPGA_REG_SCALAR(struct fpga_overlay, text0_ysize)},
    {"text1hpos",   fpgamap_get_uint, fpgamap_set_uint, "Textbox 1 Horizontal Position", FPGA_REG_SCALAR(struct fpga_overlay, text1_xpos)},
    {"text1vpos",   fpgamap_get_uint, fpgamap_set_uint, "Textbox 1 Vertical Position",  FPGA_REG_SCALAR(struct fpga_overlay, text1_ypos)},
    {"text1width",  fpgamap_get_uint, fpgamap_set_uint, "Textbox 1 Width",              FPGA_REG_SCALAR(struct fpga_overlay, text1_xsize)},
    {"text1height", fpgamap_get_uint, fpgamap_set_uint, "Textbox 1 Hight",              FPGA_REG_SCALAR(struct fpga_overlay, text1_ysize)},
    {"wmarkHpos",   fpgamap_get_uint, fpgamap_set_uint, "Watermark Horizontal Position", FPGA_REG_SCALAR(struct fpga_overlay, wmark_xpos)},
    {"wmarkVpos",   fpgamap_get_uint, fpgamap_set_uint, "Watermark Vertical Position",  FPGA_REG_SCALAR(struct fpga_overlay, wmark_ypos)},
    {"text0hoff",   fpgamap_get_uint, fpgamap_set_uint, "Textbox 0 Horizontal Offset",  FPGA_REG_SCALAR(struct fpga_overlay, text0_xoffset)},
    {"text1hoff",   fpgamap_get_uint, fpgamap_set_uint, "Textbox 1 Horizontal Offset",  FPGA_REG_SCALAR(struct fpga_overlay, text1_xoffset)},
    {"text0voff",   fpgamap_get_uint, fpgamap_set_uint, "Textbox 0 Vertical Offset",    FPGA_REG_SCALAR(struct fpga_overlay, text0_yoffset)},
    {"text1voff",   fpgamap_get_uint, fpgamap_set_uint, "Textbox 1 Vertical Offset",    FPGA_REG_SCALAR(struct fpga_overlay, text1_yoffset)},
    {"logoHpos",    fpgamap_get_uint, fpgamap_set_uint, "Logo Horizontal Position",     FPGA_REG_SCALAR(struct fpga_overlay, logo_xpos)},
    {"logoVpos",    fpgamap_get_uint, fpgamap_set_uint, "Logo Vertical Position",       FPGA_REG_SCALAR(struct fpga_overlay, logo_ypos)},
    {"logoWidth",   fpgamap_get_uint, fpgamap_set_uint, "Logo Width",                   FPGA_REG_SCALAR(struct fpga_overlay, logo_xsize)},
    {"logoHeight",  fpgamap_get_uint, fpgamap_set_uint, "Logo Hight",                   FPGA_REG_SCALAR(struct fpga_overlay, logo_ysize)},
    {"text0color",  fpgamap_get_arrayview, NULL,        "Textbox 0 Color",              FPGA_REG_ARRAY(struct fpga_overlay, text0_abgr)},
    {"text1color",  fpgamap_get_arrayview, NULL,        "Textbox 1 Color",              FPGA_REG_ARRAY(struct fpga_overlay, text1_abgr)},
    {"wmarkColor",  fpgamap_get_arrayview, NULL,        "Watermark Color",              FPGA_REG_ARRAY(struct fpga_overlay, wmark_abgr)},
    {"text0buf",    fpgamap_get_buf, NULL,              "Textbox 0 Buffer",             FPGA_REG_SCALAR(struct fpga_overlay, text0_buffer)},
    {"text1buf",    fpgamap_get_buf, NULL,              "Textbox 1 Buffer",             FPGA_REG_SCALAR(struct fpga_overlay, text1_buffer)},
    {"logoRedLut",  fpgamap_get_arrayview, NULL,        "Logo Red Lookup Table",        FPGA_REG_ARRAY(struct fpga_overlay, logo_red_lut)},
    {"logoGreenLut", fpgamap_get_arrayview, NULL,       "Logo Green Lookup Table",      FPGA_REG_ARRAY(struct fpga_overlay, logo_green_lut)},
    {"logoBlueLut", fpgamap_get_arrayview, NULL,        "Logo Blue Lookup Table",       FPGA_REG_ARRAY(struct fpga_overlay, logo_blue_lut)},
    {"text0bitmap", fpgamap_get_buf, NULL,              "Textbox 0 Font Bitmaps",       FPGA_REG_ARRAY(struct fpga_overlay, text0_bitmap)},
    {"text1bitmap", fpgamap_get_buf, NULL,              "Textbox 1 Font Bitmaps",       FPGA_REG_ARRAY(struct fpga_overlay, text1_bitmap)},
    {"logo",        fpgamap_get_buf, NULL,              "Logo Image Buffer",            FPGA_REG_SCALAR(struct fpga_overlay, logo)},
    {NULL}
};

static int
overlay_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    struct fpgamap *fmap = (struct fpgamap *)self;
    fmap->roffset = OVERLAY_CONTROL;
    fmap->rsize = sizeof(struct fpga_overlay);
    return fpgamap_init(self, args, kwds);
}

static PyTypeObject overlay_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.overlay",
    .tp_doc = "FPGA Video Overlay Object",
    .tp_basicsize = sizeof(struct fpgamap),
    .tp_itemsize = 0,
    .tp_base = &fpgamap_type,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_init = overlay_init,
    .tp_getset = overlay_getset,
};

/*=====================================*
 * Chronos Python Module
 *=====================================*/
static PyModuleDef pychronos_module = {
    PyModuleDef_HEAD_INIT,
    "pychronos",    /* name of the module */
    NULL,           /* module documentation */
    -1,             /* module uses global state */
    NULL,           /* module methods */
};

PyMODINIT_FUNC
PyInit_pychronos(void)
{
    int i;
    PyObject *m, *o;
    PyTypeObject *types[] = {
        &fpgamap_type,
        &sensor_type,
        &sequencer_type,
        &display_type,
        &vram_type,
        &overlay_type,
    };

    /* Initialize the FPGA register mapping and the Python objects. */
    if (pychronos_init_maps()) {
        return NULL;
    }
    PyType_Ready(&arrayview_type);

    /* Load the module. */
    m = PyModule_Create(&pychronos_module);
    if (m == NULL) {
        return NULL;
    }

    /* Register the raw memory mapping */
    o = PyMemoryView_FromBuffer(&fpga_regbuffer);
    if (o == NULL) {
        return NULL;
    }
    Py_INCREF(o);
    PyModule_AddObject(m, "reg",  o);

    o = PyMemoryView_FromBuffer(&fpga_regbuffer);
    if (o == NULL) {
        return NULL;
    }
    Py_INCREF(o);
    PyModule_AddObject(m, "ram",  o);

    /* Register all types. */
    Py_INCREF(&arrayview_type);
    for (i = 0; i < arraysize(types); i++) {
        PyTypeObject *t = types[i]; 
        if (PyType_Ready(t) < 0) {
            return NULL;
        }
        Py_INCREF(t);
        PyModule_AddObject(m, strchr(t->tp_name, '.') + 1,  (PyObject *)t);
    }
    return m;
}
