/* Python runtime. */
#include <Python.h>
#include <structmember.h>

#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

#include "fpga.h"
#include "pychronos.h"

Py_buffer fpga_regbuffer = {
    .buf = MAP_FAILED,
    .obj = NULL,
    .len = FPGA_MAP_SIZE,
    .format = "H",
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
    .format = "H",
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

/*===============================================*
 * Generic Array Iterator
 *===============================================*/
struct pychronos_arrayiter {
    PyObject_HEAD
    PyObject *array;
    Py_ssize_t length;
    Py_ssize_t next;
};

static PyObject *
pychronos_arrayiter_next(PyObject *self)
{
    struct pychronos_arrayiter *iter = (struct pychronos_arrayiter *)self;
    PyTypeObject *type = Py_TYPE(iter->array);

    if (iter->next < iter->length) {
        PyObject *ret = type->tp_as_mapping->mp_subscript(iter->array, PyLong_FromLong(iter->next));
        iter->next++;
        return ret;
    }
    else {
        PyErr_SetNone(PyExc_StopIteration);
        return NULL;
    }
}

static void
pychronos_arrayiter_finalize(PyObject *self)
{
    struct pychronos_arrayiter *iter = (struct pychronos_arrayiter *)self;
    Py_DECREF(iter->array);
}

static PyTypeObject pychronos_arrayiter_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.arrayiter",
    .tp_doc = "Generic Array Iterator",
    .tp_basicsize = sizeof(struct pychronos_arrayiter),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_FINALIZE,
    .tp_new = PyType_GenericNew,
    .tp_iternext = pychronos_arrayiter_next,
    .tp_finalize = pychronos_arrayiter_finalize,
};

PyObject *
pychronos_array_getiter(PyObject *self)
{
    struct pychronos_arrayiter *iter = PyObject_New(struct pychronos_arrayiter, &pychronos_arrayiter_type);
    PyTypeObject *type = Py_TYPE(self);
    if (iter) {
        Py_INCREF(self);
        iter->array = self;
        iter->length = type->tp_as_mapping->mp_length(self);
        iter->next = 0;
    }
    return (PyObject *)iter;
}

/*=====================================*
 * Frame Object Type
 *=====================================*/
struct frame_object {
    PyObject_VAR_HEAD
    Py_ssize_t vRes;
    Py_ssize_t hRes;
    uint16_t data[];
};

static Py_ssize_t
frame_array_length(PyObject *self)
{
    struct frame_object *frame = (struct frame_object *)self;
    return (frame->hRes * frame->vRes);
}

static PyObject *
frame_array_getval(PyObject *self, PyObject *key)
{
    struct frame_object *frame = (struct frame_object *)self;
    unsigned long index = PyLong_AsUnsignedLong(key);

    if (PyErr_Occurred()) {
        return NULL;
    }
    if (index >= (frame->hRes * frame->vRes)) {
        PyErr_SetString(PyExc_IndexError, "Array index out of range");
        return NULL;
    }
    return PyLong_FromLong(frame->data[index]);
}

static int
frame_array_setval(PyObject *self, PyObject *key, PyObject *val)
{
    struct frame_object *frame = (struct frame_object *)self;
    unsigned long index, value;

    /* Parse the register address. */
    index = PyLong_AsUnsignedLong(key);
    if (PyErr_Occurred()) {
        return -1;
    }
    if (index >= (frame->hRes * frame->vRes)) {
        PyErr_SetString(PyExc_IndexError, "Array index out of range");
        return -1;
    }

    /* Parse the register value. */
    value = PyLong_AsUnsignedLong(val);
    if (PyErr_Occurred()) {
        return -1;
    }
    if (value >= 0xfff) {
        PyErr_SetString(PyExc_ValueError, "Register value out of range");
        return -1;
    }
    frame->data[index] = value;
    return 0;
}

static PyMappingMethods frame_as_array = {
    .mp_length = frame_array_length,
    .mp_subscript = frame_array_getval,
    .mp_ass_subscript = frame_array_setval
};

