/*
Copyright © 2011-2012 Clint Bellanger
Copyright © 2013 Henrik Andersson

This file is part of FLARE.

FLARE is free software: you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.

FLARE is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
FLARE.  If not, see http://www.gnu.org/licenses/
*/

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <algorithm>
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_opengl.h>

using namespace std;

#include "Settings.h"
#include "GameSwitcher.h"
#include "SharedResources.h"

GameSwitcher *gswitch;
SDL_Surface *titlebar_icon;

/**
 * Game initialization.
 */
static void init() {

	setPaths();

	// SDL Inits
	if ( SDL_Init (SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0 ) {
		fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
		exit(1);
	}

	// Shared Resources set-up

	mods = new ModManager();

	if (!loadSettings()) {
		fprintf(stderr, "%s",
				("Could not load settings file: ‘" + PATH_CONF + FILE_SETTINGS + "’.\n").c_str());
		exit(1);
	}

	msg = new MessageEngine();
	font = new FontEngine();
	anim = new AnimationManager();
	comb = new CombatText();
	imag = new ImageManager();
	inpt = new InputState();

	// Load tileset options (must be after ModManager is initialized)
	loadTilesetSettings();

	// Load miscellaneous settings
	loadMiscSettings();

	// Add Window Titlebar Icon
	titlebar_icon = IMG_Load(mods->locate("images/logo/icon.png").c_str());
	SDL_WM_SetIcon(titlebar_icon, NULL);

	// Create window
	Uint32 flags = SDL_OPENGL;

	if (FULLSCREEN) flags = flags | SDL_FULLSCREEN;
	if (DOUBLEBUF) flags = flags | SDL_DOUBLEBUF;
	if (HWSURFACE)
		flags = flags | SDL_HWSURFACE | SDL_HWACCEL;
	else
		flags = flags | SDL_SWSURFACE;

	SDL_Surface *_screen = SDL_SetVideoMode (VIEW_W, VIEW_H, 0, flags);

Uint32 rmask, gmask, bmask, amask;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    rmask = 0xff000000;
    gmask = 0x00ff0000;
    bmask = 0x0000ff00;
    amask = 0x000000ff;
#else
    rmask = 0x000000ff;
    gmask = 0x0000ff00;
    bmask = 0x00ff0000;
    amask = 0xff000000;
#endif
screen = SDL_CreateRGBSurface(SDL_SWSURFACE, _screen->w, _screen->h, 24, rmask, gmask, bmask, amask);

	if (screen == NULL) {

		fprintf (stderr, "Error during SDL_SetVideoMode: %s\n", SDL_GetError());
		SDL_Quit();
		exit(1);
	}
	
	// Set Gamma
	if (CHANGE_GAMMA)
		SDL_SetGamma(GAMMA,GAMMA,GAMMA);

	if (AUDIO && Mix_OpenAudio(22050, AUDIO_S16SYS, 2, 1024)) {
		fprintf (stderr, "Error during Mix_OpenAudio: %s\n", SDL_GetError());
		AUDIO = false;
	}

	snd = new SoundManager();

	// initialize Joysticks
	if(SDL_NumJoysticks() == 1) {
		printf("1 joystick was found:\n");
	}
	else if(SDL_NumJoysticks() > 1) {
		printf("%d joysticks were found:\n", SDL_NumJoysticks());
	}
	else {
		printf("No joysticks were found.\n");
	}
	for(int i = 0; i < SDL_NumJoysticks(); i++)
	{
		printf("  Joy %d) %s\n", i, SDL_JoystickName(i));
	}
	if ((ENABLE_JOYSTICK) && (SDL_NumJoysticks() > 0)) joy = SDL_JoystickOpen(JOYSTICK_DEVICE);
	printf("Using joystick #%d.\n", JOYSTICK_DEVICE);

	// Set sound effects volume from settings file
	if (AUDIO)
		Mix_Volume(-1, SOUND_VOLUME);

	// Window title
	const char* title = msg->get(WINDOW_TITLE).c_str();
	SDL_WM_SetCaption(title, title);


	gswitch = new GameSwitcher();
}

static void mainLoop (bool debug_event) {

	bool done = false;
	int max_fps = MAX_FRAMES_PER_SEC;
	int delay = 1000/max_fps;
	int prevTicks = SDL_GetTicks();
	int nowTicks;

// setup opengl viewport
	//SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	glEnable(GL_TEXTURE_2D);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glViewport(0,0,VIEW_W, VIEW_H);
	glClear(GL_COLOR_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0f, VIEW_W, VIEW_H, 0.0f, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();


	// create a gl texture
	GLuint t, q;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	q = glGenLists(1);
	glNewList(q, GL_COMPILE);
	glBegin(GL_QUADS);
	glTexCoord2i(0,0); glVertex3f(0, 0, 0);
	glTexCoord2i(1,0); glVertex3f(VIEW_W, 0, 0);
	glTexCoord2i(1,1); glVertex3f(VIEW_W, VIEW_H, 0);
	glTexCoord2i(0,1); glVertex3f(0, VIEW_H, 0);
	glEnd();
	glEndList();

	while ( !done ) {

		SDL_PumpEvents();
		inpt->handle(debug_event);
		gswitch->logic();

		// black out
		SDL_FillRect(screen, NULL, 0);

		gswitch->render();

		// Engine done means the user escapes the main game menu.
		// Input done means the user closes the window.
		done = gswitch->done || inpt->done;

		
		nowTicks = SDL_GetTicks();
		if (nowTicks - prevTicks < delay) SDL_Delay(delay - (nowTicks - prevTicks));
		gswitch->showFPS(1000 / (SDL_GetTicks() - prevTicks));
		prevTicks = SDL_GetTicks();

		// update texture from rendered offscreen surface
		glTexImage2D(GL_TEXTURE_2D, 0, 3, 
			     screen->w, screen->h, 0,
			     GL_RGB, 
			     GL_UNSIGNED_BYTE, screen->pixels);

		// render textured quad	
		glCallList(q);

		SDL_GL_SwapBuffers();

		//SDL_Flip(screen);
	}
	glDeleteLists(q, 1);
	glDeleteTextures(1, &t);
}

static void cleanup() {
	delete gswitch;

	delete anim;
	delete comb;
	delete font;
	delete imag;
	delete inpt;
	delete mods;
	delete msg;
	delete snd;

	SDL_FreeSurface(screen);
	SDL_FreeSurface(titlebar_icon);

	Mix_CloseAudio();
	SDL_Quit();
}

string parseArg(const string &arg)
{
	string result = "";

	// arguments must start with '--'
	if (arg.length() > 2 && arg[0] == '-' && arg[1] == '-') {
		for (unsigned i = 2; i < arg.length(); ++i) {
			if (arg[i] == '=') break;
			result += arg[i];
		}
	}

	return result;
}

string parseArgValue(const string &arg)
{
	string result = "";
	bool found_equals = false;

	for (unsigned i = 0; i < arg.length(); ++i) {
		if (found_equals) {
			result += arg[i];
		}
		if (arg[i] == '=') found_equals = true;
	}

	return result;
}

int main(int argc, char *argv[])
{
	bool debug_event = false;

	for (int i = 1 ; i < argc; i++) {
		string arg = string(argv[i]);
		if (parseArg(arg) == "debug_event") {
			debug_event = true;
		} else if (parseArg(arg) == "game_data") {
			USER_PATH_DATA = parseArgValue(arg);
		}
	}

	srand((unsigned int)time(NULL));

	init();
	mainLoop(debug_event);
	cleanup();

	return 0;
}
