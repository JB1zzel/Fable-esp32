/* tui_fabgl.cpp - FabGL backend for Fable on TTGO VGA32 */

#include <fabgl.h>
#include <Arduino.h>
#include "tui.h"
#include "file_fabgl.h"
#include <string.h>

static fabgl::VGA16Controller VGAController;   /* was fabgl::VGAController */
//static fabgl::VGAController       VGAController;
static fabgl::Terminal            Terminal;
static fabgl::Keyboard            Keyboard;
static fabgl::PS2Controller       PS2Controller;
static fabgl::TerminalController *termctrl = nullptr;

static char linebuf[81];
static int  linepos = 0;

static char *frame_buf = NULL;
static int   frame_len = 0;
static int   frame_cap = 0;


/* Map TUI colour index (0-15) to FabGL Color enum */
static fabgl::Color tui_to_fabgl(int idx)
{
    switch (idx) {
    case 0:  return fabgl::Color::Black;
    case 1:  return fabgl::Color::Blue;
    case 2:  return fabgl::Color::Green;
    case 3:  return fabgl::Color::Cyan;
    case 4:  return fabgl::Color::Red;
    case 5:  return fabgl::Color::Magenta;
    case 6:  return fabgl::Color::Yellow;
    case 7:  return fabgl::Color::White;
    case 8:  return fabgl::Color::BrightBlack;
    case 9:  return fabgl::Color::BrightBlue;
    case 10: return fabgl::Color::BrightGreen;
    case 11: return fabgl::Color::BrightCyan;
    case 12: return fabgl::Color::BrightRed;
    case 13: return fabgl::Color::BrightMagenta;
    case 14: return fabgl::Color::BrightYellow;
    case 15: return fabgl::Color::BrightWhite;
    default: return fabgl::Color::White;
    }
}

