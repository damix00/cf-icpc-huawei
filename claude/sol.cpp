// CF 2251A - Edge-Cloud Collaborative Scheduling (ICPC 2026 Online Challenge 1, Huawei)
//
// SimSelect: the 16109.263 policy, refactored off file-scope globals into a clonable `Sched`, so
// the solution can carry an exact model of its own environment and *choose its policy vector per
// instance, online, by simulating* instead of shipping one globally-fitted compromise.
//
// Why: `claude/tmp/oracle.py judge` measures the shipped constants at 712.212/test on `judge/`,
// the best single fixed policy at 716.001, and the per-instance argmax at **724.444**.  The gap
// between the last two -- +8.4/test that no global constant can reach -- is what this architecture
// is for.  30 of 40 instances prefer a non-default theta and the picks are diverse, which is the
// same pathology the judge showed directly: `waitPost` moves only #19, `holdPreToo` moves six other
// tests and not #19, `mStar = L` wins #19 and loses #7.
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

inline double envD(const char* k, double d) { const char* v = getenv(k); return v ? atof(v) : d; }
inline const char* envS(const char* k, const char* d) { const char* v = getenv(k); return v ? v : d; }

// ===================== the policy vector =====================
// Every instance-level constant the judge has shown to have a large, *test-specific* effect.  The
// defaults are exactly the 16109.263 settings, so a `Sched` built with a default `Theta` reproduces
// the shipped schedule byte for byte.
struct Theta {
    double prefillUrgency = 0.5;   // fraction of SLO1 after which prefill preempts decode
    double pieceMul = 128.0;       // target prefill piece size, in multiples of max(2S, dproc(1))
    int poolTarget = 1 << 30;      // admit new requests until this many are decoding
    // local-computer preference order; digits are 0=D POST, 1=P POST, 2=D PRE, 3=P PRE
    char ordA[8] = {0};            // empty = derive from wTp at init
    char ordD[8] = "0213";
    double ordW = 0.75;            // wTp threshold that selects the P PRE-first admission order

    double waitPost = 32.0;        // D POST merge-hold budget, in merge savings
    double waitProc = 14.0;        // D PROC merge-hold budget on a remote
    double holdCap = 4.0;          // cap on total hold time, in budgets
    double eBottleW = 1.0;         // budget ceiling while the local computer is the bottleneck
    int holdPreToo = 1;            // bit mask of what a D POST hold blocks: 1=D PRE, 2=P POST, 4=P PRE

    double remBusyW = 1.0;         // weight on a remote's already-committed work when placing
    double decW = 1.0;             // weight on a remote's committed decode load when placing
    double dTol = 0.04;            // tie tolerance when widening the decode set
    double warmUp = 100.0;         // S multiples before the utilisation ratios are trusted
    double linkFixedFrac = 0.05;   // batching only pays on a link if latency is this share

    int jitPre = 1, jitProc = 3, jitMode = 1, jitL = 2;
    double jitSlack = 3.0;
    double tpotMargin = 0.75;      // measured-TPOT-slack gate on the P PRE hold
    double tdrGuard = 0.5;         // is mean TDR actually costing anything?
    double arrExpect = 0.0;        // required expected arrivals inside a hold window
    double swapMin = 0.05;         // realised share of the tp span before decode may overtake
    int swapWarm = 8;

    int deferFirst = 0;            // do not open a request's TPOT window into a congested link
    double deferSlo = 1.0;

    bool sjf = true;
    int sjfProc = 1, sjfPost = 0;

    // Cap the merge holds by the run's REMAINING total TPOT budget (0 = off, the shipped
    // behaviour).  Only computable now that tp_base yields the exact token total.
    double tpotCap = 0.0;

    // wave-shape override (0 = let the rate model choose).  The rate model is a compromise too:
    // a per-instance fixed (d, m) beats it by +3.54/test on judge/ (claude/tmp/wave_oracle.txt).
    int fixM = 0, fixD = 0, fixMR = 0;

    // Field-by-field, not memcmp: padding bytes are unspecified and would make equal policies look
    // different, which matters because a duplicate candidate costs a whole forward simulation.
    bool operator==(const Theta& o) const {
        return prefillUrgency == o.prefillUrgency && pieceMul == o.pieceMul && poolTarget == o.poolTarget
            && strcmp(ordA, o.ordA) == 0 && strcmp(ordD, o.ordD) == 0 && ordW == o.ordW
            && waitPost == o.waitPost && waitProc == o.waitProc && holdCap == o.holdCap
            && eBottleW == o.eBottleW && holdPreToo == o.holdPreToo && remBusyW == o.remBusyW
            && decW == o.decW && dTol == o.dTol && warmUp == o.warmUp
            && linkFixedFrac == o.linkFixedFrac && jitPre == o.jitPre && jitProc == o.jitProc
            && jitMode == o.jitMode && jitL == o.jitL && jitSlack == o.jitSlack
            && tpotMargin == o.tpotMargin && tdrGuard == o.tdrGuard && arrExpect == o.arrExpect
            && swapMin == o.swapMin && swapWarm == o.swapWarm && deferFirst == o.deferFirst
            && deferSlo == o.deferSlo && sjf == o.sjf && sjfProc == o.sjfProc && sjfPost == o.sjfPost
            && fixM == o.fixM && fixD == o.fixD && fixMR == o.fixMR && tpotCap == o.tpotCap;
    }

    // Read the shipped defaults from the environment.  Only used to build the *default* theta;
    // the selector's candidates are constructed in code.
    void fromEnv() {
        prefillUrgency = envD("CF_URG", prefillUrgency);
        pieceMul = envD("CF_PIECE", pieceMul);
        poolTarget = (int)envD("CF_POOL", poolTarget);
        const char* oa = envS("CF_ORD_A", "");
        snprintf(ordA, sizeof ordA, "%s", oa);
        snprintf(ordD, sizeof ordD, "%s", envS("CF_ORD_D", "0213"));
        ordW = envD("CF_ORDW", ordW);
        waitPost = envD("CF_WAIT_P", waitPost);
        waitProc = envD("CF_WAIT_R", waitProc);
        holdCap = envD("CF_HOLDCAP", holdCap);
        eBottleW = envD("CF_EBW", eBottleW);
        holdPreToo = (int)envD("CF_HOLDPRE", holdPreToo);
        remBusyW = envD("CF_RBW", remBusyW);
        decW = envD("CF_DECW", decW);
        dTol = envD("CF_DTOL", dTol);
        warmUp = envD("CF_WARM", warmUp);
        linkFixedFrac = envD("CF_LFF", linkFixedFrac);
        jitPre = (int)envD("CF_JITP", jitPre);
        jitProc = (int)envD("CF_JITR", jitProc);
        jitMode = (int)envD("CF_JITM", jitMode);
        jitL = (int)envD("CF_JITL", jitL);
        jitSlack = envD("CF_JITS", jitSlack);
        tpotMargin = envD("CF_TPOTM", tpotMargin);
        tdrGuard = envD("CF_TDRG", tdrGuard);
        arrExpect = envD("CF_ARRE", arrExpect);
        swapMin = envD("CF_SWAP", swapMin);
        swapWarm = (int)envD("CF_SWAPW", swapWarm);
        deferFirst = (int)envD("CF_DEFER", deferFirst);
        deferSlo = envD("CF_DEFSLO", deferSlo);
        sjf = envD("CF_SJF", 1.0) > 0.5;
        sjfProc = (int)envD("CF_SJFP", sjfProc);
        sjfPost = (int)envD("CF_SJFO", sjfPost);
        tpotCap = envD("CF_TPCAP", 0.0);
        fixM = (int)envD("CF_FIXM", 0);
        fixD = (int)envD("CF_FIXD", 0);
        fixMR = (int)envD("CF_FIXMR", 0);
    }
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

// What one merge actually saves on a resource: one schedule cost plus the task's fixed term,
// estimated from the curve itself (2*f(q) - f(2q) is the intercept of an affine f).
inline double mergeSaving(const PLCurve& c, double q, double S) {
    return S + max(0.0, 2 * c.at(q) - c.at(2 * q));
}

// The whole scheduler state, copyable.  Cloning one and stepping the copy forward under a
// hypothetical future is what lets the selector price a policy instead of guessing at it.
struct Sched {
    Params P;
    Table T;
    Theta th;

    // per-request state
    vector<int> st, lin_, rem, layersDone, chunkStep, tokCnt;
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

    // ---- link predictor ---------------------------------------------------------------
    // The interactor is deterministic and every duration is known up front, so we can mirror the
    // two FIFO transfer queues exactly and know when the next batch of work will land.  That turns
    // "should I wait for a bigger group?" from a guess into a comparison of two known times.
    double uPerToken = 0;
    double busyE = 0, busyUp = 0, busyDn = 0;
    double preE = 0, preUp = 0, preDn = 0, preR = 0;   // the prefill share of the above
    double t0 = -1;
    vector<double> busyR;
    double curT = 0;
    double upFreeAt = 0, downFreeAt = 0;
    double eFreeAt = 0;
    vector<double> rFreeAt;
    // `ids` is carried only so the internal simulator can fork a consistent interactor state; the
    // policy itself reads nothing but fin/dec/remote.
    struct Xf { double fin; char dec; int remote; vector<int> ids; };
    deque<Xf> upQ, downQ;

    // ---- controller state ----
    int mStar = 1, mStarR = 1, dStar = 1;
    int lastL = -1, lastLd = -1, sinceRetarget = 0;
    double holdPostSince = -1;
    vector<double> holdProcSince;
    long long tokensOut = 0;
    double pieceTargetMs = 1.0;
    double decWeight = 0;
    // Held as arrays, not pointers into `th`: a `Sched` gets copied to be simulated forward, and a
    // pointer would keep aiming at the original's Theta.
    char ordAdmitS[8] = "1302";
    char ordDecodeS[8] = "0213";

    vector<int> load, decLoad;
    vector<double> pendProc;
    int activeDecode = 0;
    int preOutstanding = 0;
    multiset<int> upstreamLin;

    // running TPOT, measured exactly the way the scorer measures it
    double spanSum = 0;
    long long gapCnt = 0;
    vector<double> lastTok;
    // carried only for the internal simulator's scorer: the two per-request timestamps the score
    // function reads (TPOT's window opens at the FIRST token; TDR's clock stops at P POST).
    vector<double> firstTok, tdrAt;

