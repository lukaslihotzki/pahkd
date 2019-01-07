pahkd: pahkd.c
	$(CC) -O2 $(CCFLAGS) $< -lxcb -lpulse -o $@
