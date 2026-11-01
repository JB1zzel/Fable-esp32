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

| Ctrl+O | Open file |

| Ctrl+N | New / open document (file browser) |

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