    // is mean TDR actually costing us anything?
    double tdrSum = 0;
    long long tdrCnt = 0;
    double outArrSum = 0, outCostSum = 0;
    long long outCnt = 0;

    // will waiting actually change the order?
    long long arrCount = 0;
    double firstArr = -1;

    // scratch, per-instance so a clone never shares it
    vector<int> scratchCnt, eligDPre;

    // ---------------------------------------------------------------------------------------
    inline void upIns(int i) { upstreamLin.insert(lin_[i]); }
    inline void upErase(int i) { auto it = upstreamLin.find(lin_[i]); if (it != upstreamLin.end()) upstreamLin.erase(it); }
    inline double tpotNow() const { return gapCnt ? spanSum / (double)gapCnt : 0.0; }

    // The solved total output-token count, pushed in by the selector (0 = not yet known).
    double totTok = 0;

    // How many milliseconds of token gap the whole run can still afford before TPOT passes SLO2.
    //
    // The scorer's TPOT is sum_i(last_i - first_i) / sum_i(L_out_i - 1), and that denominator is
    // exactly T - R: a request with L_out = 1 contributes zero to both the restricted and the full
    // sum.  With T solved from tp_base and R observed, the denominator is KNOWN -- so the total gap
    // the run may spend is SLO2*(T-R), of which spanSum is already gone.  That is a real budget in
    // milliseconds, where tpotNow() is only a running average: biased high early when few gaps have
    // been measured, and unable to say how much room is left.
    //
    // Note this is the opposite of the rule refuted in NOTES 17e.  That one shrank hold budgets
    // wherever the measured TPOT *rate* was high -- the -104.0 mechanism applied selectively.  This
    // one is slack-total: unconstrained early, when the budget is nearly all intact, and binding
    // only as the run actually spends it.
    inline double tpotSlackMs() const {
        if (totTok <= 0 || P.SLO2 <= 0) return 1e300;
        double D = totTok - (double)st.size();          // = sum(L_out - 1) over the whole run
        if (D <= 0) return 1e300;
        return max(0.0, P.SLO2 * D - spanSum);
    }

    // The slack turned into a per-hold ceiling: a hold of x ms delays the next token of every live
    // request, so it costs roughly x per live request out of the shared budget.  Off (infinite)
    // unless the selector picks a theta that switches it on, so the default schedule is untouched.
    inline double tpotCapMs() const {
        if (th.tpotCap <= 0) return 1e300;
        double s = tpotSlackMs();
        if (s >= 1e299) return 1e300;
        return th.tpotCap * s / max(1.0, (double)activeDecode);
    }

    vector<int> xfIds;   // scratch for the id list of the transfer being queued
    inline void pushXfer(bool up, double len, bool dec, int remote) {
        double& freeAt = up ? upFreeAt : downFreeAt;
        double start = max(curT, freeAt);
        double d = P.lat + uPerToken * len;
        (up ? busyUp : busyDn) += d;
        if (!dec) { if (up) preUp += d; else preDn += d; }
        freeAt = start + d;
        (up ? upQ : downQ).push_back({freeAt, (char)dec, remote, xfIds});
        xfIds.clear();
    }
    // When does the next decode transfer that would feed this queue land?  +inf if none is queued.
    inline double nextDecAt(bool up, int remote) {
        double best = 1e300;
        for (auto& p : (up ? upQ : downQ))
            if (p.dec && (remote < 0 || p.remote == remote)) { best = p.fin; break; }
        return max(best, curT);
    }

    // Which resource is the bottleneck right now: -1 = local computer, 0 = a remote, 1 = a link.
    inline int bottleneck() const {
        double r = 0;
        for (int j = 0; j < P.K; j++) r = max(r, busyR[j]);
        double link = max(busyUp, busyDn);
        if (busyE >= r && busyE >= link) return -1;
        return link >= r ? 1 : 0;
    }
    // The wait only costs nothing while the resource being held has something else to do; if it is
    // the bottleneck and it idles, the wait is charged against the whole schedule.
    inline double waitBudget(double base) const { return bottleneck() == -1 ? min(base, th.eBottleW) : base; }

    // Merging two tasks only removes work from a resource that charges a fixed cost per task.  When
    // the link is the bottleneck and its time is nearly all payload (u*m >> lat), a bigger group
    // moves exactly the same bytes and buys nothing while lengthening every round trip.
    inline bool batchingHelpsBottleneck(double m) const {
        double r = 0;
        for (int j = 0; j < P.K; j++) r = max(r, busyR[j]);
        double link = max(busyUp, busyDn);
        if (link > busyE && link > r) return P.lat > th.linkFixedFrac * (P.lat + uPerToken * max(1.0, m));
        return true;
    }

    inline bool arrivalLikely(double window) const {
        if (th.arrExpect <= 0) return true;
        if (arrCount < 2 || firstArr < 0) return true;
        double span = curT - firstArr;
        if (span <= 0) return true;
        double lam = (double)(arrCount - 1) / span;
        return lam * window >= th.arrExpect;
    }
    inline bool tdrWorthIt() const {
        if (th.tdrGuard <= 0) return true;
        long long cnt = tdrCnt + outCnt;
        if (cnt == 0) return true;
        double sum = tdrSum + max((double)outCnt * curT - outArrSum, outCostSum);
        return sum > th.tdrGuard * P.SLO1 * (double)cnt;
    }

    inline void ensureReq(int i) {
        if ((int)st.size() > i) return;
        size_t n = i + 1;
        st.resize(n, R_DONE); lin_.resize(n, 1); rem.resize(n, 0);
        layersDone.resize(n, 0); chunkStep.resize(n, 1); tokCnt.resize(n, 0);
        lastTok.resize(n, 0.0); firstTok.resize(n, 0.0); tdrAt.resize(n, 0.0);
        arrT.resize(n, 0.0); procTotal.resize(n, 0.0); tdrCost.resize(n, 0.0); finished.resize(n, 0);
    }

    void init(const Params& p, const Table& t, const Theta& theta) {
        P = p; T = t; th = theta;
        qPProc.assign(P.K, {});
        qDProc.assign(P.K, {});
        rBusy.assign(P.K, 0);
        rRec.assign(P.K, BusyRec());
        load.assign(P.K, 0);
        decLoad.assign(P.K, 0);
        pendProc.assign(P.K, 0.0);
        // marginal remote cost of carrying one more decoding request, times a nominal remaining Lout
        double slope = max(0.0, (T.c[C_DPROC].at(64.0) - T.c[C_DPROC].at(1.0)) / 63.0);
        decWeight = th.decW * slope;
        uPerToken = 8.0 * P.bytesPerToken / (P.bw * 1e6);
        double dr1 = T.c[C_DPROC].at(1.0);
        // Splitting a prefill costs another S on the remote every time and only buys the chance to
        // slot decode work in between pieces.  Measured across both suites that trade is a loss at
        // every split size, so the default target is large enough to keep prefills whole.
        pieceTargetMs = max(2.0 * P.S, dr1) * th.pieceMul;
        // Which prefill step the local computer runs first is a direct trade between the two score
        // components, and the weights say which one to buy.
        setOrders();
        curT = 0; upFreeAt = 0; downFreeAt = 0;
        upQ.clear(); downQ.clear();
        preOutstanding = 0; upstreamLin.clear();
        mStar = mStarR = 1; dStar = P.K; lastL = -1; lastLd = -1; sinceRetarget = 0;
        preE = preUp = preDn = preR = 0; t0 = -1;
        holdPostSince = -1;
        holdProcSince.assign(P.K, -1.0);
        busyE = busyUp = busyDn = 0; busyR.assign(P.K, 0.0);
        eFreeAt = 0; rFreeAt.assign(P.K, 0.0);
        arrCount = 0; firstArr = -1;
        tdrSum = 0; tdrCnt = 0; outArrSum = 0; outCostSum = 0; outCnt = 0;
        spanSum = 0; gapCnt = 0; lastTok.clear(); firstTok.clear(); tdrAt.clear();
        tokensOut = 0; xfIds.clear();
        activeDecode = 0; pendingTransfers = 0; eBusy = false;
        eRec = BusyRec();
        st.clear(); lin_.clear(); rem.clear(); layersDone.clear(); chunkStep.clear(); tokCnt.clear();
        arrT.clear(); procTotal.clear(); tdrCost.clear(); finished.clear();
        qPPre.clear(); qPPost.clear(); qDPre.clear(); qDPost.clear();
    }

    void setOrders() {
        snprintf(ordAdmitS, sizeof ordAdmitS, "%s", th.ordA[0] ? th.ordA : (P.wTp >= th.ordW ? "3102" : "1302"));
        snprintf(ordDecodeS, sizeof ordDecodeS, "%s", th.ordD);
    }

    // Switch policy mid-run.  Only the knobs are replaced; every piece of measured state stays, so
    // the schedule already built is carried forward rather than re-derived.
    void setTheta(const Theta& theta) {
        th = theta;
        double slope = max(0.0, (T.c[C_DPROC].at(64.0) - T.c[C_DPROC].at(1.0)) / 63.0);
        decWeight = th.decW * slope;
        pieceTargetMs = max(2.0 * P.S, T.c[C_DPROC].at(1.0)) * th.pieceMul;
        setOrders();
        lastL = -1; lastLd = -1; sinceRetarget = 0;   // force a retarget under the new shape
    }

    // Prefill is unavoidable work competing for the same resources, so a resource's usable capacity
    // for decode is what prefill has not already taken.
    inline double avail(double preBusy, double share) const {
        double el = curT - t0;
        // Work is charged when a task is issued, so during the first few milliseconds the charged
        // total exceeds the elapsed time and every resource looks fully occupied.
        if (el <= th.warmUp * P.S) return 1.0;
        return max(0.05, 1.0 - preBusy / (share * el));
    }

