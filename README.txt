chronos-cli
Command line tools for controlling the Chronos camera. Includes a D-Bus API mock.

Build:
	> sudo apt-get install automake libtool #add missing entries as required
	> ./bootstrap
	> ./configure
	> If building cam-pipeline for the camera, run ./xcompile.sh , followed by the path to your targetfs.
	> make
	- Output is in ~src/.

