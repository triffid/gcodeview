EXECUTABLE = gcodeview

LIBS		= sdl ftgl fontconfig gl freetype2

OPTIMIZE	= 2

CFLAGS		= -O$(OPTIMIZE) -std=c99  -Wall `pkg-config --cflags $(LIBS)`
LDFLAGS		= `pkg-config --libs $(LIBS)` -lm

SOURCES		= main.c
OBJECTS		= $(SOURCES:.c=.o)

.PHONY: all clean

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -std=c99 -c $< -o $@

clean:
	rm $(OBJECTS) $(EXECUTABLE)