static int
frame_buffer_getbuffer(PyObject *self, Py_buffer *view, int flags)
{
    struct frame_object *frame = (struct frame_object *)self;

    view->buf = frame->data;
    view->obj = self;
    view->len = frame->hRes * frame->vRes * sizeof(uint16_t);
    view->readonly = 0;
    view->itemsize = sizeof(uint16_t);
    view->format = (flags & PyBUF_FORMAT) ? "H" : NULL;
    if (flags & PyBUF_ND) {
        view->ndim = 2;
        view->shape = &frame->vRes; /* Superhacky */
    } else {
        view->ndim = 1;
        view->shape = NULL;
    }
    /* Simple 2-dimensional array */
    view->strides = NULL;
    view->suboffsets = NULL;
    view->internal = NULL;

    Py_INCREF(self);
    return 0;
}

static PyBufferProcs frame_as_buffer = {
    .bf_getbuffer = frame_buffer_getbuffer,
    .bf_releasebuffer = NULL,
};

static PyObject *
frame_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    struct frame_object *self;
    unsigned long hRes, vRes;

    if (!PyArg_ParseTuple(args, "ll", &hRes, &vRes)) {
        return NULL;
    }
    self = (struct frame_object *)type->tp_alloc(type, hRes * vRes);
    if (self) {
        self->hRes = hRes;
        self->vRes = vRes;
    }
    return (PyObject *)self;
}

static PyMemberDef frame_members[] = {
    { "hRes", T_PYSSIZET, offsetof(struct frame_object, hRes), READONLY, "Horizontal Resolution" },
    { "vRes", T_PYSSIZET, offsetof(struct frame_object, vRes), READONLY, "Vertical Resolution" },
    { NULL }
};

PyDoc_STRVAR(frame_docstring,
"frame(hRes, vRes)\n\
--\n\
\n\
Create a frame buffer to contain an image of the desired resolution\n\
and initially set to zeroes. The frame can be accessed as a 2D array\n\
of integers.\n\
\n\
Parameters\n\
----------\n\
hRes, vRes : `int`\n\
    Horizontal and vertical resolution of the frame");

static PyTypeObject frame_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pychronos.frame",
    .tp_doc = frame_docstring,
    .tp_basicsize = sizeof(struct frame_object),
    .tp_itemsize = sizeof(uint16_t),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = frame_new,
    .tp_as_buffer = &frame_as_buffer,
    .tp_as_mapping = &frame_as_array,
    .tp_iter = pychronos_array_getiter,
    .tp_members = frame_members,
};

/*=====================================*
 * Fast RAM Readout Helper
 *=====================================*/
static void *
pychronos_read_alloc(volatile struct fpga_vram *vram, unsigned long address, unsigned long length)
{
    /* Acquire some scratch memory for the readout. */
    uint8_t *buffer = malloc(length);
    if (!buffer) {
        PyErr_NoMemory();
        return NULL;
    }

    /* Can we do readout via the fast region? */
    if (vram->identifier == VRAM_IDENTIFIER) {
        uint32_t offset = 0;
        int i;
    
        vram->burst = 0x20;
        for (offset = 0; offset < length; offset += sizeof(vram->buffer)) {
            /* Read a page from memory into the VRAM buffer. */
            vram->address = address + (offset / FPGA_FRAME_WORD_SIZE);
            vram->control = VRAM_CTL_TRIG_READ;
            for (i = 0; i < 1000; i++) {
                if (vram->control == 0) break;
            }

            /* Copy memory out of the VRAM buffer. */
            if ((offset + sizeof(vram->buffer)) >= length) {
                memcpy(buffer + offset, (void *)vram->buffer, length - offset);
            } else {
                memcpy(buffer + offset, (void *)vram->buffer, sizeof(vram->buffer));
            }
        }
    }
    /* Readout via the old method. */
    else {
        uint16_t *registers = (uint16_t *)fpga_regbuffer.buf;
        registers[GPMC_PAGE_OFFSET + 0] = (address & 0x0000ffff) >> 0;
        registers[GPMC_PAGE_OFFSET + 1] = (address & 0xffff0000) >> 16;
        memcpy(buffer, fpga_rambuffer.buf, length);
        registers[GPMC_PAGE_OFFSET + 0] = 0;
        registers[GPMC_PAGE_OFFSET + 1] = 0;
    }

    return buffer;
}

