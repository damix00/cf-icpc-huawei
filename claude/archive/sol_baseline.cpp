// CF 2251A - Edge-Cloud Collaborative Scheduling (ICPC 2026 Online Challenge 1, Huawei)
//
// Build for submission:  g++ -O2 -std=gnu++17 -o sol.exe sol.cpp
// The strategy (schedInit/schedFrame) is I/O free so sim.cpp can drive it in-process;
// main() below is only the stdin/stdout protocol adapter and is compiled out for the sim.
#include <bits/stdc++.h>
using namespace std;

// ===================== shared model types =====================

struct Params {
    int K = 1;
    double S = 1, lat = 1, bw = 1, bytesPerToken = 1;
    int numLayers = 1;
    double SLO1 = 1, SLO2 = 1, tpUB = 1, tpBase = 0, distBase = 0, wTp = 0.5, wC = 0.5;
};

// Piecewise-linear lookup over batch_size, flat outside the listed range.
struct PLCurve {
    vector<double> xs, ys;
    void add(double x, double y) { xs.push_back(x); ys.push_back(y); }
    void finish() {
        vector<int> id(xs.size());
        iota(id.begin(), id.end(), 0);
        sort(id.begin(), id.end(), [&](int a, int b) { return xs[a] < xs[b]; });
        vector<double> nx, ny;
        for (int i : id) { nx.push_back(xs[i]); ny.push_back(ys[i]); }
        xs.swap(nx); ys.swap(ny);
    }
    double at(double x) const {
        if (xs.empty()) return 0.0;
        if (x <= xs.front()) return ys.front();
        if (x >= xs.back()) return ys.back();
        int hi = int(lower_bound(xs.begin(), xs.end(), x) - xs.begin());
        int lo = hi - 1;
        double d = xs[hi] - xs[lo];
        if (d <= 0) return ys[hi];
        return ys[lo] + (x - xs[lo]) / d * (ys[hi] - ys[lo]);
    }
};

// column order: prefill_pre, prefill_proc, prefill_post, decode_pre, decode_proc, decode_post
enum { C_PPRE = 0, C_PPROC = 1, C_PPOST = 2, C_DPRE = 3, C_DPROC = 4, C_DPOST = 5 };
struct Table { PLCurve c[6]; };

// task steps
enum { ST_PPRE = 0, ST_PPROC = 1, ST_PPOST = 2, ST_DPRE = 3, ST_DPROC = 4, ST_DPOST = 5 };
// event types
enum { EV_ARR = 0, EV_TDN = 1, EV_XDN = 2, EV_FIN = 3 };
enum { DIR_UP = 0, DIR_DOWN = 1 };
enum { KIND_PRE = 0, KIND_DEC = 1 };

struct Event {
    int type;
    int a = 0, b = 0;      // ARR: rid, Lin | TDN: server(-1=E) | XDN: dir, kind | FIN: rid
    int off = 0, cnt = 0;  // XDN: slice of Frame::ids
};

struct Frame {
    double t = 0;
    vector<Event> evs;
    vector<int> ids;
    void clear() { evs.clear(); ids.clear(); }
};

struct Assign {
    int server;        // -1 = local (E), else remote index
    int step;
    int remote;        // -1 for D PRE / D POST
    int ls = 0, le = 0;
    vector<int> ids;
};

struct Response {
    int n = 0;
    Assign a[16];
};

// ===================== strategy =====================