    inline double waveRate(double m, double L, int d) const {
        // A wave of m requests can only touch min(m, d) remotes, so that -- not d -- is how many
        // transfers it queues.  Capacity, on the other hand, scales with all d remotes.
        double de = min((double)d, m);
        double mr = max(1.0, m / de);
        double up = de * P.lat + uPerToken * m;
        double dpre = T.c[C_DPRE].at(m), dpost = T.c[C_DPOST].at(m), dproc = T.c[C_DPROC].at(mr);
        double C = 3 * P.S + dpre + dproc + dpost + 2 * up;
        double r = L / C;                                                    // its own round trip
        r = min(r, avail(preE, 1) * m / (2 * P.S + dpre + dpost));           // local computer
        r = min(r, avail(preR, d) * d * mr / (P.S + dproc));                 // the d remotes
        r = min(r, avail(preUp, 1) * m / up);                                // uplink
        r = min(r, avail(preDn, 1) * m / up);                                // downlink
        return r;
    }

    // Re-pick the wave shape whenever the live population changes materially.
    inline void retarget() {
        int L = max(1, activeDecode);
        // Placement is decided at P PRE, long before those requests reach the decode loop, so
        // sizing the wave by the CURRENT decode population would answer "how many remotes?" with
        // L=1 during ramp-up and pin everything onto one remote for the rest of the run.
        int inSys = (int)qPPre.size();
        for (int j = 0; j < P.K; j++) inSys += load[j];
        int Ld = max(L, inSys);
        if (L == lastL && Ld == lastLd && ++sinceRetarget < 64) return;
        lastL = L; lastLd = Ld; sinceRetarget = 0;
        double best = -1;
        for (int d = 1; d <= P.K; d++)
            for (int m = 1; m <= L; m = (m < 8 ? m + 1 : (m * 5) / 4 + 1)) {
                double r = waveRate(min(m, L), L, d);
                if (r > best + 1e-15) { best = r; mStar = min(m, L); }
                if (m >= L) break;
            }
        { double r = waveRate(L, L, dStar); if (r > best + 1e-15) { best = r; mStar = L; } }
        // How wide the decode set should be, judged on the population that will actually be
        // decoding and over the best wave size for each candidate d.
        double bestD = -1;
        for (int d = 1; d <= P.K; d++) {
            double rd = 0;
            for (int m = 1; ; m = (m < 8 ? m + 1 : (m * 5) / 4 + 1)) {
                rd = max(rd, waveRate(min(m, Ld), Ld, d));
                if (m >= Ld) break;
            }
            // Ties go to the WIDER set: within the tolerance the model cannot distinguish the
            // options, and extra remotes buy pipeline depth it does not represent.
            if (rd > bestD * (1 - th.dTol)) { bestD = max(bestD, rd); dStar = d; }
        }
        if (th.fixM > 0) mStar = max(1, min(th.fixM, L));
        if (th.fixD > 0) dStar = max(1, min(th.fixD, P.K));
        mStarR = max(1, mStar / dStar);
        if (th.fixMR > 0) mStarR = th.fixMR;
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

    // Mean TDR is a mean waiting time, and shortest-job-first minimises that.
    inline int peekPPre() {
        if (!th.sjf || qPPre.size() == 1) return qPPre.front();
        size_t bestK = 0;
        for (size_t k = 1; k < qPPre.size(); k++)
            if (tdrCost[qPPre[k]] < tdrCost[qPPre[bestK]]) bestK = k;
        return qPPre[bestK];
    }
    inline int popPPre() {
        if (!th.sjf || qPPre.size() == 1) { int i = qPPre.front(); qPPre.pop_front(); return i; }
        size_t bestK = 0;
        for (size_t k = 1; k < qPPre.size(); k++)
            if (tdrCost[qPPre[k]] < tdrCost[qPPre[bestK]]) bestK = k;
        int i = qPPre[bestK];
        qPPre.erase(qPPre.begin() + bestK);
        return i;
    }

    // Finish time of the last prefill transfer currently queued on this link (-1 if none).  Our own
    // transfer cannot start before it, so until then holding is free.
    inline double lastPreFin(bool up) const {
        double best = -1;
        for (auto& p : (up ? upQ : downQ)) if (!p.dec) best = max(best, p.fin);
        return best;
    }

    // which request this remote would run next -- shortest remote work first
    inline int peekPProc(int j) {
        if (!th.sjfProc || qPProc[j].size() == 1) return qPProc[j].front();
        size_t bk = 0;
        for (size_t k = 1; k < qPProc[j].size(); k++)
            if (procTotal[qPProc[j][k]] < procTotal[qPProc[j][bk]]) bk = k;
        return qPProc[j][bk];
    }

    // Balance by projected remote work, not request count: prefill_proc varies by orders of
    // magnitude with Lin, so counting requests leaves remotes badly skewed.
    inline int pickRemote() {
        int active = 0;
        for (int j = 0; j < P.K; j++) if (load[j] > 0) active++;
        int best = -1;
        double bestCost = 1e300;
        for (int j = 0; j < P.K; j++) {
            // Widening the decode set costs one more transfer latency per wave in each direction.
            if (load[j] == 0 && active >= dStar) continue;
            // pendProc alone ignores two things the remote is already committed to: the task it is
            // running now, and the decode groups already queued on it.
            double cost = pendProc[j] + decWeight * decLoad[j];
            if (th.remBusyW > 0) cost += th.remBusyW * (max(0.0, rFreeAt[j] - curT)
                                      + (qDProc[j].empty() ? 0.0 : P.S + T.c[C_DPROC].at((double)qDProc[j].size())));
            if (cost < bestCost - 1e-9) { bestCost = cost; best = j; }
        }
        if (best < 0) {
            best = 0; bestCost = 1e300;
            for (int j = 0; j < P.K; j++) {
                double cost = pendProc[j] + decWeight * decLoad[j];
                if (th.remBusyW > 0) cost += th.remBusyW * max(0.0, rFreeAt[j] - curT);
                if (cost < bestCost - 1e-9) { bestCost = cost; best = j; }
            }
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
                xfIds.assign(1, i);
                pushXfer(true, lin_[i], false, rem[i]);
                break;
            }
            case ST_PPROC: {
                int i = rec.ids[0];
                pendProc[rem[i]] = max(0.0, pendProc[rem[i]]
                                     - (double)(rec.le - rec.ls) / P.numLayers * procTotal[i] - P.S);
                layersDone[i] = rec.le;
                if (rec.le >= P.numLayers) { st[i] = R_WAIT_PRE_DOWN; pendingTransfers++; xfIds.assign(1, i); pushXfer(false, lin_[i], false, rem[i]); }
                else { st[i] = R_NEED_PPROC; qPProc[rem[i]].push_back(i); }
                break;
            }
            case ST_PPOST: {
                int i = rec.ids[0];
                st[i] = R_NEED_DPRE; qDPre.push_back(i); activeDecode++; decLoad[rem[i]]++;
                preOutstanding--;
                tdrAt[i] = curT;
                tdrSum += curT - arrT[i]; tdrCnt++;
                outCnt--; outArrSum -= arrT[i]; outCostSum -= tdrCost[i];
                break;
            }
            case ST_DPRE: {
                scratchCnt.assign(P.K, 0);
                int distinct = 0;
                for (int i : rec.ids) { st[i] = R_WAIT_DEC_UP; if (!scratchCnt[rem[i]]++) distinct++; }
                pendingTransfers += distinct;
                // one UP per distinct remote, entering the queue in increasing remote index
                for (int j = 0; j < P.K; j++) if (scratchCnt[j]) {
                    xfIds.clear();
                    for (int i : rec.ids) if (rem[i] == j) xfIds.push_back(i);
                    pushXfer(true, scratchCnt[j], true, j);
                }
                break;
            }
            case ST_DPROC: {
                for (int i : rec.ids) st[i] = R_WAIT_DEC_DOWN;
                pendingTransfers++;
                xfIds = rec.ids;
                pushXfer(false, (double)rec.ids.size(), true, server);
                break;
            }
            case ST_DPOST: {
                tokensOut += (long long)rec.ids.size();
                for (int i : rec.ids) {
                    if (tokCnt[i] >= 1) { spanSum += curT - lastTok[i]; gapCnt++; }
                    else firstTok[i] = curT;
                    lastTok[i] = curT;
                    tokCnt[i]++;
                }
                for (int i : rec.ids) if (!finished[i]) { st[i] = R_NEED_DPRE; qDPre.push_back(i); }
                break;
            }
        }
        rec.step = -1;
        rec.ids.clear();
    }

    inline void onTransfer(const Event& e, const vector<int>& ids) {
        pendingTransfers--;
        { auto& q = (e.a == DIR_UP) ? upQ : downQ; if (!q.empty()) q.pop_front(); }
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

    // earliest moment a D PROC still on a remote could deliver its results to qDPost
    inline double nextDownAfterProc() const {
        double best = 1e300;
        for (int j = 0; j < P.K; j++) {
            if (!rBusy[j] || rRec[j].step != ST_DPROC) continue;
            double fin = max(rFreeAt[j], downFreeAt) + P.lat + uPerToken * (double)rRec[j].ids.size();
            best = min(best, fin);
        }
        return best;
    }
    // earliest moment a D PRE still on the local computer could deliver members to qDProc[remote]
    inline double nextUpAfterPre(int remote) const {
        if (!eBusy || eRec.step != ST_DPRE) return 1e300;
        int cnt = 0;
        for (int i : eRec.ids) if (rem[i] == remote) cnt++;
        if (!cnt) return 1e300;
        return max(eFreeAt, upFreeAt) + P.lat + uPerToken * cnt;
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

    inline double taskDur(const Assign& A) const {
        switch (A.step) {
            case ST_PPRE:  return T.c[C_PPRE].at(lin_[A.ids[0]]);
            case ST_PPROC: return (double)(A.le - A.ls) / P.numLayers * T.c[C_PPROC].at(lin_[A.ids[0]]);
            case ST_PPOST: return T.c[C_PPOST].at(lin_[A.ids[0]]);
            case ST_DPRE:  return T.c[C_DPRE].at((double)A.ids.size());
            case ST_DPROC: return T.c[C_DPROC].at((double)A.ids.size());
            default:       return T.c[C_DPOST].at((double)A.ids.size());
        }
    }

    inline void recordBusy(BusyRec& rec, const Assign& A) {
        rec.step = A.step; rec.ls = A.ls; rec.le = A.le;
        rec.ids.assign(A.ids.begin(), A.ids.end());
        double d = P.S + taskDur(A);
        if (A.server < 0) { busyE += d; eFreeAt = curT + d; }
        else { busyR[A.server] += d; rFreeAt[A.server] = curT + d; }
        if (A.step <= ST_PPOST) { if (A.server < 0) preE += d; else preR += d; }
    }

    void frame(double t, const Frame& f, Response& out) { ingest(t, f); decide(out); }

    // Applying the frame's events and choosing the response are split so a *consistent* snapshot
    // can be taken between them: after ingest the mirrored state is exactly the interactor's state
    // at time t, which is what the internal simulator has to fork from.
    void ingest(double t, const Frame& f) {
        curT = t;
        if (t0 < 0) t0 = t;

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
                    lin_[i] = e.b; arrT[i] = t; layersDone[i] = 0; finished[i] = 0; tokCnt[i] = 0;
                    st[i] = R_NEED_PPRE; preOutstanding++;
                    arrCount++; if (firstArr < 0) firstArr = t;
                    planChunks(i);
                    outCnt++; outArrSum += t; outCostSum += tdrCost[i];
                    qPPre.push_back(i);
                    break;
                }
                case EV_TDN: onTaskDone(e.a); break;
                case EV_XDN: onTransfer(e, f.ids); break;
                default: break;
            }
        }
    }

