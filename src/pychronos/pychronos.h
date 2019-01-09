#ifndef __PYCHRONOS_H
#define __PYCHRONOS_H

#include <Python.h>

int pychronos_init_maps(void);
int pychronos_init_regs(PyObject *mod);
int pychronos_init_lux1310(PyObject *mod);

extern Py_buffer fpga_regbuffer;
extern Py_buffer fpga_rambuffer;

#ifndef arraysize
#define arraysize(_array_) (sizeof(_array_)/sizeof((_array_)[0]))
#endif

#ifndef offsetof
#define offsetof(_type_, _member_) (uintptr_t)(&((_type_ *)0)->_member_)
#endif

#define FPGA_MAP_SIZE (16 * 1024 *1024)

#endif /* __PYCHRONOS_H */
