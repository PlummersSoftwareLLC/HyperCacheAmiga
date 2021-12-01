AS=dh2:a68k/a68k
CFLAGS=-cfist -v -y -O
STARTUP=lib:cback.o
LFLAGS=sc sd nd verbose define __main=__tinymain
LIBS=lib lib:lc.lib lib:amiga.lib lib:debug.lib 
MAIN=cache
OBJECTS=cache.o infoserver.o arg.o backio.o 

cache: $(OBJECTS)
	$(LD) from $(STARTUP) $(OBJECTS) to $(MAIN) $(LFLAGS) $(LIBS)

.c.o:	
	$(CC) $(CFLAGS) $*

.asm.o:
	$(AS) $*
