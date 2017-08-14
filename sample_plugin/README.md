## Sample plugin

This is just a sample of a way to add plugins to BESS.

To use this particular sample plugin, build BESS from the top
level with:

    ./build.py --plugin sample_plugin

and the modules and protobuf directory will be read for additional
modules and protobuf definitions.  (You may also add utility routines
in a utils/ directory, and port drivers in a drivers/ directory,
along with port driver protobuf definitions in the protobuf/ports/
directory, though this is not shown here.)
