/* Python runtime. */
#include <Python.h>

#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "fpga.h"
#include "pychronos.h"

Py_buffer fpga_regbuffer = {
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

Py_buffer fpga_rambuffer = {
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

int
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

/*=====================================*
 * Chronos Python Module
 *=====================================*/
PyDoc_STRVAR(pychronos__doc__,
"This module provides an interface to the Chronos FPGA and image sensor.\n\
Each section of the FPGA register space is represented as classes, providing\n\
read and write access to the underlying registers.");

static PyModuleDef pychronos_module = {
    PyModuleDef_HEAD_INIT,
    "pychronos",    /* name of the module */
    pychronos__doc__, /* module documentation */
    -1,             /* module uses global state */
    NULL,           /* module methods */
};

PyMODINIT_FUNC
PyInit_pychronos(void)
{
    PyObject *m, *o;

    /* Initialize the FPGA register mapping and the Python objects. */
    if (pychronos_init_maps()) {
        return NULL;
    }

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

    pychronos_init_regs(m);
    pychronos_init_lux1310(m);
    return m;
}