namespace Sch {

// request lifecycle states
enum {
    R_NEED_PPRE = 0, R_INFL_PPRE, R_WAIT_PRE_UP,
    R_NEED_PPROC, R_INFL_PPROC, R_WAIT_PRE_DOWN,
    R_NEED_PPOST, R_INFL_PPOST,
    R_NEED_DPRE, R_INFL_DPRE, R_WAIT_DEC_UP,
    R_NEED_DPROC, R_INFL_DPROC, R_WAIT_DEC_DOWN,
    R_NEED_DPOST, R_INFL_DPOST, R_DONE
};

struct BusyRec {
    int step = -1, ls = 0, le = 0;
    vector<int> ids;
};

Params P;
Table T;

// per-request state
vector<int> st, lin_, rem, layersDone, chunkStep;
vector<double> arrT, procTotal, tdrCost;
vector<char> finished;

// ready queues
deque<int> qPPre, qPPost;
vector<int> qDPre, qDPost;
vector<deque<int>> qPProc;
vector<vector<int>> qDProc;

// resource occupancy
bool eBusy = false;
vector<char> rBusy;
BusyRec eRec;
vector<BusyRec> rRec;

long long pendingTransfers = 0;
vector<int> load;      // active (unfinished) requests assigned per remote
vector<int> decLoad;   // of those, the ones already past P POST (i.e. decoding)
vector<double> pendProc;   // prefill compute still owed to each remote, in ms
double decWeight = 0;      // ms of remote decode work a decoding request still represents
double linkPenalty = 0;    // cost of pulling one more remote into the decode groups
double uPerToken = 0;      // ms of link time per token-unit transferred
bool sjf = true;           // order admissions shortest-path-first
int activeDecode = 0;

// tunables (overridable from the environment so run.sh can sweep them without editing code)
double PREFILL_URGENCY = 0.5;   // fraction of SLO1 after which prefill preempts decode
double pieceTargetMs = 1.0;     // target duration of one prefill piece
int poolTarget = 1 << 30;       // admit new requests until this many are decoding
// local-computer preference order; digits are 0=D POST, 1=P POST, 2=D PRE, 3=P PRE
const char* ordAdmit = "1302";
const char* ordDecode = "0213";

inline double envD(const char* k, double d) { const char* v = getenv(k); return v ? atof(v) : d; }
inline const char* envS(const char* k, const char* d) { const char* v = getenv(k); return v ? v : d; }

inline void ensureReq(int i) {
    if ((int)st.size() > i) return;
    size_t n = i + 1;
    st.resize(n, R_DONE); lin_.resize(n, 1); rem.resize(n, 0);
    layersDone.resize(n, 0); chunkStep.resize(n, 1);
    arrT.resize(n, 0.0); procTotal.resize(n, 0.0); tdrCost.resize(n, 0.0); finished.resize(n, 0);
}

void schedInit(const Params& p, const Table& t) {
    P = p; T = t;
    qPProc.assign(P.K, {});
    qDProc.assign(P.K, {});
    rBusy.assign(P.K, 0);
    rRec.assign(P.K, BusyRec());
    load.assign(P.K, 0);
    decLoad.assign(P.K, 0);
    pendProc.assign(P.K, 0.0);
    // marginal remote cost of carrying one more decoding request, times a nominal remaining Lout
    double slope = max(0.0, (T.c[C_DPROC].at(64.0) - T.c[C_DPROC].at(1.0)) / 63.0);
    decWeight = envD("CF_DECW", 0.0) * slope;
    // Every extra remote represented in a decode group costs one more link latency per round,
    // in each direction. When latency is high and the remotes are idle, concentrating wins.
    linkPenalty = envD("CF_LINKP", 10000.0) * P.lat;
    uPerToken = 8.0 * P.bytesPerToken / (P.bw * 1e6);
    sjf = envD("CF_SJF", 1.0) > 0.5;
    double dr1 = T.c[C_DPROC].at(1.0);
    pieceTargetMs = max(2.0 * P.S, dr1) * envD("CF_PIECE", 16.0);
    PREFILL_URGENCY = envD("CF_URG", 0.5);
    poolTarget = (int)envD("CF_POOL", 1e9);
    ordAdmit = envS("CF_ORD_A", "1302");
    ordDecode = envS("CF_ORD_D", "0213");
}

// number of pieces to split a prefill into, so one piece cannot monopolise a remote
inline void planChunks(int i) {
    double L = lin_[i];
    double pp = T.c[C_PPROC].at(L);
    procTotal[i] = pp;
    // whole uncontended arrival -> P POST path; used to order admissions shortest-first
    tdrCost[i] = 3 * P.S + T.c[C_PPRE].at(L) + pp + T.c[C_PPOST].at(L)
               + 2 * (P.lat + uPerToken * L);
    int c = (int)llround(pp / pieceTargetMs);
    c = max(1, min(P.numLayers, c));
    chunkStep[i] = max(1, (P.numLayers + c - 1) / c);
}

// Mean TDR is a mean waiting time, and shortest-job-first minimises that. Under load the
// admission queue is long, so ordering it by path cost instead of arrival is worth a lot.
inline int popPPre() {
    if (!sjf || qPPre.size() == 1) { int i = qPPre.front(); qPPre.pop_front(); return i; }
    size_t bestK = 0;
    for (size_t k = 1; k < qPPre.size(); k++)
        if (tdrCost[qPPre[k]] < tdrCost[qPPre[bestK]]) bestK = k;
    int i = qPPre[bestK];
    qPPre.erase(qPPre.begin() + bestK);
    return i;
}

// Balance by projected remote work, not request count: prefill_proc varies by orders of
// magnitude with Lin, so counting requests leaves remotes badly skewed.
inline int pickRemote() {
    int best = 0;
    double bestCost = 1e300;
    for (int j = 0; j < P.K; j++) {
        double cost = pendProc[j] + decWeight * decLoad[j] + (load[j] == 0 ? linkPenalty : 0.0);
        if (cost < bestCost - 1e-9) { bestCost = cost; best = j; }
    }
    return best;
}

inline void onTaskDone(int server) {
    BusyRec& rec = (server < 0) ? eRec : rRec[server];
    if (server < 0) eBusy = false; else rBusy[server] = 0;
    switch (rec.step) {
        case ST_PPRE: {
            int i = rec.ids[0];
            st[i] = R_WAIT_PRE_UP;
            pendingTransfers++;
            break;
        }
        case ST_PPROC: {
            int i = rec.ids[0];
            pendProc[rem[i]] = max(0.0, pendProc[rem[i]]
                                 - (double)(rec.le - rec.ls) / P.numLayers * procTotal[i] - P.S);
            layersDone[i] = rec.le;
            if (rec.le >= P.numLayers) { st[i] = R_WAIT_PRE_DOWN; pendingTransfers++; }
            else { st[i] = R_NEED_PPROC; qPProc[rem[i]].push_back(i); }
            break;
        }
        case ST_PPOST: {
            int i = rec.ids[0];
            st[i] = R_NEED_DPRE; qDPre.push_back(i); activeDecode++; decLoad[rem[i]]++;
            break;
        }
        case ST_DPRE: {
            static vector<char> seen;
            seen.assign(P.K, 0);
            int distinct = 0;
            for (int i : rec.ids) { st[i] = R_WAIT_DEC_UP; if (!seen[rem[i]]) { seen[rem[i]] = 1; distinct++; } }
            pendingTransfers += distinct;
            break;
        }
        case ST_DPROC: {
            for (int i : rec.ids) st[i] = R_WAIT_DEC_DOWN;
            pendingTransfers++;
            break;
        }
        case ST_DPOST: {
            for (int i : rec.ids) if (!finished[i]) { st[i] = R_NEED_DPRE; qDPre.push_back(i); }
            break;
        }
    }
    rec.step = -1;
    rec.ids.clear();
}

inline void onTransfer(const Event& e, const vector<int>& ids) {
    pendingTransfers--;
    if (e.b == KIND_PRE) {
        int i = ids[e.off];
        if (e.a == DIR_UP) { st[i] = R_NEED_PPROC; qPProc[rem[i]].push_back(i); }
        else { st[i] = R_NEED_PPOST; qPPost.push_back(i); }
    } else if (e.a == DIR_UP) {
        for (int k = 0; k < e.cnt; k++) { int i = ids[e.off + k]; st[i] = R_NEED_DPROC; qDProc[rem[i]].push_back(i); }
    } else {
        for (int k = 0; k < e.cnt; k++) { int i = ids[e.off + k]; st[i] = R_NEED_DPOST; qDPost.push_back(i); }
    }
}

inline Assign& newAssign(Response& out, int server, int step, int remote) {
    Assign& A = out.a[out.n++];
    A.server = server; A.step = step; A.remote = remote;
    A.ls = A.le = 0; A.ids.clear();
    return A;
}

inline void takeAll(vector<int>& q, Assign& A, int inflightState) {
    A.ids.assign(q.begin(), q.end());
    for (int i : q) st[i] = inflightState;
    q.clear();
}

inline void recordBusy(BusyRec& rec, const Assign& A) {
    rec.step = A.step; rec.ls = A.ls; rec.le = A.le;
    rec.ids.assign(A.ids.begin(), A.ids.end());
}

void schedFrame(double t, const Frame& f, Response& out) {
    out.n = 0;

    // pass 1: FIN first, so a D POST TDN in the same frame does not requeue a finished request
    for (const Event& e : f.evs) {
        if (e.type == EV_FIN) {
            int i = e.a;
            finished[i] = 1; st[i] = R_DONE;
            load[rem[i]]--; decLoad[rem[i]]--; activeDecode--;
        }
    }
    // pass 2
    for (const Event& e : f.evs) {
        switch (e.type) {
            case EV_ARR: {
                int i = e.a;
                ensureReq(i);
                lin_[i] = e.b; arrT[i] = t; layersDone[i] = 0; finished[i] = 0;
                st[i] = R_NEED_PPRE;
                planChunks(i);
                qPPre.push_back(i);
                break;
            }
            case EV_TDN: onTaskDone(e.a); break;
            case EV_XDN: onTransfer(e, f.ids); break;
            default: break;
        }
    }

    // how long has the oldest un-admitted request been waiting?
    double prefillWait = 0;
    if (!qPPre.empty())  prefillWait = max(prefillWait, t - arrT[qPPre.front()]);
    if (!qPPost.empty()) prefillWait = max(prefillWait, t - arrT[qPPost.front()]);
    // Admit while the decode pool is below target: bigger pools mean bigger decode groups,
    // which amortise S and cut the total number of local tasks.
    bool prefillUrgent = activeDecode < poolTarget || prefillWait > PREFILL_URGENCY * P.SLO1;

    // ---- local computer ----
    if (!eBusy) {
        int choice = -1;   // 0 = D POST, 1 = P POST, 2 = D PRE, 3 = P PRE
        const char* ord = prefillUrgent ? ordAdmit : ordDecode;
        for (int p = 0; ord[p] && choice < 0; p++) {
            int c = ord[p] - '0';
            if ((c == 0 && !qDPost.empty()) || (c == 1 && !qPPost.empty()) ||
                (c == 2 && !qDPre.empty())  || (c == 3 && !qPPre.empty())) choice = c;
        }
        if (choice == 0) {
            Assign& A = newAssign(out, -1, ST_DPOST, -1);
            takeAll(qDPost, A, R_INFL_DPOST);
            recordBusy(eRec, A); eBusy = true;
        } else if (choice == 1) {
            int i = qPPost.front(); qPPost.pop_front();
            Assign& A = newAssign(out, -1, ST_PPOST, rem[i]);
            A.ids.push_back(i); st[i] = R_INFL_PPOST;
            recordBusy(eRec, A); eBusy = true;
        } else if (choice == 2) {
            Assign& A = newAssign(out, -1, ST_DPRE, -1);
            takeAll(qDPre, A, R_INFL_DPRE);
            recordBusy(eRec, A); eBusy = true;
        } else if (choice == 3) {
            int i = popPPre();
            int j = pickRemote();
            rem[i] = j; load[j]++;
            pendProc[j] += procTotal[i] + P.S * ceil((double)P.numLayers / chunkStep[i]);
            Assign& A = newAssign(out, -1, ST_PPRE, j);
            A.ids.push_back(i); st[i] = R_INFL_PPRE;
            recordBusy(eRec, A); eBusy = true;
        }
    }

    // ---- remote computers ----
    for (int j = 0; j < P.K; j++) {
        if (rBusy[j]) continue;
        bool doDecode = !qDProc[j].empty();
        if (doDecode && prefillUrgent && !qPProc[j].empty()) doDecode = false;
        if (doDecode) {
            Assign& A = newAssign(out, j, ST_DPROC, j);
            takeAll(qDProc[j], A, R_INFL_DPROC);
            recordBusy(rRec[j], A); rBusy[j] = 1;
        } else if (!qPProc[j].empty()) {
            int i = qPProc[j].front(); qPProc[j].pop_front();
            Assign& A = newAssign(out, j, ST_PPROC, j);
            A.ls = layersDone[i];
            // Splitting only pays for itself when decode work on this remote needs to interleave;
            // otherwise the extra S per piece is pure loss (and can blow SLO1 on a lone request).
            int stepSz = decLoad[j] > 0 ? chunkStep[i] : P.numLayers;
            A.le = min(P.numLayers, A.ls + stepSz);
            A.ids.push_back(i); st[i] = R_INFL_PPROC;
            recordBusy(rRec[j], A); rBusy[j] = 1;
        }
    }
}

}  // namespace Sch

