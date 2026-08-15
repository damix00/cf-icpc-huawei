"""End-to-end protocol check: drive the real sol.exe binary over PIPES, the way the judge does.

sim.exe compiles the strategy in-process (LOCAL_SIM), so main()'s stdin/stdout adapter is never
exercised by the local suite; and `sol.exe < file` cannot reproduce interactive behaviour either,
because a redirected file makes short reads impossible.  This replays a recorded interactor
stream one frame at a time -- nothing is sent until the previous response has come back -- so a
blocking read deadlocks here exactly as it does on the judge.

  py pipecheck.py tmp/ex1          # needs tmp/ex1.in and tmp/ex1.out from `sim.exe -dump tmp/ex1`
"""
import subprocess, sys, threading, queue, time, os

TIMEOUT = 20.0  # seconds to wait for one response; the judge's idleness limit is far tighter


def load_frames(path):
    """Split the interactor stream into (header, [frame, ...]).  Frame = t / count / count lines."""
    with open(path) as fh:
        lines = fh.read().split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    # header: 2 parameter lines, then N and N table rows
    n_rows = int(lines[2])
    pos = 3 + n_rows
    header = lines[:pos]
    frames = []
    while pos < len(lines):
        if lines[pos].strip() == "END":
            frames.append(["END"])
            pos += 1
            break
        n_ev = int(lines[pos + 1])
        frames.append(lines[pos:pos + 2 + n_ev])
        pos += 2 + n_ev
    return header, frames


def load_expected(path):
    with open(path) as fh:
        lines = fh.read().split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    out, pos = [], 0
    while pos < len(lines):
        n = int(lines[pos])
        out.append(lines[pos:pos + 1 + n])
        pos += 1 + n
    return out


def run(prefix, exe):
    header, frames = load_frames(prefix + ".in")
    expected = load_expected(prefix + ".out")

    p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         text=True, bufsize=1)
    q = queue.Queue()

    def reader():
        try:
            for line in p.stdout:
                q.put(line.rstrip("\r\n"))
        except Exception:
            pass
        q.put(None)
    threading.Thread(target=reader, daemon=True).start()

    def expect_line(what, fno):
        try:
            v = q.get(timeout=TIMEOUT)
        except queue.Empty:
            raise SystemExit("FAIL %s: no %s after frame %d within %gs -- "
                             "this is IDLENESS_LIMIT_EXCEEDED" % (prefix, what, fno, TIMEOUT))
        if v is None:
            raise SystemExit("FAIL %s: solution closed stdout / died at frame %d" % (prefix, fno))
        return v

    p.stdin.write("\n".join(header) + "\n")
    p.stdin.flush()

    t0 = time.time()
    n_frames = 0
    for fno, frame in enumerate(frames):
        p.stdin.write("\n".join(frame) + "\n")
        p.stdin.flush()
        if frame == ["END"]:
            break
        n_frames += 1
        got = [expect_line("count", fno)]
        try:
            cnt = int(got[0])
        except ValueError:
            raise SystemExit("FAIL %s: frame %d response count not an integer: %r"
                             % (prefix, fno, got[0]))
        for _ in range(cnt):
            got.append(expect_line("assignment", fno))
        want = expected[fno] if fno < len(expected) else None
        if want is None:
            raise SystemExit("FAIL %s: binary answered frame %d but the reference has no response"
                             % (prefix, fno))
        if [s.strip() for s in got] != [s.strip() for s in want]:
            raise SystemExit("FAIL %s: frame %d mismatch\n  frame: %s\n  got:   %s\n  want:  %s"
                             % (prefix, fno, frame, got, want))
    wall = time.time() - t0

    # after END the solution must exit promptly and cleanly
    try:
        p.stdin.close()
    except Exception:
        pass
    try:
        rc = p.wait(timeout=10)
    except subprocess.TimeoutExpired:
        p.kill()
        raise SystemExit("FAIL %s: did not exit after END" % prefix)
    if rc != 0:
        raise SystemExit("FAIL %s: exit code %d after END" % (prefix, rc))
    if len(expected) != n_frames:
        raise SystemExit("FAIL %s: %d frames answered but reference has %d responses"
                         % (prefix, n_frames, len(expected)))
    print("ok   %-22s %6d frames  %6.2fs wall  %8.1f frames/s"
          % (os.path.basename(prefix), n_frames, wall, n_frames / wall if wall else 0))


if __name__ == "__main__":
    exe = os.environ.get("SOL", "./sol.exe")
    for pre in sys.argv[1:]:
        run(pre, exe)
