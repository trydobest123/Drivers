KERN_DIR = /work/KernelSource/linux-4.9-jz2440

all:
	make -C $(KERN_DIR) M=`pwd` modules 

clean:
	make -C $(KERN_DIR) M=`pwd` modules clean
	rm -rf modules.order

obj-m	+= input_mouse.o


install:
	sudo cp input_mouse.ko /work/NFS_ROOT/RootFs/fs_mdev/drivers/input
