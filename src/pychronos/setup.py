#!/usr/bin/python
from distutils.core import setup, Extension
import sysconfig

extra_cflags = sysconfig.get_config_var('CFLAGS').split()
extra_cflags += ["-mfloat-abi=softfp", "-mcpu=cortex-a8", "-mfpu=neon"]
extra_ldflags = sysconfig.get_config_var('LDFLAGS').split()
extra_ldflags += ["-mfloat-abi=softfp", "-mcpu=cortex-a8", "-mfpu=neon"]

pychronos = Extension('pychronos',
                      include_dirs = ['../lib'],
                      depends = ['pychronos.h',
                                 '../lib/fpga.h',
                                 '../lib/lux1310.h'],
                      sources = ['module.c',
                                 'registers.c',
                                 'lux1310.c',
                                 'sequencer.c',
                                 'pwm.c'],
                      extra_compile_args=extra_cflags,
                      extra_link_args=extra_ldflags)

setup (name = 'PyChronos',
       version = '0.3.1',
       description = 'Python bindings for the Chronos High Speed Camera',
       author = 'Owen Kirby',
       author_email = 'oskirby@gmail.com',
       url = 'https://github.com/krontech/chronos-cli',
       ext_modules = [pychronos])

