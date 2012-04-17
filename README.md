GCodeView
=========

Purpose
-------

A *FAST* gcode viewer.

Nothing fancy, nothing else.

Usage
-----

`gcodeview <file.gcode>`

Controls
--------

* __Scroll up/down__
	
	zoom in and out

* __PgUp / PgDn / shift + Scroll__
	
	go up and down layers

* __left click + drag__
	
	pan view

* __r__
	
	reset viewing window

* __q / ESC__
	
	exit

Platforms
---------

* works on linux
* should work on mac, with some massaging of the Makefile (pull requests please!)
* no idea about windows, pull requests welcome

Dependencies
------------

1. SDL
2. OpenGL
3. FTGL
4. fontconfig (linux/mac, windows probably can give me the path to a font some other way)

Mac OS X (Lion)
---------------
Install deps with homebrew
1.  brew install sdl
2.  brew install ftgl
3.  brew install fontconfig

