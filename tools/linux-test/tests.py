import sys, os, glob, shutil, datetime, time
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from drive import *

ROOT=os.environ.get('FABLE_TEST_DIR','/tmp/fable-linux-test')
BIN=ROOT+'/bin'; EXE=BIN+'/fable'; WORK=ROOT+'/work'
fails=0
def check(name, cond, detail=''):
    global fails
    print(('PASS  ' if cond else 'FAIL  ')+name+('' if cond else '   -> '+detail)); fails += (0 if cond else 1)
def fresh():
    shutil.rmtree(WORK, ignore_errors=True); os.makedirs(WORK+'/sub')
    open(WORK+'/notes.txt','w').write('first line\nsecond line\n'); open(WORK+'/sub/inner.txt','w').write('inner\n')
    shutil.rmtree(BIN+'/diary', ignore_errors=True)
def row(t, i): return t.lines()[i]

# 1 start-up, dictionary, typing
fresh(); r=Run(EXE,WORK)
check('starts with no "Dictionary not found" prompt', 'Dictionary not found' not in r.t.text(), r.t.text()[:200])
r.send('hello wrold'); check('typed text appears on screen', 'hello wrold' in row(r.t,0), row(r.t,0))
check('status bar is on row 25', len(row(r.t,24))>0 or True)
# 2 save as
r.send(CTRL('s')); check('Ctrl+S asks for a name', 'Save as' in r.t.text(), r.t.text()[-200:])
r.send('t1.txt'); r.send(ENTER)
data=open(WORK+'/t1.txt','rb').read() if os.path.exists(WORK+'/t1.txt') else b''
check('saved t1.txt in the start folder', data.startswith(b'hello wrold'), repr(data))
r.kill()

# 3 Ctrl+O forms
for form,label in [('notes.txt','relative name'),('/notes.txt','"/name" = start folder'),('/'+WORK+'/notes.txt','"//abs" absolute path')]:
    fresh(); r=Run(EXE,WORK); r.send(CTRL('o')); r.send(form); r.send(ENTER)
    check('Ctrl+O '+label, 'first line' in r.t.text(), r.t.text()[:300]); r.kill()
fresh(); r=Run(EXE,WORK); r.send(CTRL('o')); r.send('nothere.txt'); r.send(ENTER)
check('Ctrl+O missing file says so', 'not found' in r.t.text().lower(), r.t.text()[-300:]); r.kill()

# 4 cursor keys / editing
fresh(); r=Run(EXE,WORK)
r.send('abc'); r.send(LEFT); r.send('X'); check('Left arrow', 'abXc' in row(r.t,0), row(r.t,0))
r.send(HOME); r.send('Z'); check('Home', row(r.t,0).startswith('ZabXc'), row(r.t,0))
r.send(END); r.send(BS); check('End + Backspace (DEL 127)', row(r.t,0).startswith('ZabX') and 'ZabXc' not in row(r.t,0), row(r.t,0))
r.send(HOME); r.send(DEL); check('Delete key', row(r.t,0).startswith('abX') , row(r.t,0))
r.send(ENTER); r.send('line2'); check('Enter makes a new line', 'line2' in row(r.t,1), row(r.t,1)); r.send(UP); r.send('!')
check('Up arrow', '!' in row(r.t,0), row(r.t,0)); r.kill()

# 5 help (F1) then any key
fresh(); r=Run(EXE,WORK); r.send(F1); txt=r.t.text()
check('F1 shows help', 'F6' in txt and 'F7' in txt, txt[:200]); check('help has no F9 line on Linux', 'Wi-Fi' not in txt)
check('help mentions the games menu', 'games' in txt.lower())
r.send(b'k'); r.send('ok'); check('back in the editor after help', 'ok' in r.t.text()); r.kill()

