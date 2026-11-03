# SDL for Android (Java side)

The Java classes of SDL 3.4.16 (`android-project/app/src/main/java/org/libsdl/app`), zlib license
(`LICENSE.txt`). The activity of an Android game extends `SDLActivity`; the native side of SDL is
linked into `libdevex-engine.so` by vcpkg. Both sides must be the same version: update these files
whenever the `sdl3` port of `cmake/vcpkg-ports` changes version.
