#define		_GNU_SOURCE

#include	<features.h>

#include	<stdlib.h>
#include	<stdint.h>
#include	<stdio.h>

#include	<sys/mman.h>
#include	<sys/stat.h>
#include	<sys/types.h>

#include	<math.h>
#include	<regex.h>
#include	<errno.h>
#include	<string.h>
#include	<fcntl.h>

#include	<SDL.h>
#include	<SDL_gfxPrimitives.h>

#define		bool uint8_t
#define		true	255
#define		false	0

bool Running;
SDL_Surface* Surf_Display;
int Surf_width;
int Surf_height;

int filesz;
char* gcodefile;

int lineCount;
char** lineIndex;

int layerCount;
char** layerIndex;
int* layerSize;
double* layerHeight;


int currentLayer;

double zoomFactor;

double viewPortL, viewPortR, viewPortT, viewPortB;

double mind(double a, double b) {
	if (a < b)
		return a;
	return b;
}

double maxd(double a, double b) {
	if (a > b)
		return a;
	return b;
}

double linint(double value, double oldmin, double oldmax, double newmin, double newmax) {
	return (value - oldmin) * (newmax - newmin) / (oldmax - oldmin) + newmin;
}

void die(char* call, char* data) {
	int errsv = errno;
	fprintf(stderr, "%s%s failed: %s\n", call, data, strerror(errsv));
	exit(1);
}

void gline(double x1, double y1, double x2, double y2, double width, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	thickLineRGBA(Surf_Display,
		(x1 - viewPortL) * zoomFactor,
		(viewPortB - y1) * zoomFactor,
		(x2 - viewPortL) * zoomFactor,
		(viewPortB - y2) * zoomFactor,
		mind(maxd(width * zoomFactor, 1), 2),
		r, g, b, a
		);
}

