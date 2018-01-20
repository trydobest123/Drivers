KERN_DIR = /work/KernelSource/linux-3.4.22_jz2440

all:
	make -C $(KERN_DIR) M=`pwd` modules 

clean:
	make -C $(KERN_DIR) M=`pwd` modules clean
	rm -rf modules.order

obj-m	+= cmos_ov7740_dev.o cmos_ov7740_drv.o


install:
	sudo cp cmos_ov7740_dev.ko cmos_ov7740_drv.ko /work/NFS_ROOT/RootFs/fs_mdev/drivers/camera/vb2
