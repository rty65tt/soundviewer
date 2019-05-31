BASSPATH = ../lib
BASSLIB = $(BASSPATH)/bass.lib
OUTDIR = ../RELEASE
FLAGS = -Os -I$(BASSPATH) -m32

CC = g++
RM = del
RES = windres

TARGET = soundviewer.exe
FLAGS += -mwindows -static -static-libstdc++ -s 
LIBS = -lcomdlg32 -lgdi32  -lgdiplus

.PHONY: all clean

%.exe: %.cpp
	$(RES) -i resource.rc -o rsrc.obj
	$(CC) $(FLAGS) $*.cpp rsrc.obj $(BASSLIB) $(LIBS) -o $(OUTDIR)/$@
	$(RM) rsrc.obj


all: $(TARGET)

clean:
	$(RM) $(OUTDIR)/$(TARGET)

