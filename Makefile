ifneq ($(wildcard /System/Library/Extensions/AppleFileSystemDriver.kext),)
	OSTYPE=darwin
endif

LIBS		= sdl ftgl fontconfig 
CFLAGS		= -O$(OPTIMIZE) -std=c99  -Wall `pkg-config --cflags $(LIBS)`
LDFLAGS		= `pkg-config --libs $(LIBS)`

ifeq ($(OSTYPE),darwin)
	LIBS		+= freetype2
	LDFLAGS		+= -lm -framework OpenGL
else
	LIBS		+= gl
endif

EXECUTABLE = gcodeview

OPTIMIZE	= 2

SOURCES		= main.c
OBJECTS		= $(SOURCES:.c=.o)

.PHONY: all clean

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	@echo Building for [$(OSTYPE)].

%.o: %.c
	$(CC) $(CFLAGS) -std=c99 -c $< -o $@

clean:
	rm $(OBJECTS) $(EXECUTABLE)
