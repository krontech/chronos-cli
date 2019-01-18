/* Python runtime. */
#include <Python.h>

#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "fpga.h"
#include "lux1310.h"
#include "pychronos.h"

#if (PY_MAJOR_VERSION < 3)
#error "Required python major version must be >=3"
#endif

struct lux1310map {
    PyObject_HEAD
    volatile struct fpga_sensor *sensor;
};

static PyObject *
lux1310_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    struct lux1310map *self = (struct lux1310map *)type->tp_alloc(type, 0);
    if (pychronos_init_maps() != 0) {
        Py_DECREF(self);
        return NULL;
    }
    self->sensor = (struct fpga_sensor *)(fpga_regbuffer.buf + SENSOR_CONTROL);
    return (PyObject *)self;
}

/* Arrayview helper type */
struct lux1310_arrayview {
    PyObject_HEAD
    volatile struct fpga_sensor *sensor;
};

/* Raw SCI read access. */
static uint16_t
lux1310_sci_read(volatile struct fpga_sensor *sensor, uint16_t address)
{
    int i;

    /* Set RW, address and length. */
    sensor->sci_control |= SENSOR_SCI_CONTROL_RW_MASK;
    sensor->sci_address = address;
    sensor->sci_datalen = 2;

    /* Start the transfer and then wait for completion. */
    sensor->sci_control |= SENSOR_SCI_CONTROL_RUN_MASK;
    for (i = 0; i < 1000; i++) {
        if (!(sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK)) break;
    }
    return sensor->sci_fifo_read;
}

/* Raw SCI write access. */
static void
lux1310_sci_write(volatile struct fpga_sensor *sensor, uint16_t address, uint16_t value)
{
    int i;

    sensor->sci_control = SENSOR_SCI_CONTROL_FIFO_RESET_MASK;
    sensor->sci_address = address;
    sensor->sci_datalen = 2;
    sensor->sci_fifo_write = (value >> 8) & 0xff;
    sensor->sci_fifo_write = (value >> 0) & 0xff;
   
    /* Start the write and wait for completion. */
    sensor->sci_control = SENSOR_SCI_CONTROL_RUN_MASK;
    for (i = 0; i < 1000; i++) {
        if (!(sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK)) break;
    }
}

/*===============================================*
 * Register Array View Methods
 *===============================================*/
static Py_ssize_t
lux1310_array_length(PyObject *self)
{
    return LUX1310_SCI_REGISTER_COUNT;
}

static PyObject *
lux1310_array_getval(PyObject *self, PyObject *key)
{
    struct lux1310_arrayview *view = (struct lux1310_arrayview *)self;
    unsigned long address = PyLong_AsUnsignedLong(key);

    if (PyErr_Occurred()) {
        return NULL;
    }
    if (address >= LUX1310_SCI_REGISTER_COUNT) {
        PyErr_SetString(PyExc_IndexError, "Array index out of range");
        return NULL;
    }
    return PyLong_FromLong(lux1310_sci_read(view->sensor, address));
}

static int
lux1310_array_setval(PyObject *self, PyObject *key, PyObject *value)
{
    struct lux1310_arrayview *view = (struct lux1310_arrayview *)self;
    unsigned long val, address;

    /* Parse the register address. */
    address = PyLong_AsUnsignedLong(key);
    if (PyErr_Occurred()) {
        return -1;
    }
    if (address >= LUX1310_SCI_REGISTER_COUNT) {
        PyErr_SetString(PyExc_IndexError, "Array index out of range");
        return -1;
    }

    /* Parse the register value. */
    val = PyLong_AsUnsignedLong(value);
    if (PyErr_Occurred()) {
        return -1;
    }
    if (val > 0xffff) {
        PyErr_SetString(PyExc_ValueError, "Register value out of range");
        return -1;
    }

    lux1310_sci_write(view->sensor, address, val);
    return 0;
}

static PyMappingMethods lux1310_as_array = {
    .mp_length = lux1310_array_length,
    .mp_subscript = lux1310_array_getval,
    .mp_ass_subscript = lux1310_array_setval
};

static PyTypeObject lux1310_arrayview_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.lux1310.adcoffset",
    .tp_doc = "LUX1310 Register Array View",
    .tp_basicsize = sizeof(struct lux1310_arrayview),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_as_mapping = &lux1310_as_array,
    .tp_iter = pychronos_array_getiter,
};

