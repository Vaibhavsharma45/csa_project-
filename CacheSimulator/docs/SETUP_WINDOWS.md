# Windows Setup Guide (MinGW-w64 + raylib + VS Code)

Every team member does this once. Total time: about 15 minutes.

## 1. Install a 64-bit MinGW-w64 GCC

You need `gcc` and `mingw32-make` on your `PATH`.

**Option A - WinLibs (zip, no installer, no admin rights):**
1. Download the *x86_64* zip from <https://winlibs.com> (GCC + MinGW-w64, "Win64").
2. Extract it so that you get `C:\mingw64\bin\gcc.exe`.
3. Add `C:\mingw64\bin` to your user `PATH` (Start menu > "Edit environment variables for your account" > Path > New).

**Option B - MSYS2:** follow the "Using GCC with MinGW" page of the VS Code docs
(`pacman -S --needed base-devel mingw-w64-ucrt-x86_64-toolchain`), then add
`C:\msys64\ucrt64\bin` to `PATH`.

Open a **new** terminal and verify:

```
gcc --version
mingw32-make --version
```

## 2. Install raylib 5.5

1. Download `raylib-5.5_win64_mingw-w64.zip` from
   <https://github.com/raysan5/raylib/releases/tag/5.5>.
2. Extract it to `C:\raylib-5.5_win64_mingw-w64`. The folder must contain
   `include\raylib.h` and `lib\libraylib.a` directly.

Any other location works, as long as the path has no spaces.

## 3. Tell the project where raylib is

Pick **one** method. Use forward slashes (`C:/...`).

| Method | How |
|---|---|
| Environment variable (recommended: also feeds VS Code IntelliSense) | `setx RAYLIB_DIR "C:/raylib-5.5_win64_mingw-w64"`, then close and reopen the terminal **and VS Code**. |
| Local file | Copy `config.mk.example` to `config.mk` and edit the path. `config.mk` is git-ignored. |
| Command line | `mingw32-make RAYLIB_DIR=C:/your/path` |

If your raylib has a different layout (for example the official Windows installer
keeps `raylib.h` and `libraylib.a` together in `...\raylib\src`), set
`RAYLIB_INC` and `RAYLIB_LIB` instead of `RAYLIB_DIR`.

## 4. Build, test and run

```
cd CacheSimulator
mingw32-make help      # shows the detected configuration
mingw32-make test      # automated tests (no raylib needed): expect "ALL TESTS PASSED"
mingw32-make           # builds build\CacheSimulator.exe
mingw32-make run       # builds and launches
mingw32-make clean
```

Use `make` instead of `mingw32-make` if your toolchain (for example w64devkit) calls it that.

Expected result: a 1280x720 dark dashboard titled "Cache Memory Simulator" with the
configuration panel, an empty cache table, statistics cards and the memory access panel.
Press **Next Access** seven times: you should see MISS, MISS, HIT, HIT, HIT, MISS, HIT
and 4 hits / 3 misses / 57.14 % / 42.86 %.

The text uses Segoe UI if `C:/Windows/Fonts/segoeui.ttf` exists (or `assets/font.ttf`),
otherwise raylib's built-in font.

## 5. VS Code

1. Install the **C/C++** extension (VS Code offers it automatically).
2. File > Open Folder > `CacheSimulator`.
3. `Ctrl+Shift+B` builds, `F5` builds and starts the debugger (needs `gdb`, which
   both toolchain options include).

## Troubleshooting

| Message | Cause and fix |
|---|---|
| `'mingw32-make' is not recognized` | MinGW `bin` folder is not on `PATH`, or the terminal was opened before you changed it. |
| `raylib.h not found in "/include"` | `RAYLIB_DIR` is not set (step 3). |
| `cannot find -lraylib` | `RAYLIB_LIB` does not contain `libraylib.a`. |
| Window does not open / GLFW or OpenGL 3.3 error | Update the graphics driver. Remote Desktop and some virtual machines lack OpenGL 3.3. |
| VS Code shows red squiggles on `#include "raylib.h"` | `RAYLIB_DIR` env var not set, or VS Code was not restarted after setting it. |
