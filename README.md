chronos-cli
===========
Command line tools for controlling the Chronos camera. Includes a D-Bus API mock.

Chronos-cli should be build on the chronos 1.4/2.1 cameras as gstreamer is no longer available in the version that this
repo depends on.

Build:
------
```bash
user@host:~/root/$ apt-get install automake libtool # sudo if user is not root, add missing entries as required
user@host:~/root/$ ./bootstrap
user@host:~/root/$ ./configure
user@host:~/root/$ make # or automake. If building cam-pipeline run ./xcompile.sh, followed by the path to you targets.
# The output of make lives in ~src/ folder 
```