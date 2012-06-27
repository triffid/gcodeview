/***************************************************************************\
*                                                                           *
* Copyright 2012 Michael Moon                                               *
*                                                                           *
*                                                                           *
* This program is free software: you can redistribute it and/or modify      *
* it under the terms of the GNU General Public License as published by      *
* the Free Software Foundation, either version 3 of the License, or         *
* (at your option) any later version.                                       *
*                                                                           *
* This program is distributed in the hope that it will be useful,           *
* but WITHOUT ANY WARRANTY; without even the implied warranty of            *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
* GNU General Public License for more details.                              *
*                                                                           *
* You should have received a copy of the GNU General Public License         *
* along with this program.  If not, see <http://www.gnu.org/licenses/>.     *
*                                                                           *
*                                                                           *
* This program lives at http://github.com/triffid/gcodeview and Author can  *
* be contacted via that site                                                *
*                                                                           *
\***************************************************************************/


#ifndef	_GNU_SOURCE
#define		_GNU_SOURCE
#endif

#ifdef __linux__
#include	<features.h>
#endif

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
#include	<getopt.h>

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

#ifndef	SDL_NOEVENT
#define	SDL_NOEVENT 0
#endif

// main loop fall-through flag
bool Running;

// whether or not to do cacheing of each layer
bool cache;

// drawing stuff
#define SHADOW_LAYERS 3
#define	SHADOW_ALPHA  0.2
int shadow_layers = 3;
float shadow_alpha = 0.2;

// busy flags
#define	BUSY_SCANFILE	1
#define	BUSY_RENDER		2
int busy;

// getopt stuff
static const char *optString = "l:w:nh?";
static const struct option longOpts[] = {
	{ "layer", required_argument, NULL, 'l' },
	{ "width", required_argument, NULL, 'w' },
	{ "no-cache", no_argument,    NULL, 'n' },
	{ 0      , 0                , 0   , 0   }
};

// GCODE file related stuff
int filesz;
char* gcodefile;
char* gcodefile_end;
float extrusionWidth = 0.3;
int layerCount;
size_t layerSize;
float linewords[26];

// for tracking hop/z-lift moves
int ZstackIndex = 0;
typedef struct {
	char *start;
	float E, X, Y, Z;
} ZstackItem;
ZstackItem Zstack[8];

// file scan stuff
#define	LMASK(l) (1<<((l & ~0x20) - 'A'))
#define	SEEN(c) ((seen & LMASK(c)) != 0)
#define	LW(c)	linewords[c -'A']

// layer data
#define	LD_LISTGENERATED 1
typedef struct {
	char*	index;
	int		size;
	float	height;
	uint8_t	flags;
	int		glList;
	float	startX;
	float	startY;
	float	startE;
	float 	endX;
	float 	endY;
	float	endE;
} layerData;
layerData* layer;

// FTGL stuff for drawing text
FTGLfont* font = NULL;
char *msgbuf;

// SDL window and GL Viewport
SDL_Surface* Surf_Display;
int Surf_width;
int Surf_height;

// Current View settings
int layerVelocity;
int currentLayer;
float zoomFactor;

// SDL Events Interface
#define	KMM_LSHIFT 1
#define KMM_RSHIFT 2
#define	KMM_CTRL   4
#define	KMM_ALT    8
int keymodifiermask;

#define	TIMER_KEYREPEAT  1
#define	TIMER_DRAGRENDER 2
#define TIMER_IDLE       3
SDL_TimerID timerKeyRepeat = NULL;
SDL_TimerID timerDragRender = NULL;
SDL_TimerID timerIdle = NULL;

float gXmouseDown = 0.0, gYmouseDown = 0.0;

/***************************************************************************\
*                                                                           *
* Utility Functions                                                         *
*                                                                           *
\***************************************************************************/

void display_usage() {
	printf("\n");
	printf("USAGE: gcodeview [-w|--width width] [-l|--layer layer] [-n|--no-cache] <file.gcode>\n");
	printf("\n");
	printf("\twidth:    Extrusion Width used to draw lines\n");
	printf("\tlayer:    Render this layer first\n");
	printf("\tno-cache: Don't cache layers (large files)\n");
	printf("\n");
	printf("Color Key:\n");
	printf("\n");
	printf("\tBlack:    Extrusion move at current layer height\n");
	printf("\tGreen:    Travel move at current layer height\n");
	printf("\tRed:      Travel move at higher layer height (ie hop/z-lift)\n");
	printf("\tMagenta:  Travel move at lower layer height (ie cutting/etching)\n");
	printf("\n");
	exit(0);
}