static PyObject *
lux1310_get_arrayview(PyObject *pyself, void *closure)
{
    struct lux1310map *self = (struct lux1310map *)pyself;
    struct lux1310_arrayview *view = PyObject_New(struct lux1310_arrayview, &lux1310_arrayview_type);
    if (view) {
        view->sensor = self->sensor;
    }
    return (PyObject *)view;
}

/*===============================================*
 * ADC Offset Array View Methods
 *===============================================*/
static Py_ssize_t
adcoffset_array_length(PyObject *self)
{
    return LUX1310_NUM_ADC_CHANNELS;
}

static PyObject *
adcoffset_array_getval(PyObject *self, PyObject *key)
{
    struct lux1310_arrayview *view = (struct lux1310_arrayview *)self;
    unsigned long index = PyLong_AsUnsignedLong(key);
    unsigned long value;

    if (PyErr_Occurred()) {
        return NULL;
    }
    if (index >= LUX1310_NUM_ADC_CHANNELS) {
        PyErr_SetString(PyExc_IndexError, "Array index out of range");
        return NULL;
    }
    value = lux1310_sci_read(view->sensor, LUX1310_SCI_ADC_OS(index) >> LUX1310_SCI_REG_ADDR);

    if (value & 0x400) {
        return PyLong_FromLong(-(value & 0x3ff));
    } else {
        return PyLong_FromLong(value & 0x3ff);
    }
}

static int
adcoffset_array_setval(PyObject *self, PyObject *key, PyObject *value)
{
    struct lux1310_arrayview *view = (struct lux1310_arrayview *)self;
    unsigned long index = PyLong_AsUnsignedLong(key);
    long val;

    /* Parse the register address. */
    if (PyErr_Occurred()) {
        return -1;
    }
    if (index >= LUX1310_NUM_ADC_CHANNELS) {
        PyErr_SetString(PyExc_IndexError, "Array index out of range");
        return -1;
    }

    /* Parse the register value. */
    val = PyLong_AsLong(value);
    if (PyErr_Occurred()) {
        return -1;
    }
    /* Separate cases for the sign bit. */
    if (val > 0) {
        if (val > 0x3ff) {
            PyErr_SetString(PyExc_ValueError, "Register value out of range");
            return -1;
        }
        lux1310_sci_write(view->sensor, LUX1310_SCI_ADC_OS(index) >> LUX1310_SCI_REG_ADDR, val);
    }
    else {
        if (val < -0x3ff) {
            PyErr_SetString(PyExc_ValueError, "Register value out of range");
            return -1;
        }
        lux1310_sci_write(view->sensor, LUX1310_SCI_ADC_OS(index) >> LUX1310_SCI_REG_ADDR, (-val) | 0x400);
    }
    return 0;
}

static PyMappingMethods adcoffset_as_array = {
    .mp_length = adcoffset_array_length,
    .mp_subscript = adcoffset_array_getval,
    .mp_ass_subscript = adcoffset_array_setval
};

static PyTypeObject adcoffset_array_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.lux1310.adcoffset",
    .tp_doc = "LUX1310 ADC Offset Array View",
    .tp_basicsize = sizeof(struct lux1310_arrayview),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_as_mapping = &adcoffset_as_array,
    .tp_iter = pychronos_array_getiter,
};

static PyObject *
adcoffset_get_arrayview(PyObject *pyself, void *closure)
{
    struct lux1310map *self = (struct lux1310map *)pyself;
    struct lux1310_arrayview *view = PyObject_New(struct lux1310_arrayview, &adcoffset_array_type);
    if (view) {
        view->sensor = self->sensor;
    }
    return (PyObject *)view;
}

/*===============================================*
 * LUX1310 Named Registers and Constants
 *===============================================*/
/* Bit hacking to extract a value from a bitmask and shift it down. */
static inline unsigned long
getbits(unsigned long value, unsigned long mask)
{
    if (!mask) {
        return 0;
    }
    else {
        unsigned long lsb = (~mask + 1) & mask;
        return (value & mask) / lsb;
    }
} /* getbits */

static PyObject *
lux1310_get_uint(PyObject *pyself, void *closure)
{
    struct lux1310map *self = (struct lux1310map *)pyself;
    uintptr_t regdef = (uintptr_t)closure;
    uint16_t value = lux1310_sci_read(self->sensor, (regdef >> LUX1310_SCI_REG_ADDR));

    return PyLong_FromLong(getbits(value, regdef & LUX1310_SCI_REG_MASK));
}