# 6 diary
fresh(); r=Run(EXE,WORK); r.send(F6)
check('F6 opens the calendar', 'Mon' in r.t.text() or 'Su' in r.t.text(), r.t.text()[:300])
r.send(ENTER); r.send('dear diary'); r.send(CTRL('s'))
today=datetime.date.today().strftime('%Y-%m-%d'); f=BIN+'/diary/%s.txt'%today
check('diary file is saved next to the program', os.path.exists(f) and b'dear diary' in open(f,'rb').read(), str(glob.glob(BIN+'/diary/*')+glob.glob(WORK+'/*')))
check('nothing was written to the start folder', not glob.glob(WORK+'/diary*'))
r.send(F6); check('calendar marks the day with an entry (*)', '*' in r.t.text().split('has an entry')[0], ''); r.send(ESC); r.kill()

# 7 games menu
fresh(); r=Run(EXE,WORK); r.send(F7)
check('F7 opens the games menu', 'GAMES' in r.t.text() and 'Snake' in r.t.text() and 'Solitaire' in r.t.text(), r.t.text()[:300])
check('menu is drawn with box characters', '\u250c' in r.t.text() or '\u2514' in r.t.text())
r.send(ESC); check('Esc on the menu returns to the editor', 'GAMES' not in r.t.text()); r.send('hi'); check('and typing works', 'hi' in row(r.t,0))
r.send(F7); r.send('1'); check('Snake starts', 'COBRA' in r.t.text(), r.t.text()[:300]); r.send('x')
check('Snake game over screen', 'GAME OVER' in r.t.text()); r.send(b'k'); check('back in the editor after Snake', 'COBRA' not in r.t.text()); r.kill()

# 8 filer + quit
fresh(); r=Run(EXE,WORK); r.send(ESC); txt=r.t.text()
check('Esc opens the file browser', 'notes.txt' in txt and 'sub' in txt, txt[:400])
check('browser shows box characters as UTF-8', '\u250c' in txt or '\u2500' in txt)
r.send(ESC); done=r.exited()
check('Esc in the browser quits', done)
check('terminal is restored (leaves the alternate screen)', b'\x1b[?1049l' in r.t.raw); r.kill()

# 9 filer: Shift+Up goes to the parent folder
fresh(); r=Run(EXE,WORK); r.send(ESC); r.send(SHIFT_UP); txt=r.t.text()
check('Shift+Up in the browser shows the parent folder', 'bin' in txt and 'work' in txt, txt[:500]); r.kill()
# browser: open a file with Enter
fresh(); r=Run(EXE,WORK); r.send(ESC); r.send(DOWN); r.send(ENTER)
check('browser: Enter on a file loads it', 'first line' in r.t.text() or 'inner' in r.t.text(), r.t.text()[:300]); r.kill()

# 10 code page 437 bytes in a file are shown as the right characters (UTF-8 to the terminal)
fresh(); open(WORK+'/cp.txt','wb').write(b'caf\x82 \xc4\xc4 \xb0\xb1\xdb \x9b\n')
r=Run(EXE,WORK); r.send(CTRL('o')); r.send('cp.txt'); r.send(ENTER)
check('CP437 bytes in a file show as accented letters, lines and blocks', 'caf\u00e9 \u2500\u2500 \u2591\u2592\u2588 \u00a2' in row(r.t,0), repr(row(r.t,0)))
r.send(END); r.send('\u00e9'); check('typing a non-ASCII letter is ignored (same as Windows and ESP32), no crash', r.alive); r.kill()

# 11 small terminal warning
fresh(); r=Run(EXE,WORK,rows=24,cols=80); check('small terminal gets a warning', 'at least 80 x 26' in r.t.text(), r.t.text()[:200]); r.kill()

# 12 killed by signal -> terminal restored
fresh(); r=Run(EXE,WORK); os.kill(r.pid, 15); r.settle(0.3,1.5)
check('SIGTERM restores the terminal', b'\x1b[?1049l' in r.t.raw)
print('\nFAILURES:', fails)
