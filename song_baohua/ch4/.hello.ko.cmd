cmd_/home/zyl/code/driver_linux/bh_ch4/hello.ko := ld -r -m elf_x86_64 -z max-page-size=0x200000 -T ./scripts/module-common.lds --build-id  -o /home/zyl/code/driver_linux/bh_ch4/hello.ko /home/zyl/code/driver_linux/bh_ch4/hello.o /home/zyl/code/driver_linux/bh_ch4/hello.mod.o ;  true