// ===================== stdin/stdout protocol adapter =====================
#ifndef LOCAL_SIM

// The refill MUST hand back a short read as soon as any bytes are available: this is an
// interactive problem, so the interactor is waiting for our answer to the frame it just sent
// and will send nothing more until it arrives.  fread() does not do that -- it loops on the
// underlying read() until the whole buffer is full or EOF, so with a pipe on stdin it blocks
// forever on the very first frame (IDLENESS_LIMIT_EXCEEDED on every test, 0 ms consumed).
// A file redirect hides this completely, which is why the local suite never caught it.
#if defined(_WIN32)
#include <io.h>
#define SOL_READ(buf, n) _read(0, (buf), (unsigned int)(n))
#else
#include <unistd.h>
#define SOL_READ(buf, n) read(0, (buf), (size_t)(n))
#endif

static char ibuf[1 << 16];
static int ipos = 0, ilen = 0;
static inline int gc() {
    if (ipos == ilen) {
        ipos = 0;
        do { ilen = (int)SOL_READ(ibuf, sizeof(ibuf)); } while (ilen < 0 && errno == EINTR);
        if (ilen <= 0) { ilen = 0; return -1; }
    }
    return (unsigned char)ibuf[ipos++];
}
static char tok[64];
static inline bool readTok() {
    int c = gc();
    while (c == ' ' || c == '\n' || c == '\r' || c == '\t') c = gc();
    if (c < 0) return false;
    int n = 0;
    while (c > ' ') { if (n < 63) tok[n++] = (char)c; c = gc(); }
    tok[n] = 0;
    return n > 0;
}
static inline int pInt(const char* s) {
    int sg = 1;
    if (*s == '+') s++; else if (*s == '-') { sg = -1; s++; }
    int v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v * sg;
}
static inline int rInt() { readTok(); return pInt(tok); }
static inline double rDbl() { readTok(); return strtod(tok, nullptr); }