static void
pychronos_write_ram(volatile struct fpga_vram *vram, unsigned long address, const uint8_t *data, unsigned long len)
{
    /* If the fast VRAM block is unavailable, use the slow interface */
    if (vram->identifier != VRAM_IDENTIFIER) {
        uint16_t *registers = (uint16_t *)fpga_regbuffer.buf;
        registers[GPMC_PAGE_OFFSET + 0] = (address & 0x0000ffff) >> 0;
        registers[GPMC_PAGE_OFFSET + 1] = (address & 0xffff0000) >> 16;
        memcpy(fpga_rambuffer.buf, data, len);
        registers[GPMC_PAGE_OFFSET + 0] = 0;
        registers[GPMC_PAGE_OFFSET + 1] = 0;
    }
    /* Otherwise, write the data in bursts via the VRAM block. */
    else {
        uint32_t offset = 0;
        int i;
    
        vram->burst = 0x20;
        for (offset = 0; offset < len; offset += sizeof(vram->buffer)) {
            /* Copy memory into the VRAM buffer. */
            if ((offset + sizeof(vram->buffer)) >= len) {
                memcpy((void *)vram->buffer, data + offset, len - offset);
            } else {
                memcpy((void *)vram->buffer, data + offset, sizeof(vram->buffer));
            }

            /* Write the page into memory using the fast VRAM block. */
            vram->address = address + (offset / FPGA_FRAME_WORD_SIZE);
            vram->control = VRAM_CTL_TRIG_WRITE;
            for (i = 0; i < 1000; i++) {
                if (vram->control == 0) break;
            }
        }
    }
}

PyDoc_STRVAR(pychronos_read_raw_docstring,
    "readraw(address, length) -> bytes\n\n"
    "Read a raw data from acquisition memory.");

static PyObject *
pychronos_read_raw(PyObject *self, PyObject *args)
{
    struct fpga_vram *vram = (struct fpga_vram *)(fpga_regbuffer.buf + FPGA_VRAM_BASE);
    unsigned long address, length;
    uint8_t *buffer;
    PyObject *obj;

    if (!PyArg_ParseTuple(args, "ll", &address, &length)) {
        return NULL;
    }

    buffer = pychronos_read_alloc(vram, address, length);
    if (!buffer) {
        return NULL;
    }

    obj = Py_BuildValue("y#", buffer, (int)length);
    free(buffer);
    return obj;
}

PyDoc_STRVAR(pychronos_read_frame_docstring,
    "readframe(address, width, height) -> array\n\n"
    "Read a frame from acquisition memory.");

static PyObject *
pychronos_read_frame(PyObject *self, PyObject *args)
{
    struct fpga_vram *vram = (struct fpga_vram *)(fpga_regbuffer.buf + FPGA_VRAM_BASE);
    struct frame_object *frame;
    unsigned long address, length;
    uint8_t *buffer;
    uint16_t *pixels;
    int ndims[2];
    int i;

    if (!PyArg_ParseTuple(args, "lii", &address, &ndims[1], &ndims[0])) {
        return NULL;
    }
    if ((ndims[0] <= 0) || (ndims[1] <= 0)) {
        PyErr_SetString(PyExc_ValueError, "Resolution out of range");
        return NULL;
    }

    /* Read the raw packed frame out of RAM. */
    length = (ndims[0] * ndims[1] * 12) / 8;
    buffer = pychronos_read_alloc(vram, address, length);
    if (!buffer) {
        return NULL;
    }

    frame = PyObject_NewVar(struct frame_object, &frame_type, ndims[0] * ndims[1]);
    if (!frame) {
        free(buffer);
        return NULL;
    }
    frame->hRes = ndims[1];
    frame->vRes = ndims[0];
    pixels = frame->data;

#ifdef __ARM_NEON
    for (i = 0; (i + 24) <= length; i += 24) {
        asm volatile (
            "   vld3.8 {d0,d1,d2}, [%[s]]   \n"
            /* low nibble of split byte to high nibble of first pixel. */
            "   vshll.u8  q2, d1, #8        \n"
            "   vaddw.u8  q2, q2, d0        \n"
            "   vand.u16  q2, #0x0fff       \n" /* q2 = first pixel */
            /* high nibble of split byte to low nibble of second pixel. */
            "   vshr.u8   d1, d1, #4        \n"
            "   vshll.u8  q3, d2, #4        \n"
            "   vaddw.u8  q3, q3, d1        \n" /* q3 = second pixel */
            /* write out */
            "   vst2.16 {q2,q3}, [%[d]]     \n"
            :: [d]"r"(pixels), [s]"r"(buffer + i) : );
        pixels += 16;
    }
#else
    for (i = 0; (i+3) <= length; i += 3) {
        uint8_t split = buffer[i + 1];
        *(pixels++) = (buffer[i + 0] << 0) | (split & 0x0f) << 8;
        *(pixels++) = (buffer[i + 2] << 4) | (split & 0xf0) >> 4;
    }
#endif

    free(buffer);
    return (PyObject *)frame;
}

