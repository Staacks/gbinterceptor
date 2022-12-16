# GB Interceptor Firmware

This folder contains the source code for the firmware of the GB Interceptor. It requires the Raspberry Pi Pico SDK as well as TinyUSB (which is usually a submodule of the Pico SDK repository). If you want to build the firmware yourself, remember to set `PICO_SDK_PATH` correctly.

The subfolder screens contains the info screens shown when no game is running. The `sh` script in the same folder can be used to convert new images to header files.

# License

This code is licensed under GNU General Public License v3.