static string obuf;
static inline void oInt(int v) {
    char tmp[12]; int n = 0;
    if (v < 0) { obuf += '-'; v = -v; }
    do { tmp[n++] = char('0' + v % 10); v /= 10; } while (v);
    while (n) obuf += tmp[--n];
}

static void emit(const Response& r) {
    obuf.clear();
    oInt(r.n); obuf += '\n';
    for (int k = 0; k < r.n; k++) {
        const Assign& A = r.a[k];
        if (A.server < 0) obuf += 'E'; else { obuf += 'C'; oInt(A.server); }
        obuf += ' ';
        switch (A.step) {
            case ST_PPRE:  obuf += "P PRE ";  oInt(A.remote); obuf += ' '; oInt(A.ids[0]); break;
            case ST_PPROC: obuf += "P PROC "; oInt(A.ls); obuf += ' '; oInt(A.le); obuf += ' ';
                           oInt(A.remote); obuf += ' '; oInt(A.ids[0]); break;
            case ST_PPOST: obuf += "P POST "; oInt(A.remote); obuf += ' '; oInt(A.ids[0]); break;
            case ST_DPRE:  obuf += "D PRE -1 ";  oInt((int)A.ids.size());
                           for (int i : A.ids) { obuf += ' '; oInt(i); } break;
            case ST_DPROC: obuf += "D PROC "; oInt(A.remote); obuf += ' '; oInt((int)A.ids.size());
                           for (int i : A.ids) { obuf += ' '; oInt(i); } break;
            case ST_DPOST: obuf += "D POST -1 "; oInt((int)A.ids.size());
                           for (int i : A.ids) { obuf += ' '; oInt(i); } break;
        }
        obuf += '\n';
    }
    fwrite(obuf.data(), 1, obuf.size(), stdout);
    fflush(stdout);
}

