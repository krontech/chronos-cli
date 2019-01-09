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

PyDoc_STRVAR(
    lux1310_read_docstring,
    "read(address) -> int\n\n"
    "Read a 16-bit register from the LUX1310 image sensor."); 

static PyObject *
lux1310_read(PyObject *pyself, PyObject *args)
{
    struct lux1310map *self = (struct lux1310map *)pyself;
    unsigned long address;
    uint16_t first;
    int i = 0;

    /* Get the address to read. */
    if (!PyArg_ParseTuple(args, "l", &address)) {
        return NULL;
    }
    if ((address < 0) || (address > 0x7F)) {
        PyErr_SetString(PyExc_ValueError, "Address out of range");
        return NULL;
    }

    /* Set RW, address and length. */
    self->sensor->sci_control |= SENSOR_SCI_CONTROL_RW_MASK;
    self->sensor->sci_address = address;
    self->sensor->sci_datalen = 2;

    /* Start the transfer and then wait for completion. */
    self->sensor->sci_control |= SENSOR_SCI_CONTROL_RUN_MASK;
    first = self->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK;
    while (self->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK) {
        i++;
    }

#if 0
    if (!first && (i != 0)) {
        fprintf(stderr, "lux1310_read: Read first busy was missed, address: 0x%02x\n", r->offset);
    }
    else if (i == 0) {
        fprintf(stderr, "lux1310_read: Read busy not detected, something probably wrong, address: 0x%02x\n", r->offset);
    }
#endif
    usleep(1000);
    return PyLong_FromLong(self->sensor->sci_fifo_read);
}

PyDoc_STRVAR(
    lux1310_write_docstring,
    "write(address, value) -> None\n\n"
    "Write a 16-bit register to the LUX1310 image sensor.");

static PyObject *
lux1310_write(PyObject *pyself, PyObject *args)
{
    struct lux1310map *self = (struct lux1310map *)pyself;
    unsigned long address, value;
    int i = 0;

    /* Get the address and value to write. */
    if (!PyArg_ParseTuple(args, "ll", &address, &value)) {
        return NULL;
    }
    if ((address < 0) || (address > 0x7F)) {
        PyErr_SetString(PyExc_ValueError, "Address out of range");
        return NULL;
    }
    if ((value < 0) || (value > 0xffff)) {
        PyErr_SetString(PyExc_ValueError, "Value out of range");
        return NULL;
    }

    self->sensor->sci_control = SENSOR_SCI_CONTROL_FIFO_RESET_MASK;
    self->sensor->sci_address = address;
    self->sensor->sci_datalen = 2;
    self->sensor->sci_fifo_write = (value >> 8) & 0xff;
    self->sensor->sci_fifo_write = (value >> 0) & 0xff;
   
    /* Start the write and wait for completion. */
    self->sensor->sci_control = SENSOR_SCI_CONTROL_RUN_MASK;
    while (self->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK) {
        i++;
    }
    
    fprintf(stderr, "DEBUG: address=%lu, value=%lu, loops=%d\n", address, value, i);
    Py_RETURN_NONE;

#if 0
	int i = 0;

	//Clear RW and reset FIFO
	gpmc->write16(SENSOR_SCI_CONTROL_ADDR, 0x8000);

	//Set up address, transfer length and put data into FIFO
	gpmc->write16(SENSOR_SCI_ADDRESS_ADDR, address);
	gpmc->write16(SENSOR_SCI_DATALEN_ADDR, 2);
	gpmc->write16(SENSOR_SCI_FIFO_WR_ADDR_ADDR, data >> 8);
	gpmc->write16(SENSOR_SCI_FIFO_WR_ADDR_ADDR, data & 0xFF);

	// Start the write and wait for completion.
	gpmc->write16(SENSOR_SCI_CONTROL_ADDR, SENSOR_SCI_CONTROL_RUN_MASK);
	while(gpmc->read16(SENSOR_SCI_CONTROL_ADDR) & SENSOR_SCI_CONTROL_RUN_MASK) i++;

	if (readback) {
		UInt16 readback;

		if (i == 0) {
			qDebug() << "No busy detected, something is probably very wrong, address:" << address;
		}
		readback = SCIRead(address);
		if (data != readback) {
			qDebug() << "SCI readback wrong, address: " << address << " expected: " << data << " got: " << readback;
			return false;
		}
	}
	return true;



    return PyLong_FromLong(0);
#endif
}

PyDoc_STRVAR(
    lux1310_write_wavetable_docstring,
    "write_wavetable(value) -> None\n\n"
    "Writes a wavetable to the image sensor.");

static PyObject *
lux1310_write_wavetable(PyObject *pyself, PyObject *args)
{
    struct lux1310map *self = (struct lux1310map *)pyself;
    char *wtdata;
    int wtlength;
    int i;

    if (!PyArg_ParseTuple(args, "y*", &wtdata, &wtlength)) {
        return NULL;
    }

	/* Clear the RW flag and reset the FIFO */
    self->sensor->sci_control = SENSOR_SCI_CONTROL_FIFO_RESET_MASK;
    self->sensor->sci_address = 0x7F;
    self->sensor->sci_datalen = wtlength;
	for (i = 0; i < wtlength; i++) {
        self->sensor->sci_fifo_write = wtdata[i];
	}

    /* Start the transfer and wait for completion. */
    self->sensor->sci_control = SENSOR_SCI_CONTROL_RUN_MASK;
    while (self->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK);
    Py_RETURN_NONE;
}

static PyMethodDef lux1310_methods[] = {
    {"read", lux1310_read, METH_VARARGS, lux1310_read_docstring},
    {"write", lux1310_write, METH_VARARGS, lux1310_write_docstring},
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
};

/* Initialization */
int
pychronos_init_lux1310(PyObject *mod)
{
    int i;
    PyTypeObject *types[] = {
        &lux1310_type,
    };

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
