obj-m := main.o
main-y := kprobe_main.o kprobe_genetlink.o kprobe_kthread.o kretprobe_time.o
KDIR := /lib/modules/$(shell uname -r)/build
all:
	make -C $(KDIR) M=$(PWD) modules   
clean:
	make -C $(KDIR) M=$(PWD) clean