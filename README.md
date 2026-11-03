\# Fable (TTGO VGA32)



A full-screen text editor and spell checker for the \*\*TTGO VGA32\*\*

(ESP32 + VGA output + PS/2 keyboard + SD card), built on

\[FabGL](https://github.com/fabgl/fabgl).



Fable runs entirely on the ESP32 with no PC attached. A VGA monitor

displays an 80×60 character terminal, a PS/2 keyboard provides input,

and documents are loaded from and saved to a FAT-formatted microSD card.



> \*\*Windows / PC version:\*\* see \[fable](https://github.com/JB1zzel/fable)



\---



\## Features



\- 80×60 character terminal at 640×480 (8×8 font)

\- Full-screen editing with cursor, horizontal scroll, and status bar

\- Load / save documents on SD card

\- Dictionary-based spell checker

&#x20; - \*\*F4\*\* — interactive spell fix (in-process, no save)

&#x20; - \*\*F5\*\* — toggle inline misspelling marks (red)

\- Find and find-and-replace

\- Block mark / cut / paste with clipboard

\- Paragraph reflow (F2)

\- File browser (Ctrl+N / Esc) for new and existing documents

\- Word count and modification indicator in the status bar



\---



\## Hardware



| Item | Notes |

|---|---|

| TTGO VGA32 v1.4 | ESP32 with VGA DAC and PS/2 connectors |

| VGA monitor | 640×480 capable |

| PS/2 keyboard | Standard, plugged into the board's PS/2 port |

| microSD card | FAT32, with `dict.smd` at the root for spell check |



SD wiring on the TTGO VGA32: `SPI.begin(14, 2, 12, 13)` and `SD.begin(13)`.



\---



\## Keys



| Key | Action |

|---|---|

| Arrows | Move cursor |

| Shift+Left/Right | Word left / right |

| Shift+Up/Down | Previous / next paragraph |

| Home / End | Line start / end |

| PgUp / PgDn | Page up / down |

| Enter | Split line |

| Backspace | Delete char / merge up at column 0 |

| Delete | Delete char / merge down at end of line |

| F1 | Help screen |

| F2 | Reflow paragraph |

| F3 / Ctrl+S | Save (prompts for name if untitled) |

| F4 | Interactive spell check |

| F5 | Toggle spell marks |

| F6 | Diary calendar (days with an entry are marked `*`) |

| F7 | Snake game |

| F9 | Wi-Fi setup |

| Ctrl+O | Open file |

| Ctrl+N | New / open document (file browser: N new file, D delete, R rename) |

| Ctrl+B | Mark block; press again to end mark and copy to clipboard |

| Ctrl+X | Cut marked block (also copies to clipboard) |

| Ctrl+P | Paste clipboard at cursor |

| Ctrl+F | Find |

| Ctrl+R | Find and replace all |

| Esc | Cancel mark / prompt, else open file browser |



\---


\## Dictionary



'dict.smd' is derived from SCOWL (Spell Checker Oriented Word Lists)

by Kevin Atkinson, used under its MIT-like license.

See http://wordlist.aspell.net/ for details.

\---


\## Build



This project uses \[PlatformIO](https://platformio.org/).



```bash

pio run -t upload



### Post-clone step

After the first `pio run`, edit:

    .pio/libdeps/ttgo-vga32/FabGL/src/fabglconf.h

Change `FABGLIB_MINFREELARGESTBLOCK` from `40000` to `20000`.
Without this the VGA viewport allocates at 640×444 or smaller
and the editor loses rows. Lost on `pio run -t clean`.



## Source layout (`src/`)

| File | What it does |
|---|---|
| `main.cpp` | Arduino `setup()`: Wi-Fi + NTP time at boot, SD card, starts `fable_main` |
| `fableGL.c` | `fable_main()`: start-up and the key loop |
| `editor.h` / `editor.c` | The document being edited (line buffer, cursor, file name) shared by the files below |
| `edit.c` | Cursor movement and text edits |
| `block.c` | Mark, cut, copy, paste |
| `find.c` | Find and replace all |
| `reflow.c` | Paragraph reflow (F2) |
| `docio.c` | Load, save, Save As, new document |
| `spellui.c` | Spell marks (F5) and interactive spellcheck (F4) |
| `screen.c` | Status bar, prompt bar, drawing the text |
| `filer.c` | File browser (Ctrl+N) |
| `calendar.c` / `diary.c` | Diary calendar (F6), one file per day |
| `clock.c` | Time string and the Wi-Fi setup page (F9) |
| `prompt.c`, `help.c`, `snake.c` | Text prompt, help screen, snake game (F7) |
| `tui_fabgl.cpp`, `file_fabgl.cpp` | FabGL screen/keyboard and SD card drivers |
| `tinyspell.c` | Dictionary spell checker |

`tools/equiv-test/` holds a host-side regression test: it replays random key
presses through the editor and hashes the screen output, so two versions of
the source can be compared without an ESP32 (see `run.sh`).