    void decide(Response& out) {
        const double t = curT;
        out.n = 0;

        // how long has the oldest un-admitted request been waiting?
        double prefillWait = 0;
        if (!qPPre.empty())  prefillWait = max(prefillWait, t - arrT[qPPre.front()]);
        if (!qPPost.empty()) prefillWait = max(prefillWait, t - arrT[qPPost.front()]);
        // Admit while the decode pool is below target: bigger pools mean bigger decode groups.
        bool prefillUrgent = activeDecode < th.poolTarget || prefillWait > th.prefillUrgency * P.SLO1;

        retarget();

        // Anything still moving is a guarantee that another frame will arrive, which is what makes
        // it safe to hold a task back.  With nothing in flight, holding would deadlock the run.
        bool inFlight = pendingTransfers > 0;
        for (int j = 0; j < P.K && !inFlight; j++) inFlight = rBusy[j];

        // Waiting for one more D POST member merges two local tasks into one, saving a whole S plus
        // a task's fixed cost.
        bool holdPost = false;
        if (inFlight && !qDPost.empty() && (int)qDPost.size() < mStar && batchingHelpsBottleneck(mStar)) {
            double t1 = min(nextDecAt(false, -1), nextDownAfterProc());
            double budget = waitBudget(th.waitPost) * mergeSaving(T.c[C_DPOST], (double)qDPost.size(), P.S);
            budget = min(budget, tpotCapMs());
            if (holdPostSince < 0) holdPostSince = t;
            if (t1 - t <= budget && t - holdPostSince <= th.holdCap * budget) holdPost = true;
        }
        if (!holdPost) holdPostSince = -1;

        // The uplink cannot start our next prefill transfer before it finishes the current one, so
        // while it is busy for longer than the P PRE takes, dispatching only fixes the FIFO order
        // early.  Hold, and let the queue grow into something worth sorting.
        bool holdPPre = false;
        bool tpotRoom = th.tpotMargin > 0 && P.SLO2 > 0 && gapCnt > 0 && tpotNow() < th.tpotMargin * P.SLO2;
        if (th.jitPre && (activeDecode <= th.jitL || tpotRoom) && tdrWorthIt()
            && !qPPre.empty() && pendingTransfers > 0) {
            int i = peekPPre();
            double lim = th.jitMode ? lastPreFin(true) : upFreeAt;
            if (lim > t + th.jitSlack * (P.S + T.c[C_PPRE].at(lin_[i])) && arrivalLikely(lim - t))
                holdPPre = true;
        }

        // Same argument on the downlink, where the release valve is the LAST P PROC piece.
        int bestProcJ = -1;
        double bestProcLin = 1e300;
        if (th.jitProc) {
            for (int j = 0; j < P.K; j++) {
                if (rBusy[j] || qPProc[j].empty()) continue;
                int i = peekPProc(j);
                if (lin_[i] < bestProcLin) { bestProcLin = lin_[i]; bestProcJ = j; }
            }
        }

        // Which members may enter a D PRE now.  A request that has already produced a token keeps
        // going regardless -- its window is open, so holding it only widens the gap being measured.
        eligDPre.clear();
        bool deferring = false;
        if (th.deferFirst && P.SLO2 > 0) {
            bool preOnLink = (lastPreFin(true) > t) || (lastPreFin(false) > t);
            bool preLeft = (th.deferFirst >= 2) ? (preOutstanding > 0) : false;
            if (preOnLink || preLeft) {
                // what one decode round would cost if started right now, queueing behind the link
                double m = max(1.0, (double)qDPre.size());
                double xf = P.lat + uPerToken * m;
                double round = max(0.0, upFreeAt - t) + xf + max(0.0, downFreeAt - t) + xf
                             + 3 * P.S + T.c[C_DPRE].at(m) + T.c[C_DPROC].at(m) + T.c[C_DPOST].at(m);
                if (round > th.deferSlo * P.SLO2) deferring = true;
            }
        }
        for (int i : qDPre) if (!deferring || tokCnt[i] > 0) eligDPre.push_back(i);

        bool dpreFirst = false;
        if (P.wTp > 0 && tokensOut >= th.swapWarm && curT > t0) {
            double tpNow = (double)tokensOut / (curT - t0);
            double span = P.tpUB - P.tpBase;
            if (span > 1e-12 && (tpNow - P.tpBase) / span > th.swapMin) dpreFirst = true;
        }

        // ---- local computer ----
        if (!eBusy) {
            int choice = -1;   // 0 = D POST, 1 = P POST, 2 = D PRE, 3 = P PRE
            const char* ord = prefillUrgent ? ordAdmitS : ordDecodeS;
            char ordBuf[8];
            if (dpreFirst) {           // swap the two decode entries; the prefill order is untouched
                int n = 0;
                for (; ord[n] && n < 7; n++) ordBuf[n] = ord[n] == '0' ? '2' : ord[n] == '2' ? '0' : ord[n];
                ordBuf[n] = 0;
                ord = ordBuf;
            }
            for (int p = 0; ord[p] && choice < 0; p++) {
                int c = ord[p] - '0';
                if ((c == 0 && !qDPost.empty() && !holdPost) ||
                    (c == 1 && !qPPost.empty() && !(holdPost && (th.holdPreToo & 2))) ||
                    (c == 2 && !eligDPre.empty() && !(holdPost && (th.holdPreToo & 1))) ||
                    (c == 3 && !qPPre.empty() && !holdPPre && !(holdPost && (th.holdPreToo & 4)))) choice = c;
            }
            if (choice == 0) {
                Assign& A = newAssign(out, -1, ST_DPOST, -1);
                takeAll(qDPost, A, R_INFL_DPOST);
                recordBusy(eRec, A); eBusy = true;
            } else if (choice == 1) {
                // same argument for the local computer's prefill-final queue
                int i;
                if (th.sjfPost && qPPost.size() > 1) {
                    size_t bk = 0;
                    for (size_t k = 1; k < qPPost.size(); k++)
                        if (tdrCost[qPPost[k]] < tdrCost[qPPost[bk]]) bk = k;
                    i = qPPost[bk];
                    qPPost.erase(qPPost.begin() + bk);
                } else { i = qPPost.front(); qPPost.pop_front(); }
                Assign& A = newAssign(out, -1, ST_PPOST, rem[i]);
                A.ids.push_back(i); st[i] = R_INFL_PPOST;
                recordBusy(eRec, A); eBusy = true;
            } else if (choice == 2) {
                Assign& A = newAssign(out, -1, ST_DPRE, -1);
                A.ids = eligDPre;
                for (int i : eligDPre) st[i] = R_INFL_DPRE;
                if (eligDPre.size() == qDPre.size()) qDPre.clear();
                else {
                    vector<int> keep;
                    for (int i : qDPre) if (st[i] != R_INFL_DPRE) keep.push_back(i);
                    qDPre.swap(keep);
                }
                recordBusy(eRec, A); eBusy = true;
            } else if (choice == 3) {
                int i = popPPre();
                int j = pickRemote();
                rem[i] = j; load[j]++;
                pendProc[j] += procTotal[i] + P.S * ceil((double)P.numLayers / chunkStep[i]);
                Assign& A = newAssign(out, -1, ST_PPRE, j);
                A.ids.push_back(i); st[i] = R_INFL_PPRE; upIns(i);
                recordBusy(eRec, A); eBusy = true;
            }
        }

        // ---- remote computers ----
        for (int j = 0; j < P.K; j++) {
            if (rBusy[j]) continue;
            bool doDecode = !qDProc[j].empty();
            // same trade-off on the remote: a bigger D PROC costs one S instead of two
            if (doDecode && inFlight && (int)qDProc[j].size() < mStarR && batchingHelpsBottleneck(mStarR)) {
                double t1 = min(nextDecAt(true, j), nextUpAfterPre(j));
                double budget = th.waitProc * mergeSaving(T.c[C_DPROC], (double)qDProc[j].size(), P.S);
                budget = min(budget, tpotCapMs());
                if (holdProcSince[j] < 0) holdProcSince[j] = t;
                if (t1 - t <= budget && t - holdProcSince[j] <= th.holdCap * budget) doDecode = false;
                else holdProcSince[j] = -1;
            } else holdProcSince[j] = -1;
            if (doDecode && prefillUrgent && !qPProc[j].empty()) doDecode = false;
            if (doDecode) {
                Assign& A = newAssign(out, j, ST_DPROC, j);
                takeAll(qDProc[j], A, R_INFL_DPROC);
                recordBusy(rRec[j], A); rBusy[j] = 1;
            } else if (!qPProc[j].empty()) {
                // TDR is a mean completion time over requests, and on a saturated remote the mean
                // is minimised by shortest-processing-time first.
                int i = peekPProc(j);
                // Splitting only pays for itself when decode work on this remote needs to
                // interleave; otherwise the extra S per piece is pure loss.
                int stepSz = decLoad[j] > 0 ? chunkStep[i] : P.numLayers;
                int le = min(P.numLayers, layersDone[i] + stepSz);
                bool last = (le >= P.numLayers);
                bool hold = false;
                if (last && th.jitProc && pendingTransfers > 0) {
                    double dur = (double)(le - layersDone[i]) / P.numLayers * procTotal[i];
                    double lim = th.jitMode ? lastPreFin(false) : downFreeAt;
                    bool blocked = lim > t + th.jitSlack * (P.S + dur);
                    // Is anything shorter still able to take the slot instead?  Without a shorter
                    // rival, holding reorders nothing and merely idles a remote.
                    bool rival = !upstreamLin.empty() && *upstreamLin.begin() < lin_[i];
                    if (th.jitProc == 1) hold = blocked || j != bestProcJ;
                    else if (th.jitProc == 2) hold = (j != bestProcJ);
                    else if (th.jitProc == 3) hold = (blocked && rival) || (j != bestProcJ);
                    else if (th.jitProc == 4) hold = (blocked && rival);
                }
                if (!hold) {
                    for (size_t k = 0; k < qPProc[j].size(); k++)
                        if (qPProc[j][k] == i) { qPProc[j].erase(qPProc[j].begin() + k); break; }
                    Assign& A = newAssign(out, j, ST_PPROC, j);
                    A.ls = layersDone[i]; A.le = le;
                    A.ids.push_back(i); st[i] = R_INFL_PPROC;
                    if (le >= P.numLayers) upErase(i);
                    recordBusy(rRec[j], A); rBusy[j] = 1;
                }
            }
        }
    }
};

