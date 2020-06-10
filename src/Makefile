CC=vc
RMRF=rm -rf

CFLAGS=
LDFLAGS=-lamiga -lmieee

TARGETS := lm75 akutest i2cget i2cset akuhei2c i2cdetect lm75th

OBJECTS := $(addsuffix .o, $(TARGETS) )

all: $(TARGETS)

$(TARGETS) : % : %.o akuhei2c.o
	$(CC) -o $@ $(LDFLAGS) $^

%.o : %.c
	$(CC) $(CFLAGS) -c $<

clean :
	$(RMRF) $(OBJECTS) $(TARGETS)
