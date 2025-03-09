# Proximity Sensor Driver for YC-SM41P

If your YC-SM41P was sold to you with a light sensor, it also has a proximity sensor built in, which isn't active in the default firmware.

## Can I use the Proximity sensor if I have one?

Yes! I have created a custom driver and Android HAL that exposes the proximity sensor in Android as a normal Sensor device that will be compatible with all Android software.

This will only work if you have the sensor hardware present in your tablet. The sensor is appears to be an EM30719 from Epticore Microelectronics. 

The firmware is unlocked enough that we do not need to re-flash to install the driver. However, we do need to flash the dtb device tree, which describes to Android's kernel what hardware is connected to the system.

## How can I tell if I have a Light/Proximity Sensor?

Portworld/Sunworld makes the device with the sensor as an option. You probably won't have one unless you specifically requested the option. 

One good physical indication is the little black window at the top of the may have a dim flashing red LED in it, visible in the dark. This is the proximity sensor in action, as the default firmware initialises the sensor incorrectly and enables the proximity hardware, despite not reading the proximity information.

Another way to check is with the i2cget function from `adb` as root:
```
rk3566_r:/ # i2cget -y 3 0x24 0x00 -f
0x31
```
0x31 indicates that the sensor is present.

## Will this work with other Android 11 Portworld/Sunworld devices with a light sensor?

Probably. I do not have any others to test. If the device is using the same em3071x hardware, these instructions are likely to work. However, you should manually create the dtbo.img file using `Android_boot_image_editor`, as there may be differences to the pre-built image I have included here.

# Installing from binaries

I've gone to the trouble of building the binaries so you don't have to...

These instructions are unlikely to brick your device, but **continue at your own risk**.

## Preparing

