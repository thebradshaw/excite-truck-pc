set pagination off
handle SIGUSR1 nostop noprint pass
handle SIGPIPE nostop noprint pass
break DVDInit_8015EA1C
run
delete 1
call (int)mprotect((void*)0x100000000000, 0x1000, 1)
call (int)mprotect((void*)0x1000C0000000, 0x1000, 1)
c
bt 4
x/xw 0x100080560B18
x/4xw 0x10008037BAC0
x/8i $pc-20
info registers