static int
lux1310_set_uint(PyObject *pyself, PyObject *value, void *closure)
{
    struct lux1310map *self = (struct lux1310map *)pyself;
    uintptr_t regdef = (uintptr_t)closure;
    uint16_t mask = regdef & LUX1310_SCI_REG_MASK;
    uint16_t readvalue = lux1310_sci_read(self->sensor, (regdef >> LUX1310_SCI_REG_ADDR));
    unsigned long lsb = (~mask + 1) & mask; /* Find the lsb of the mask - for shifting bits into place. */
    unsigned long newvalue;

    /* Parse the register value. */
    newvalue = PyLong_AsUnsignedLong(value) * lsb;;
    if (PyErr_Occurred()) {
        return -1;
    }
    if (newvalue & ~mask) {
        PyErr_SetString(PyExc_ValueError, "Register value out of range");
        return -1;
    }

    /* Mask out the old bits, and replace them with the new. */
    readvalue = (readvalue & ~mask) | newvalue;
    lux1310_sci_write(self->sensor, (regdef >> LUX1310_SCI_REG_ADDR), newvalue);
    return 0;
}

static PyObject *
lux1310_get_bool(PyObject *pyself, void *closure)
{
    struct lux1310map *self = (struct lux1310map *)pyself;
    uintptr_t regdef = (uintptr_t)closure;
    uint16_t value = lux1310_sci_read(self->sensor, (regdef >> LUX1310_SCI_REG_ADDR));

    return PyBool_FromLong(value & (regdef & LUX1310_SCI_REG_MASK));
}

static int
lux1310_set_bool(PyObject *pyself, PyObject *value, void *closure)
{
    struct lux1310map *self = (struct lux1310map *)pyself;
    uintptr_t regdef = (uintptr_t)closure;
    uint16_t regvalue = lux1310_sci_read(self->sensor, (regdef >> LUX1310_SCI_REG_ADDR));

    if (PyObject_IsTrue(value)) {
        regvalue |= (regdef & LUX1310_SCI_REG_MASK);
    } else {
        regvalue &= ~(regdef & LUX1310_SCI_REG_MASK);
    }
    lux1310_sci_write(self->sensor, (regdef >> LUX1310_SCI_REG_ADDR), regvalue);
    return 0;
}

#define LUX1310_GETSET_UINT(_name_, _regdef_, _desc_) \
    { _name_, lux1310_get_uint, lux1310_set_uint, _desc_, (void *)(_regdef_)}

#define LUX1310_GETSET_BOOL(_name_, _regdef_, _desc_) \
    { _name_, lux1310_get_bool, lux1310_set_bool, _desc_, (void *)(_regdef_)}

