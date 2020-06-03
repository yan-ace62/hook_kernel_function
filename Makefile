obj-m := kprobe_main.o
kprobe_main-objs := kprobe_genetlink.o kprobe_kthread.o kretprobe_time.o
KDIR := /lib/modules/$(shell uname -r)/build
all:
	make -C $(KDIR) M=$(PWD) modules   
clean:
	rm -f *.ko *.o *.mod.o *.mod.c .*.cmd *.symvers  modul*  