void render() {
	uint32_t yellow;

	yellow = SDL_MapRGB(Surf_Display->format, 224, 224, 128);

	SDL_LockSurface(Surf_Display);
		int lines = 0;
		char *s = layerIndex[currentLayer];
		char *e = layerIndex[currentLayer] + layerSize[currentLayer];
		double X = 0.0, Y = 0.0, E = 0.0, v = 0.0, lastX = NAN, lastY = NAN, lastE = NAN;
		char *r;
		int seen = 0;

		SDL_FillRect(Surf_Display, NULL, yellow);
		
		for (X = 0; X < 201.0; X += 10.0) {
			gline(X, 0, X, 200, ((((int) X) % 50) == 0)?1:0.01, 0, 0, 0, 32);
			gline(0, X, 200, X, ((((int) X) % 50) == 0)?1:0.01, 0, 0, 0, 32);
		}

		while (s < e) {
			//printf("s is at %d, char is %c\n", s - layerIndex[currentLayer], *s);
			switch (*s) {
				case 'x': case 'X':
					//printf("found X\n");
					v = strtod(s + 1, &r);
					if (r > s + 1) {
						//printf("length is %d, value is %g\n", r - (s + 1), v);
						X = v;
						s = r;
						seen |= 1;
					}
					else
						s++;
					//printf("break\n");
					break;
				case 'y': case 'Y':
					//printf("found Y\n");
					v = strtod(s + 1, &r);
					if (r > s + 1) {
						//printf("length is %d, value is %g\n", r - (s + 1), v);
						Y = v;
						s = r;
						seen |= 2;
					}
					else
						s++;
					//printf("break\n");
					break;
				case 'e': case 'E':
					//printf("found E\n");
					v = strtod(s + 1, &r);
					if (r > s + 1) {
						//printf("length is %d, value is %g\n", r - (s + 1), v);
						E = v;
						s = r;
						seen |= 4;
					}
					else
						s++;
					break;
				case 13: case 10:
					s++;
					// draw
					if (seen & 3) {
						uint8_t r = 0, g = 0, b = 0, a = 224;
						if (isnan(lastX))
							lastX = X;
						if (isnan(lastY))
							lastY = Y;
						if (isnan(lastE))
							lastE = E;
						if (E > lastE) {
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
						gline(lastX, lastY, X, Y, 0.5, r, g, b, a);
						//printf("drawn\n");
					}
					seen = 0;
					//
					lastX = X;
					lastY = Y;
					lastE = E;
					break;
				default:
					s++;
					break;
			}
		}

	SDL_UnlockSurface(Surf_Display);

	SDL_Flip(Surf_Display);
}

void resize(int w, int h) {
	Surf_width = w;
	Surf_height = h;
	Surf_Display = SDL_SetVideoMode(Surf_width, Surf_height, 32, SDL_HWSURFACE | SDL_DOUBLEBUF | SDL_RESIZABLE);
	if (Surf_Display == NULL) {
		SDL_FreeSurface(Surf_Display);
		SDL_Quit();
		die("SDL resize", "");
	}
	render(); // redraw whole window
}

void drawLayer(int layer) {
	printf("Drawing layer %3d (%5.1f)\n", layer, layerHeight[layer]);
	currentLayer = layer;
	render();
}

void scanLines() {
	int nlayers = 0;
	double lastZ = 0;
	int l = 0;
	int r;
	uint8_t c;
	uint8_t nl = 0;
	int ls = 0;
	int comment = 0;
	regex_t* lineRegex;

	printf("Indexing lines... ");

	lineCount = 0;
	lineIndex = malloc(filesz);

	layerCount = 0;
	layerIndex = malloc(filesz);
	layerSize = malloc(filesz);
	layerHeight = malloc(filesz);

	while (l < filesz) {
		c = gcodefile[l];
		if (nl == 0) {
			if (c != 13 && c != 10) {
				nl = 1;
				lineIndex[lineCount++] = &gcodefile[l];
				ls = l;
				if (lineCount >> 2 > filesz)
					die("Scan failed: ", "too many newlines!");
			}
		}
		else if (c == 13 || c == 10) {
			nl = 0;
			comment = 0;
		}
		if (comment == 2 && c == ')')
			comment  = 0;
		else if (comment == 0) {
			if (c == ';')
				comment = 1;
			else if (c == '(')
				comment = 2;
			else if (c == 'z' || c == 'Z') {
				//printf("found a Z at %d: %c... \n", l, gcodefile[l]);
				char *end;
				double zvalue = strtod(&gcodefile[l + 1], &end);
				if (end > &gcodefile[l + 1]) {
					//printf("height: %g...\n", zvalue);
					if (zvalue > lastZ) {
						//printf("greater than %g...\n", lastZ);
						lastZ = zvalue;
						//printf("layer %d starts at %d\n", layerCount, ls);
						//printf("Layer %3d starts at %7d\n", layerCount, ls);
						layerIndex[layerCount] = &gcodefile[ls];
						layerHeight[layerCount] = zvalue;
						if (layerCount > 0) {
							layerSize[layerCount - 1] = layerIndex[layerCount] - layerIndex[layerCount - 1];
						}
						layerCount++;
					}
				}
			}
		}
		l++;
	}

	if (layerCount > 0)
		layerSize[layerCount - 1] = &gcodefile[filesz] - layerIndex[layerCount - 1];

	printf("%d lines, ", lineCount);
	printf("%d layers OK\n", layerCount);

	lineIndex = realloc(lineIndex, lineCount * sizeof(lineIndex));
	if (lineIndex == NULL)
		die("Scan: realloc lineindex ","");

	layerIndex = realloc(layerIndex , layerCount * sizeof(layerIndex ));
	if (layerIndex == NULL)
		die("Scan: realloc layerIndex ","");

	layerSize = realloc(layerSize  , layerCount * sizeof(layerSize  ));
	if (layerSize == NULL)
		die("Scan: realloc layerSize ","");

	layerHeight = realloc(layerHeight, layerCount * sizeof(layerHeight));
	if (layerHeight == NULL)
		die("Scan: realloc layerHeight ","");
}

int main(int argc, char* argv[]) {
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

	for (int i = 0; i < layerCount; i++)
		printf("Layer %3d starts at %7d and is %7d bytes long\n", i, layerIndex[i] - gcodefile, layerSize[i]);

	Running = true;
	Surf_Display = NULL;

	if (SDL_Init(SDL_INIT_EVERYTHING) < 0)
		die("SDL_init", "");

	viewPortL = viewPortT = 0.0;
	viewPortR = viewPortB = 200.0;
	zoomFactor = 3;

	resize(viewPortR * zoomFactor, viewPortB * zoomFactor);

	SDL_WM_SetCaption("gcodeview", 0);

	drawLayer(0);

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
						//if (currentLayer < layerCount - 1)
						//	drawLayer(++currentLayer);
						do {
							//double viewX = (gX - viewPortL) * zoomFactor,
							double gX = ((double) Event.button.x) / zoomFactor + viewPortL;
							// double viewY = (viewPortB - gY) * zoomFactor,
							double gY = viewPortB - ((double) Event.button.y) / zoomFactor;
							zoomFactor *= 1.1;
							printf("Zoom %g\n", zoomFactor);
							viewPortL = gX - ((double) Event.button.x) / zoomFactor;
							viewPortB = ((double) Event.button.y) / zoomFactor + gY;
							render();
						} while (0);
						break;
					case 5: // wheel down
						//if (currentLayer > 0)
						//	drawLayer(--currentLayer);
						do {
							//double viewX = (gX - viewPortL) * zoomFactor,
							double gX = ((double) Event.button.x) / zoomFactor + viewPortL;
							// double viewY = (viewPortB - gY) * zoomFactor,
							double gY = viewPortB - ((double) Event.button.y) / zoomFactor;
							zoomFactor /= 1.1;
							printf("Zoom %g\n", zoomFactor);
							viewPortL = gX - ((double) Event.button.x) / zoomFactor;
							viewPortB = ((double) Event.button.y) / zoomFactor + gY;
							render();
						} while (0);
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
						printf("got keypress Q, quitting\n");
						Running = false;
						break;
					case SDLK_r:
						printf("Resetting position\n");
						zoomFactor = 3;
						viewPortL = 0.0;
						viewPortB = 200.0;
						render();
						break;
					case SDLK_PAGEUP:
						if (currentLayer < layerCount - 1)
							drawLayer(++currentLayer);
						break;
					case SDLK_PAGEDOWN:
						if (currentLayer > 0)
							drawLayer(--currentLayer);
						break;
				}
				break;
			case SDL_KEYUP:
				break;
			default:
				printf("SDL Event %d\n", Event.type);
				break;
		}
		//idle code
		//render code
	}
	free(lineIndex);
	free(layerIndex);
	free(layerSize);
	free(layerHeight);
	SDL_FreeSurface(Surf_Display);
	SDL_Quit();
	return 0;
}
