# Run a prompt fixture on a pseudo-terminal, type keys once the prompt is shown,
# and report what each prompt resolved to and whether the terminal settings were
# restored afterwards.
import fcntl, json, os, select, subprocess, sys, termios, time

runtime, fixture, mode = sys.argv[1], sys.argv[2], sys.argv[3]
keys = [k.encode('latin1') for k in json.loads(sys.argv[4])]

if mode == 'no-tty':
    p = subprocess.run([runtime, fixture, '1'], stdin=subprocess.DEVNULL, capture_output=True, start_new_session=True, timeout=30)
    out = p.stdout.decode()
    print(json.dumps({'results': [json.loads(l[7:]) for l in out.splitlines() if l.startswith('RESULT ')], 'restored': True}))
    sys.exit(0)

master, slave = os.openpty()
before = termios.tcgetattr(slave)

def setup():
    os.setsid()
    fcntl.ioctl(0, termios.TIOCSCTTY, 0)

child = subprocess.Popen([runtime, fixture, str(len(keys))], stdin=slave, stdout=slave, stderr=slave, preexec_fn=setup, close_fds=True)

out = b''

def drain(until=None, timeout=10.0):
    global out
    end = time.time() + timeout
    while time.time() < end:
        if until is not None and out.count(b'secret: ') >= until: return
        r, _, _ = select.select([master], [], [], 0.05)
        if r:
            try: data = os.read(master, 65536)
            except OSError: return
            if not data: return
            out += data
        elif until is None and child.poll() is not None: return

for i, k in enumerate(keys):
    drain(until=i + 1)
    time.sleep(0.05)
    for j in range(0, len(k), 256): os.write(master, k[j:j + 256]); time.sleep(0.01)

# Keep reading while waiting, or a child writing a long line blocks on a full
# terminal buffer and never exits
drain(timeout=30)

exited = child.poll() is not None

if not exited:
    child.kill()

child.wait()
drain(timeout=0.5)

after = termios.tcgetattr(slave)
text = out.decode(errors='replace').replace('\r', '')
print(json.dumps({
    'results': [json.loads(l[7:]) for l in text.splitlines() if l.startswith('RESULT ')],
    'restored': before == after,
    'exited': exited,
    'status': child.returncode,
    'output': text
}))