// ===================== the exact internal simulator =====================
// Everything this needs is handed to us in the startup block: the six-column duration table, S,
// lat, u = 8*bytes_per_token/(bw*1e6), num_layers, and every scoring constant.  So the environment
// is not a black box -- it is a function we can evaluate.  The only hidden quantities are future
// arrivals and L_out, which `Future` supplies.  Forking the live scheduler and running it forward
// in here is what turns "which policy suits this instance?" from a guess into a measurement.

// The instance as currently believed: observed history for the requests already seen, projection
// for the rest.
struct Future {
    int R = 0;
    vector<double> arr;
    vector<int> Lin, Lout;
};

struct Pred {
    bool ok = false;
    double score = 0, tp = 0, tdr = 0, tpot = 0, elapsed = 0;
};

struct Engine {
    Params P;
    Table T;
    double u = 0;
    int R = 0;

    // interactor-side request state; the state codes are the same enum the policy mirrors
    vector<int> qs, qRem, qLay, qTok, qLin, qLout;
    vector<double> qArr, qFirst, qLast, qTdr;
    int finishedCount = 0;
    bool eBusy = false;
    vector<char> rBusy;
    double upFree = 0, downFree = 0;

    struct Task { int server, step, remote, ls, le; vector<int> ids; };
    struct Xfer { int dir, remote, kind; vector<int> ids; };
    struct Ev {
        double t; long long seq; int kind, idx;
        bool operator<(const Ev& o) const { return t != o.t ? t > o.t : seq > o.seq; }
    };
    vector<Task> tasks;
    vector<Xfer> xfers;
    priority_queue<Ev> pq;
    long long seqCtr = 0;

    inline double xferTime(double len) const { return P.lat + u * len; }
    inline void pushEv(double t, int kind, int idx) { pq.push(Ev{t, seqCtr++, kind, idx}); }

    inline void queueXfer(double q, int dir, int remote, int kind, double len, const vector<int>& ids) {
        double& linkFree = (dir == DIR_UP) ? upFree : downFree;
        double start = max(q, linkFree);
        double fin = start + xferTime(len);
        linkFree = fin;
        xfers.push_back(Xfer{dir, remote, kind, ids});
        pushEv(fin, 2, (int)xfers.size() - 1);
    }

    inline double stepDur(int step, const vector<int>& ids, int ls, int le) const {
        switch (step) {
            case ST_PPRE:  return T.c[C_PPRE].at((double)qLin[ids[0]]);
            case ST_PPROC: return (double)(le - ls) / P.numLayers * T.c[C_PPROC].at((double)qLin[ids[0]]);
            case ST_PPOST: return T.c[C_PPOST].at((double)qLin[ids[0]]);
            case ST_DPRE:  return T.c[C_DPRE].at((double)ids.size());
            case ST_DPROC: return T.c[C_DPROC].at((double)ids.size());
            default:       return T.c[C_DPOST].at((double)ids.size());
        }
    }

    // The policy is trusted (the real interactor already validates it), so this only guards the
    // cases that would corrupt the simulation rather than merely lose points.
    inline bool startTask(double t, const Assign& A) {
        int srv = A.server;
        if (srv < -1 || srv >= P.K || A.ids.empty()) return false;
        if (srv < 0 ? eBusy : (bool)rBusy[srv]) return false;
        if (srv < 0) eBusy = true; else rBusy[srv] = 1;
        if (A.step == ST_PPRE) qRem[A.ids[0]] = A.remote;
        tasks.push_back(Task{srv, A.step, A.remote, A.ls, A.le, A.ids});
        pushEv(t + P.S + stepDur(A.step, A.ids, A.ls, A.le), 1, (int)tasks.size() - 1);
        int running = (A.step == ST_PPRE)  ? R_INFL_PPRE  : (A.step == ST_PPROC) ? R_INFL_PPROC :
                      (A.step == ST_PPOST) ? R_INFL_PPOST : (A.step == ST_DPRE)  ? R_INFL_DPRE  :
                      (A.step == ST_DPROC) ? R_INFL_DPROC : R_INFL_DPOST;
        for (int i : A.ids) qs[i] = running;
        return true;
    }

    inline void applyTaskDone(double t, int ti, Frame& f) {
        Task& tk = tasks[ti];
        if (tk.server < 0) eBusy = false; else rBusy[tk.server] = 0;
        Event ev; ev.type = EV_TDN; ev.a = tk.server;
        f.evs.push_back(ev);
        switch (tk.step) {
            case ST_PPRE: {
                int i = tk.ids[0];
                qs[i] = R_WAIT_PRE_UP;
                queueXfer(t, DIR_UP, qRem[i], KIND_PRE, (double)qLin[i], tk.ids);
                break;
            }
            case ST_PPROC: {
                int i = tk.ids[0];
                qLay[i] = tk.le;
                if (tk.le >= P.numLayers) {
                    qs[i] = R_WAIT_PRE_DOWN;
                    queueXfer(t, DIR_DOWN, qRem[i], KIND_PRE, (double)qLin[i], tk.ids);
                } else qs[i] = R_NEED_PPROC;
                break;
            }
            case ST_PPOST: {
                int i = tk.ids[0];
                qs[i] = R_NEED_DPRE;
                qTdr[i] = t;
                break;
            }
            case ST_DPRE: {
                for (int i : tk.ids) qs[i] = R_WAIT_DEC_UP;
                for (int j = 0; j < P.K; j++) {      // one UP per remote, increasing remote index
                    scratch.clear();
                    for (int i : tk.ids) if (qRem[i] == j) scratch.push_back(i);
                    if (!scratch.empty()) queueXfer(t, DIR_UP, j, KIND_DEC, (double)scratch.size(), scratch);
                }
                break;
            }
            case ST_DPROC: {
                for (int i : tk.ids) qs[i] = R_WAIT_DEC_DOWN;
                queueXfer(t, DIR_DOWN, tk.remote, KIND_DEC, (double)tk.ids.size(), tk.ids);
                break;
            }
            case ST_DPOST: {
                for (int i : tk.ids) {
                    qTok[i]++;
                    if (qTok[i] == 1) qFirst[i] = t;
                    qLast[i] = t;
                    if (qTok[i] >= qLout[i]) {
                        qs[i] = R_DONE;
                        finishedCount++;
                        Event fe; fe.type = EV_FIN; fe.a = i;
                        f.evs.push_back(fe);
                    } else qs[i] = R_NEED_DPRE;
                }
                break;
            }
        }
        tk.ids.clear();
    }

    inline void applyXferDone(double t, int xi, Frame& f) {
        Xfer& x = xfers[xi];
        Event ev; ev.type = EV_XDN; ev.a = x.dir; ev.b = x.kind;
        ev.off = (int)f.ids.size(); ev.cnt = (int)x.ids.size();
        for (int i : x.ids) f.ids.push_back(i);
        f.evs.push_back(ev);
        if (x.kind == KIND_PRE) qs[x.ids[0]] = (x.dir == DIR_UP) ? R_NEED_PPROC : R_NEED_PPOST;
        else for (int i : x.ids) qs[i] = (x.dir == DIR_UP) ? R_NEED_DPROC : R_NEED_DPOST;
        x.ids.clear();
    }

    vector<int> scratch;

    // Fork the interactor's state at the instant `sc` has just ingested a frame.  Every field below
    // is one the policy already mirrors exactly for its own link predictor -- which is why this is
    // a fork and not an approximation.
    void seed(const Sched& sc, const Future& fu) {
        P = sc.P; T = sc.T; u = sc.uPerToken;
        R = fu.R;
        qLin = fu.Lin; qLout = fu.Lout; qArr = fu.arr;
        qs.assign(R, -1);
        qRem.assign(R, 0); qLay.assign(R, 0); qTok.assign(R, 0);
        qFirst.assign(R, 0.0); qLast.assign(R, 0.0); qTdr.assign(R, 0.0);
        finishedCount = 0;
        tasks.clear(); xfers.clear(); seqCtr = 0;
        while (!pq.empty()) pq.pop();

        int nSeen = (int)sc.st.size();
        for (int i = 0; i < R; i++) {
            if (i < nSeen) {
                qs[i] = sc.finished[i] ? R_DONE : sc.st[i];
                qRem[i] = sc.rem[i]; qLay[i] = sc.layersDone[i]; qTok[i] = sc.tokCnt[i];
                qFirst[i] = sc.firstTok[i]; qLast[i] = sc.lastTok[i]; qTdr[i] = sc.tdrAt[i];
                if (qs[i] == R_DONE) finishedCount++;
            } else {
                pushEv(fu.arr[i], 0, i);
            }
        }
        eBusy = sc.eBusy; rBusy = sc.rBusy;
        upFree = sc.upFreeAt; downFree = sc.downFreeAt;

        // in-flight tasks: their completion times are already known exactly
        if (sc.eBusy && sc.eRec.step >= 0) {
            tasks.push_back(Task{-1, sc.eRec.step, -1, sc.eRec.ls, sc.eRec.le, sc.eRec.ids});
            pushEv(sc.eFreeAt, 1, (int)tasks.size() - 1);
        }
        for (int j = 0; j < P.K; j++)
            if (sc.rBusy[j] && sc.rRec[j].step >= 0) {
                tasks.push_back(Task{j, sc.rRec[j].step, j, sc.rRec[j].ls, sc.rRec[j].le, sc.rRec[j].ids});
                pushEv(sc.rFreeAt[j], 1, (int)tasks.size() - 1);
            }
        // queued transfers, in FIFO order on each link
        for (auto& x : sc.upQ) {
            xfers.push_back(Xfer{DIR_UP, x.remote, x.dec ? KIND_DEC : KIND_PRE, x.ids});
            pushEv(x.fin, 2, (int)xfers.size() - 1);
        }
        for (auto& x : sc.downQ) {
            xfers.push_back(Xfer{DIR_DOWN, x.remote, x.dec ? KIND_DEC : KIND_PRE, x.ids});
            pushEv(x.fin, 2, (int)xfers.size() - 1);
        }
    }

