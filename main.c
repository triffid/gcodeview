#ifndef	_GNU_SOURCE
#define		_GNU_SOURCE
#endif

#include	<features.h>

#include	<stdlib.h>
#include	<stdint.h>
#include	<stdio.h>

#include	<sys/mman.h>
#include	<sys/stat.h>
#include	<sys/types.h>

#include	<math.h>
#include	<errno.h>
#include	<string.h>
#include	<fcntl.h>

#include	<SDL/SDL.h>
#include	<FTGL/ftgl.h>
#include	<fontconfig/fontconfig.h>

#define		bool uint8_t
#define		true	255
#define		false	0

#define		OPENGL

#ifdef	OPENGL
	#include	<SDL/SDL_opengl.h>
	float transX, transY;
#else
	#include	<SDL_gfxPrimitives.h>
	float viewPortL, viewPortR, viewPortT, viewPortB;
#endif

float extrusionWidth = 0.3;

bool Running;
SDL_Surface* Surf_Display;
int Surf_width;
int Surf_height;

int filesz;
char* gcodefile;

int layerCount;
size_t layerSize;

typedef struct {
	char*	index;
	int		size;
	float	height;
} layerData;

layerData* layer;

char *msgbuf;

int layerVelocity;

FTGLfont* font = NULL;

int currentLayer;

#define	KMM_LSHIFT 1
#define KMM_RSHIFT 2
#define	KMM_CTRL   4
#define	KMM_ALT    8
int keymodifiermask;

float zoomFactor;

float linewords[26];

float mind(float a, float b) {
	if (a < b)
		return a;
	return b;
}

float maxd(float a, float b) {
	if (a > b)
		return a;
	return b;
}

float linint(float value, float oldmin, float oldmax, float newmin, float newmax) {
	return (value - oldmin) * (newmax - newmin) / (oldmax - oldmin) + newmin;
}

void die(char* call, char* data) {
	int errsv = errno;
	fprintf(stderr, "%s%s failed: %s\n", call, data, strerror(errsv));
	exit(1);
}

Uint32 timerCallback(Uint32 interval, void* param) {
	SDL_Event e;
	e.type = SDL_USEREVENT;
	e.user.code = 1;
	e.user.data1 = 0;
	e.user.data2 = 0;

	SDL_PushEvent(&e);

	return 50;
}

#define	LMASK(l) (1<<((l & ~0x20) - 'A'))
#define	SEEN(c) ((seen & LMASK(c)) != 0)

uint32_t scanline(char *line, int length, float *words, char **end) {
	int i = 0;
	uint32_t seen = 0;

	#define	COMMENT_SEMICOLON 1
	#define	COMMENT_PARENTHESIS 2
	int comment = 0;
	while (i < length) {
		char c = line[i];
		if (c == 13 || c == 10) {
			*end = &line[i + 1];
			return seen;
		}
		if ((comment & COMMENT_SEMICOLON) == 0) {
			if (c == ';')
				comment |= COMMENT_SEMICOLON;
			else if (c == '(')
				comment |= COMMENT_PARENTHESIS;
			else if (c == ')')
				comment &= ~COMMENT_PARENTHESIS;

			else if (comment == 0) {
				if (c >= 'a' && c <= 'z')
					c &= ~0x20;
				if (c >= 'A' && c <= 'Z') {
					char *e;
					float v = strtof(&line[i + 1], &e);
					if (e > &line[i + 1]) {
						seen |= LMASK(c);
						words[c - 'A'] = v;
						i = e - line - 1;
					}
				}
			}
		}
		i++;
	}
	*end = &line[i];
	return seen;
}

