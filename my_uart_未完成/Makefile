KERN_DIR = /work/KernelSource/linux-3.4.22_jz2440

all:
	make -C $(KERN_DIR) M=`pwd` modules 

clean:
	make -C $(KERN_DIR) M=`pwd` modules clean
	rm -rf modules.order

obj-m	+= uart_drv.o


install:
	sudo cp uart_drv.ko /work/NFS_ROOT/RootFs/fs_mdev/drivers/uart