    // Run the forked scheduler to completion and score it exactly the way sim.cpp does.
    Pred run(Sched sc, clock_t deadline) {
        Pred res;
        Frame f;
        Response out;
        long long guard = 0;
        // The fork is taken between ingest and decide, so the clone still owes the *current* frame's
        // decision.  Without this the simulation skips straight to the next event -- losing a
        // dispatch, and deadlocking outright whenever nothing happened to be in flight.
        sc.decide(out);
        for (int k = 0; k < out.n; k++) startTask(sc.curT, out.a[k]);
        while (finishedCount < R) {
            if (pq.empty()) return res;                      // stuck: not a usable prediction
            if (((++guard) & 63) == 0 && clock() > deadline) return res;
            double t = pq.top().t;
            f.clear();
            f.t = t;
            while (!pq.empty() && pq.top().t == t) {
                Ev e = pq.top(); pq.pop();
                if (e.kind == 0) {
                    qs[e.idx] = R_NEED_PPRE;
                    Event ev; ev.type = EV_ARR; ev.a = e.idx; ev.b = qLin[e.idx];
                    f.evs.push_back(ev);
                } else if (e.kind == 1) applyTaskDone(t, e.idx, f);
                else applyXferDone(t, e.idx, f);
            }
            sc.frame(t, f, out);
            for (int k = 0; k < out.n; k++) startTask(t, out.a[k]);
        }

        double earliest = qArr.empty() ? 0.0 : *min_element(qArr.begin(), qArr.end());
        double lastTok = 0, tdrSum = 0, gapSum = 0;
        long long totalTokens = 0, gapCnt = 0;
        for (int i = 0; i < R; i++) {
            lastTok = max(lastTok, qLast[i]);
            totalTokens += qLout[i];
            tdrSum += qTdr[i] - qArr[i];
            if (qLout[i] > 1) { gapSum += qLast[i] - qFirst[i]; gapCnt += qLout[i] - 1; }
        }
        res.elapsed = lastTok - earliest;
        res.tp = res.elapsed > 0 ? (double)totalTokens / res.elapsed : 0;
        res.tdr = R ? tdrSum / R : 0;
        res.tpot = gapCnt ? gapSum / (double)gapCnt : 0;
        double den = P.tpUB - P.tpBase;
        double tpComp = den > 0 ? max(0.0, min(1.0, (res.tp - P.tpBase) / den)) : 0.0;
        double exTdr = max(0.0, (res.tdr - P.SLO1) / P.SLO1);
        double exTpot = max(0.0, (res.tpot - P.SLO2) / P.SLO2);
        double dist = sqrt(exTdr * exTdr + exTpot * exTpot);
        double waitComp = (P.distBase > 0) ? max(0.0, 1.0 - dist / P.distBase) : (dist == 0.0 ? 1.0 : 0.0);
        res.score = 1000.0 * (P.wTp * tpComp + P.wC * waitComp);
        res.ok = true;
        return res;
    }
};

// ===================== belief: the instance, as currently observed =====================
// Two quantities are hidden: future arrivals and L_out.  Neither has to be predicted well -- the
// selector only needs the *ranking* of policies to be right, and a policy that wins under both
// arrival scenarios is the robust choice regardless of which one turns out to hold.

struct Belief {
    // ---- solving for the hidden total output-token count ----------------------------------
    // NOTES 19 closed the whole "schedule by remaining work" family on the grounds that L_out is
    // never revealed.  The *total* is: `tp_base` is handed to us at startup and is by definition the
    // throughput of the REFERENCE schedule, which the statement fixes as strictly one request at a
    // time, whole prefill piece, decode groups of 1.  That schedule's makespan is therefore a known
    // function of the arrival times, the L_in values (both observed) and the single unknown
    // T = sum(L_out):
    //
    //     fin = max(fin, arr[i]) + prefillPath(L_in[i]) + L_out[i]*Cdec ,  tp_base = T/(fin - arr[0])
    //
    // which is monotone in T, so bisection inverts it.  Measured over all 158 local instances
    // (claude/tmp/infer.py): median error 0.000 %, within 1 % on 153 of them.  Splitting T evenly
    // is exact whenever the reference never idles waiting for an arrival -- only the sum enters --
    // and a good approximation when it does.
    static double totalTokens(const Sched& sc, const vector<double>& arr, const vector<int>& Lin) {
        const Params& P = sc.P;
        const Table& T = sc.T;
        int R = (int)arr.size();
        if (R <= 0 || P.tpBase <= 0) return 0.0;
        double u = sc.uPerToken;
        double Cdec = 3 * P.S + T.c[C_DPRE].at(1.0) + T.c[C_DPROC].at(1.0) + T.c[C_DPOST].at(1.0)
                    + 2 * (P.lat + u);
        if (Cdec <= 0) return 0.0;
        static vector<double> path;
        path.resize(R);
        for (int i = 0; i < R; i++) {
            double L = Lin[i];
            path[i] = 3 * P.S + T.c[C_PPRE].at(L) + T.c[C_PPROC].at(L) + T.c[C_PPOST].at(L)
                    + 2 * (P.lat + u * L);
        }
        auto tpOf = [&](double tot) {
            double per = tot / R;
            double fin = arr[0];
            for (int i = 0; i < R; i++) fin = max(fin, arr[i]) + path[i] + per * Cdec;
            double el = fin - arr[0];
            return el > 0 ? tot / el : 0.0;
        };
        double lo = 1e-9, hi = 1.0;
        for (int it = 0; it < 80 && tpOf(hi) < P.tpBase; it++) hi *= 2.0;
        for (int it = 0; it < 60; it++) {
            double mid = 0.5 * (lo + hi);
            if (tpOf(mid) < P.tpBase) lo = mid; else hi = mid;
        }
        return 0.5 * (lo + hi);
    }

    // DIAGNOSTIC ONLY (CF_TRUTH=<testfile>): hand the projection the real instance.  This is what
    // separates the three ways the selector can be wrong -- an inexact engine, a wrong belief, or
    // selection bias over many candidates -- and is never active on the judge.
    static bool truth(Future& fu) {
        const char* path = getenv("CF_TRUTH");
        if (!path || !*path) return false;
        static Future cache;
        static bool loaded = false, okFlag = false;
        if (!loaded) {
            loaded = true;
            FILE* fp = fopen(path, "r");
            if (fp) {
                double d; int k, nl, N, R;
                if (fscanf(fp, "%d %lf %lf %lf %lf %d", &k, &d, &d, &d, &d, &nl) == 6 &&
                    fscanf(fp, "%lf %lf %lf %lf %lf %lf %lf", &d, &d, &d, &d, &d, &d, &d) == 7 &&
                    fscanf(fp, "%d", &N) == 1) {
                    for (int r = 0; r < N; r++) for (int c = 0; c < 7; c++) if (fscanf(fp, "%lf", &d) != 1) break;
                    if (fscanf(fp, "%d", &R) == 1) {
                        cache.R = R; cache.arr.resize(R); cache.Lin.resize(R); cache.Lout.resize(R);
                        okFlag = true;
                        for (int i = 0; i < R; i++)
                            if (fscanf(fp, "%lf %d %d", &cache.arr[i], &cache.Lin[i], &cache.Lout[i]) != 3) { okFlag = false; break; }
                    }
                }
                fclose(fp);
            }
        }
        if (!okFlag) return false;
        fu = cache;
        return true;
    }