void gline(float x1, float y1, float x2, float y2, float width, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	#ifdef	OPENGL
		glBegin(GL_QUADS);
		glColor4f(((float) r) / 255.0, ((float) g) / 255.0, ((float) b) / 255.0, ((float) a) / 255.0);
		//   c1x,c1y
		//  0,0......
		//   c4x,c4y  ........       c2x,c2y
		//                    ........ px,py
		//                       c3x,c3y
		float c1x, c1y, c1l, c2x, c2y, c2l, c3x, c3y, c3l, c4x, c4y, c4l;

		float px = x2 - x1;
		float py = y2 - y1;

		c1x = -py;
		c1y = px;
		c1l = hypotf(c1x, c1y);
		c1x = (c1x * width / c1l / 2.0) + x1;
		c1y = (c1y * width / c1l / 2.0) + y1;

		c2x = -py;
		c2y = px;
		c2l = hypotf(c2x, c2y);
		c2x = (c2x * width / c2l / 2.0) + px + x1;
		c2y = (c2y * width / c2l / 2.0) + py + y1;

		c3x = py;
		c3y = -px;
		c3l = hypotf(c3x, c3y);
		c3x = (c3x * width / c3l / 2.0) + px + x1;
		c3y = (c3y * width / c3l / 2.0) + py + y1;

		c4x = py;
		c4y = -px;
		c4l = hypotf(c4x, c4y);
		c4x = (c4x * width / c4l / 2.0) + x1;
		c4y = (c4y * width / c4l / 2.0) + y1;

		if (width == 4.0)
			printf("LINE: [%3.0f,%3.0f]->[%3.0f,%3.0f]->[%3.0f,%3.0f]->[%3.0f,%3.0f]\n", c1x, c1y, c2x, c2y, c3x, c3y, c4x, c4y);

		glVertex2f(c1x, c1y);
		glVertex2f(c2x, c2y);
		glVertex2f(c3x, c3y);
		glVertex2f(c4x, c4y);
		glEnd();
	#else
	thickLineRGBA(Surf_Display,
		(x1 - viewPortL) * zoomFactor,
		(viewPortB - y1) * zoomFactor,
		(x2 - viewPortL) * zoomFactor,
		(viewPortB - y2) * zoomFactor,
		mind(maxd(width * zoomFactor, 1), 2),
		r, g, b, a
		);
	#endif
}

void render() {
	#ifdef	OPENGL
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glLoadIdentity();
		glPushMatrix();
		glScalef(zoomFactor, zoomFactor, 0.0);
		glTranslatef(-transX, -transY, 0.0);
	#else
		uint32_t yellow;
	
		yellow = SDL_MapRGB(Surf_Display->format, 224, 224, 128);
	
		SDL_LockSurface(Surf_Display);
		SDL_FillRect(Surf_Display, NULL, yellow);
		int lines = 0;
	#endif
		char *s = layer[currentLayer].index;
		char *e = layer[currentLayer].index + layer[currentLayer].size;
		float G = NAN, X = NAN, Y = NAN, E = NAN, v = NAN, lastX = NAN, lastY = NAN, lastE = NAN;
		char *r;
		uint32_t seen = 0;

		for (X = 0; X < 201.0; X += 10.0) {
			gline(X, 0, X, 200, ((((int) X) % 50) == 0)?1:0.2, 0, 0, 0, 16);
			gline(0, X, 200, X, ((((int) X) % 50) == 0)?1:0.2, 0, 0, 0, 16);
		}

		while (s < e) {
			seen = scanline(s, e - s, linewords, &s);
			if (SEEN('G') && (SEEN('X') || SEEN('Y'))) {
				if (linewords['G' - 'A'] == 0.0 || linewords['G' - 'A'] == 1.0) {
					G = linewords['G' - 'A'];
					X = linewords['X' - 'A'];
					Y = linewords['Y' - 'A'];
					E = linewords['E' - 'A'];
					// draw
					uint8_t r = 0, g = 0, b = 0, a = 224;
					if (isnan(lastX))
						lastX = X;
					if (isnan(lastY))
						lastY = Y;
					if (isnan(lastE))
						lastE = E;
					if (SEEN('E') && (E > lastE)) {
						r = 0;
						g = 0;
						b = 0;
						a = 224;
					}
					else {
						r = 0;
						g = 128;
						b = 64;
						a = 160;
					}
					//printf("%5d lines, %6d of %6d\n", ++lines, s - layerIndex[currentLayer], e - layerIndex[currentLayer]);
					if ((lastX != X || lastY != Y) && !isnan(X) && !isnan(Y) && lastX <= 200.0)
						gline(lastX, lastY, X, Y, extrusionWidth, r, g, b, a);
					//printf("drawn\n");
				}
				seen = 0;
				//
				lastX = X;
				lastY = Y;
				lastE = E;
			}
		}
	#ifdef	OPENGL
		glPopMatrix();
		glPushMatrix();
			glTranslatef(0.0, 200.0 - (20.0 * 0.3), 0.0);
			glScalef(0.3, 0.3, 1.0);
			ftglSetFontFaceSize(font, 20, 20);
			ftglRenderFont(font, msgbuf, FTGL_RENDER_ALL);
		glPopMatrix();
		glFlush();
		glFinish();
		SDL_GL_SwapBuffers();
		glFinish();
	#else
		SDL_UnlockSurface(Surf_Display);

		SDL_Flip(Surf_Display);
	#endif
}

