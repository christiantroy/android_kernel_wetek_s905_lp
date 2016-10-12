Example build:

rm -rf out
mkdir out
make -C common O=`pwd`/out ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- meson64_defconfig
make -C common O=`pwd`/out ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
make -C common O=`pwd`/out ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- wetekplay2.dtb

Toolchain: http://openlinux.amlogic.com:8000/deploy/gcc-linaro-aarch64-linux-gnu-4.9-2014.09_linux.tar
