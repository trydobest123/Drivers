KERN_DIR = /work/KernelSource/linux-3.4.22_jz2440

all:
	make -C $(KERN_DIR) M=`pwd` modules 

clean:
	make -C $(KERN_DIR) M=`pwd` modules clean
	rm -rf modules.order

obj-m	+= sccb_ov9650.o


install:
	sudo cp sccb_ov9650.ko /work/NFS_ROOT/RootFs/fs_mdev/drivers/camera/vb2/ov9650/