1. Enter Developer Mode in the device by clicking tapping 10 times on the "Build Number" section of the "About tablet" page in settings.
1. Make sure USB Debugging is turned on: Settings->System->Advanced Settings->Developer->USB Debugging switch.
1. Install ADB (Android Debug Tools) on your computer
1. Make sure you're working in the `proximity_sensor/bin` folder, or copy the files from there to your working folder.
1. Connect the YC-SM41P to your computer with a USB cable.
1. Use `adb devices` and confirm that a device is attached. It will have a random name.
1. Optional - enable Overlayfs on the device. 
    * This isn't 100% necessary, but we will need to manually clear some space later if you don't do this.
    * Use [these adb remount instructions](https://android.googlesource.com/platform/system/core/+/master/fs_mgr/README.overlayfs.md) to allow easier modification to the system files. This will make the device even more open to modification by creating an overlay filesystem over the main system partitions. It may also make it easier to revert the changes, if necessary.

## Copying in the drivers

1. Push the following files from the `proximity_sensor/bin` directory onto the device as follows:
    ```
    adb push ps_em3071x.ko /sdcard/Download/
    adb push sensors.rk30board.so /sdcard/Download/
    adb push init.ps_em3071x.rc /sdcard/Download/
    ```
1. Open a shell with `adb shell`.
1. From the shell's `rk3566_r:/ #` prompt enter the command `su` to get root access to the device
1. Mount the /odm partition as read/write with the command:
    ```
    mount -o remount,rw /odm
    ```
1. If you _didn't_ enable overlayfs above, then we need to delete some files to clear space on /odm. Luckily there is a file called `/odm/media/seat.rar` which is a non-functional boot animation large enough for everything we need. We'll back the file up first, just in case.
    ```
    cp /odm/media/seat.rar /sdcard/Download/
    rm /odm/media/seat.rar
    ```
1. Copy the driver, HAL, and init script into the `odm` partition as follows:
   ```
   umask 022
   mkdir -p /odm/lib/modules
   mkdir -p /odm/lib64/hw
   mkdir -p /odm/etc/init
   umask 133
   cp /sdcard/Download/ps_em3071x.ko /odm/lib/modules/
   cp /sdcard/Download/sensors.rk30board.so /odm/lib64/hw/
   cp /sdcard/Download/init.ps_em3071x.rc /odm/etc/init/
   umask 000
   ```
1. At this stage we're almost ready to go. However, the device tree still needs to be updated so that the kernel knows to load the driver for the proximity sensor. This can be done by modifying the built-in device tree overlay.
1. Back up the existing device tree overlay first in case something goes wrong
1. From the adb shell, issue the command `dd if=/dev/block/by-name/dtbo of=/sdcard/Download/dtbo_backup.img`
1. Exit out of the adb shell, with the command `exit`, twice.
1. You'll now be out of the adb shell, and on your normal PC command line. Issue the command `adb pull /sdcard/Download/dtbo_backup.img dtbo_backup.img` to save the original device tree overlay on your computer.
1. Run the command `adb reboot bootloader`. This will reboot the device to a blank screen.
1. Use the command `fastboot devices` to confirm that the YC-SM41P device is connected as a fastboot device. This is a special firmware flashing mode. **Take extreme care with fastboot commands!**
    * If no device shows in the fastboot list on Windows, you probably need to manually load the fastboot driver on the USB device node.
    * Get the fastboot driver from the official [Google source](https://developer.android.com/studio/run/win-usb). Unzip the archive.
    * In Device Manager, find the unrecognised usb device - it's called something like usb file device, and will have a warning sign on it.
        * Right click on the device and choose "Update Driver..."
        * Choose "Browse my computer for drivers"
        * Choose "Let me pick from a list of available drivers on my computer"
        * Choose "Generic Device"
        * Choose "Have Disk..."
        * Browse to select the folder where you unziped the android adb drivers
        * Select the "Android Bootloader Interface" driver from the list
        * `fastboot devices` should now show the device
1. Run the command `fastboot flash dtbo dtbo.img`. This will re-flash the existing dtbo partition with an updated device tree overlay that includes the proximity sensor settings.
1. Run the command `fastboot reboot` to restart back to android. You may need to run this twice for some reason, as the device seems to like to go back to fastboot on the first reboot sometimes.
    * If you can't get your device to boot, re-flash the `dtbo_backup.img` file instead. Consider creating the image file for your device manually with `Android_boot_image_editor` instead.
1. You're all done! Try a Proximity Sensor test app from the Google Play Store, and see if it works!

## Troubleshooting

First, check that the driver is loaded with from an adb shell
```
rk3566_r:/ $ lsmod
Module                  Size  Used by
bcmdhd               2265088  0
ps_em3071x             16384  0
rk3566_r:/ $
```
If ps_em33071x isn't listed, it's possible the module loading script isn't working. Try loading the driver manually as follows:
```
rk3566_r:/ $ su
rk3566_r:/ # insmod /odm/lib/modules/ps_em3071x.ko
rk3566_r:/ # lsmod
Module                  Size  Used by
bcmdhd               2265088  0
ps_em3071x             16384  0
rk3566_r:/ #
```
If the driver still isn't listed, the last few lines of the output of `dmesg` may indicate the problem.

Check the input devices are registering correctly:

```
rk3566_r:/ # cat /proc/bus/input/devices
...

I: Bus=0000 Vendor=0000 Product=0000 Version=0000
N: Name="proximity"
P: Phys=
S: Sysfs=/devices/platform/fe5c0000.i2c/i2c-3/3-0024/input/input4
U: Uniq=
H: Handlers=event4
B: PROP=0
B: EV=9
B: ABS=2000000

rk3566_r:/ # 
```
If the proximity sensor isn't listed, it's probable that your device tree modifications have not worked. The input sensor will only load if the device tree has a ps_em3071x device listed. Confirm as follows:
```
rk3566_r:/ # ls -l /proc/device-tree/i2c@fe5c0000/ps_em3071x@24/
total 0
-r--r--r-- 1 root root 11 2025-03-09 11:55 compatible
-r--r--r-- 1 root root  4 2025-03-09 11:55 irq_enable
-r--r--r-- 1 root root 11 2025-03-09 11:55 name
-r--r--r-- 1 root root  4 2025-03-09 11:55 poll_delay_ms
-r--r--r-- 1 root root  4 2025-03-09 11:55 ps_offset
-r--r--r-- 1 root root  4 2025-03-09 11:55 ps_power
-r--r--r-- 1 root root  4 2025-03-09 11:55 ps_threshold_high
-r--r--r-- 1 root root  4 2025-03-09 11:55 ps_threshold_low
-r--r--r-- 1 root root  4 2025-03-09 11:55 reg
-r--r--r-- 1 root root  5 2025-03-09 11:55 status
-r--r--r-- 1 root root  4 2025-03-09 11:55 type
rk3566_r:/ # 
```
If you see `/proc/device-tree/i2c@fe5c0000/ps_em3071x@24/: No such file or directory`, then revisit the dtbo.img firmware flashing instructions.

If the proximity sensor is listed, but Android apps (e.g. the Sensor Test App) can't see the proximity sensor, it's likely that you have not installed the HAL correctly.
Check:
```
1|rk3566_r:/ # ls -l /odm/lib64/hw/sensors.rk30board.so
-rw-r--r-- 1 root root 50616 2025-03-08 21:25 /odm/lib64/hw/sensors.rk30board.so
rk3566_r:/ #
```
The file must be named exactly as shown, and have the `-rw-r--r-- root root` permissions.


# Advanced users

## Building the drivers and HAL yourself

Portworld/Sunworld don't make an Android SDK available with their devices, but the devices are close enough to standard Rockchip reference designs that we can use another available Rockchip + Android SDK. I used the T-Firefly Android 11 build instructions, which can be found by searching Google.

This needs quite a lot of fiddling to get a working build. All we really need is the driver `.ko` file and the sensor HAL from the build. I would not recommend trying to create the entire firmware, as we don't have enough information for the specific hardware setup.

Get T-Firefly, and patch in the appropriate files from the `proximity_sensor/src/` folder. Follow the T-Firefly build instructions, but substitute in `rk3566_r-userdebug` as necessary. Expect problems. The key changes are:
 * update the `ps_em3017x.c` source to fix the firmware bugs
 * update the board config to compile the sensor driver as a module
 * Compile the rockchip sensor hal to include proximity sensor support
 * Update the rockchip sensor hal code to report sensible distance ranges for the device

 ## Building the device tree update

 Use Android_boot_image_editor to extract the `dtbo_backup.img` from the device. You will need to rename this as `dtbo.img`. This extracts a file called dt.0.dts, which can be modified. Add the proximity sensor settings, using my example file as a guide.
 
 YMMV on the specific device settings. The important thing is the I2C address and bus must match what your device uses for the light sensor device.
 
 Going too high on the ps_power value seems to stop the sensor from working reliably. Lower powers work better with a lower ps_offset value. Refer to the EM30719 datasheet for more information.

 Re-package the dtbo into dtbo.img.clear. Note that there will be signing errors that can be ignored.

 Now you can flash your device with `dbto.img.clear` to apply the device tree updates.