void resize(int w, int h) {
	Surf_width = w;
	Surf_height = h;
	#ifdef	OPENGL
		if (Surf_Display != NULL)
			SDL_FreeSurface(Surf_Display);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		Surf_Display = SDL_SetVideoMode(Surf_width, Surf_height, 32, SDL_HWSURFACE | SDL_RESIZABLE | SDL_OPENGL);
		w = Surf_Display->w; h = Surf_Display->h;
		glViewport(0, 0, w, h);
		glClearColor(0.8, 0.8, 0.5, 0.5);
		glClearDepth(1.0f);
		glShadeModel(GL_SMOOTH);
		glEnable(GL_BLEND);
		glEnable(GL_POLYGON_SMOOTH);
		glEnable(GL_LINE_SMOOTH);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, 200, 0, 200, 0, 1);
		glDisable(GL_DEPTH_TEST);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
	#else
		Surf_Display = SDL_SetVideoMode(Surf_width, Surf_height, 32, SDL_HWSURFACE | SDL_floatBUF | SDL_RESIZABLE);
	#endif
	if (Surf_Display == NULL) {
		SDL_FreeSurface(Surf_Display);
		SDL_Quit();
		die("SDL resize", "");
	}
	render(); // redraw whole window
}

void drawLayer(int layerNum) {
	snprintf(msgbuf, 256, "Layer %3d: %gmm", layerNum, layer[layerNum].height);
	printf("Drawing layer %3d (%5.2f)\n", layerNum, layer[layerNum].height);
	currentLayer = layerNum;
	render();
}

