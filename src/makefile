BASSPATH = ../lib
BASSLIB = $(BASSPATH)/bass.lib
OUTDIR = ../RELEASE
FLAGS = -Os -I$(BASSPATH) -m32

CC = gcc
RM = del
RES = windres

TARGET = soundviewer.exe
FLAGS += -mwindows -static -static-libstdc++ -s 
LIBS = -lcomdlg32 -lgdi32 

.PHONY: all clean

%.exe: %.c %.rc
	$(RES) -i $*.rc -o rsrc.obj
	$(CC) $(FLAGS) $*.c rsrc.obj $(BASSLIB) $(LIBS) -o $(OUTDIR)/$@
	$(RM) rsrc.obj

%.exe: %.c
	$(CC) $(FLAGS) $*.c $(BASSLIB) $(LIBS) -o $(OUTDIR)/$@

all: $(TARGET)

clean:
	$(RM) $(OUTDIR)/$(TARGET)