PyDoc_STRVAR(pychronos_write_frame_docstring,
    "writeframe(address, buffer) -> None\n\n"
    "Write a frame to acquisition memory.");

static PyObject *
pychronos_write_frame(PyObject *self, PyObject *args)
{
    struct fpga_vram *vram = (struct fpga_vram *)(fpga_regbuffer.buf + FPGA_VRAM_BASE);
    unsigned long address;
    PyObject *frame;
    Py_buffer pbuffer;

    if (!PyArg_ParseTuple(args, "lO", &address, &frame)) {
        return NULL;
    }
    if (PyObject_GetBuffer(frame, &pbuffer, PyBUF_FORMAT | PyBUF_ND) != 0) {
        return NULL;
    }

    /* Handle by array item size. */
    if (pbuffer.itemsize == 1) {
        /* Assume a raw frame already packed into 12-bit data. */
        pychronos_write_ram(vram, address, pbuffer.buf, pbuffer.len);
    }
    /* Convert uint16 array into 12-bit packed frame data. */
    else if (pbuffer.itemsize == 2) {
        int i;

        /* Allocate memory for the packed representation */
        Py_ssize_t packsize = (pbuffer.len * 12) / (pbuffer.itemsize * 8);
        uint16_t *pixels = pbuffer.buf;
        uint8_t *packed = malloc(packsize);
        if (!packed) {
            PyErr_NoMemory();
            return NULL;
        }

#ifdef __ARM_NEON
        for (i = 0; (i + 24) <= packsize; i += 24) {
            asm volatile (
                "   vld2.16 {q0,q1}, [%[s]] \n" /* q0 = first pixel, q1 = second pixel */
                "   vmovn.u16 d4, q0        \n" /* d4 = low 8-lsb of first pixel */
                "   vshrn.u16 d6, q1, #4    \n" /* d6 = high 8-msb of second pixel */
                /* Combine the split byte */
                "   vshrn.u16 d5, q0, #8    \n" /* d5 = high nibble of first pixel */
                "   vmovn.u16 d0, q1        \n" /* d0 = low 8-msb of second pixel */
                "   vsli.8    d5, d0, #4    \n" /* d5 = split byte */
                /* write out */
                "   vst3.8 {d4,d5,d6}, [%[d]]   \n"
                :: [d]"r"(packed + i), [s]"r"(pixels) : );
            pixels += 16;
        }
#else
        for (i = 0; (i+3) <= packsize; i += 3) {
            uint16_t first = *(pixels++);
            uint16_t second = *(pixels++);
            packed[i + 0] = ((first >> 0) & 0xff);
            packed[i + 1] = ((first >> 8) & 0xf0) | ((second >> 0) & 0xf);
            packed[i + 2] = ((second >> 4) & 0xff);
        }
#endif
        pychronos_write_ram(vram, address, packed, packsize);
        free(packed);
    }
    /* TODO: Convert uint32 and floating point types. */
    else {
        PyErr_SetString(PyExc_NotImplementedError, "Unable to convert buffer with itemsize > 2");
        PyBuffer_Release(&pbuffer);
        return NULL;
    }

    PyBuffer_Release(&pbuffer);
    Py_RETURN_NONE;
}

/*=====================================*
 * SPI Read and Write Helper
 *=====================================*/