    // scenario 0: no further arrivals.  scenario 1: they continue at the observed rate for as long
    // again as they have already been running.
    static void project(const Sched& sc, int scenario, Future& fu, unsigned seed = 0) {
        if (truth(fu)) return;
        int n = (int)sc.st.size();                 // requests seen so far
        double t = sc.curT;

        fu.arr.clear(); fu.Lin.clear(); fu.Lout.clear();
        double linLogSum = 0;
        for (int i = 0; i < n; i++) {
            fu.arr.push_back(sc.arrT[i]);
            fu.Lin.push_back(sc.lin_[i]);
            linLogSum += log((double)max(1, sc.lin_[i]));
        }
        if (scenario == 1 && n >= 2 && sc.firstArr >= 0) {
            double span = t - sc.firstArr;
            if (span > 0) {
                double lam = (double)(n - 1) / span;                     // observed arrival rate
                int extra = min(n, (int)llround(lam * span));            // bounded: never more than doubles R
                extra = min(extra, 64);
                int meanLin = max(1, (int)llround(exp(linLogSum / max(1, n))));
                for (int q = 1; q <= extra; q++) {
                    fu.arr.push_back(t + q / max(lam, 1e-12));
                    fu.Lin.push_back(meanLin);
                }
            }
        }
        int R = (int)fu.arr.size();
        fu.R = R;
        fu.Lout.assign(R, 1);

        // ---- distribute the solved total over the requests ----
        double Ttot = totalTokens(sc, fu.arr, fu.Lin);
        long long emitted = 0, fixed = 0;
        for (int i = 0; i < n; i++) if (sc.finished[i]) fixed += max(1, sc.tokCnt[i]);
        for (int i = 0; i < n; i++) emitted += sc.tokCnt[i];
        (void)emitted;

        // A request only shows its length by stopping, so the *shape* of the split still needs a
        // prior: under L_out ~ U(1, M) the expected remainder falls as tokens are emitted.  M comes
        // from the largest token count over every request that has decoded (running ones included
        // -- taking finished requests alone is biased short, since short ones finish first).
        int maxTok = 1, nDec = 0;
        for (int i = 0; i < n; i++) if (sc.tokCnt[i] > 0) { nDec++; maxTok = max(maxTok, sc.tokCnt[i]); }
        double M = max(2.0, nDec ? (double)maxTok * (nDec + 1.0) / nDec : 2.0);

        double wsum = 0;
        static vector<double> w;
        w.assign(R, 0.0);
        // seed != 0 perturbs the SHAPE of the split while the rescale below keeps the solved total
        // exact.  MEASURED AND OFF BY DEFAULT (CF_SELNS=0): averaging candidates over sampled splits
        // was meant to stop a single point estimate producing the losers, and instead cost judge/
        // -4.6/test and created a -66.3 instance.  Perturbing the split blurs the ranking rather
        // than robustifying it -- the split is not where the belief error that matters lives.
        unsigned long long rs = (unsigned long long)seed * 0x9E3779B97F4A7C15ull + 12345ull;
        auto rnd = [&]() {
            rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
            return (double)((rs >> 11) & ((1ull << 53) - 1)) / (double)(1ull << 53);
        };
        for (int i = 0; i < R; i++) {
            if (i < n && sc.finished[i]) continue;
            int k = (i < n) ? sc.tokCnt[i] : 0;
            w[i] = max(1.0, M - k);
            if (seed) w[i] = max(0.05, w[i] * (0.25 + 1.5 * rnd()));
            wsum += w[i];
        }
        // Self-consistency floor: we have already emitted `tokensOut` tokens and every unfinished
        // request owes at least one more, so any estimate below that is provably too low.  This is
        // free evidence and it is what stops a half-arrived instance from being simulated as if it
        // were nearly over.
        long long owed = 0;
        for (int i = 0; i < R; i++) if (!(i < n && sc.finished[i])) owed++;
        Ttot = max(Ttot, (double)(sc.tokensOut + owed));

        double rem = max(0.0, Ttot - (double)fixed);
        for (int i = 0; i < R; i++) {
            if (i < n && sc.finished[i]) { fu.Lout[i] = max(1, sc.tokCnt[i]); continue; }
            int k = (i < n) ? sc.tokCnt[i] : 0;
            double share = wsum > 0 ? rem * w[i] / wsum : 1.0;
            // a live request has not stopped, so it owes at least one more token
            fu.Lout[i] = max(k + 1, (int)llround(share));
        }
    }

    // Have arrivals plausibly stopped?  If the gap since the last one is several times the mean
    // inter-arrival gap, scenario 1 is not worth simulating.
    static bool arrivalsLikelyOver(const Sched& sc) {
        int n = (int)sc.st.size();
        // two arrivals close together followed by any pause look "over" and are not; require a few
        if (n < 4 || sc.firstArr < 0) return false;
        double lastArr = 0;
        for (int i = 0; i < n; i++) lastArr = max(lastArr, sc.arrT[i]);
        double meanGap = (lastArr - sc.firstArr) / (n - 1);
        return meanGap > 0 && (sc.curT - lastArr) > 3.0 * meanGap;
    }
};

// ===================== the selector =====================
// Every candidate below is a policy that won *some* instance outright in the offline oracle sweep
// (claude/tmp/oracle.py over judge/, hold/ and val/).  No single one of them is a good global
// default -- `wp1` is the setting that cost -104.0 on the judge when shipped globally, and it is
// still the best policy on three of the 40 judge/ instances.  That is the whole point: the choice
// belongs to the instance, not to the constant.

struct Selector {
    vector<Theta> cands;
    vector<const char*> names;
    int chosen = 0;
    long long nextTok = 4;
    long long nFrames = 0, nextFrame = 4;
    int rounds = 0;
    bool arrSel = false, enoughSel = false;
    int nSwitch = 0, maxSwitch = 99;
    bool on = true, dbg = false, predOnly = false;
    clock_t totalBudget = 0, runBudget = 0, spent = 0, wallCeiling = 0;
    const Params* par = nullptr;
    int maxRounds = 6, keep = 6;
    // Picking the argmax of 40+ candidates on an estimate is optimistically biased, so a rival must
    // clear the incumbent by a margin -- and by a wider one while the token total is still only a
    // lower bound.  A tie always resolves to the shipped default, the one setting with a judge
    // measurement behind it.
    double margin = 1.0, shakyMargin = 15.0;
    // On plateaus, not spikes (root CLAUDE.md rule 4): CF_SELMA 4..12 was byte-identical before
    // the first decision became decisive and CF_SELFZ 8 and 99 are within 0.004 of each other now.
    int freezeAfter = 8, baseAnchor = 1, minArr = 8, nSplit = 0;

    // Candidates are built around an ANCHOR, not around the shipped default, so successive rounds
    // are coordinate descent over the product space rather than 40 independent single-knob probes.
    // The per-instance oracle in claude/tmp/oracle.py only ever varied one knob at a time; the real
    // optimum is a combination, and this is how it gets reached without a combinatorial sweep.
    // cands[0] is always the shipped default -- it is the reference every deviation must beat.
    void build(const Theta& anchor, const Params& P) {
        const Theta& base = anchor;
        cands.clear(); names.clear();
        // Slots 0 and 1 are reserved for the default and the applied policy and are never deduped;
        // everything after them is dropped if it duplicates a candidate already listed.
        auto add = [&](const char* nm, const Theta& th) {
            for (size_t q = 0; q < cands.size(); q++) if (cands[q] == th) return;
            cands.push_back(th); names.push_back(nm);
        };
        cands.push_back(baseTh);  names.push_back("base");
        cands.push_back(anchor);  names.push_back("cur");
        // admission order on the local computer
        for (const char* o : {"1302", "3102", "1320", "0132", "3012"}) {
            Theta th = base; snprintf(th.ordA, sizeof th.ordA, "%s", o); add(o, th);
        }
        // the P PRE just-in-time uplink hold: the mechanism worth +55 on the judge, but its gate
        for (int v : {0, 1, 2, 4, 1000}) { Theta th = base; th.jitL = v; add("jitl", th); }
        for (int v : {0, 1}) { Theta th = base; th.jitPre = v; add("jitp", th); }
        // the D POST merge-hold budget: a judge-measured gradient with two opposing tests on it
        for (double v : {1.0, 2.0, 4.0, 8.0, 32.0}) { Theta th = base; th.waitPost = v; add("wp", th); }
        for (double v : {1.0, 4.0, 14.0}) { Theta th = base; th.waitProc = v; add("wr", th); }
        for (int v : {0, 1, 3}) { Theta th = base; th.holdPreToo = v; add("hp", th); }
        // first-token deferral
        for (int v : {0, 2}) { Theta th = base; th.deferFirst = v; add("defer", th); }
        // placement / spread
        for (double v : {0.0, 0.04, 0.2, 0.5}) { Theta th = base; th.dTol = v; add("dtol", th); }
        for (double v : {0.0, 1.0}) { Theta th = base; th.decW = v; add("decw", th); }
        for (double v : {0.0, 0.5}) { Theta th = base; th.tdrGuard = v; add("tdrg", th); }
        for (int v : {4, 1 << 30}) { Theta th = base; th.poolTarget = v; add("pool", th); }
        for (double v : {1.0, 4.0, 128.0}) { Theta th = base; th.pieceMul = v; add("piece", th); }
        // fixed wave shape: the rate model is itself a compromise, and a per-instance (d, m) beats
        // it by +3.54/test on judge/ (claude/tmp/wave_oracle.txt)
        for (int d : {1, 2, 4, 8}) {
            if (d > P.K) break;
            for (int m : {1, 2, 4, 8, 16}) {
                Theta th = base; th.fixD = d; th.fixM = m; add("dm", th);
            }
        }
        { Theta th = base; th.fixD = 0; th.fixM = 0; add("dmoff", th); }
        // the global TPOT budget cap -- speculative, and safe precisely because it is a candidate:
        // an instance only runs it if the exact simulator says it is better there
        for (double v : {0.0, 0.5, 1.0, 2.0}) { Theta th = base; th.tpotCap = v; add("tpcap", th); }
    }

    Theta baseTh, curTh;

    void init(const Theta& base, const Params& P) {
        on = envD("CF_SEL", 1.0) > 0.5;
        dbg = envD("CF_SELDBG", 0.0) > 0.5;
        predOnly = envD("CF_SELPRED", 0.0) > 0.5;
        maxRounds = (int)envD("CF_SELR", 8);
        keep = (int)envD("CF_SELK", 6);
        margin = envD("CF_SELM", 1.0);
        shakyMargin = envD("CF_SELSM", 15.0);
        freezeAfter = (int)envD("CF_SELFZ", 8);
        baseAnchor = (int)envD("CF_SELBA", 1);
        minArr = (int)envD("CF_SELMA", 8);
        nSplit = (int)envD("CF_SELNS", 0);
        nextTok = (long long)envD("CF_SELT", 4);
        double budgetSec = envD("CF_SELB", 4.0);
        totalBudget = (clock_t)(budgetSec * CLOCKS_PER_SEC);
        wallCeiling = (clock_t)(envD("CF_SELWC", 9.0) * CLOCKS_PER_SEC);
        chosen = 0;
        rounds = 0;
        arrSel = false; enoughSel = false; nSwitch = 0;
        maxSwitch = (int)envD("CF_SELMS", 99);
        nFrames = 0;
        nextFrame = (long long)envD("CF_SELF", 4);
        spent = 0;
        baseTh = base; curTh = base;
        par = &P;
        build(curTh, P);
        // No single candidate may eat the whole allowance: on a 24-million-millisecond instance one
        // forward run costs seconds, and a round that spends its entire budget on the first few
        // candidates is choosing from an arbitrary subset.
        double runSec = envD("CF_SELRB", 0.0);
        runBudget = runSec > 0 ? (clock_t)(runSec * CLOCKS_PER_SEC)
                               : max((clock_t)(0.01 * CLOCKS_PER_SEC),
                                     (clock_t)(totalBudget / (2 * (clock_t)max<size_t>(1, cands.size()))));
    }

