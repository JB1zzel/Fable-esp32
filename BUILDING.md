# Building Fable

One `src/` folder builds for both targets. Files that only make sense on one
platform are wrapped in `#ifdef` and compile to nothing on the other, so no
file lists need to be kept apart.

| | ESP32 (TTGO VGA32) | Windows console | Linux terminal |
|---|---|---|---|
| Platform macro | `FABLE_ESP32` (set in `platformio.ini`) | `FABLE_WINDOWS` (automatic, from `_WIN32`) | `FABLE_POSIX` (automatic, from `__unix__`) |
| Screen / keyboard | `tui_fabgl.cpp` (FabGL) | `tui_console.c` (Win32 console) | `tui_posix.c` (ANSI escape codes, raw mode) |
| Files | `file_fabgl.cpp` (SD card) | `file_win.c` | `file_posix.c` |
| Entry point | `main.cpp` | `main_win.c` | `main_posix.c` |
| Screen size | 60 rows (`SCRH` 59) | 25 rows (`SCRH` 24); games want 26 | same as Windows: at least 80 x 26 |
| Wi-Fi time setup (F9) | yes | no (the PC clock is used) | no |
| Diary folder | `/diary` on the SD card | `diary\` next to `fable.exe` | `diary/` next to the `fable` program |
| Dictionary | `/dict.smd` on the SD card | `dict.smd` next to `fable.exe` | `dict.smd` next to the `fable` program |
| Quitting | none (runs until power off) | Esc, then Esc again in the file browser | same |

## ESP32

    pio run -t upload

## Windows

Compile every `.c` file in `src/` (the `.cpp` files are ESP32-only), for example
with tcc:

    tcc src\block.c src\calendar.c src\clock.c src\diary.c src\docio.c src\edit.c src\editor.c src\file_win.c src\filer.c src\find.c src\fableGL.c src\games.c src\help.c src\main_win.c src\mine.c src\prompt.c src\reflow.c src\screen.c src\snake.c src\sol.c src\spellui.c src\tinyspell.c src\tui_console.c -luser32 -o fable.exe

or with gcc (MinGW):

    gcc src\*.c -o fable.exe

or from PowerShell, without listing the files (so new `.c` files are picked up):

    tcc (Get-ChildItem src\*.c).FullName -luser32 -o fable.exe

Put `dict.smd` in the same folder as `fable.exe`.

## Linux

    make

or `gcc -O2 -o fable src/*.c` (compile every `.c` file; the `.cpp` files are
ESP32-only). Copy `dict.smd` next to the `fable` program, then run `./fable`
in a terminal at least 80 columns by 26 rows.

- **Paths:** as on the ESP32, `/` means the folder Fable was started in, not
  the root of the disk, so `/notes.txt` is `notes.txt` in the current folder
  and `/../x.txt` is in the parent folder. For other places use `~/x.txt`,
  `../x.txt`, or an absolute path with an extra leading slash:
  `//home/me/x.txt`. The file browser (Esc) shows the same folder and Shift+Up
  or the `..` entry goes up. Names starting with `.` are not listed.
- **Keys:** the same as on the other platforms (F1-F7, Ctrl+S, Ctrl+O...).
  Some terminals keep a few function keys for themselves, for example
  GNOME Terminal uses F1 for its help (turn that off under Preferences >
  Shortcuts, or use another terminal). Ctrl+S and Ctrl+Q reach Fable, they do
  not freeze the terminal.
- **Characters:** like the other platforms, the editor shows bytes 128-255 as
  the old PC (code page 437) characters: box drawing, shading, accented
  letters. The terminal gets them as UTF-8.
- The same code should build on macOS and the BSDs, but only Linux has been
  tried (the program folder is found through `/proc`, so on those systems
  `dict.smd` and `diary/` are looked for in the start folder instead).

## Games

F7 opens the games menu (Snake, Minesweeper, Solitaire); Esc on the menu goes
back to the editor. `tools/games-test/mine_main.c` is a standalone launcher
for trying the games without the editor (it has its own `main()`, so it is
not part of the normal build; the compile line is in its header comment).

## Checking behaviour without hardware

`tools/linux-test/run.sh` builds the Linux version and drives it in a
pseudo-terminal (typing, saving, opening, file browser, diary, games menu),
checking the screen each time.

`tools/equiv-test/` replays random key presses through the editor on a PC and
hashes everything drawn, so two versions of the source can be compared. See
`tools/equiv-test/run.sh`.