float minf(float a, float b) {
	if (a < b)
		return a;
	return b;
}

float maxf(float a, float b) {
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
	e.user.code = (int) param;
	e.user.data1 = 0;
	e.user.data2 = 0;

	SDL_PushEvent(&e);

	return 0;
}

void dumpZstack() {
	printf("Zstack has %d entries:\n", ZstackIndex);
	for (int i = 0; i < ZstackIndex; i++) {
		printf("Zstack %d:\n", i);
		printf("\tstart: %d\n", Zstack[i].start - gcodefile);
		printf("\tX: %g\n\tY: %g\n\tZ: %g\n", Zstack[i].X, Zstack[i].Y, Zstack[i].Z);
	}
}

/***************************************************************************\
*                                                                           *
* Read a single line of GCODE, extracting which words are present and their *
* values                                                                    *
*                                                                           *
\***************************************************************************/

void findEndFloat(char *c, char **end) {
	while ((*c >= '0' && *c <= '9') || (*c == '.') || (*c == 'e') || (*c == '-') || (*c == '+'))
		c++;
	*end = c;
}

uint32_t scanline(char *line, int length, float *words, char **end, uint32_t interest_mask) {
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
					if (LMASK(c) & interest_mask) {
						float v = strtof(&line[i + 1], &e);
						if (e > &line[i + 1]) {
							seen |= LMASK(c);
							words[c - 'A'] = v;
							i = e - line - 1;
						}
					}
					else {
						seen |= LMASK(c);
						findEndFloat(&line[i + 1], &e);
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

/***************************************************************************\
*                                                                           *
* Draw a thick line (QUAD) given gcode coordinates, width and RGBA          *
*                                                                           *
\***************************************************************************/

void gline(float x1, float y1, float x2, float y2, float width, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	#ifdef	OPENGL
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

/***************************************************************************\
*                                                                           *
* create the quads for a layer, no wrappers                                 *
*                                                                           *
\***************************************************************************/

void render_layer(int clayer, float alpha) {
	char *s = layer[clayer].index;
	char *e = layer[clayer].index + layer[clayer].size;
	float G = NAN, X = NAN, Y = NAN, E = NAN, Z = NAN, lastX = NAN, lastY = NAN, lastE = NAN;
	uint32_t seen = 0;

	for (X = 0; X < 201.0; X += 10.0) {
		gline(X, 0, X, 200, ((((int) X) % 50) == 0)?1:0.2, 0, 0, 0, 16);
		gline(0, X, 200, X, ((((int) X) % 50) == 0)?1:0.2, 0, 0, 0, 16);
	}

	//printf("render layer %d (%g)\n", clayer + 1, alpha);

	lastX = layer[clayer].startX;
	lastY = layer[clayer].startY;
	Z = layer[clayer].height;
	lastE = layer[clayer].startE;

	while (s < e) {
		seen = scanline(s, e - s, linewords, &s, LMASK('G') | LMASK('X') | LMASK('Y') | LMASK('Z') | LMASK('E'));
		if (SEEN('G') && (LW('G') == 0.0 || LW('G') == 1.0)) {
			G = LW('G');
			if (SEEN('X'))
				X = LW('X');
			if (SEEN('Y'))
				Y = LW('Y');
			if (SEEN('Z'))
				Z = LW('Z');
			if (SEEN('E'))
				E = LW('E');
			//if (clayer == 2)
			//	printf("SEEN %c%c%c%c X%g Y%g Z%g E%g\n", SEEN('X')?'X':' ', SEEN('Y')?'Y':' ', SEEN('Z')?'Z':' ', SEEN('E')?'E':' ', X, Y, Z, E);
			if (SEEN('X') || SEEN('Y')) {
				// draw
				uint8_t r = 0, g = 0, b = 0, a = 160;
				if (SEEN('E')) {
					r = 0;
					g = 0;
					b = 0;
					a = 224;
				}
				else if (Z > layer[clayer].height) {
					r = 224;
					g = 64;
					b = 64;
					a = 160;
				}
				else if (Z < layer[clayer].height) {
					r = 128;
					g = 0;
					b = 128;
					a = 160;
				}
				else {
					r = 0;
					g = 128;
					b = 64;
					a = 160;
				}
				if ((lastX != X || lastY != Y) && !isnan(X) && !isnan(Y) && lastX <= 200.0)
					gline(lastX, lastY, X, Y, extrusionWidth, r, g, b, a * alpha);
			}
			if (SEEN('X'))
				lastX = X;
			if (SEEN('Y'))
				lastY = Y;
			if (SEEN('E'))
				lastE = E;
		}
	}
}

/***************************************************************************\
*                                                                           *
* Update the OpenGL display with the current layer                          *
*                                                                           *
\***************************************************************************/

void render() {
	#ifdef	OPENGL
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glLoadIdentity();
		glPushMatrix();
		glScalef(zoomFactor, zoomFactor, 0.0);
		glTranslatef(-transX, -transY, 0.0);
		if (layer[currentLayer].glList) {
			glCallList(layer[currentLayer].glList);
		}
		else {
			layer[currentLayer].glList = glGenLists(1);
			glNewList(layer[currentLayer].glList, GL_COMPILE_AND_EXECUTE);
			glBegin(GL_QUADS);
	#else
		uint32_t yellow;

		yellow = SDL_MapRGB(Surf_Display->format, 224, 224, 128);

		SDL_LockSurface(Surf_Display);
		SDL_FillRect(Surf_Display, NULL, yellow);
		int lines = 0;
	#endif

			for (int i = shadow_layers; i >= 1; i--) {
				if (currentLayer - i > 0)
					render_layer(currentLayer - i, shadow_alpha - (i - 1) * (shadow_alpha / ((float) shadow_layers)));
			}
			render_layer(currentLayer, 1.0);

	#ifdef	OPENGL
			glEnd();
			glEndList();
		}
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

/***************************************************************************\
*                                                                           *
* Resize the display                                                        *
*                                                                           *
* Includes refreshing the OpenGL Context                                    *
*                                                                           *
\***************************************************************************/

void resize(int w, int h) {
	Surf_width = w;
	Surf_height = h;
	#ifdef	OPENGL
		int dim;
		if (w > h)
			dim = h;
		else
			dim = w;

		for (int i = 0; i < layerCount; i++) {
			if (layer[i].glList)
				glDeleteLists(layer[i].glList, 1);
			layer[i].glList = 0;
			layer[i].flags = 0;
		};

		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

		Surf_Display = SDL_SetVideoMode(Surf_width, Surf_height, 0, SDL_HWSURFACE | SDL_RESIZABLE | SDL_OPENGL);

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
		glOrtho(0, 200 * w / dim, 0, 200 * h / dim, 0, 1);
		glDisable(GL_DEPTH_TEST);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		busy |= BUSY_RENDER;
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

/***************************************************************************\
*                                                                           *
* Simple function to change current layer                                   *
*                                                                           *
\***************************************************************************/

void drawLayer(int layerNum) {
	if (layerNum > layerCount)
		layerNum = layerCount;
	snprintf(msgbuf, 256, "Layer %3d: %gmm", layerNum + 1, layer[layerNum].height);
//	printf("Drawing layer %3d (%5.2f)\n", layerNum, layer[layerNum].height);
	if ((currentLayer != layerNum) && (cache == false)) {
		glDeleteLists(layer[currentLayer].glList, 1);
		layer[currentLayer].glList = 0;
	}
	currentLayer = layerNum;
	render();
}

/***************************************************************************\
*                                                                           *
* Read lines from GCODE input file                                          *
* Earmark the start of each layer in the file so we can find it quickly     *
*                                                                           *
\***************************************************************************/

void scanLine() {
	static char* l = NULL;
	static float lastX = 0.0, lastY = 0.0, lastE = 0.0;

	if (l == NULL)
		l = gcodefile;
	char* end;
	uint32_t seen;

	if (l < gcodefile_end) {
		//printf("\t-\n");
		seen = scanline(l, gcodefile_end - l, linewords, &end, LMASK('G') | LMASK('X') | LMASK('Y') | LMASK('Z') | LMASK('E'));

		if (SEEN('G')) {
			if (LW('G') == 0.0 || LW('G') == 1.0) {
				if (layer[layerCount].index == NULL) {
					layer[layerCount].index = l;
					layer[layerCount].startX = lastX;
					layer[layerCount].startY = lastY;
					layer[layerCount].startE = lastE;
				}
				if (SEEN('Z')) {
					//dumpZstack();
					//printf("%d: Z%g\n", l - gcodefile, LW('Z'));
					if (layer[layerCount].height == NAN)
						layer[layerCount].height = LW('Z');
					else {
						int i;
						//dumpZstack();
						for (i = 0; i < ZstackIndex; i++) {
							//printf("Check %d: got %g vs found %g\n", i, Zstack[i].Z, LW('Z'));
							if (Zstack[i].Z == LW('Z')) {
								//printf("found end of hop\n");
								// end of hop
								ZstackIndex = i + 1;
								break;
							}
						}
						//printf("ZS %d i %d\n", ZstackIndex, i);
						if (i >= ZstackIndex || ZstackIndex == 0) {
							//printf("found start of hop\n");
							// start of hop or new layer
							Zstack[ZstackIndex].start = l;
							Zstack[ZstackIndex].X = lastX;
							Zstack[ZstackIndex].Y = lastY;
							Zstack[ZstackIndex].Z = LW('Z');
							Zstack[ZstackIndex].E = lastE;
							ZstackIndex++;
							if (ZstackIndex >= 8)
								die("Zstack overflow!","");
						}
					}
				}
				if (SEEN('E')) {
					// extrusion, collapse Z stack
					int i = ZstackIndex - 1;
					if (Zstack[i].Z != layer[layerCount].height) {
						//printf("E word at Z=%g\n", LW('Z'));
						//dumpZstack();
						//printf("new layer!\n");
						// finish previous layer
						layer[layerCount].size = Zstack[i].start - layer[layerCount].index;
						layer[layerCount].flags = 0;
						layer[layerCount].glList = 0;
						layer[layerCount].endX = Zstack[i].X;
						layer[layerCount].endY = Zstack[i].Y;
						layer[layerCount].endE = Zstack[i].E;

						// start new layer
						layerCount++;
						//printf("NEW LAYER: %d\n", layerCount);
						if (layerCount * sizeof(layerData) >= layerSize) {
							layerSize += sizeof(layerData) * 128;
							layer = realloc(layer, layerSize);
							if (layer == NULL)
								die("Scan: realloc layer","");
						}
						//printf("START LAYER %d\n", layerCount);
						// initialise
						layer[layerCount].index = Zstack[i].start;
						layer[layerCount].startX = Zstack[i].X;
						layer[layerCount].startY = Zstack[i].Y;
						layer[layerCount].height = Zstack[i].Z;
						layer[layerCount].startE = Zstack[i].E;
						// flush Z stack
						memcpy(Zstack, &Zstack[i], sizeof(ZstackItem));
						ZstackIndex = 1;
						//dumpZstack();
					}
				}
			}
			if (SEEN('X'))
				lastX = LW('X');
			if (SEEN('Y'))
				lastY = LW('Y');
			if (SEEN('E'))
				lastE = LW('E');
		}
		l = end;
	}
	if (l >= gcodefile_end) {
		layer[layerCount].size = l - layer[layerCount].index;
		layer[layerCount].flags = 0;
		layer[layerCount].glList = 0;
		layer[layerCount].endX = lastX;
		layer[layerCount].endY = lastY;
		layer[layerCount].endE = lastE;
		layerCount++;

		printf("Found %d layers\n", layerCount);

		if (0)
		for (int i = 0; i < layerCount; i++) {
			printf("Layer %d at %d+%d=%d\n", i, layer[i].index - gcodefile, layer[i].size, layer[i].index - gcodefile + layer[i].size);
			printf("\tHeight:   %g\n", layer[i].height);
			printf("\tStarts at [%g,%g:%g]\n", layer[i].startX, layer[i].startY, layer[i].startE);
			printf("\tEnds   at [%g,%g:%g]\n", layer[i].endX, layer[i].endY, layer[i].endE);
		}

		busy &= ~BUSY_SCANFILE;
	}
}

// quickly finds user-specified layer before first render
void scanLines() {
	printf("Indexing lines...\n");

	layerCount = 0;
	// preallocate for 128 layers, we double the size later if it's not enough
	layerSize = (128 * sizeof(layerData));
	layer = malloc(layerSize);

	layer[0].startX = NAN;
	layer[0].startY = NAN;
	layer[0].index = NULL;

	ZstackIndex = 0;

	while ((busy & BUSY_SCANFILE) && ((layerCount - 2) <= currentLayer)) {
		scanLine();
	}

	printf("found layer %d\n", currentLayer);
}

/***************************************************************************\
*                                                                           *
* SDL Event Handlers                                                        *
*                                                                           *
\***************************************************************************/

void handle_mousedown(SDL_MouseButtonEvent button) {
	//printf("SDL Mousebutton down event: mouse %d, button %d, state %d, %dx%d\n", Event.button.which, Event.button.button, Event.button.state, Event.button.x, Event.button.y);
	switch (button.button) {
		case 1: // left mouse
			{
				float mousex = button.x;
				float mousey = Surf_Display->h - button.y;
				float w = Surf_Display->w;
				float h = Surf_Display->h;
				float dim = minf(w, h);
				gXmouseDown = transX + (mousex / w) * 200.0 * w / dim / zoomFactor;
				gYmouseDown = transY + (mousey / h) * 200.0 * h / dim / zoomFactor;
				if (timerDragRender)
					SDL_RemoveTimer(timerDragRender);
				timerDragRender = SDL_AddTimer(50, &timerCallback, (void *) TIMER_DRAGRENDER);
			}
			break;
		case 2: // middle mouse
			break;
		case 3: // right mouse
			break;
		case 4: // wheel up
			if ((keymodifiermask & (KMM_LSHIFT | KMM_RSHIFT)) == 0) {
			#ifdef	OPENGL
				float mousex = button.x;
				float mousey = Surf_Display->h - button.y;
				float w = Surf_Display->w;
				float h = Surf_Display->h;
				float dim = minf(w, h);
				float gX = transX + (mousex / w) * 200.0 * w / dim / zoomFactor;
				float gY = transY + (mousey / h) * 200.0 * h / dim / zoomFactor;
				//printf("%d,%d->%d,%d\n", (int) transX, (int) transY, (int) gX, (int) gY);
				zoomFactor *= 1.1;
				transX = gX - (mousex / w) * 200.0 * w / dim / zoomFactor;
				transY = gY - (mousey / h) * 200.0 * h / dim/ zoomFactor;
			#else
				//float viewX = (gX - viewPortL) * zoomFactor,
				float gX = ((float) button.x) / zoomFactor + viewPortL;
				// float viewY = (viewPortB - gY) * zoomFactor,
				float gY = viewPortB - ((float) button.y) / zoomFactor;
				zoomFactor *= 1.1;
				//printf("Zoom %g\n", zoomFactor);
				viewPortL = gX - ((float) button.x) / zoomFactor;
				viewPortB = ((float) button.y) / zoomFactor + gY;
			#endif
				render();
			}
			else if (currentLayer > 0)
				drawLayer(--currentLayer);
			break;
		case 5: // wheel down
			if ((keymodifiermask & (KMM_LSHIFT | KMM_RSHIFT)) == 0) {
			#ifdef	OPENGL
				float mousex = button.x;
				float mousey = Surf_Display->h - button.y;
				float w = Surf_Display->w;
				float h = Surf_Display->h;
				float dim = minf(w, h);
				float gX = transX + (mousex / w) * 200.0 * w / dim / zoomFactor;
				float gY = transY + (mousey / h) * 200.0 * h / dim / zoomFactor;
				//printf("%d,%d->%d,%d\n", (int) transX, (int) transY, (int) gX, (int) gY);
				zoomFactor /= 1.1;
				transX = gX - (mousex / w) * 200.0 * w / dim / zoomFactor;
				transY = gY - (mousey / h) * 200.0 * h / dim / zoomFactor;
			#else
				//float viewX = (gX - viewPortL) * zoomFactor,
				float gX = ((float) button.x) / zoomFactor + viewPortL;
				// float viewY = (viewPortB - gY) * zoomFactor,
				float gY = viewPortB - ((float) button.y) / zoomFactor;
				zoomFactor /= 1.1;
				//printf("Zoom %g\n", zoomFactor);
				viewPortL = gX - ((float) button.x) / zoomFactor;
				viewPortB = ((float) button.y) / zoomFactor + gY;
			#endif
				render();
			}
			else if (currentLayer < layerCount - 1)
				drawLayer(++currentLayer);
			break;
	}
}
void handle_mousemove(SDL_MouseMotionEvent motion) {
	if (motion.state & 1) {	// left-drag
		float mousex = motion.x;
		float mousey = Surf_Display->h - motion.y;
		float w = Surf_Display->w;
		float h = Surf_Display->h;
		float dim = minf(w, h);
		transX = gXmouseDown - (mousex / w) * 200.0 * w / dim / zoomFactor;
		transY = gYmouseDown - (mousey / h) * 200.0 * h / dim / zoomFactor;
	}
}
void handle_mouseup(SDL_MouseButtonEvent button) {
	switch (button.button) {
		case 1: // left mouse
			if (timerDragRender) {
				SDL_RemoveTimer(timerDragRender);
				timerDragRender = NULL;
			}
			break;
		}
}

void handle_keydown(SDL_KeyboardEvent key) {
	switch(key.keysym.sym) {
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
			if (timerKeyRepeat)
				SDL_RemoveTimer(timerKeyRepeat);
			else if (currentLayer < layerCount - 1)
				drawLayer(++currentLayer);
			timerKeyRepeat = SDL_AddTimer(500, &timerCallback, (void *) TIMER_KEYREPEAT);
			break;
		case SDLK_PAGEDOWN:
			layerVelocity = -1;
			if (timerKeyRepeat)
				SDL_RemoveTimer(timerKeyRepeat);
			else if (currentLayer > 0)
				drawLayer(--currentLayer);
			timerKeyRepeat = SDL_AddTimer(500, &timerCallback, (void *) TIMER_KEYREPEAT);
			break;
		case SDLK_LSHIFT:
			keymodifiermask |= KMM_LSHIFT;
			break;
		case SDLK_RSHIFT:
			keymodifiermask |= KMM_RSHIFT;
			break;
		default:
			printf("key %d pressed (%c)\n", key.keysym.sym, key.keysym.sym);
			break;
	}
}

void handle_keyup(SDL_KeyboardEvent key) {
	switch(key.keysym.sym) {
		case SDLK_PAGEUP:
			layerVelocity = 0;
			if (timerKeyRepeat) {
				SDL_RemoveTimer(timerKeyRepeat);
				timerKeyRepeat = NULL;
			}
			break;
		case SDLK_PAGEDOWN:
			layerVelocity = 0;
			if (timerKeyRepeat) {
				SDL_RemoveTimer(timerKeyRepeat);
				timerKeyRepeat = NULL;
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
}

void handle_userevent(SDL_UserEvent user) {
	switch (user.code) {
		case TIMER_KEYREPEAT:
			SDL_RemoveTimer(timerKeyRepeat);
			if (layerVelocity > 0) {
				if (currentLayer < layerCount - 1)
					drawLayer(++currentLayer);
				else
					break;
			}
			else if (layerVelocity < 0) {
				if (currentLayer > 0)
					drawLayer(--currentLayer);
				else
					break;
			}
			timerKeyRepeat = SDL_AddTimer(20, &timerCallback, (void *) TIMER_KEYREPEAT);
			break;
		case TIMER_DRAGRENDER:
			SDL_RemoveTimer(timerDragRender);
			render();
			timerDragRender = SDL_AddTimer(50, &timerCallback, (void *) TIMER_DRAGRENDER);
			break;
	}
}

/***************************************************************************\
*                                                                           *
* Main                                                                      *
*                                                                           *
* Read GCODE, Initialise SDL window and OpenGL surface, Start FTGL, run SDL *
* Event loop                                                                *
*                                                                           *
\***************************************************************************/

int main(int argc, char* argv[]) {
	msgbuf = malloc(256);
	msgbuf[0] = 0;

	currentLayer = 0;
	cache = true;

	int longIndex;
	int opt;
	do {
		opt = getopt_long(argc, argv, optString, longOpts, &longIndex);
		if (opt != -1) {
			switch( opt ) {
				case 'l':
					currentLayer = strtol(optarg, NULL, 10);
					break;

				case 'w':
					extrusionWidth = strtof(optarg, NULL);
					break;

				case 'n':
					printf("DISABLING CACHE\n");
					cache = false;
					break;

				case 'h':   /* fall-through is intentional */
				case '?':
					display_usage();
					break;

				case 0:     /* long option without a short arg */
					//if( strcmp( "randomize", longOpts[longIndex].name ) == 0 ) {
					//	globalArgs.randomized = 1;
					//}
					break;

				default:
					/* You won't actually get here. */
					break;
			}
		}
	}
	while (opt != -1);

	if (optind >= argc)
		display_usage();

	int fd = open(argv[optind], 0);
	if (fd == -1)
		die("Open ", argv[optind]);

	struct stat filestats;
	if (fstat(fd, &filestats) == -1)
		die("fstat ", argv[optind]);

	filesz = filestats.st_size;

	printf("File is %d long\n", filesz);

#ifdef __linux__
	gcodefile = mmap(NULL, filesz, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
#elif defined __APPLE__
	gcodefile = mmap(NULL, filesz, PROT_READ, MAP_PRIVATE, fd, 0);
#else
	#error "don't know how to mmap on this system!"
#endif

	if (gcodefile == MAP_FAILED)
		die("mmap ", argv[optind]);
	gcodefile_end = &gcodefile[filesz];

	busy = BUSY_SCANFILE;

	scanLines();

	if (currentLayer >= layerCount)
		currentLayer = layerCount - 1;

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

	drawLayer(currentLayer);

	layerVelocity = 0;

	timerIdle = SDL_AddTimer(20, &timerCallback, (void *) TIMER_IDLE);

	SDL_Event Event;
	while(Running != false) {
		if (busy) {
			Event.type = SDL_NOEVENT;
			SDL_PollEvent(&Event);
		}
		else {
			if (SDL_WaitEvent(&Event) == 0)
				die("SDL_WaitEvent", "");
		}
		//SDL_RemoveTimer(timerIdle);
		switch (Event.type) {
			case SDL_NOEVENT:
				if (busy & BUSY_SCANFILE) {
					// TODO: scan next layer
					scanLine();
					if ((busy & BUSY_SCANFILE) == 0) {
						if (cache) {
							printf("File scanned, rendering...\n");
							busy = BUSY_RENDER;
						}
						else {
							printf("File scanned.\n");
							busy = 0;
						}
					}
				}
				else if ((busy & BUSY_RENDER) && cache) {
					bool allRendered = true;
					int i;
					// TODO: render next layer in background
					for (i = 0; i < layerCount; i++) {
						if (layer[i].glList == 0) {
							layer[i].glList = glGenLists(1);
							glNewList(layer[i].glList, GL_COMPILE);
							glBegin(GL_QUADS);
							for (int j = SHADOW_LAYERS; j >= 1; j--) {
								if (i - j > 0)
									render_layer(i - j, SHADOW_ALPHA - (j - 1) * (SHADOW_ALPHA / SHADOW_LAYERS));
							}
							render_layer(i, 1.0);
							glEnd();
							glEndList();
							layer[i].flags |= LD_LISTGENERATED;
							allRendered = false;
							break;
						}
					}
					if (allRendered) {
						printf("All %d layers rendered\n", i);
						busy &= ~BUSY_RENDER;
					}
				}
				break;
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
				handle_mousedown(Event.button);
				break;
			case SDL_MOUSEBUTTONUP:
				handle_mouseup(Event.button);
				break;
			case SDL_MOUSEMOTION:
				handle_mousemove(Event.motion);
				break;
			case SDL_ACTIVEEVENT: // lose or gain focus
				break;
			case SDL_KEYDOWN:
				handle_keydown(Event.key);
				break;
			case SDL_KEYUP:
				handle_keyup(Event.key);
				break;
			case SDL_USEREVENT:
				handle_userevent(Event.user);
				break;
			default:
				printf("SDL Event %d\n", Event.type);
				break;
		}
		//idle code
		//if (busy)
		//	timerIdle = SDL_AddTimer(20, &timerCallback, (void *) TIMER_IDLE);
	}
	if (timerKeyRepeat)
		SDL_RemoveTimer(timerKeyRepeat);
	if (timerDragRender)
		SDL_RemoveTimer(timerDragRender);
	free(layer);
	SDL_FreeSurface(Surf_Display);
	SDL_Quit();
	return 0;
}