    // Called between ingest and decide, so the fork is a consistent snapshot of time `t`.
    void maybeSelect(Sched& sc) {
        if (!on || rounds >= maxRounds || cands.size() < 2) return;
        if (sc.st.empty()) return;
        // The token total is solved from tp_base and the ARRIVALS -- no decode history is needed --
        // so the moment arrivals stop the belief is exact.  Fire then, because most of what the
        // candidates disagree about (admission order, the P PRE hold, the wave shape) is decided
        // during ramp-up: a selection made after the ramp has run can no longer buy it.
        bool arrOver = Belief::arrivalsLikelyOver(sc);
        bool enough = arrOver || (int)sc.st.size() >= minArr;
        bool trig = false;
        if (arrOver && !arrSel) { arrSel = true; trig = true; }
        // Fire the instant the belief first becomes good enough to act on, rather than waiting for
        // the next frame or token milestone.  Measured: the remaining gap to the per-instance oracle
        // is almost entirely the cost of running under the default before the switch lands, so the
        // earliest admissible decision is worth more than any later refinement.
        if (enough && !enoughSel) { enoughSel = true; trig = true; }
        if (sc.tokensOut >= nextTok) { nextTok *= 4; trig = true; }
        // A token-count trigger alone is useless on a prefill-bound instance: h_4_15 emits its
        // fourth token at t=135910 of a 141200 ms run, by which time every placement decision the
        // candidates disagree about has already been made.  Frames advance regardless.
        if (++nFrames >= nextFrame) { nextFrame *= 16; trig = true; }
        if (!trig) return;
        if (spent >= totalBudget) return;
        // Belt and braces against a final test far larger than anything local: whatever clock()
        // counts on the judge, once the process as a whole is deep into the 15 s limit we stop
        // searching and simply run the shipped policy.
        if (clock() > wallCeiling) return;

        clock_t start = clock();
        // Publish the solved token total to the live policy.  Exact once arrivals are done; while
        // they are still coming it is a lower bound, and every gate that reads it treats it as one.
        {
            static Future tf;
            Belief::project(sc, 0, tf);
            double tot = 0;
            for (int v : tf.Lout) tot += v;
            sc.totTok = tot;
        }
        // Coordinate-descent step: re-centre the candidate set on the policy currently applied.
        build(curTh, *par);
        // Share what is left of the allowance across this round's candidates, so a round late in
        // the run is not starved by an early one and no single forward run can eat the budget.
        clock_t remain = totalBudget - spent;
        clock_t hardStop = start + remain;
        runBudget = max((clock_t)(0.005 * CLOCKS_PER_SEC),
                        (clock_t)(remain / (2 * (clock_t)max<size_t>(1, cands.size()))));

        static Engine eng;
        static Future fu;
        // The total is EXACT once arrivals are done and an underestimate while they are still
        // coming -- precisely the state the two worst losses were in.  So price the likely case
        // first and stay sceptical otherwise.
        int primary = arrOver ? 0 : 1;
        int secondary = arrOver ? -1 : 0;
        double needMargin = margin + (arrOver ? 0.0 : shakyMargin);

        // DIAGNOSTIC: predict the incumbent only and report it, changing nothing.  Comparing this
        // against the run's actual score is what calibrates the engine and the belief separately.
        if (predOnly) {
            Belief::project(sc, 0, fu);
            Sched clone = sc;
            clone.setTheta(curTh);
            eng.seed(sc, fu);
            Pred p = eng.run(std::move(clone), start + runBudget);
            fprintf(stderr, "[pred] tok=%lld T=%d pred=%.3f tp=%.6g tdr=%.4g tpot=%.4g ok=%d\n",
                    sc.tokensOut, fu.R ? (int)accumulate(fu.Lout.begin(), fu.Lout.end(), 0LL) : 0,
                    p.score, p.tp, p.tdr, p.tpot, (int)p.ok);
            rounds++;
            spent += clock() - start;
            return;
        }

        // round 1: every candidate under the primary scenario
        vector<pair<double, int>> sc1;
        bool complete = true;
        Belief::project(sc, primary, fu);
        for (size_t c = 0; c < cands.size(); c++) {
            clock_t dl = min(hardStop, clock() + runBudget);
            if (clock() >= hardStop) { complete = false; break; }
            Sched clone = sc;
            clone.setTheta(cands[c]);
            eng.seed(sc, fu);
            Pred p = eng.run(std::move(clone), dl);
            if (p.ok) sc1.push_back({p.score, (int)c});
            else complete = false;          // a candidate that ran out of time was never priced
        }
        // A round that could not price every candidate is choosing from an arbitrary subset -- the
        // slow-to-simulate ones drop out, and which those are has nothing to do with which is best.
        // Left unguarded that cost -107.6 on one hold/ instance at a tight budget.
        if (sc1.empty() || !complete) { rounds++; spent += clock() - start; return; }
        sort(sc1.begin(), sc1.end(), [](const pair<double,int>& a, const pair<double,int>& b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
        });

        int best = sc1[0].second;
        double bestScore = sc1[0].first;
        // Round 2 re-prices the survivors over the OTHER futures that are consistent with what we
        // have observed -- the second arrival scenario, and several sampled splits of the (exactly
        // known) token total -- and ranks them on the mean.  A policy that only wins under one
        // particular guess about the future does not get picked on that guess alone.
        double meanBase = -1, meanCur = -1;
        vector<pair<int, unsigned>> alts;                       // (scenario, split seed)
        if (secondary >= 0) alts.push_back({secondary, 0});
        for (int s = 1; s <= nSplit; s++) alts.push_back({primary, (unsigned)s});
        if (!alts.empty() && (int)sc1.size() > 1) {
            int lim = min((int)sc1.size(), keep);
            vector<double> acc(lim), cnt(lim, 1.0);
            for (int r = 0; r < lim; r++) acc[r] = sc1[r].first;
            for (auto& a : alts) {
                if (clock() >= hardStop) break;
                Belief::project(sc, a.first, fu, a.second);
                for (int r = 0; r < lim; r++) {
                    if (clock() >= hardStop) break;
                    clock_t dl = min(hardStop, clock() + runBudget);
                    Sched clone = sc;
                    clone.setTheta(cands[sc1[r].second]);
                    eng.seed(sc, fu);
                    Pred p = eng.run(std::move(clone), dl);
                    if (p.ok) { acc[r] += p.score; cnt[r] += 1.0; }
                }
            }
            double bestMean = -1;
            int bestIdx = -1;
            for (int r = 0; r < lim; r++) {
                double mean = acc[r] / cnt[r];
                // the reference scores must be means too, or a mean is compared against a
                // single-future estimate and the margin stops meaning anything
                if (sc1[r].second == 0) meanBase = mean;
                if (sc1[r].second == 1) meanCur = mean;
                if (mean > bestMean + 1e-12) { bestMean = mean; bestIdx = sc1[r].second; }
            }
            if (bestIdx >= 0) { best = bestIdx; bestScore = bestMean; }
        }

        // The shipped default is the only setting with a judge measurement behind it, so it is the
        // reference at EVERY round, not just the first: a deviation has to keep out-predicting it
        // by the margin or the slot goes back.  Without this an early pick made on a half-formed
        // belief simply persists (h_7_12 lost 10.1 exactly that way).
        double incumbent = -1, baseScore = -1;
        for (auto& pr : sc1) {
            if (pr.second == 1) incumbent = pr.first;    // cands[1] is always the applied policy
            if (pr.second == 0) baseScore = pr.first;
        }
        if (meanCur >= 0) incumbent = meanCur;
        if (meanBase >= 0) baseScore = meanBase;
        // Switching is only allowed while the choice is still cheap to make.  A policy evaluated
        // "from here on" can genuinely be the better continuation and still produce a WORSE run
        // than either pure policy, because the schedule it inherits was built by the other one:
        // j_47 chose the deferral early (the oracle's pick, worth +60.9), reverted at t=246436 on
        // an accurate prediction, and finished below both.  So converge instead of tracking.
        // A switch needs something to have been observed.  The token total is solved from the
        // arrivals, so with two of them in hand the simulated instance is a stub and its ranking is
        // meaningless -- j_47 ran `jitp0` through its whole admission window on exactly that.
        // Either arrivals are done (the total is then exact) or enough of them are in.
        // Each switch splices a new policy onto a schedule the previous one built, and the hybrid
        // can be worse than either.  Cap how many times that may happen.
        if (rounds < freezeAfter && enough && nSwitch < maxSwitch) {
            // A deviation must out-predict the shipped default by the margin, every round.
            int want = baseAnchor ? ((bestScore > baseScore + needMargin) ? best : 0) : best;
            bool change = (want == 0) ? (baseScore > incumbent + 1e-9)
                                      : (bestScore > incumbent + needMargin);
            if (change) {
                chosen = want;
                curTh = cands[want];
                sc.setTheta(curTh);
                nSwitch++;
            }
        }
        rounds++;
        spent += clock() - start;
        if (dbg)
            fprintf(stderr, "[sel] t=%.1f tok=%lld round=%d cands=%d -> %s (pred %.2f, incumbent %.2f) %.0fms\n",
                    sc.curT, sc.tokensOut, rounds, (int)sc1.size(), names[chosen], bestScore, incumbent,
                    1000.0 * (clock() - start) / CLOCKS_PER_SEC);
    }
};

// ---- the live scheduler, plus the free-function entry points sim.cpp drives -----------------
Sched live;
Theta defaultTheta;
Selector selector;

void schedInit(const Params& p, const Table& t) {
    defaultTheta = Theta();
    defaultTheta.fromEnv();
    live.init(p, t, defaultTheta);
    selector.init(defaultTheta, p);
}

void schedFrame(double t, const Frame& f, Response& out) {
    live.ingest(t, f);
    selector.maybeSelect(live);
    live.decide(out);
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
