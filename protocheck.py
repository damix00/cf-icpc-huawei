"""Adversarial checks on sol.exe's stdin/stdout protocol adapter.

The adapter is never exercised by sim.cpp (it is compiled out under LOCAL_SIM), and a bug there
already cost every point once.  These cases attack the parts a recorded replay cannot reach:
byte-level fragmentation, CRLF, odd whitespace, empty frames, every TDN shape, EOF without END.

  py protocheck.py [path-to-sol.exe]
"""
import subprocess, sys, threading, queue, time

EXE = sys.argv[1] if len(sys.argv) > 1 else "./sol.exe"
TIMEOUT = 8.0
fails = []


def start(exe=EXE):
    p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0)
    q = queue.Queue()

    def reader():
        buf = b""
        try:
            while True:
                ch = p.stdout.read(1)
                if not ch:
                    break
                buf += ch
                if ch == b"\n":
                    q.put(buf.decode().strip())
                    buf = b""
        except Exception:
            pass
        q.put(None)
    threading.Thread(target=reader, daemon=True).start()
    return p, q


def getline(q, what):
    try:
        v = q.get(timeout=TIMEOUT)
    except queue.Empty:
        raise AssertionError("no %s within %gs (deadlock)" % (what, TIMEOUT))
    if v is None:
        raise AssertionError("process closed stdout while waiting for %s" % what)
    return v


def resp(q):
    n = int(getline(q, "count"))
    return [getline(q, "assignment") for _ in range(n)]


def check(name, fn):
    try:
        fn()
        print("ok   %s" % name)
    except Exception as e:
        print("FAIL %s: %s" % (name, e))
        fails.append(name)


# Startup block of worked Example 1 plus its first frame.
HDR = ("1 1.000000000 2.000000000 1.000000000 125000 4\n"
       "30.000000000 15.000000000 0.062500000 0.022222222 0.000000000 0.500000000 0.500000000\n"
       "2\n"
       "1 3.000000000 10.000000000 2.000000000 1.000000000 4.000000000 1.000000000\n"
       "4 3.000000000 10.000000000 2.000000000 1.000000000 4.000000000 1.000000000\n")
F1 = "0.000000000\n1\nARR 0 4\n"


def case_dribble():
    """One byte at a time: the refill must return short reads, never wait for a full buffer."""
    p, q = start()
    for b in (HDR + F1).encode():
        p.stdin.write(bytes([b]))
        p.stdin.flush()
    assert resp(q) == ["E P PRE 0 0"], "unexpected first response"
    p.kill()


def case_crlf():
    p, q = start()
    p.stdin.write((HDR + F1).replace("\n", "\r\n").encode())
    p.stdin.flush()
    assert resp(q) == ["E P PRE 0 0"], "CRLF stream mishandled"
    p.kill()


def case_odd_whitespace():
    """Extra blank lines and runs of spaces between tokens."""
    s = (HDR + F1).replace(" ", "   ").replace("\n", "\n\n")
    p, q = start()
    p.stdin.write(s.encode())
    p.stdin.flush()
    assert resp(q) == ["E P PRE 0 0"], "extra whitespace mishandled"
    p.kill()


def case_empty_frame():
    """A frame with zero events is legal and must still get a response."""
    p, q = start()
    p.stdin.write((HDR + F1).encode()); p.stdin.flush()
    resp(q)
    p.stdin.write("1.000000000\n0\n".encode()); p.stdin.flush()
    r = resp(q)
    assert r == [], "empty frame should draw an empty response, got %r" % (r,)
    p.stdin.write("2.000000000\n1\nTDN E P PRE 0 0 3.000000000\n".encode()); p.stdin.flush()
    resp(q)   # must not desync
    p.kill()


def case_all_tdn_shapes():
    """Walk one request through every task shape; a token miscount desyncs and then hangs."""
    p, q = start()
    steps = [
        (F1, ["E P PRE 0 0"]),
        ("4.000000000\n1\nTDN E P PRE 0 0 3.000000000\n", []),
        ("10.000000000\n1\nXDN UP 0 500000 PRE 1 0\n", ["C0 P PROC 0 4 0 0"]),
        ("21.000000000\n1\nTDN C0 P PROC 0 4 0 0 10.000000000\n", []),
        ("27.000000000\n1\nXDN DOWN 0 500000 PRE 1 0\n", ["E P POST 0 0"]),
        ("30.000000000\n1\nTDN E P POST 0 0 2.000000000\n", ["E D PRE -1 1 0"]),
        ("32.000000000\n1\nTDN E D PRE -1 1 0 1.000000000\n", []),
        ("35.000000000\n1\nXDN UP 0 125000 DEC 1 0\n", ["C0 D PROC 0 1 0"]),
        ("40.000000000\n1\nTDN C0 D PROC 0 1 0 4.000000000\n", []),
        ("43.000000000\n1\nXDN DOWN 0 125000 DEC 1 0\n", ["E D POST -1 1 0"]),
        ("45.000000000\n2\nTDN E D POST -1 1 0 1.000000000\nFIN 0\n", []),
    ]
    p.stdin.write(HDR.encode())
    for i, (frame, want) in enumerate(steps):
        p.stdin.write(frame.encode()); p.stdin.flush()
        got = resp(q)
        assert got == want, "frame %d: got %r want %r" % (i, got, want)
    p.stdin.write("END\n".encode()); p.stdin.flush()
    assert p.wait(timeout=TIMEOUT) == 0, "nonzero exit after END"


def case_eof_no_end():
    """Statement: on EOF exit immediately with code 0 -- do not block, do not crash."""
    p, q = start()
    p.stdin.write((HDR + F1).encode()); p.stdin.flush()
    resp(q)
    p.stdin.close()
    rc = p.wait(timeout=TIMEOUT)
    assert rc == 0, "exit code %d on EOF (must be 0)" % rc


def case_eof_midheader():
    """EOF part-way through the startup block must also exit 0 rather than hang."""
    p, q = start()
    p.stdin.write(HDR[:40].encode()); p.stdin.flush()
    p.stdin.close()
    rc = p.wait(timeout=TIMEOUT)
    assert rc == 0, "exit code %d on truncated header" % rc


def case_big_group():
    """2000 arrivals in one frame, then a wide D PRE echo: exercises the id buffers."""
    p, q = start()
    hdr = ("8 1.000000000 0.500000000 10.000000000 1000 1\n"
           "1000.0 1000.0 1.0 0.0 0.0 1.0 0.0\n"
           "2\n"
           "1 1.0 1.0 1.0 1.0 1.0 1.0\n"
           "4096 1.0 1.0 1.0 1.0 1.0 1.0\n")
    ev = "".join("ARR %d %d\n" % (i, 1 + i % 4096) for i in range(2000))
    p.stdin.write((hdr + "0.000000000\n2000\n" + ev).encode()); p.stdin.flush()
    r = resp(q)
    assert len(r) >= 1 and r[0].startswith("E P PRE"), "unexpected: %r" % (r[:2],)
    p.kill()


for name, fn in [
    ("byte-at-a-time stdin", case_dribble),
    ("CRLF line endings", case_crlf),
    ("extra whitespace/blank lines", case_odd_whitespace),
    ("frame with zero events", case_empty_frame),
    ("all six TDN task shapes", case_all_tdn_shapes),
    ("EOF instead of END", case_eof_no_end),
    ("EOF inside startup block", case_eof_midheader),
    ("2000 arrivals in one frame", case_big_group),
]:
    check(name, fn)

print()
print("protocol checks: %d/%d passed" % (8 - len(fails), 8))
sys.exit(1 if fails else 0)
