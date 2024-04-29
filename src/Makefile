
OUT = usimm
BINDIR = ../bin
OBJDIR = ../obj
OBJS = $(OBJDIR)/main.o $(OBJDIR)/memory_controller.o $(OBJDIR)/scheduler.o $(OBJDIR)/VAT.o $(OBJDIR)/meta_cache.o
CC = gcc
DEBUG = -g
MATH = -lm
CFLAGS = -std=c99 -Wall -c $(DEBUG)
LFLAGS = -Wall $(DEBUG)


$(BINDIR)/$(OUT): $(OBJS)
	$(CC) $(LFLAGS) $(OBJS) -o $(BINDIR)/$(OUT) $(MATH)
	chmod 777 $(BINDIR)/$(OUT)

$(OBJDIR)/main.o: main.c processor.h configfile.h memory_controller.h scheduler.h params.h VAT.h meta_cache.h
	$(CC) $(CFLAGS) main.c -o $(OBJDIR)/main.o $(MATH)
	chmod 777 $(OBJDIR)/main.o


$(OBJDIR)/memory_controller.o: memory_controller.c utlist.h utils.h params.h memory_controller.h scheduler.h processor.h 
	$(CC) $(CFLAGS) memory_controller.c -o $(OBJDIR)/memory_controller.o
	chmod 777 $(OBJDIR)/memory_controller.o

$(OBJDIR)/scheduler.o: scheduler.c scheduler.h utlist.h utils.h memory_controller.h params.h
	$(CC) $(CFLAGS) scheduler.c -o $(OBJDIR)/scheduler.o
	chmod 777 $(OBJDIR)/scheduler.o

$(OBJDIR)/VAT.o: VAT.c VAT.h 
	$(CC) $(CFLAGS) VAT.c -o $(OBJDIR)/VAT.o $(MATH)
	chmod 777 $(OBJDIR)/VAT.o

$(OBJDIR)/meta_cache.o: meta_cache.c meta_cache.h 
	$(CC) $(CFLAGS) meta_cache.c -o $(OBJDIR)/meta_cache.o $(MATH)
	chmod 777 $(OBJDIR)/meta_cache.o
	 

clean:
	rm -f $(BINDIR)/$(OUT) $(OBJS)

