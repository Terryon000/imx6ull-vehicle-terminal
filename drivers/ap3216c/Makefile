export ARCH = arm
export CROSS_COMPILE = arm-buildroot-linux-gnueabihf-
export PATH:=$(PATH):/home/book/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/bin

obj-m += ap3216c.o
KERN_DIR := /home/book/100ask_imx6ull-sdk/Linux-4.9.88

all:
    $(MAKE) -C $(KERN_DIR) M=$(PWD) modules ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-
clean:
    $(MAKE) -C $(KERN_DIR) M=$(PWD) clean
