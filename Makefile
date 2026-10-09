NAME     := highfleet_elevation
VERSION  ?= 1.0.0
# cross compiler on Linux; on Windows (MSYS2 mingw64 shell) run: make WINCXX=g++
WINCXX   ?= x86_64-w64-mingw32-g++
CXXFLAGS ?= -O2 -std=c++17 -Wall
LDLIBS   := -lgdi32 -luser32

.PHONY: all dist clean

all: build/$(NAME).dll

build/$(NAME).dll: elevation.cpp
	mkdir -p build
	$(WINCXX) $(CXXFLAGS) -shared -static -static-libgcc -static-libstdc++ -o $@ $< $(LDLIBS)

dist: build/$(NAME).dll
	rm -rf dist/stage
	mkdir -p dist/stage/Modloader/mods dist/stage/Modloader/config
	cp build/$(NAME).dll dist/stage/Modloader/mods/
	cp elevation.ini.example dist/stage/Modloader/config/
	cp README.md LICENSE CHANGELOG.md dist/stage/
	cd dist/stage && python3 -m zipfile -c ../$(NAME)-v$(VERSION).zip Modloader README.md LICENSE CHANGELOG.md
	sha256sum dist/$(NAME)-v$(VERSION).zip

clean:
	rm -rf build dist