static PyGetSetDef lux1310_getset[] = {
    /* Special cases for handling arrayview types. */
    {"reg",         lux1310_get_arrayview, NULL, "Raw image sensor registers", NULL},
    {"regAdcOs",    adcoffset_get_arrayview, NULL, "ADC offsets", NULL},
    /* Named subfields registers. */
    {"revChip",     lux1310_get_uint, NULL, "Chip Revision Number",         (void *)LUX1310_SCI_REV_CHIP},
    {"regChipId",   lux1310_get_uint, NULL, "Chip ID Number",               (void *)LUX1310_SCI_CHIP_ID},
    LUX1310_GETSET_BOOL("regTimingEn",      LUX1310_SCI_TIMING_EN,          "Enable timing engine access to wavetable"),
    LUX1310_GETSET_UINT("regSofDelay",      LUX1310_SCI_SOF_DELAY,          "Delay from TXN rising edge to start of frame"),
    LUX1310_GETSET_UINT("regHblank",        LUX1310_SCI_HBLANK,             "Horizontal blanking period"),
    LUX1310_GETSET_UINT("regRoiNb",         LUX1310_SCI_ROI_NB,             "Number of regions of interest when in ROI mode"),
    LUX1310_GETSET_BOOL("regRoiEn",         LUX1310_SCI_ROI_EN,             "Enable regions of interest"),
    LUX1310_GETSET_BOOL("regDrkColRd",      LUX1310_SCI_DRK_COL_RD,         "Enable dark column readout"),
    LUX1310_GETSET_BOOL("regVflip",         LUX1310_SCI_VFLIP,              "Vertical image flip"),
    LUX1310_GETSET_BOOL("regHflip",         LUX1310_SCI_HFLIP,              "Horizontal image flip"),
    LUX1310_GETSET_UINT("regXstart",        LUX1310_SCI_X_START,            "Start of standard window (X direction)"),
    LUX1310_GETSET_UINT("regXend",          LUX1310_SCI_X_END,              "End of standard window (X direction)"),
    LUX1310_GETSET_UINT("regYstart",        LUX1310_SCI_Y_START,            "Start of standard window (Y direction)"),
    LUX1310_GETSET_UINT("regYend",          LUX1310_SCI_Y_END,              "End of standard window (Y direction)"),
    /* TODO: Need special arrayview for ROI sections. */
    LUX1310_GETSET_UINT("regDrkRowsStAddr", LUX1310_SCI_DRK_ROWS_ST_ADDR,   "Address of the first dark row to readout"),
    LUX1310_GETSET_UINT("regNbDrkRows",     LUX1310_SCI_NB_DRK_ROWS,        "Number of dark rows to output"),
    LUX1310_GETSET_UINT("regNextRowAddrOvr",LUX1310_SCI_NEXT_ROW_ADDR_OVR,  "Specify next row address to be read out"),
    LUX1310_GETSET_UINT("regNextRowOvrEn",  LUX1310_SCI_NEXT_ROW_OVR_EN,    "Enable next row to be specified through SCI"),
    LUX1310_GETSET_BOOL("regInterRoiSp",    LUX1310_SCI_INTER_ROI_SP,       "Add a clock of hblack at ROI transitions"),
    LUX1310_GETSET_UINT("regClkSelScip",    LUX1310_SCI_CLK_SEL_SCIP,       "Select clock frequency for SCI interface"),
    LUX1310_GETSET_UINT("regClkSelFt",      LUX1310_SCI_CLK_SEL_FT,         "Select clock frequency for frame timer"),
    LUX1310_GETSET_UINT("regFtTrigNbPulse", LUX1310_SCI_FT_TRIG_NB_PULSE,   "Number of TXN pulses per frame"),
    LUX1310_GETSET_UINT("regFtRstNbPulse",  LUX1310_SCI_FT_RST_NB_PULSE,    "Number of ABN pulses per frame"),
    LUX1310_GETSET_BOOL("regAbn2En",        LUX1310_SCI_ABN2_EN,            "Enable ABN2 usage for odd row exposure"),
    LUX1310_GETSET_UINT("regRdoutDly",      LUX1310_SCI_RDOUT_DLY,          "Delay from start of row to start of readout"),
    LUX1310_GETSET_BOOL("regAdcCalEn",      LUX1310_SCI_ADC_CAL_EN,         "Enable ADC offset registers during vertical blanking"),
    LUX1310_GETSET_UINT("regAdcOsSeqWidth", LUX1310_SCI_ADC_OS_SEQ_WIDTH,   "Time to spend applying each of the ADC offsets"),
    LUX1310_GETSET_UINT("regPclkLinevalid", LUX1310_SCI_PCLK_LINEVALID,     "PCLK channel output when valid pixels"),
    LUX1310_GETSET_UINT("regPclkVblank",    LUX1310_SCI_PCLK_VBLANK,        "PCLK channel output during vertical blanking"),
    LUX1310_GETSET_UINT("regPclkHblank",    LUX1310_SCI_PCLK_HBLANK,        "PCLK channel output during horizontal blanking"),
    LUX1310_GETSET_UINT("regPclkOpticalBlack", LUX1310_SCI_PCLK_OPTICAL_BLACK, "PCLK channel output when output is dark pixels"),
    LUX1310_GETSET_BOOL("regMono",          LUX1310_SCI_MONO,               "Monochrome/color selection (binning mode only)"),
    LUX1310_GETSET_BOOL("regRow2En",        LUX1310_SCI_ROW2EN,             "Enable 2-row averaging"),
    LUX1310_GETSET_BOOL("regPoutsel",       LUX1310_SCI_POUTSEL,            "Pixel output select (test mode)"),
    LUX1310_GETSET_BOOL("regInvertAnalog",  LUX1310_SCI_INVERT_ANALOG,      "Invert polarity of signal chain"),
    LUX1310_GETSET_BOOL("regGlobalShutter", LUX1310_SCI_GLOBAL_SHUTTER,     "Global shutter mode"),
    LUX1310_GETSET_UINT("regGainSelSamp",   LUX1310_SCI_GAIN_SEL_SAMP,      "Gain selection sampling cap"),
    LUX1310_GETSET_UINT("regGainSelFb",     LUX1310_SCI_GAIN_SEL_FB,        "Gain selection feedback cap"),
    LUX1310_GETSET_UINT("regGainBit",       LUX1310_SCI_GAIN_BIT,           "Serial gain"),
    LUX1310_GETSET_BOOL("regColbin2",       LUX1310_SCI_COLBIN2,            "2-column binning"),
    LUX1310_GETSET_UINT("regTstPat",        LUX1310_SCI_TST_PAT,            "Test pattern selection"),
    LUX1310_GETSET_UINT("regCustPat",       LUX1310_SCI_CUST_PAT,           "Custom pattern for ADC training pattern"),
    LUX1310_GETSET_UINT("regMuxMode",       LUX1310_SCI_MUX_MODE,           "ADC channel multiplex mode"),
    LUX1310_GETSET_UINT("regPwrEnSerializerB", LUX1310_SCI_PWR_EN_SERIALIZER_B, "Power enable for LVDS channels 15 to 0"),
    LUX1310_GETSET_UINT("regDacIlv",        LUX1310_SCI_DAC_ILV,            "Current control for LVDS data channels"),
    LUX1310_GETSET_BOOL("regMsbFirstData",  LUX1310_SCI_MSB_FIRST_DATA,     "Output MSB first for data channels"),
    LUX1310_GETSET_BOOL("regPclkInv",       LUX1310_SCI_PCLK_INV,           "Invert PCLK output"),
    LUX1310_GETSET_BOOL("regTermbData",     LUX1310_SCI_TERMB_DATA,         "Enable on-chip termination resistor for data channels"),
    LUX1310_GETSET_BOOL("regDclkInv",       LUX1310_SCI_DCLK_INV,           "Invert DCLK output"),
    LUX1310_GETSET_BOOL("regTermbClk",      LUX1310_SCI_TERMB_CLK,          "Enable on-chip termination resistor for clock channel"),
    LUX1310_GETSET_BOOL("regTermbRxclk",    LUX1310_SCI_TERMB_RXCLK,        "Enable on-chip termination resistor for serializer"),
    LUX1310_GETSET_BOOL("regPwrenDclkB",    LUX1310_SCI_PWREN_DCLK_B,       "DCLK channel power enable (active low)"),
    LUX1310_GETSET_BOOL("regPwrenPclkB",    LUX1310_SCI_PWREN_PCLK_B,       "PCLK channel power enable (active low)"),
    LUX1310_GETSET_BOOL("regPwrenBiasB",    LUX1310_SCI_PWREN_BIAS_B,       "Bias generator power enable (active low)"),
    LUX1310_GETSET_BOOL("regPwrenDrvB",     LUX1310_SCI_PWREN_DRV_B,        "Analog drivers power enable (active low)"),
    LUX1310_GETSET_BOOL("regPwrenAdcB",     LUX1310_SCI_PWREN_ADC_B,        "ADC power enable (active low)"),
    LUX1310_GETSET_UINT("regSelVcmi",       LUX1310_SCI_SEL_VCMI,           "Reserved"),
    LUX1310_GETSET_UINT("regSelVcmo",       LUX1310_SCI_SEL_VCMO,           "Reserved"),
    LUX1310_GETSET_UINT("regSelVcmp",       LUX1310_SCI_SEL_VCMP,           "ADC positive reference voltage setting"),
    LUX1310_GETSET_UINT("regSelVcmn",       LUX1310_SCI_SEL_VCMN,           "ADC negative reference voltage setting"),
    LUX1310_GETSET_UINT("regSelVdumrst",    LUX1310_SCI_SEL_VDUMRST,        "Electrical signal levels for gain calibration"),
    LUX1310_GETSET_BOOL("regHidyEn",        LUX1310_SCI_HIDY_EN,            "Enable high dynamic range operation"),
    LUX1310_GETSET_UINT("regHidyTrigNbPulse", LUX1310_SCI_HIDY_TRIG_NB_PULSE, "Number of PRSTN pulses per frame before triggering HIDY timing"),
    LUX1310_GETSET_UINT("regSelVdr1Width",  LUX1310_SCI_SEL_VDR1_WIDTH,     "Width of the first HDR voltage pulse"),
    LUX1310_GETSET_UINT("regSelVdr2Width",  LUX1310_SCI_SEL_VDR1_WIDTH,     "Width of the second HDR voltage pulse"),
    LUX1310_GETSET_UINT("regSelVdr3Width",  LUX1310_SCI_SEL_VDR1_WIDTH,     "Width of the third HDR voltage pulse"),
    LUX1310_GETSET_UINT("regLvDelay",       LUX1310_SCI_LV_DELAY,           "Linevalid delay to match ADC latency"),
    LUX1310_GETSET_UINT("regSerSync",       LUX1310_SCI_SER_SYNC,           "Synchronized the serializers"),
    LUX1310_GETSET_UINT("regClkSync",       LUX1310_SCI_CLK_SYNC,           "Re-synchronizes the SCIP clock divider"),
    LUX1310_GETSET_UINT("regSresetB",       LUX1310_SCI_SRESET_B,           "Soft reset: resets all registers"),
    {NULL}
};

