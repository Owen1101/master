#obj-m := fpga_pcie1.o#fpga_pcie1.o#fpga_pcie.o
obj-m := fpga_pcie_single_lan.o
obj-m := fpga_pcie_single_lan_older.o

#KERNEL	:=	/lib/modules/$(shell uname -r)/build

KERNEL := /home/yglc/ti-processor-sdk-linux-rt-am57xx-evm-03.01.00.06/board-support/linux-rt-4.4.19+gitAUTOINC+f572d285f0-gf572d285f0
#KERNEL := /home/yglc/ti-processor-sdk-linux-am57xx-evm-04.00.00.04/board-support/linux-4.9.28+gitAUTOINC+eed43d1050-geed43d1050
#KERNEL := /home/yglc/ti-processor-sdk-linux-rt-am57xx-evm-04.00.00.04/board-support/linux-rt-4.9.28+gitAUTOINC+786e64041b-g786e64041b
all:
	make -C $(KERNEL) M=$(shell pwd) modules
#	arm-linux-gnueabihf-gcc -o  app_base1 -D_FILE_OFFSET_BITS=64 app_base1.c
	arm-linux-gnueabihf-gcc -o  pthread_app -D_FILE_OFFSET_BITS=64 pthread_app.c -pthread
	arm-linux-gnueabihf-gcc -o  pthread_app_mutex -D_FILE_OFFSET_BITS=64 pthread_app_mutex.c -pthread
	arm-linux-gnueabihf-gcc -o  pthread_app_rcv -D_FILE_OFFSET_BITS=64 pthread_app_rcv.c -pthread
	arm-linux-gnueabihf-gcc -o  pthread_app_rcv_dan -D_FILE_OFFSET_BITS=64 pthread_app_rcv_dan.c -pthread
	arm-linux-gnueabihf-gcc -o  pthread_app_nosata -D_FILE_OFFSET_BITS=64 pthread_app_nosata.c -pthread
	arm-linux-gnueabihf-gcc -o  pthread_app_multpth -D_FILE_OFFSET_BITS=64 pthread_app_multpth.c -pthread
#	arm-linux-gnueabihf-gcc -o  app_base -D_FILE_OFFSET_BITS=64 app_base.c
#	arm-linux-gnueabihf-gcc -o  app_base2 -D_FILE_OFFSET_BITS=64 app_base2.c
#	arm-linux-gnueabihf-gcc -o  mmap_app -D_FILE_OFFSET_BITS=64 mmap_app.c
clean:
	make -C $(KERNEL) M=`pwd` clean
	rm -f app_base app_base1 app_base2 mmap_app	test 