void scanLines() {
	int l = 0;

	printf("Indexing lines... ");

	layerCount = 0;
	// preallocate for 128 layers, we double the size later if it's not enough
	layerSize = (128 * sizeof(layerData));
	layer = malloc(layerSize);
	//printf("allocated %d bytes (%d entries)\n", layerSize, layerSize / sizeof(layerData));

	char *end;
	uint32_t seen;
	float G, Z, lastZ, hopZ, E;

	int ZstackIndex = 0;
	struct {
		char *start;
		float Z;
	} Zstack[8];

	while (l < filesz) {
		seen = scanline(&gcodefile[l], filesz - l, linewords, &end);
		//printf("found line %d chars long at %d. G:%d Z:%d E:%d\n", end - &gcodefile[l], l, seen & LMASK('G')?1:0, seen & LMASK('Z')?1:0, seen & LMASK('E')?1:0);

		G = linewords['G' - 'A'];
		Z = linewords['Z' - 'A'];
		E = linewords['E' - 'A'];

		//printf("G%g Z%g E%g\n", G, Z, E);

		if (((seen & LMASK('G')) != 0) && (G == 0.0 || G == 1.0)) {
			if ((seen & LMASK('Z')) != 0) {
				for (int i = 0; i < ZstackIndex; i++) {
					if (Zstack[i].Z == Z) {
						ZstackIndex = i;
						break;
					}
				}
				//printf("Zstack: %d\n", ZstackIndex);
				Zstack[ZstackIndex].start = &gcodefile[l];
				Zstack[ZstackIndex].Z = Z;
				if (ZstackIndex < 8 - 1)
					ZstackIndex++;
				else
					die("overflow while checking if Z moves are related to hop","");
			}
			if (((seen & LMASK('E')) != 0) && (ZstackIndex > 0) && (Z != lastZ)) {
				int i;
				for (i = 0; i < ZstackIndex; i++) {
					if (Zstack[i].Z == Z)
						break;
				}
				//printf("Got G%g and E%g in same line! Zstack is at %d (%d) and that layer starts at char %d\n", G, E, ZstackIndex, i, Zstack[i].start - gcodefile); // exit(1);
				if (i < 8) {
					layer[layerCount].index = Zstack[i].start;
					layer[layerCount].height = Zstack[i].Z;
					lastZ = layer[layerCount].height;
					Zstack[0].start = layer[layerCount].index;
					Zstack[0].Z = layer[layerCount].height;
					ZstackIndex = 1;
					//printf("LAYER %d RECORDED\n", layerCount);
					if (layerCount > 0)
						layer[layerCount - 1].size = layer[layerCount].index - layer[layerCount - 1].index;
					layerCount++;
					if ((layerCount + 1) * sizeof(layerData) > layerSize) {
						//printf("reallocating layer buffer to %d bytes (%d entries)\n", layerSize << 1, (layerSize << 1) / sizeof(layerData));
						layer = realloc(layer, layerSize << 1);
						if (layer == NULL)
							die("Scan: realloc layer","");
						layerSize <<= 1;
					}
				}
				else
					die("Zstack: can't find Z value in stack!","this should never happen");
			}
		}
		l = end - gcodefile;

		#if 0
			layer[layerCount].index = &gcodefile[ls];
			layer[layerCount].height = zvalue;
			if (layerCount > 0)
				layer[layerCount - 1].size = layer[layerCount].index - layer[layerCount - 1].index;
			layerCount++;
			if ((layerCount + 1) * sizeof(layerData) > layerSize) {
				//printf("reallocating layer buffer to %d bytes (%d entries)\n", layerSize << 1, (layerSize << 1) / sizeof(layerData));
				layer = realloc(layer, layerSize << 1);
				if (layer == NULL)
					die("Scan: realloc layer","");
				layerSize <<= 1;
			}
		#endif
	}

	if (layerCount > 0)
		layer[layerCount - 1].size = &gcodefile[filesz] - layer[layerCount - 1].index;

	printf("%d layers OK\n", layerCount);

	layer = realloc(layer, layerCount * sizeof(layerData));
	if (layer == NULL)
		die("Scan: realloc layer","");
	layerSize = layerCount * sizeof(layerData);
}