/*===============================================*
 * Wavetable Loader Function 
 *===============================================*/
PyDoc_STRVAR(
    lux1310_write_wavetable_docstring,
    "wavetable(bytes) -> None\n\n"
    "Load a wavetable into the image sensor.");

static PyObject *
lux1310_write_wavetable(PyObject *pyself, PyObject *args)
{
    struct lux1310map *self = (struct lux1310map *)pyself;
    Py_buffer wavetable;
    int i;

    if (!PyArg_ParseTuple(args, "y*", &wavetable)) {
        return NULL;
    }

    if (PyBuffer_IsContiguous(&wavetable, 'C')) {
        const uint8_t *wtdata = (const uint8_t *)wavetable.buf;

        /* Clear the RW flag and reset the FIFO */
        self->sensor->sci_control = SENSOR_SCI_CONTROL_FIFO_RESET_MASK;
        self->sensor->sci_address = 0x7F;
        self->sensor->sci_datalen = wavetable.len;
        for (i = 0; i < wavetable.len; i++) {
            self->sensor->sci_fifo_write = wtdata[i];
        }
    }
    /* Complicated case for complex buffers. */
    else {
        uint8_t *wtdata = malloc(wavetable.len);
        if (!wtdata) {
            PyErr_NoMemory();
            return NULL;
        }
        PyBuffer_ToContiguous(wtdata, &wavetable, wavetable.len, 'C');

        self->sensor->sci_control = SENSOR_SCI_CONTROL_FIFO_RESET_MASK;
        self->sensor->sci_address = 0x7F;
        self->sensor->sci_datalen = wavetable.len;
        for (i = 0; i < wavetable.len; i++) {
            self->sensor->sci_fifo_write = wtdata[i];
        }
        free(wtdata);
    }

    /* Start the transfer and wait for completion. */
    self->sensor->sci_control = SENSOR_SCI_CONTROL_RUN_MASK;
    while (self->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK);
    Py_RETURN_NONE;
}

static PyMethodDef lux1310_methods[] = {
    {"wavetable", lux1310_write_wavetable, METH_VARARGS, lux1310_write_wavetable_docstring},
    {NULL, NULL, 0, NULL},
};

static PyTypeObject lux1310_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.lux1310",
    .tp_doc = "Luxima LUX1310 Register Map",
    .tp_basicsize = sizeof(struct lux1310map),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = lux1310_new,
    .tp_methods = lux1310_methods,
    .tp_getset = lux1310_getset,
};

/* Initialization */
int
pychronos_init_lux1310(PyObject *mod)
{
    int i;
    PyTypeObject *types[] = {
        &lux1310_type,
    };

    PyType_Ready(&lux1310_arrayview_type);
    Py_INCREF(&lux1310_arrayview_type);
    PyType_Ready(&adcoffset_array_type);
    Py_INCREF(&adcoffset_array_type);

    /* Register all types. */
    for (i = 0; i < arraysize(types); i++) {
        PyTypeObject *t = types[i]; 
        if (PyType_Ready(t) < 0) {
            return -1;
        }
        Py_INCREF(t);
        PyModule_AddObject(mod, strchr(t->tp_name, '.') + 1,  (PyObject *)t);
    }
    return 0;
}