int main() {
    Params P;
    P.K = rInt(); P.S = rDbl(); P.lat = rDbl(); P.bw = rDbl(); P.bytesPerToken = rDbl(); P.numLayers = rInt();
    P.SLO1 = rDbl(); P.SLO2 = rDbl(); P.tpUB = rDbl(); P.tpBase = rDbl(); P.distBase = rDbl();
    P.wTp = rDbl(); P.wC = rDbl();

    Table T;
    int N = rInt();
    for (int r = 0; r < N; r++) {
        double bs = rDbl();
        for (int c = 0; c < 6; c++) {
            double v = rDbl();
            if (v >= 0) T.c[c].add(bs, v);
        }
    }
    for (int c = 0; c < 6; c++) T.c[c].finish();

    Sch::schedInit(P, T);

    Frame f;
    static Response out;
    while (true) {
        if (!readTok()) break;
        if (tok[0] == 'E') break;                 // END
        f.clear();
        f.t = strtod(tok, nullptr);
        int ne = rInt();
        for (int q = 0; q < ne; q++) {
            readTok();
            if (tok[0] == 'A') {                  // ARR <rid> <Lin>
                Event e; e.type = EV_ARR; e.a = rInt(); e.b = rInt();
                f.evs.push_back(e);
            } else if (tok[0] == 'T') {           // TDN <server> <spec> <dur>
                Event e; e.type = EV_TDN;
                readTok();
                e.a = (tok[0] == 'E') ? -1 : pInt(tok + 1);
                readTok();                        // P | D
                char cls = tok[0];
                readTok();                        // PRE | PROC | POST
                char kind = tok[1];               // R(E) -> PRE, R(O) -> PROC, O(S) -> POST
                if (cls == 'P') {
                    if (tok[0] == 'P' && tok[1] == 'R' && tok[2] == 'E') { rInt(); rInt(); }
                    else if (tok[2] == 'O' && tok[3] == 'C') { rInt(); rInt(); rInt(); rInt(); }
                    else { rInt(); rInt(); }
                } else {
                    rInt();                       // -1 or remote
                    int m = rInt();
                    for (int z = 0; z < m; z++) rInt();
                }
                (void)kind;
                readTok();                        // dur (unused: durations come from the table)
                f.evs.push_back(e);
            } else if (tok[0] == 'X') {           // XDN <UP|DOWN> <remote> <size> <PRE|DEC> <m> <rid...>
                Event e; e.type = EV_XDN;
                readTok(); e.a = (tok[0] == 'U') ? DIR_UP : DIR_DOWN;
                rInt();                           // remote (rederivable from the request)
                readTok();                        // size in bytes (unused, can exceed int32)
                readTok(); e.b = (tok[0] == 'P') ? KIND_PRE : KIND_DEC;
                int m = rInt();
                e.off = (int)f.ids.size(); e.cnt = m;
                for (int z = 0; z < m; z++) f.ids.push_back(rInt());
                f.evs.push_back(e);
            } else {                              // FIN <rid>
                Event e; e.type = EV_FIN; e.a = rInt();
                f.evs.push_back(e);
            }
        }
        Sch::schedFrame(f.t, f, out);
        emit(out);
    }
    return 0;
}
#endif