static PyObject *
pychronos_write_spi(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *ret = NULL;
    void *rxbuf;
    int spifd = -1;

    /* Python argument parsing. */
    char *keywords[] = {
        "device",
        "data",
        "mode",
        "csel",
        "speed",
        "bitsPerWord",
        NULL,
    };
    const char *path;
    Py_buffer data;
    unsigned char mode = 0;
    unsigned long speed = 1000000;
    unsigned int wordsize = 0;
    const char *csel = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sy*|bskI", keywords,
            &path, &data, &mode, &csel, &speed, &wordsize)) {
        return NULL;
    }

    rxbuf = malloc(data.len);
    if(!rxbuf) {
        PyBuffer_Release(&data);
        PyErr_NoMemory();
        return NULL;
    }
    /* Open the SPI device. */
    spifd = open(path, O_RDWR);
    if (spifd < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        PyBuffer_Release(&data);
        free(rxbuf);
        return NULL;
    }

    /* Break out to cleanup and return */
    do {
        struct spi_ioc_transfer xfer = {
            .tx_buf = (uintptr_t)data.buf,
            .rx_buf = (uintptr_t)rxbuf,
            .len = data.len,
            .speed_hz = speed,
            .delay_usecs = 0,
            .bits_per_word = wordsize,
            .cs_change = 0,
        };

        if (ioctl(spifd, SPI_IOC_WR_MODE, &mode)) {
            PyErr_SetFromErrno(PyExc_OSError);
            break;
        }
        /* Optional soft chip-select */
        if (csel) {
            int spics = open(csel, O_WRONLY);
            if (spics < 0) {
                PyErr_SetFromErrno(PyExc_OSError);
                break;
            }
            write(spics, "0", 1);
            if (ioctl(spifd, SPI_IOC_MESSAGE(1), &xfer) < 0) {
                PyErr_SetFromErrno(PyExc_OSError);
            } else {
                ret = PyBytes_FromStringAndSize(rxbuf, data.len);
            }
            write(spics, "1", 1);
            close(spics);
        }
        /* Hardware chipselect */
        else {
            if (ioctl(spifd, SPI_IOC_MESSAGE(1), &xfer) < 0) {
                PyErr_SetFromErrno(PyExc_OSError);
            } else {
                ret = PyBytes_FromStringAndSize(rxbuf, data.len);
            }
        }
    } while(0);

    /* Cleanup */
    PyBuffer_Release(&data);
    free(rxbuf);
    close(spifd);
    return ret;
}

PyDoc_STRVAR(pychronos_write_spi_docstring,
"writespi(device, data, csel=None, mode=0)\n\
--\n\
\n\
Helper function to write data to an SPI device and return the bytes\n\
received back via the MISO signal.\n\
\n\
Parameters\n\
----------\n\
device : `string`\n\
    Path to the SPI device to open for writing.\n\
data : `bytes`\n\
    Data to write to the SPI device.\n\
mode : `int`, optional\n\
    SPI clock phase and polarity (default: 0).\n\
csel : `string`, optional\n\
    Path to GPIO device to use as a soft chip select.\n\
speed : `int`, optional\n\
    SPI device speed in Hertz (default: 1000000).\n\
bitsPerWord : `int`, optional\n\
    SPI word size in bits (default: 8).");

/*=====================================*
 * Chronos Python Module
 *=====================================*/
PyDoc_STRVAR(pychronos__doc__,
"This module provides an interface to the Chronos FPGA and image sensor.\n\
Each section of the FPGA register space is represented as classes, providing\n\
read and write access to the underlying registers.");

static PyMethodDef pychronos_methods[] = {
    {"readraw",  pychronos_read_raw,        METH_VARARGS, pychronos_read_raw_docstring},
    {"readframe",  pychronos_read_frame,    METH_VARARGS, pychronos_read_frame_docstring},
    {"writeframe", pychronos_write_frame,   METH_VARARGS, pychronos_write_frame_docstring},
    {"writespi",   (PyCFunction)pychronos_write_spi, METH_VARARGS | METH_KEYWORDS, pychronos_write_spi_docstring},
    {NULL, NULL, 0, NULL}
};

static PyModuleDef pychronos_module = {
    PyModuleDef_HEAD_INIT,
    "pychronos",        /* name of the module */
    pychronos__doc__,   /* module documentation */
    -1,                 /* module uses global state */
    pychronos_methods,  /* module methods */
};

PyMODINIT_FUNC
PyInit_pychronos(void)
{
    int i;
    PyObject *m, *o;
    PyTypeObject *pubtypes[] = {
        &frame_type,
        &seqcommand_type,
    };

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

    /* Register all public types. */
    for (i = 0; i < arraysize(pubtypes); i++) {
        PyTypeObject *t = pubtypes[i]; 
        if (PyType_Ready(t) < 0) {
            return NULL;
        }
        Py_INCREF(t);
        PyModule_AddObject(m, strchr(t->tp_name, '.') + 1,  (PyObject *)t);
    }

    PyType_Ready(&pychronos_arrayiter_type);
    Py_INCREF(&pychronos_arrayiter_type);

    pychronos_init_regs(m);
    pychronos_init_lux1310(m);
    pychronos_init_pwm(m);
    return m;
}
