CC       = cc
CFLAGS   = -g -std=gnu99 -fpic -Werror
INCLUDE  = -I. -I/usr/include/PCSC
DEPENDFILE = .depend

#LDFLAGS  = -lm
#LDLIBS  = -lm


OBJ     := $(filter-out %test.o %Test.o, $(patsubst %.c,%.o,$(wildcard *.c)))
LIBNAME := ifdhPersoSim.so
PREFIX   = /usr/local/pcsc

DEFS     = -DPCSC_DEBUG=1 #-DATR_DEBUG=1

all: $(LIBNAME) tests

# gennerate and pull in dependency info
$(DEPENDFILE): *.c *.h
	$(CC) -MM $+ > $(DEPENDFILE)
-include $(DEPENDFILE)

clean:
	rm -f *.o $(LIBNAME) $(DEPENDFILE) *Test

install: $(LIBNAME)
	mkdir -p $(PREFIX)/etc/reader.conf.d/
	cp reader.conf $(PREFIX)/etc/reader.conf.d/persoSim
	mkdir -p $(PREFIX)/usr/lib/pcsc/drivers/serial/
	cp $(LIBNAME) $(PREFIX)/usr/lib/pcsc/drivers/serial/

uninstall: 
	rm -f $(PREFIX)/etc/reader.conf.d/persoSim
	rm -f $(PREFIX)/usr/lib/pcsc/drivers/serial/$(LIBNAME)

tests: hexStringTest

runtests: tests
	./hexStringTest

%.so: $(OBJ)
	$(CC) $(CFLAGS) -shared $(OBJ) -o $@

%.o : %.c
	$(CC) $(CFLAGS) -c $< $(INCLUDE) $(DEFS)

% : %.c hexString.o 
	#TODO remove the hardcoded dependecy above
	$(CC) $(CFLAGS) $^ -o $@ $(INCLUDE) 

.PHONY: clean test