extern "C" {

/* ---------- frame buffer API ---------- */

void tui_frame_begin(void)
{
    frame_len = 0;
}

void tui_frame_put(const char *s, int n)
{
    if (frame_len + n > frame_cap) {
        int newcap = frame_cap ? frame_cap * 2 : 4096;
        while (newcap < frame_len + n) newcap *= 2;
        char *nb = (char *)realloc(frame_buf, newcap);
        if (!nb) return;
        frame_buf = nb;
        frame_cap = newcap;
    }
    memcpy(frame_buf + frame_len, s, n);
    frame_len += n;
}

void tui_frame_end(void)
{
    if (frame_len > 0 && frame_buf)
        Terminal.write(frame_buf, frame_len);
    frame_len = 0;
}

/* ---------- core TUI API ---------- */

void tui_init(void)
{
    PS2Controller.begin(GPIO_NUM_33, GPIO_NUM_32, GPIO_NUM_26, GPIO_NUM_27);
    Keyboard.begin(true, true, 0);

    VGAController.begin();
    VGAController.setResolution(VGA_640x480_60Hz);

    Terminal.begin(&VGAController);
    Terminal.loadFont(&fabgl::FONT_8x8);
    Terminal.setBackgroundColor(fabgl::Color::Black);
    Terminal.setForegroundColor(fabgl::Color::White);
    Terminal.clear();
    Terminal.enableCursor(true);

    termctrl = new fabgl::TerminalController(&Terminal);

    Serial.printf("Terminal: %d cols x %d rows\n",
                  Terminal.getColumns(), Terminal.getRows());
    Serial.printf("viewport %d x %d\n",
              VGAController.getViewPortWidth(), VGAController.getViewPortHeight());
}

void tui_restore(void)
{
    Terminal.enableCursor(true);
    VGAController.end();
}

void tui_clear(void)
{
    if (linepos > 0) {
        linebuf[linepos] = '\0';
        Terminal.write(linebuf);
        linepos = 0;
    }
    Terminal.clear();
}

void tui_gotoxy(int x, int y)
{
    if (linepos > 0) {
        linebuf[linepos] = '\0';
        Terminal.write(linebuf);
        linepos = 0;
    }
    if (termctrl) termctrl->setCursorPos(x, y);
}

void tui_color(int fg, int bg)
{
    if (linepos > 0) {
        linebuf[linepos] = '\0';
        Terminal.write(linebuf);
        linepos = 0;
    }
    if (fg < 0 || fg > 15) fg = 7;
    if (bg < 0 || bg > 15) bg = 0;
    Terminal.setForegroundColor(tui_to_fabgl(fg));
    Terminal.setBackgroundColor(tui_to_fabgl(bg));
}

void tui_putc(int c)
{
    if (linepos >= 80) {
        linebuf[linepos] = '\0';
        Terminal.write(linebuf);
        linepos = 0;
    }
    linebuf[linepos++] = (char)c;
}

void tui_puts(const char *s)
{
    while (*s) tui_putc(*s++);
}

void tui_cursor(int visible, int block)
{
    Terminal.enableCursor(visible ? true : false);
}

/* ---------- keyboard ---------- */

static int pending_code = -1;          /* 2nd half of an extended key */
static int ready = -1, ready_second = -1;   /* decoded, waiting for getch */

static int vk_to_scancode(int vk)
{
    switch (vk) {
    case fabgl::VK_UP:       return 72;
    case fabgl::VK_DOWN:     return 80;
    case fabgl::VK_LEFT:     return 75;
    case fabgl::VK_RIGHT:    return 77;
    case fabgl::VK_HOME:     return 71;
    case fabgl::VK_END:      return 79;
    case fabgl::VK_PAGEUP:   return 73;
    case fabgl::VK_PAGEDOWN: return 81;
    case fabgl::VK_DELETE:   return 83;
    case fabgl::VK_F1:       return 59;
    case fabgl::VK_F2:       return 60;
    case fabgl::VK_F3:       return 61;
    case fabgl::VK_F4:       return 62;
    case fabgl::VK_F5:       return 63;
    default:                 return -1;
    }
}

static int decode(const fabgl::VirtualKeyItem &it, int *second)
{
    *second = -1;
    if (!it.down) return -1;

    int code = vk_to_scancode(it.vk);
    if (code >= 0) { *second = code; return 0; }

    if (it.vk == fabgl::VK_RETURN)    return 13;
    if (it.vk == fabgl::VK_BACKSPACE) return 8;
    if (it.vk == fabgl::VK_ESCAPE)    return 27;
    if (it.vk == fabgl::VK_TAB)       return 9;

    if (it.CTRL || Keyboard.isVKDown(fabgl::VK_LCTRL) || Keyboard.isVKDown(fabgl::VK_RCTRL)) {
    int a = it.ASCII;
    if (a >= 1   && a <= 26)  return a;                  /* layout already gave control code */
    if (a >= 'a' && a <= 'z') return a - 'a' + 1;
    if (a >= 'A' && a <= 'Z') return a - 'A' + 1;
    if (it.vk >= fabgl::VK_a && it.vk <= fabgl::VK_z) return 1 + (it.vk - fabgl::VK_a);
    if (it.vk >= fabgl::VK_A && it.vk <= fabgl::VK_Z) return 1 + (it.vk - fabgl::VK_A);
    }

    if (it.ASCII >= 32 && it.ASCII < 127) return it.ASCII;
    return -1;
}

int tui_key_pending(void)
{
    if (pending_code >= 0 || ready >= 0) return 1;
    fabgl::VirtualKeyItem it;
    while (Keyboard.virtualKeyAvailable() && Keyboard.getNextVirtualKey(&it, 0)) {
        int r = decode(it, &ready_second);
        if (r >= 0) { ready = r; return 1; }
    }
    return 0;
}

int tui_getch(void)
{
    int r;
    if (linepos > 0) { linebuf[linepos] = '\0'; Terminal.write(linebuf); linepos = 0; }
    if (pending_code >= 0) { r = pending_code; pending_code = -1; return r; }
    if (ready >= 0) {
        r = ready; ready = -1;
        pending_code = ready_second; ready_second = -1;
        return r;
    }
    for (;;) {
        fabgl::VirtualKeyItem it;
        int second;
        if (Keyboard.getNextVirtualKey(&it)) {
            r = decode(it, &second);
            if (r >= 0) { pending_code = second; return r; }
        } else {
            delay(1);
        }
    }
}

int tui_shift_down(void)
{
    return (Keyboard.isVKDown(fabgl::VK_LSHIFT) ||
            Keyboard.isVKDown(fabgl::VK_RSHIFT)) ? 1 : 0;
}

int tui_list_dir(const char *path, char names[][32], int max)
{
    return fab_list_dir(path, names, max);
}

void tui_exe_dir(char *out, int len)
{
    if (len > 0) out[0] = '\0';
}

}   /* extern "C" */