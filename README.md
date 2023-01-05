# GB Interceptor
Capture or stream Game Boy gameplay footage via USB without modifying the Game Boy.

This open source and open hardware Game Boy adapter uses an rp2040 to capture the communication on the cartridge bus. This data is fed into an emulator, rendered and then offered as a USB video class device.

Details about the concept can be found at https://there.oughta.be/a/game-boy-capture-cartridge and an overview is given in the following video:

[![Youtube Video: GB Interceptor: A Game Boy capture cartridge](https://img.youtube.com/vi/6mOJtrFnawk/0.jpg)](https://youtu.be/6mOJtrFnawk)

If you have pull-requests, bug reports or want to contribute new case designs, please do not hesitate to open an issue. For generic discussions, "show and tell" and if you are looking for support for something that is not a problem in the code here, I would recommend .

<a href="https://www.buymeacoffee.com/there.oughta.be" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-blue.png" alt="Buy Me A Coffee" height="47" width="174" ></a>

# Warnings

Please make sure to understand what this device does and what its limitations are. Most importantly, there is a little lag which will make it unsuitable for playing on a bis screen. It might be ok for slow games, but its focus is on recording and streaming.

Also check out the compatibility tables for host software (for example OBS) and particular games. In principle this is a USB video class device and does not require drivers, but not all software supports the unsusual video format of the Interceptor. Similarly, games should usually work, but sometimes there are some details that the Interceptor does not yet support properly and some rare things that cannot work based on the principle of this device.

* [Game Boy compatibility](https://github.com/Staacks/gbinterceptor/wiki/Game-Boy-compatibility)
* [Host software compatibility](https://github.com/Staacks/gbinterceptor/wiki/Host-software-compatibility)
* [Game compatibility](https://github.com/Staacks/gbinterceptor/wiki/Game-compatibility)

# Building the Interceptor

There is an [order and build video](https://youtu.be/Lg92tVkEE98) to guide you through the build process which I highly recommend, especially if you are not used to ordering PCBs. A [written guide can be found in this repository's wiki](https://github.com/Staacks/gbinterceptor/wiki/Build-guide).

# Usage

Simply plug the Interceptor into your Game Boy and the Game into the Interceptor. Connect the Interceptor to your computer before turning on the Game Boy. It should show up as a webcam in any software that supports USB Video devices (for example OBS Studio). Note that some software is not entirely happy with the exotic format, but it should work in most cases (see [Host software compatibility](https://github.com/Staacks/gbinterceptor/wiki/Host-software-compatibility)).

Then just turn on the Game Boy and play.

Note that you do not need to open the video stream first. The only important step is that the Interceptor is powered via USB before the Game Boy is turned on.

## Buttons

The [Mode] button (next to the LEDs) allows to switch between grayscale/green colors and toggle frame blending. Each press switches colors and frame blending is toggled each time it returns to grayscale.

The [Flash] button (near the center, usually inside the case) is only used to flash a new firmware version. Simply hold it while plugging in the USB cable. The Interceptor should appear as a storage device (like a USB stick) and you can simply drag the new firmware file (a uf2 file) into it.

## LEDs

The red LED indicates that the Interceptor is powered.

The blue LED stays on as long as the Interceptor is in sync with the game. (It may shortly turn off for a fraction of a second when the Game Boy turns the display off, i.e. when loading a level.)

# Support

While I do not give any guarantees I (and probably others too) am happy to help if I have the time.

If you are having difficulties to order, build or use your Interceptor, please do not contact me directly but use [r/thereoughtabe on reddit](https://www.reddit.com/r/thereoughtabe/) as this allows others to help and the answer might help others, too.

If you found a bug and in particular if you find glitches in a game, please check the [compatibility lists](https://github.com/Staacks/gbinterceptor/wiki) in the Wiki first to see if it is a known problem and open an issue here on Github otherwise.

# Building the firmware

Most users should just install the latest release uf2 as described in the [Build guide](https://github.com/Staacks/gbinterceptor/wiki/Build-guide), but if you want to build it yourself, you will need the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk). Don't forget to set PICO_SDK_PATH accordingly.

Make sure to check out the submodules too and specifically make sure that the [TinyUSB](https://github.com/hathach/tinyusb) submodule is at least on version 0.14.0 as the Pico SDK currently references an older version. (Also see [#3](https://github.com/Staacks/gbinterceptor/issues/3))

# License
The code is released under the GNU General Public Licence 3 and the design files (PCB layout and 3d printed case) are released under the Creative Commons licence CC-BY 4.0.
