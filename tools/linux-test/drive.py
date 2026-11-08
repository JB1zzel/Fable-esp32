import os, pty, sys, time, select, fcntl, termios, struct, codecs, signal, glob, shutil, datetime

class Term:
    def __init__(self, rows, cols):
        self.rows, self.cols = rows, cols
        self.g = [[' ']*cols for _ in range(rows)]
        self.r = self.c = 0
        self.dec = codecs.getincrementaldecoder('utf-8')('replace')
        self.state = 0; self.buf = ''
        self.raw = b''
    def scroll(self):
        self.g.pop(0); self.g.append([' ']*self.cols)
    def put(self, ch):
        if self.c >= self.cols: self.c = 0; self.lf()
        self.g[self.r][self.c] = ch; self.c += 1
    def lf(self):
        if self.r == self.rows-1: self.scroll()
        else: self.r += 1
    def csi(self, params, final, inter):
        p = [int(x) if x.isdigit() else 0 for x in params.replace('?','').split(';')] if params.replace('?','') else []
        n = p[0] if p and p[0] else 1
        if final in 'Hf':
            r = (p[0] if len(p)>0 and p[0] else 1)-1; c = (p[1] if len(p)>1 and p[1] else 1)-1
            self.r = min(max(r,0),self.rows-1); self.c = min(max(c,0),self.cols-1)
        elif final == 'A': self.r = max(self.r-n,0)
        elif final == 'B': self.r = min(self.r+n,self.rows-1)
        elif final == 'C': self.c = min(self.c+n,self.cols-1)
        elif final == 'D': self.c = max(self.c-n,0)
        elif final == 'K':
            mode = p[0] if p else 0
            if mode == 0: self.g[self.r][self.c:] = [' ']*(self.cols-self.c)
            elif mode == 2: self.g[self.r] = [' ']*self.cols
        elif final == 'J':
            mode = p[0] if p else 0
            if mode == 2: self.g = [[' ']*self.cols for _ in range(self.rows)]
            elif mode == 0:
                self.g[self.r][self.c:] = [' ']*(self.cols-self.c)
                for i in range(self.r+1,self.rows): self.g[i] = [' ']*self.cols
    def feed(self, data):
        self.raw += data
        for ch in self.dec.decode(data):
            if self.state == 0:
                if ch == '\x1b': self.state = 1
                elif ch == '\r': self.c = 0
                elif ch == '\n': self.lf()
                elif ch == '\b': self.c = max(self.c-1,0)
                elif ch >= ' ': self.put(ch)
            elif self.state == 1:
                if ch == '[': self.state = 2; self.buf = ''
                else: self.state = 0
            elif self.state == 2:
                if '0' <= ch <= '?' : self.buf += ch
                elif ch == ' ': self.buf += ch
                else:
                    self.state = 0
                    inter = ' ' if self.buf.endswith(' ') else ''
                    self.csi(self.buf.strip(), ch, inter)
    def lines(self): return [''.join(r).rstrip() for r in self.g]
    def text(self): return '\n'.join(self.lines())

class Run:
    def __init__(self, exe, cwd, rows=30, cols=100):
        self.t = Term(rows, cols)
        pid, fd = pty.fork()
        if pid == 0:
            os.chdir(cwd)
            fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
            os.environ['TERM'] = 'xterm'
            os.execv(exe, [exe])
        self.pid, self.fd = pid, fd
        self.alive = True
        self.settle()
    def settle(self, quiet=0.25, maxwait=3.0):
        end = time.time() + maxwait; last = time.time()
        while time.time() < end:
            r,_,_ = select.select([self.fd],[],[],0.05)
            if r:
                try: d = os.read(self.fd, 65536)
                except OSError: d = b''
                if not d: self.alive = False; break
                self.t.feed(d); last = time.time()
            elif time.time() - last > quiet: break
    def send(self, data):
        if isinstance(data, str): data = data.encode('utf-8')
        os.write(self.fd, data); self.settle()
    def exited(self, timeout=3.0):
        end = time.time() + timeout
        while time.time() < end:
            self.settle(0.1, 0.3)
            try:
                p, st = os.waitpid(self.pid, os.WNOHANG)
            except ChildProcessError:
                return True
            if p != 0: return True
        return False
    def kill(self):
        try: os.kill(self.pid, signal.SIGKILL); os.waitpid(self.pid, 0)
        except Exception: pass

ENTER=b'\r'; ESC=b'\x1b'; UP=b'\x1b[A'; DOWN=b'\x1b[B'; RIGHT=b'\x1b[C'; LEFT=b'\x1b[D'
HOME=b'\x1b[H'; END=b'\x1b[F'; DEL=b'\x1b[3~'; PGUP=b'\x1b[5~'; BS=b'\x7f'
F1=b'\x1bOP'; F2=b'\x1bOQ'; F3=b'\x1bOR'; F4=b'\x1bOS'; F5=b'\x1b[15~'; F6=b'\x1b[17~'; F7=b'\x1b[18~'; F9=b'\x1b[20~'
SHIFT_UP=b'\x1b[1;2A'
CTRL=lambda ch: bytes([ord(ch.upper())-64])
