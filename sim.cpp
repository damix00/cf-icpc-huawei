// Local interactor + scorer for CF 2251A.
//
//   g++ -O2 -std=gnu++17 -o sim.exe sim.cpp
//   sim.exe tests/ex1.txt            -> one score line
//   sim.exe tests/ex1.txt -v         -> also print the interactor transcript
//   sim.exe tests/ex1.txt -script f  -> replay fixed responses from f instead of the strategy
//
// Implements the model exactly as specified: occupancy [t, t+S+dur], per-column piecewise-linear
// task times, single-server FIFO UP/DOWN links, and the 0..1000 score formula.
#define LOCAL_SIM
// Which strategy source to link against; override to score a variant side by side:
//   g++ -O2 -std=gnu++17 -DSOL_SRC=\"sol_v2.cpp\" -o sim_v2.exe sim.cpp
#ifndef SOL_SRC
#define SOL_SRC "sol.cpp"
#endif
#include SOL_SRC

// ===================== test case =====================

struct TestCase {
    Params P;
    Table T;
    int R = 0;
    vector<double> arr;
    vector<int> Lin, Lout;
    vector<array<double, 7>> rawRows;   // kept so -dump can reproduce the startup block
};

static bool loadTest(const char* path, TestCase& tc) {
    FILE* fp = fopen(path, "r");
    if (!fp) { fprintf(stderr, "cannot open %s\n", path); return false; }
    Params& P = tc.P;
    if (fscanf(fp, "%d %lf %lf %lf %lf %d", &P.K, &P.S, &P.lat, &P.bw, &P.bytesPerToken, &P.numLayers) != 6) return false;
    if (fscanf(fp, "%lf %lf %lf %lf %lf %lf %lf", &P.SLO1, &P.SLO2, &P.tpUB, &P.tpBase, &P.distBase, &P.wTp, &P.wC) != 7) return false;
    int N; if (fscanf(fp, "%d", &N) != 1) return false;
    for (int r = 0; r < N; r++) {
        double bs, v[6];
        if (fscanf(fp, "%lf %lf %lf %lf %lf %lf %lf", &bs, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 7) return false;
        for (int c = 0; c < 6; c++) if (v[c] >= 0) tc.T.c[c].add(bs, v[c]);
        tc.rawRows.push_back({bs, v[0], v[1], v[2], v[3], v[4], v[5]});
    }
    for (int c = 0; c < 6; c++) tc.T.c[c].finish();
    if (fscanf(fp, "%d", &tc.R) != 1) return false;
    tc.arr.resize(tc.R); tc.Lin.resize(tc.R); tc.Lout.resize(tc.R);
    for (int i = 0; i < tc.R; i++)
        if (fscanf(fp, "%lf %d %d", &tc.arr[i], &tc.Lin[i], &tc.Lout[i]) != 3) return false;
    fclose(fp);
    return true;
}

// ===================== simulator =====================

namespace Sim {

// simulator-side request states (independent of the strategy's own bookkeeping)
enum {
    Q_UNBORN = 0, Q_ARRIVED, Q_PPRE_RUN, Q_PRE_UP, Q_PPROC_RDY, Q_PPROC_RUN, Q_PRE_DOWN,
    Q_PPOST_RDY, Q_PPOST_RUN, Q_DPRE_RDY, Q_DPRE_RUN, Q_DEC_UP, Q_DPROC_RDY, Q_DPROC_RUN,
    Q_DEC_DOWN, Q_DPOST_RDY, Q_DPOST_RUN, Q_FIN
};

struct Task {
    int server, step, remote, ls, le;
    vector<int> ids;
    double dur;
};
struct Xfer {
    int dir, remote, kind;
    double sizeBytes;
    vector<int> ids;
};
// kind: 0 = ARR, 1 = task done, 2 = transfer done
struct Ev {
    double t;
    long long seq;
    int kind, idx;
    bool operator<(const Ev& o) const { return t != o.t ? t > o.t : seq > o.seq; }
};

TestCase tc;
Params P;
Table T;
priority_queue<Ev> pq;
long long seqCtr = 0;
vector<Task> tasks;
vector<Xfer> xfers;

vector<int> qs, qRemote, qLayers, qTokens;
vector<double> qTdr, qFirstTok, qLastTok;

bool eBusy = false;
vector<char> rBusy;
double upFree = 0, downFree = 0;
int finishedCount = 0;
bool verbose = false;
bool failed = false;
string failMsg;

// utilisation / batching statistics
double busyE = 0, busyUp = 0, busyDown = 0;
vector<double> busyR;
double grpSum[6] = {0, 0, 0, 0, 0, 0};
long long grpCnt[6] = {0, 0, 0, 0, 0, 0};

// Where a decode round's wall clock actually goes: one accumulator per lifecycle state, charged
// per request-millisecond.  Comparing the ready-states (queueing) against the run-states
// (unavoidable service) shows which stage is stalling.
double stateTime[20] = {0};
vector<double> qSince;
inline void setState(int i, int ns, double t);

inline void fail(const string& m) { if (!failed) { failed = true; failMsg = m; } }

inline void setState(int i, int ns, double t) {
    if (!qSince.empty()) { stateTime[qs[i]] += t - qSince[i]; qSince[i] = t; }
    qs[i] = ns;
}

inline double xferTime(double len) {
    return P.lat + 8.0 * (len * P.bytesPerToken) / (P.bw * 1e6);
}

inline void pushEv(double t, int kind, int idx) {
    pq.push(Ev{t, seqCtr++, kind, idx});
}

// queue a transfer onto its FIFO link at time q
inline void queueXfer(double q, int dir, int remote, int kind, double len, const vector<int>& ids) {
    double& linkFree = (dir == DIR_UP) ? upFree : downFree;
    double start = max(q, linkFree);
    double fin = start + xferTime(len);
    linkFree = fin;
    (dir == DIR_UP ? busyUp : busyDown) += fin - start;
    Xfer x;
    x.dir = dir; x.remote = remote; x.kind = kind;
    x.sizeBytes = len * P.bytesPerToken;
    x.ids = ids;
    xfers.push_back(x);
    pushEv(fin, 2, (int)xfers.size() - 1);
}

inline double stepDur(int step, const vector<int>& ids, int ls, int le) {
    switch (step) {
        case ST_PPRE:  return T.c[C_PPRE].at((double)tc.Lin[ids[0]]);
        case ST_PPROC: return (double)(le - ls) / P.numLayers * T.c[C_PPROC].at((double)tc.Lin[ids[0]]);
        case ST_PPOST: return T.c[C_PPOST].at((double)tc.Lin[ids[0]]);
        case ST_DPRE:  return T.c[C_DPRE].at((double)ids.size());
        case ST_DPROC: return T.c[C_DPROC].at((double)ids.size());
        default:       return T.c[C_DPOST].at((double)ids.size());
    }
}

// Validate one assignment; on success start it and return true.
bool startTask(double t, const Assign& A, vector<char>& usedServer) {
    int srv = A.server;
    if (srv < -1 || srv >= P.K) { fail("bad server index"); return false; }
    int slot = srv + 1;
    if (usedServer[slot]) { fail("two tasks assigned to one resource in a response"); return false; }
    bool busy = (srv < 0) ? eBusy : (bool)rBusy[srv];
    if (busy) { fail("task assigned to a busy computer"); return false; }

    if (A.ids.empty()) { fail("empty group"); return false; }
    for (int i : A.ids) {
        if (i < 0 || i >= tc.R) { fail("unknown request id"); return false; }
        if (qs[i] == Q_FIN) { fail("request already finished"); return false; }
    }
    for (size_t x = 0; x < A.ids.size(); x++)
        for (size_t y = x + 1; y < A.ids.size(); y++)
            if (A.ids[x] == A.ids[y]) { fail("duplicate request id in a group"); return false; }

    int ls = A.ls, le = A.le;
    switch (A.step) {
        case ST_PPRE: {
            if (srv != -1) { fail("P PRE not on the local computer"); return false; }
            if (A.ids.size() != 1) { fail("P PRE group size != 1"); return false; }
            if (A.remote < 0 || A.remote >= P.K) { fail("P PRE remote out of range"); return false; }
            if (qs[A.ids[0]] != Q_ARRIVED) { fail("P PRE on a request not awaiting it"); return false; }
            qRemote[A.ids[0]] = A.remote;
            break;
        }
        case ST_PPROC: {
            int i = A.ids[0];
            if (A.ids.size() != 1) { fail("P PROC group size != 1"); return false; }
            if (qs[i] != Q_PPROC_RDY) { fail("P PROC on a request not awaiting it"); return false; }
            if (srv != qRemote[i] || A.remote != qRemote[i]) { fail("P PROC on the wrong remote"); return false; }
            if (ls != qLayers[i]) { fail("P PROC piece does not continue from the previous one"); return false; }
            if (le <= ls || le > P.numLayers || ls < 0) { fail("illegal P PROC piece range"); return false; }
            break;
        }
        case ST_PPOST: {
            int i = A.ids[0];
            if (srv != -1) { fail("P POST not on the local computer"); return false; }
            if (A.ids.size() != 1) { fail("P POST group size != 1"); return false; }
            if (qs[i] != Q_PPOST_RDY) { fail("P POST on a request not awaiting it"); return false; }
            if (A.remote != qRemote[i]) { fail("P POST names the wrong remote"); return false; }
            break;
        }
        case ST_DPRE: {
            if (srv != -1) { fail("D PRE not on the local computer"); return false; }
            for (int i : A.ids) if (qs[i] != Q_DPRE_RDY) { fail("D PRE member not ready"); return false; }
            break;
        }
        case ST_DPROC: {
            if (srv < 0) { fail("D PROC not on a remote"); return false; }
            if (A.remote != srv) { fail("D PROC remote field mismatch"); return false; }
            for (int i : A.ids) {
                if (qs[i] != Q_DPROC_RDY) { fail("D PROC member not ready"); return false; }
                if (qRemote[i] != srv) { fail("D PROC member assigned to a different remote"); return false; }
            }
            break;
        }
        case ST_DPOST: {
            if (srv != -1) { fail("D POST not on the local computer"); return false; }
            for (int i : A.ids) if (qs[i] != Q_DPOST_RDY) { fail("D POST member not ready"); return false; }
            break;
        }
        default: fail("unknown step"); return false;
    }

    usedServer[slot] = 1;
    if (srv < 0) eBusy = true; else rBusy[srv] = 1;

    Task tk;
    tk.server = srv; tk.step = A.step; tk.remote = A.remote; tk.ls = ls; tk.le = le; tk.ids = A.ids;
    tk.dur = stepDur(A.step, A.ids, ls, le);
    tasks.push_back(tk);
    pushEv(t + P.S + tk.dur, 1, (int)tasks.size() - 1);
    if (srv < 0) busyE += P.S + tk.dur; else busyR[srv] += P.S + tk.dur;
    grpSum[A.step] += (double)A.ids.size();
    grpCnt[A.step]++;

    int running = (A.step == ST_PPRE)  ? Q_PPRE_RUN  : (A.step == ST_PPROC) ? Q_PPROC_RUN :
                  (A.step == ST_PPOST) ? Q_PPOST_RUN : (A.step == ST_DPRE)  ? Q_DPRE_RUN  :
                  (A.step == ST_DPROC) ? Q_DPROC_RUN : Q_DPOST_RUN;
    for (int i : A.ids) setState(i, running, t);
    return true;
}

// Apply a completed task; append its event lines to the frame.
void applyTaskDone(double t, int ti, Frame& f, vector<string>& lines) {
    Task& tk = tasks[ti];
    if (tk.server < 0) eBusy = false; else rBusy[tk.server] = 0;

    char buf[128];
    string txt = "TDN ";
    if (tk.server < 0) txt += "E"; else { snprintf(buf, sizeof buf, "C%d", tk.server); txt += buf; }
    txt += " ";
    switch (tk.step) {
        case ST_PPRE:  snprintf(buf, sizeof buf, "P PRE %d %d", tk.remote, tk.ids[0]); txt += buf; break;
        case ST_PPROC: snprintf(buf, sizeof buf, "P PROC %d %d %d %d", tk.ls, tk.le, tk.remote, tk.ids[0]); txt += buf; break;
        case ST_PPOST: snprintf(buf, sizeof buf, "P POST %d %d", tk.remote, tk.ids[0]); txt += buf; break;
        case ST_DPRE:  txt += "D PRE -1"; break;
        case ST_DPROC: snprintf(buf, sizeof buf, "D PROC %d", tk.remote); txt += buf; break;
        case ST_DPOST: txt += "D POST -1"; break;
    }
    if (tk.step >= ST_DPRE) {
        snprintf(buf, sizeof buf, " %d", (int)tk.ids.size()); txt += buf;
        for (int i : tk.ids) { snprintf(buf, sizeof buf, " %d", i); txt += buf; }
    }
    snprintf(buf, sizeof buf, " %.9f", tk.dur); txt += buf;
    lines.push_back(txt);

    Event ev; ev.type = EV_TDN; ev.a = tk.server;
    f.evs.push_back(ev);

    switch (tk.step) {
        case ST_PPRE: {
            int i = tk.ids[0];
            setState(i, Q_PRE_UP, t);
            queueXfer(t, DIR_UP, qRemote[i], KIND_PRE, (double)tc.Lin[i], tk.ids);
            break;
        }
        case ST_PPROC: {
            int i = tk.ids[0];
            qLayers[i] = tk.le;
            if (tk.le >= P.numLayers) {
                setState(i, Q_PRE_DOWN, t);
                queueXfer(t, DIR_DOWN, qRemote[i], KIND_PRE, (double)tc.Lin[i], tk.ids);
            } else setState(i, Q_PPROC_RDY, t);
            break;
        }
        case ST_PPOST: {
            int i = tk.ids[0];
            setState(i, Q_DPRE_RDY, t);
            qTdr[i] = t;
            break;
        }
        case ST_DPRE: {
            for (int i : tk.ids) setState(i, Q_DEC_UP, t);
            for (int j = 0; j < P.K; j++) {      // one UP per remote, increasing remote index
                vector<int> mem;
                for (int i : tk.ids) if (qRemote[i] == j) mem.push_back(i);
                if (!mem.empty()) queueXfer(t, DIR_UP, j, KIND_DEC, (double)mem.size(), mem);
            }
            break;
        }
        case ST_DPROC: {
            for (int i : tk.ids) setState(i, Q_DEC_DOWN, t);
            queueXfer(t, DIR_DOWN, tk.remote, KIND_DEC, (double)tk.ids.size(), tk.ids);
            break;
        }
        case ST_DPOST: {
            for (int i : tk.ids) {
                qTokens[i]++;
                if (qTokens[i] == 1) qFirstTok[i] = t;
                qLastTok[i] = t;
                if (qTokens[i] >= tc.Lout[i]) {
                    setState(i, Q_FIN, t);
                    finishedCount++;
                    snprintf(buf, sizeof buf, "FIN %d", i);
                    lines.push_back(buf);
                    Event fe; fe.type = EV_FIN; fe.a = i;
                    f.evs.push_back(fe);
                } else setState(i, Q_DPRE_RDY, t);
            }
            break;
        }
    }
}

void applyXferDone(double t, int xi, Frame& f, vector<string>& lines) {
    Xfer& x = xfers[xi];
    char buf[128];
    string txt = "XDN ";
    txt += (x.dir == DIR_UP) ? "UP " : "DOWN ";
    snprintf(buf, sizeof buf, "%d %lld %s %d", x.remote, (long long)llround(x.sizeBytes),
             x.kind == KIND_PRE ? "PRE" : "DEC", (int)x.ids.size());
    txt += buf;
    for (int i : x.ids) { snprintf(buf, sizeof buf, " %d", i); txt += buf; }
    lines.push_back(txt);

    Event ev; ev.type = EV_XDN; ev.a = x.dir; ev.b = x.kind;
    ev.off = (int)f.ids.size(); ev.cnt = (int)x.ids.size();
    for (int i : x.ids) f.ids.push_back(i);
    f.evs.push_back(ev);

    if (x.kind == KIND_PRE) {
        int i = x.ids[0];
        setState(i, (x.dir == DIR_UP) ? Q_PPROC_RDY : Q_PPOST_RDY, t);
    } else {
        for (int i : x.ids) setState(i, (x.dir == DIR_UP) ? Q_DPROC_RDY : Q_DPOST_RDY, t);
    }
}

// ---- scripted responses (for replaying the statement's worked examples) ----
FILE* scriptFp = nullptr;
FILE* dumpIn = nullptr;    // the exact byte stream a submitted binary would read
FILE* dumpOut = nullptr;   // the responses the in-process strategy produced

void writeAssign(FILE* fp, const Assign& A, const char* prefix) {
    fputs(prefix, fp);
    if (A.server < 0) fputs("E ", fp); else fprintf(fp, "C%d ", A.server);
    switch (A.step) {
        case ST_PPRE:  fprintf(fp, "P PRE %d %d", A.remote, A.ids[0]); break;
        case ST_PPROC: fprintf(fp, "P PROC %d %d %d %d", A.ls, A.le, A.remote, A.ids[0]); break;
        case ST_PPOST: fprintf(fp, "P POST %d %d", A.remote, A.ids[0]); break;
        case ST_DPRE:  fprintf(fp, "D PRE -1 %d", (int)A.ids.size());
                       for (int i : A.ids) fprintf(fp, " %d", i); break;
        case ST_DPROC: fprintf(fp, "D PROC %d %d", A.remote, (int)A.ids.size());
                       for (int i : A.ids) fprintf(fp, " %d", i); break;
        case ST_DPOST: fprintf(fp, "D POST -1 %d", (int)A.ids.size());
                       for (int i : A.ids) fprintf(fp, " %d", i); break;
    }
    fputc('\n', fp);
}

bool readScriptResponse(Response& out) {
    out.n = 0;
    int n;
    if (fscanf(scriptFp, "%d", &n) != 1) { fail("script ran out of responses"); return false; }
    char w1[16], w2[16], w3[16];
    for (int k = 0; k < n; k++) {
        if (fscanf(scriptFp, "%15s %15s %15s", w1, w2, w3) != 3) { fail("bad script line"); return false; }
        Assign& A = out.a[out.n++];
        A.ids.clear(); A.ls = A.le = 0;
        A.server = (w1[0] == 'E') ? -1 : atoi(w1 + 1);
        bool isP = (w2[0] == 'P');
        if (isP && strcmp(w3, "PRE") == 0) {
            A.step = ST_PPRE; int r, id; if (fscanf(scriptFp, "%d %d", &r, &id) != 2) return false;
            A.remote = r; A.ids.push_back(id);
        } else if (isP && strcmp(w3, "PROC") == 0) {
            A.step = ST_PPROC; int ls, le, r, id;
            if (fscanf(scriptFp, "%d %d %d %d", &ls, &le, &r, &id) != 4) return false;
            A.ls = ls; A.le = le; A.remote = r; A.ids.push_back(id);
        } else if (isP) {
            A.step = ST_PPOST; int r, id; if (fscanf(scriptFp, "%d %d", &r, &id) != 2) return false;
            A.remote = r; A.ids.push_back(id);
        } else {
            A.step = (strcmp(w3, "PRE") == 0) ? ST_DPRE : (strcmp(w3, "PROC") == 0) ? ST_DPROC : ST_DPOST;
            int r, m; if (fscanf(scriptFp, "%d %d", &r, &m) != 2) return false;
            A.remote = r;
            for (int z = 0; z < m; z++) { int id; if (fscanf(scriptFp, "%d", &id) != 1) return false; A.ids.push_back(id); }
        }
    }
    return true;
}

// ---- reference schedule: strictly one request at a time, full prefill piece, groups of 1 ----
// This is what tp_base / dist_base are defined against in the statement.
bool refMode = false;
int refCur = -1, refPtr = 0;

void refSchedule(Response& out) {
    out.n = 0;
    if (refCur >= 0 && qs[refCur] == Q_FIN) refCur = -1;
    if (refCur < 0) {
        while (refPtr < (int)qs.size() && qs[refPtr] == Q_FIN) refPtr++;
        if (refPtr < (int)qs.size() && qs[refPtr] == Q_ARRIVED) refCur = refPtr++;
        else return;
    }
    int i = refCur;
    Assign& A = out.a[0];
    A.ids.clear(); A.ls = A.le = 0; A.remote = 0; A.server = -1;
    switch (qs[i]) {
        case Q_ARRIVED:    A.step = ST_PPRE;  break;
        case Q_PPROC_RDY:  A.step = ST_PPROC; A.server = 0; A.ls = 0; A.le = P.numLayers; break;
        case Q_PPOST_RDY:  A.step = ST_PPOST; break;
        case Q_DPRE_RDY:   A.step = ST_DPRE;  A.remote = -1; break;
        case Q_DPROC_RDY:  A.step = ST_DPROC; A.server = 0; break;
        case Q_DPOST_RDY:  A.step = ST_DPOST; A.remote = -1; break;
        default: return;   // in flight or in transit: nothing to do
    }
    A.ids.push_back(i);
    out.n = 1;
}

// ---- main loop ----
struct Result {
    bool ok = false;
    double score = 0, tp = 0, tdr = 0, tpot = 0, tpComp = 0, waitComp = 0, elapsed = 0;
    long long frames = 0;
    string err;
};

Result run(const TestCase& t_, bool verb, FILE* script) {
    tc = t_; P = tc.P; T = tc.T; verbose = verb; scriptFp = script;
    qs.assign(tc.R, Q_UNBORN); qSince.assign(tc.R, 0.0);
    for (int z = 0; z < 20; z++) stateTime[z] = 0; qRemote.assign(tc.R, 0); qLayers.assign(tc.R, 0); qTokens.assign(tc.R, 0);
    qTdr.assign(tc.R, 0.0); qFirstTok.assign(tc.R, 0.0); qLastTok.assign(tc.R, 0.0);
    rBusy.assign(P.K, 0);
    eBusy = false; upFree = downFree = 0; finishedCount = 0; failed = false; failMsg.clear();
    tasks.clear(); xfers.clear(); seqCtr = 0;
    busyE = busyUp = busyDown = 0; busyR.assign(P.K, 0.0);
    for (int i = 0; i < 6; i++) { grpSum[i] = 0; grpCnt[i] = 0; }
    refCur = -1; refPtr = 0;
    while (!pq.empty()) pq.pop();

    for (int i = 0; i < tc.R; i++) pushEv(tc.arr[i], 0, i);
    if (!scriptFp && !refMode) Sch::schedInit(P, T);

    if (dumpIn) {
        fprintf(dumpIn, "%d %.9f %.9f %.9f %.9f %d\n", P.K, P.S, P.lat, P.bw, P.bytesPerToken, P.numLayers);
        fprintf(dumpIn, "%.9f %.9f %.9f %.9f %.9f %.9f %.9f\n",
                P.SLO1, P.SLO2, P.tpUB, P.tpBase, P.distBase, P.wTp, P.wC);
        fprintf(dumpIn, "%d\n", (int)tc.rawRows.size());
        for (auto& r : tc.rawRows)
            fprintf(dumpIn, "%d %.9f %.9f %.9f %.9f %.9f %.9f\n",
                    (int)llround(r[0]), r[1], r[2], r[3], r[4], r[5], r[6]);
    }

    Result res;
    Frame f;
    Response out;
    vector<char> usedServer(P.K + 1, 0);
    vector<string> lines;

    while (finishedCount < tc.R) {
        if (pq.empty()) { res.err = "stuck: no future event with unfinished requests"; return res; }
        double t = pq.top().t;
        f.clear(); lines.clear();
        f.t = t;
        while (!pq.empty() && pq.top().t == t) {
            Ev e = pq.top(); pq.pop();
            if (e.kind == 0) {
                char buf[64];
                snprintf(buf, sizeof buf, "ARR %d %d", e.idx, tc.Lin[e.idx]);
                lines.push_back(buf);
                Event ev; ev.type = EV_ARR; ev.a = e.idx; ev.b = tc.Lin[e.idx];
                f.evs.push_back(ev);
                qSince[e.idx] = t; setState(e.idx, Q_ARRIVED, t);
            } else if (e.kind == 1) applyTaskDone(t, e.idx, f, lines);
            else applyXferDone(t, e.idx, f, lines);
        }
        res.frames++;

        if (verbose) {
            printf("%.9f\n%d\n", t, (int)lines.size());
            for (auto& s : lines) printf("%s\n", s.c_str());
        }
        if (dumpIn) {
            fprintf(dumpIn, "%.9f\n%d\n", t, (int)lines.size());
            for (auto& s : lines) fprintf(dumpIn, "%s\n", s.c_str());
        }

        if (scriptFp) { if (!readScriptResponse(out)) { res.err = failMsg; return res; } }
        else if (refMode) refSchedule(out);
        else Sch::schedFrame(t, f, out);

        if (dumpOut) {
            fprintf(dumpOut, "%d\n", out.n);
            for (int k = 0; k < out.n; k++) writeAssign(dumpOut, out.a[k], "");
        }

        if (out.n < 0 || out.n > P.K + 1) { res.err = "response has more than K+1 assignments"; return res; }
        fill(usedServer.begin(), usedServer.end(), 0);
        for (int k = 0; k < out.n; k++)
            if (!startTask(t, out.a[k], usedServer)) { res.err = failMsg; return res; }

        if (verbose) {
            printf("> %d\n", out.n);
            for (int k = 0; k < out.n; k++) writeAssign(stdout, out.a[k], "> ");
        }
    }
    if (verbose) printf("END\n");
    if (dumpIn) fprintf(dumpIn, "END\n");

    // ---- score ----
    double earliestArr = *min_element(tc.arr.begin(), tc.arr.end());
    double lastTok = 0;
    long long totalTokens = 0, gapCnt = 0;
    double gapSum = 0, tdrSum = 0;
    for (int i = 0; i < tc.R; i++) {
        lastTok = max(lastTok, qLastTok[i]);
        totalTokens += tc.Lout[i];
        tdrSum += qTdr[i] - tc.arr[i];
        if (tc.Lout[i] > 1) { gapSum += qLastTok[i] - qFirstTok[i]; gapCnt += tc.Lout[i] - 1; }
    }
    res.elapsed = lastTok - earliestArr;
    res.tp = res.elapsed > 0 ? (double)totalTokens / res.elapsed : 0;
    res.tdr = tdrSum / tc.R;
    res.tpot = gapCnt ? gapSum / gapCnt : 0;
    double den = P.tpUB - P.tpBase;
    res.tpComp = den > 0 ? max(0.0, min(1.0, (res.tp - P.tpBase) / den)) : 0.0;
    double exTdr = max(0.0, (res.tdr - P.SLO1) / P.SLO1);
    double exTpot = max(0.0, (res.tpot - P.SLO2) / P.SLO2);
    double dist = sqrt(exTdr * exTdr + exTpot * exTpot);
    if (P.distBase > 0) res.waitComp = max(0.0, 1.0 - dist / P.distBase);
    else res.waitComp = (dist == 0.0) ? 1.0 : 0.0;
    res.score = 1000.0 * (P.wTp * res.tpComp + P.wC * res.waitComp);
    res.ok = true;
    return res;
}

}  // namespace Sim

// Fill in the scoring line of a generated test from the reference schedule.
// The placeholder line carries <slo1Factor> <slo2Factor> 1 0 0 <w_tp> <w_c>.
static int calibrate(const char* path, TestCase& tc) {
    Params& P = tc.P;
    Table& T = tc.T;
    double f1 = P.SLO1, f2 = P.SLO2, wTp = P.wTp, wC = P.wC;
    double u = 8.0 * P.bytesPerToken / (P.bw * 1e6);
    auto Txf = [&](double len) { return P.lat + u * len; };

    vector<int> ls = tc.Lin;
    sort(ls.begin(), ls.end());
    double medLin = ls[ls.size() / 2];
    double idealTdr = 3 * P.S + T.c[C_PPRE].at(medLin) + T.c[C_PPROC].at(medLin) + T.c[C_PPOST].at(medLin)
                    + 2 * Txf(medLin);
    double idealTpot = 3 * P.S + T.c[C_DPRE].at(1) + T.c[C_DPROC].at(1) + T.c[C_DPOST].at(1) + 2 * Txf(1);
    P.SLO1 = f1 * idealTdr;
    P.SLO2 = f2 * idealTpot;

    // Work-conservation bound on throughput: no schedule can beat the busiest resource's
    // total unavoidable work. Prefill work is included -- on prefill-heavy tests it, not decode,
    // is what pins the remotes, and a decode-only bound would make tp_UB unreachable.
    long long sumOut = 0;
    for (int x : tc.Lout) sumOut += x;
    double sumPProc = 0, sumPLocal = 0, sumLinXfer = 0;
    for (int i = 0; i < tc.R; i++) {
        double L = tc.Lin[i];
        sumPProc += T.c[C_PPROC].at(L);
        sumPLocal += T.c[C_PPRE].at(L) + T.c[C_PPOST].at(L) + 2 * P.S;
        sumLinXfer += P.lat + u * L;
    }
    double tokens = (double)sumOut;
    double best = 0;
    int mCap = min(4096, max(1, tc.R));
    for (int k = 1; k <= P.K; k++)
        for (int m = 1; m <= mCap; m *= 2) {
            double mj = max(1.0, (double)m / k);
            double rounds = tokens / m;
            double localWork = sumPLocal + rounds * (2 * P.S + T.c[C_DPRE].at(m) + T.c[C_DPOST].at(m));
            double remoteWork = (sumPProc + tc.R * P.S + rounds * k * (P.S + T.c[C_DPROC].at(mj))) / P.K;
            double linkWork = sumLinXfer + rounds * (k * P.lat + u * m);
            double span = max(max(localWork, remoteWork), linkWork);
            if (span > 0) best = max(best, tokens / span);
        }
    double arrSpan = tc.arr.back() - tc.arr.front();
    if (arrSpan > 0) best = min(best, tokens / arrSpan);

    P.tpBase = 0; P.tpUB = 1; P.distBase = 0;      // neutral while running the reference
    Sim::refMode = true;
    Sim::Result r = Sim::run(tc, false, nullptr);
    Sim::refMode = false;
    if (!r.ok) { fprintf(stderr, "calibrate: reference schedule failed (%s)\n", r.err.c_str()); return 1; }

    P.tpBase = r.tp;
    P.tpUB = max(best, r.tp * 1.2);
    double exTdr = max(0.0, (r.tdr - P.SLO1) / P.SLO1);
    double exTpot = max(0.0, (r.tpot - P.SLO2) / P.SLO2);
    P.distBase = sqrt(exTdr * exTdr + exTpot * exTpot);
    P.wTp = wTp; P.wC = wC;

    FILE* fp = fopen(path, "w");
    if (!fp) return 1;
    fprintf(fp, "%d %.9f %.9f %.9f %.9f %d\n", P.K, P.S, P.lat, P.bw, P.bytesPerToken, P.numLayers);
    fprintf(fp, "%.9f %.9f %.9f %.9f %.9f %.9f %.9f\n",
            P.SLO1, P.SLO2, P.tpUB, P.tpBase, P.distBase, P.wTp, P.wC);
    fprintf(fp, "%d\n", (int)tc.rawRows.size());
    for (auto& row : tc.rawRows)
        fprintf(fp, "%d %.9f %.9f %.9f %.9f %.9f %.9f\n",
                (int)llround(row[0]), row[1], row[2], row[3], row[4], row[5], row[6]);
    fprintf(fp, "%d\n", tc.R);
    for (int i = 0; i < tc.R; i++) fprintf(fp, "%.9f %d %d\n", tc.arr[i], tc.Lin[i], tc.Lout[i]);
    fclose(fp);
    printf("calibrated %s: tp_base=%.6g tp_UB=%.6g SLO1=%.6g SLO2=%.6g dist_base=%.4g (ref tdr=%.4g tpot=%.4g)\n",
           path, P.tpBase, P.tpUB, P.SLO1, P.SLO2, P.distBase, r.tdr, r.tpot);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: sim <test> [-v] [-script <f>] [-dump <pre>] [-calibrate]\n"); return 2; }
    bool verbose = false, doCal = false, stats = false;
    FILE* script = nullptr;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = true;
        else if (!strcmp(argv[i], "-calibrate")) doCal = true;
        else if (!strcmp(argv[i], "-stats")) stats = true;
        else if (!strcmp(argv[i], "-script") && i + 1 < argc) {
            script = fopen(argv[++i], "r");
            if (!script) { fprintf(stderr, "cannot open script\n"); return 2; }
        } else if (!strcmp(argv[i], "-dump") && i + 1 < argc) {
            string pre = argv[++i];
            Sim::dumpIn = fopen((pre + ".in").c_str(), "w");
            Sim::dumpOut = fopen((pre + ".out").c_str(), "w");
            if (!Sim::dumpIn || !Sim::dumpOut) { fprintf(stderr, "cannot open dump files\n"); return 2; }
        }
    }
    TestCase tc;
    if (!loadTest(argv[1], tc)) return 2;
    if (doCal) return calibrate(argv[1], tc);
    Sim::Result r = Sim::run(tc, verbose, script);
    if (Sim::dumpIn) fclose(Sim::dumpIn);
    if (Sim::dumpOut) fclose(Sim::dumpOut);
    if (!r.ok) {
        printf("test=%s FAILED (%s)\n", argv[1], r.err.c_str());
        return 1;
    }
    printf("test=%s score=%.3f tp=%.6g (comp %.4f) tdr=%.4g/%.4g tpot=%.4g/%.4g (comp %.4f) elapsed=%.4g frames=%lld\n",
           argv[1], r.score, r.tp, r.tpComp, r.tdr, tc.P.SLO1, r.tpot, tc.P.SLO2, r.waitComp, r.elapsed, r.frames);
    if (stats) {
        double e = r.elapsed > 0 ? r.elapsed : 1;
        printf("   util: E=%.3f up=%.3f down=%.3f rem=", Sim::busyE / e, Sim::busyUp / e, Sim::busyDown / e);
        for (int j = 0; j < tc.P.K; j++) printf("%s%.3f", j ? "," : "", Sim::busyR[j] / e);
        {
            static const char* nm[18] = {"unborn","arrived","P PRE run","pre UP","P PROC rdy",
                "P PROC run","pre DOWN","P POST rdy","P POST run","D PRE wait","D PRE run",
                "dec UP","D PROC wait","D PROC run","dec DOWN","D POST wait","D POST run","fin"};
            double tot = 0; for (int z = 9; z <= 16; z++) tot += Sim::stateTime[z];
            long long toks = 0; for (int i = 0; i < tc.R; i++) toks += tc.Lout[i];
            printf("   decode round, ms per token: ");
            for (int z = 9; z <= 16; z++)
                if (Sim::stateTime[z] > 1e-9)
                    printf("%s=%.1f ", nm[z], Sim::stateTime[z] / (double)toks);
            printf("| sum=%.1f tpot=%.1f\n", tot / (double)toks, r.tpot);
        }
        printf("\n   groups: dpre=%.1f(x%lld) dproc=%.1f(x%lld) dpost=%.1f(x%lld)  prefill pieces=%lld for R=%d\n",
               Sim::grpCnt[ST_DPRE] ? Sim::grpSum[ST_DPRE] / Sim::grpCnt[ST_DPRE] : 0.0, Sim::grpCnt[ST_DPRE],
               Sim::grpCnt[ST_DPROC] ? Sim::grpSum[ST_DPROC] / Sim::grpCnt[ST_DPROC] : 0.0, Sim::grpCnt[ST_DPROC],
               Sim::grpCnt[ST_DPOST] ? Sim::grpSum[ST_DPOST] / Sim::grpCnt[ST_DPOST] : 0.0, Sim::grpCnt[ST_DPOST],
               Sim::grpCnt[ST_PPROC], tc.R);
    }
    return 0;
}
