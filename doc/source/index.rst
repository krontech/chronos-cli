.. Chronos API Reference documentation master file, created by
   sphinx-quickstart on Tue Feb  6 21:59:04 2018.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

Welcome to the Chronos API Reference
====================================

The remote API to the Chronos camera is implemented as a DBus protocol between three major
programs. The control daemon, the video pipeline, and the user interface. This guide describes
the APIs used to control the Chronos daemon and video pipeline from the user interface.

All callable DBus functions will return a variant map type, with the mashalling signature ``"a{sv}"``
which will contain the return parameters from the function. All functions either take no parameters,
or may accept a variant hash map of input arguments.

.. toctree::
   :maxdepth: 2
   
   control
   video

Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`