int main(int argc, char* argv[]) {
	msgbuf = malloc(256);
	msgbuf[0] = 0;

	if (argc == 1) {
		printf("USAGE: gcodeview <file>\n");
		return 0;
	}

	int fd = open(argv[1], 0);
	if (fd == -1)
		die("Open ", argv[1]);

	struct stat filestats;
	if (fstat(fd, &filestats) == -1)
		die("fstat ", argv[1]);

	filesz = filestats.st_size;

	gcodefile = mmap(NULL, filesz, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
	if (gcodefile == MAP_FAILED)
		die("mmap ", argv[1]);

	scanLines();

	//for (int i = 0; i < layerCount; i++)
	//	printf("Layer %3d starts at %7d and is %7d bytes long\n", i, layer[i].index - gcodefile, layer[i].size);

	Running = true;
	Surf_Display = NULL;

	if (SDL_Init(SDL_INIT_EVERYTHING) < 0)
		die("SDL_init", "");

	if (FcInitLoadConfigAndFonts() == ((void *) FcTrue))
		die("FontConfig Init","");

	// from http://www.spinics.net/lists/font-config/msg03050.html
		FcPattern *pat, *match;
		FcResult result;
		char *file;
		int index;
		pat = FcPatternCreate();
		FcPatternAddString(pat, FC_FAMILY, (FcChar8 *) "Mono");
		FcConfigSubstitute(NULL, pat, FcMatchPattern);
		FcDefaultSubstitute(pat);
		match = FcFontMatch(NULL, pat, &result);
		FcPatternGetString(match, FC_FILE, 0, (FcChar8 **) &file);
		FcPatternGetInteger(match, FC_INDEX, 0, &index);

		FcPatternDestroy (match);
		FcPatternDestroy (pat);

	font = ftglCreateExtrudeFont(file);
	if (!font)
		die("FTGL createFont", "");

	#ifdef	OPENGL
		transX = transY = 0.0;
		zoomFactor = 1.0;
		resize(600, 600);
	#else
		viewPortL = viewPortT = 0.0;
		viewPortR = viewPortB = 200.0;
		zoomFactor = 3.0;
		resize(viewPortR * zoomFactor, viewPortB * zoomFactor);
	#endif

	SDL_WM_SetCaption("gcodeview", 0);

	drawLayer(0);

	layerVelocity = 0;

	SDL_TimerID timer = NULL;
	SDL_Event Event;
	while(Running != false) {
		if (SDL_WaitEvent(&Event) == 0)
			die("SDL_WaitEvent", "");
		switch (Event.type) {
			case SDL_QUIT:
				Running = false;
				break;
			case SDL_VIDEORESIZE:
				resize(Event.resize.w, Event.resize.h);
				break;
			case SDL_VIDEOEXPOSE:
				render();
				break;
			case SDL_MOUSEBUTTONDOWN:
				//printf("SDL Mousebutton down event: mouse %d, button %d, state %d, %dx%d\n", Event.button.which, Event.button.button, Event.button.state, Event.button.x, Event.button.y);
				switch (Event.button.button) {
					case 0: // left mouse
						break;
					case 1: // right mouse
						break;
					case 2: // middle mouse
						break;
					case 3: // ???
						break;
					case 4: // wheel up
						if ((keymodifiermask & (KMM_LSHIFT | KMM_RSHIFT)) == 0) {
						#ifdef	OPENGL
							float mousex = Event.button.x;
							float mousey = Surf_Display->h - Event.button.y;
							float w = Surf_Display->w;
							float h = Surf_Display->h;
							float gX = transX + (mousex / w) * 200.0 / zoomFactor;
							float gY = transY + (mousey / h) * 200.0 / zoomFactor;
							//printf("%d,%d->%d,%d\n", (int) transX, (int) transY, (int) gX, (int) gY);
							zoomFactor *= 1.1;
							transX = gX - (mousex / w) * 200.0 / zoomFactor;
							transY = gY - (mousey / h) * 200.0 / zoomFactor;
						#else
							//float viewX = (gX - viewPortL) * zoomFactor,
							float gX = ((float) Event.button.x) / zoomFactor + viewPortL;
							// float viewY = (viewPortB - gY) * zoomFactor,
							float gY = viewPortB - ((float) Event.button.y) / zoomFactor;
							zoomFactor *= 1.1;
							//printf("Zoom %g\n", zoomFactor);
							viewPortL = gX - ((float) Event.button.x) / zoomFactor;
							viewPortB = ((float) Event.button.y) / zoomFactor + gY;
						#endif
							render();
						}
						else if (currentLayer > 0)
							drawLayer(--currentLayer);
						break;
					case 5: // wheel down
						if ((keymodifiermask & (KMM_LSHIFT | KMM_RSHIFT)) == 0) {
						#ifdef	OPENGL
							float mousex = Event.button.x;
							float mousey = Surf_Display->h - Event.button.y;
							float w = Surf_Display->w;
							float h = Surf_Display->h;
							float gX = transX + (mousex / w) * 200.0 / zoomFactor;
							float gY = transY + (mousey / h) * 200.0 / zoomFactor;
							//printf("%d,%d->%d,%d\n", (int) transX, (int) transY, (int) gX, (int) gY);
							zoomFactor /= 1.1;
							transX = gX - (mousex / w) * 200.0 / zoomFactor;
							transY = gY - (mousey / h) * 200.0 / zoomFactor;
						#else
							//float viewX = (gX - viewPortL) * zoomFactor,
							float gX = ((float) Event.button.x) / zoomFactor + viewPortL;
							// float viewY = (viewPortB - gY) * zoomFactor,
							float gY = viewPortB - ((float) Event.button.y) / zoomFactor;
							zoomFactor /= 1.1;
							//printf("Zoom %g\n", zoomFactor);
							viewPortL = gX - ((float) Event.button.x) / zoomFactor;
							viewPortB = ((float) Event.button.y) / zoomFactor + gY;
						#endif
						}
						else if (currentLayer < layerCount - 1)
							drawLayer(++currentLayer);
						render();
						break;
				}
				break;
			case SDL_MOUSEBUTTONUP:
				break;
			case SDL_MOUSEMOTION:
				break;
			case SDL_ACTIVEEVENT: // lose or gain focus
				break;
			case SDL_KEYDOWN:
				switch(Event.key.keysym.sym) {
					case SDLK_q:
					case SDLK_ESCAPE:
						printf("Exiting\n");
						Running = false;
						break;
					case SDLK_r:
						printf("Resetting position\n");
						zoomFactor = 3;
						#ifdef	OPENGL
							transX = transY = 0.0;
						#else
							viewPortL = 0.0;
							viewPortB = 200.0;
						#endif
						resize(600, 600);
						render();
						break;
					case SDLK_PAGEUP:
						layerVelocity = 1;
						if (currentLayer < layerCount - 1)
							drawLayer(++currentLayer);
						if (timer)
							SDL_RemoveTimer(timer);
						timer = SDL_AddTimer(500, &timerCallback, NULL);
						break;
					case SDLK_PAGEDOWN:
						layerVelocity = -1;
						if (currentLayer > 0)
							drawLayer(--currentLayer);
						if (timer)
							SDL_RemoveTimer(timer);
						timer = SDL_AddTimer(500, &timerCallback, NULL);
						break;
					case SDLK_LSHIFT:
						keymodifiermask |= KMM_LSHIFT;
						break;
					case SDLK_RSHIFT:
						keymodifiermask |= KMM_RSHIFT;
						break;
					default:
						printf("key %d pressed (%c)\n", Event.key.keysym.sym, Event.key.keysym.sym);
						break;
				}
				break;
			case SDL_KEYUP:
				switch(Event.key.keysym.sym) {
					case SDLK_PAGEUP:
						layerVelocity = 0;
						if (timer) {
							SDL_RemoveTimer(timer);
							timer = NULL;
						}
						break;
					case SDLK_PAGEDOWN:
						layerVelocity = 0;
						if (timer) {
							SDL_RemoveTimer(timer);
							timer = NULL;
						}
						break;
					case SDLK_LSHIFT:
						keymodifiermask &= ~KMM_LSHIFT;
						break;
					case SDLK_RSHIFT:
						keymodifiermask &= ~KMM_RSHIFT;
						break;
					default:
						break;
				}
				break;
			case SDL_USEREVENT:
				if (layerVelocity > 0) {
					if (currentLayer < layerCount - 1)
						drawLayer(++currentLayer);
				}
				else if (layerVelocity < 0) {
					if (currentLayer > 0)
						drawLayer(--currentLayer);
				}
				break;
			default:
				printf("SDL Event %d\n", Event.type);
				break;
		}
		//idle code
		//render code
	}
	if (timer)
		SDL_RemoveTimer(timer);
	free(layer);
	SDL_FreeSurface(Surf_Display);
	SDL_Quit();
	return 0;
}
