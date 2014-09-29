########################  CALIXTO-AM335x-EVM BASIC CONFIGURATION #######################

After Downloading the Kernel from GIT,

1> Setuping the Compiler Path
  
Eg:
     -> Download Toolchain from following link https://github.com/calixtolinux/am335x-toolchain.git
     -> cp  linux-devkit.tar.gz /opt
     -> tar -xvf linux-devkit.tar.gz
     -> edit ~/.bashrc Add the Following Line at end
     -> export PATH=/opt/compiler-path/bin:$PATH
     -> export ARCH=arm
     -> export CROSS_COMPILE=arm-arago-linux-gnueabi-
     -> save & Exit
     -> source ~/.bashrc

2> Compiling Kernel at first time

Eg:
     -> Get into Calixto-AM335x-Linux3.2-Versa-EVM-V1/
     -> make distclean
     -> make am335x_calixto_nxt_defconfig
     -> make uImage

While compiling your Kernel, if found any firmware related error,
    -> goto make menuconfig
                 -> Device Drivers
                    -> Generic Driver Options
                       -> External firmware blobs to build into the kernel library (Press Enter and type 'am335x-pm-firmware.bin')
                       -> Firmware blobs root directory  (Press Enter and type 'firmware')
     
    -> Download the firmware from the GIT Location (https://github.com/calixtolinux/Calixto-AM335x-Linux-Firmware.git)
    -> Copy Downloaded file to Kernel-Directory/firmware/
    -> Recompile the Kernel by make uImage

3> Calixto Pheripheral Configuration
   
 -> make menuconfig
   -> System Type
     -> TI OMAP2/3/4 Specific Features
       -> [*] Calixto-EVM Pheripheral Configuration
             -> [] Calixto-EVM Wireless Interface  (Enable only when the WLAN Module Present in Board)
             ->    Calixto-EVM Interface Support   (Enable multiple Function in Board)(Currently CAN0 only Available, will update more features Later)
             ->    Calixto-EVM Display Support     (Enable Different LCD Size Configuration Eg: 4.7inch, VGA Mode, 7inch, 3.5inch)

-> Any More Quires Email @: support@calixto.co.in
-> More Product Details Log on to :www.calixto.co.in
