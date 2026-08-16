#include <bits/stdc++.h>
using namespace std;
struct Params {
    int K = 1;
    double S = 1, lat = 1, bw = 1, bytesPerToken = 1;
    int numLayers = 1;
    double SLO1 = 1, SLO2 = 1, tpUB = 1, tpBase = 0, distBase = 0, wTp = 0.5, wC = 0.5;
};
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
enum { C_PPRE = 0, C_PPROC = 1, C_PPOST = 2, C_DPRE = 3, C_DPROC = 4, C_DPOST = 5 };
struct Table { PLCurve c[6]; };
enum { ST_PPRE = 0, ST_PPROC = 1, ST_PPOST = 2, ST_DPRE = 3, ST_DPROC = 4, ST_DPOST = 5 };
enum { EV_ARR = 0, EV_TDN = 1, EV_XDN = 2, EV_FIN = 3 };
enum { DIR_UP = 0, DIR_DOWN = 1 };
enum { KIND_PRE = 0, KIND_DEC = 1 };
struct Event {
    int type;
    int a = 0, b = 0;
    int off = 0, cnt = 0;
};
struct Frame {
    double t = 0;
    vector<Event> evs;
    vector<int> ids;
    void clear() { evs.clear(); ids.clear(); }
};
struct Assign {
    int server;
    int step;
    int remote;
    int ls = 0, le = 0;
    vector<int> ids;
};
struct Response {
    int n = 0;
    Assign a[16];
};
inline double envD(const char* k, double d) { const char* v = getenv(k); return v ? atof(v) : d; }
inline const char* envS(const char* k, const char* d) { const char* v = getenv(k); return v ? v : d; }
struct Theta {
    double prefillUrgency = 0.5;
    double pieceMul = 128.0;
    int poolTarget = 1 << 30;
    char ordA[8] = {0};
    char ordD[8] = "0213";
    double ordW = 0.75;
    double waitPost = 32.0;
    double waitProc = 14.0;
    double holdCap = 4.0;
    double eBottleW = 1.0;
    int holdPreToo = 1;
    double remBusyW = 1.0;
    double decW = 1.0;
    double dTol = 0.04;
    double warmUp = 100.0;
    double linkFixedFrac = 0.05;
    int jitPre = 1, jitProc = 3, jitMode = 1, jitL = 2;
    double jitSlack = 3.0;
    double tpotMargin = 0.75;
    double tdrGuard = 0.5;
    double arrExpect = 0.0;
    double swapMin = 0.05;
    int swapWarm = 8;
    int deferFirst = 0;
    double deferSlo = 1.0;
    bool sjf = true;
    int sjfProc = 1, sjfPost = 0;
    int fixM = 0, fixD = 0, fixMR = 0;
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
        fixM = (int)envD("CF_FIXM", 0);
        fixD = (int)envD("CF_FIXD", 0);
        fixMR = (int)envD("CF_FIXMR", 0);
    }
};
namespace Sch {
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
inline double mergeSaving(const PLCurve& c, double q, double S) {
    return S + max(0.0, 2 * c.at(q) - c.at(2 * q));
}
struct Sched {
    Params P;
    Table T;
    Theta th;
    vector<int> st, lin_, rem, layersDone, chunkStep, tokCnt;
    vector<double> arrT, procTotal, tdrCost;
    vector<char> finished;
    deque<int> qPPre, qPPost;
    vector<int> qDPre, qDPost;
    vector<deque<int>> qPProc;
    vector<vector<int>> qDProc;
    bool eBusy = false;
    vector<char> rBusy;
    BusyRec eRec;
    vector<BusyRec> rRec;
    long long pendingTransfers = 0;
    double uPerToken = 0;
    double busyE = 0, busyUp = 0, busyDn = 0;
    double preE = 0, preUp = 0, preDn = 0, preR = 0;
    double t0 = -1;
    vector<double> busyR;
    double curT = 0;
    double upFreeAt = 0, downFreeAt = 0;
    double eFreeAt = 0;
    vector<double> rFreeAt;
    struct Xf { double fin; char dec; int remote; vector<int> ids; };
    deque<Xf> upQ, downQ;
    int mStar = 1, mStarR = 1, dStar = 1;
    int lastL = -1, lastLd = -1, sinceRetarget = 0;
    double holdPostSince = -1;
    vector<double> holdProcSince;
    long long tokensOut = 0;
    double pieceTargetMs = 1.0;
    double decWeight = 0;
    char ordAdmitS[8] = "1302";
    char ordDecodeS[8] = "0213";
    vector<int> load, decLoad;
    vector<double> pendProc;
    int activeDecode = 0;
    int preOutstanding = 0;
    multiset<int> upstreamLin;
    double spanSum = 0;
    long long gapCnt = 0;
    vector<double> lastTok;
    vector<double> firstTok, tdrAt;
    double tdrSum = 0;
    long long tdrCnt = 0;
    double outArrSum = 0, outCostSum = 0;
    long long outCnt = 0;
    long long arrCount = 0;
    double firstArr = -1;
    vector<int> scratchCnt, eligDPre;
    inline void upIns(int i) { upstreamLin.insert(lin_[i]); }
    inline void upErase(int i) { auto it = upstreamLin.find(lin_[i]); if (it != upstreamLin.end()) upstreamLin.erase(it); }
    inline double tpotNow() const { return gapCnt ? spanSum / (double)gapCnt : 0.0; }
    vector<int> xfIds;
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
    inline double nextDecAt(bool up, int remote) {
        double best = 1e300;
        for (auto& p : (up ? upQ : downQ))
            if (p.dec && (remote < 0 || p.remote == remote)) { best = p.fin; break; }
        return max(best, curT);
    }
    inline int bottleneck() const {
        double r = 0;
        for (int j = 0; j < P.K; j++) r = max(r, busyR[j]);
        double link = max(busyUp, busyDn);
        if (busyE >= r && busyE >= link) return -1;
        return link >= r ? 1 : 0;
    }
    inline double waitBudget(double base) const { return bottleneck() == -1 ? min(base, th.eBottleW) : base; }
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
        double slope = max(0.0, (T.c[C_DPROC].at(64.0) - T.c[C_DPROC].at(1.0)) / 63.0);
        decWeight = th.decW * slope;
        uPerToken = 8.0 * P.bytesPerToken / (P.bw * 1e6);
        double dr1 = T.c[C_DPROC].at(1.0);
        pieceTargetMs = max(2.0 * P.S, dr1) * th.pieceMul;
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
    void setTheta(const Theta& theta) {
        th = theta;
        double slope = max(0.0, (T.c[C_DPROC].at(64.0) - T.c[C_DPROC].at(1.0)) / 63.0);
        decWeight = th.decW * slope;
        pieceTargetMs = max(2.0 * P.S, T.c[C_DPROC].at(1.0)) * th.pieceMul;
        setOrders();
        lastL = -1; lastLd = -1; sinceRetarget = 0;
    }
    inline double avail(double preBusy, double share) const {
        double el = curT - t0;
        if (el <= th.warmUp * P.S) return 1.0;
        return max(0.05, 1.0 - preBusy / (share * el));
    }
    inline double waveRate(double m, double L, int d) const {
        double de = min((double)d, m);
        double mr = max(1.0, m / de);
        double up = de * P.lat + uPerToken * m;
        double dpre = T.c[C_DPRE].at(m), dpost = T.c[C_DPOST].at(m), dproc = T.c[C_DPROC].at(mr);
        double C = 3 * P.S + dpre + dproc + dpost + 2 * up;
        double r = L / C;
        r = min(r, avail(preE, 1) * m / (2 * P.S + dpre + dpost));
        r = min(r, avail(preR, d) * d * mr / (P.S + dproc));
        r = min(r, avail(preUp, 1) * m / up);
        r = min(r, avail(preDn, 1) * m / up);
        return r;
    }
    inline void retarget() {
        int L = max(1, activeDecode);
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
        double bestD = -1;
        for (int d = 1; d <= P.K; d++) {
            double rd = 0;
            for (int m = 1; ; m = (m < 8 ? m + 1 : (m * 5) / 4 + 1)) {
                rd = max(rd, waveRate(min(m, Ld), Ld, d));
                if (m >= Ld) break;
            }
            if (rd > bestD * (1 - th.dTol)) { bestD = max(bestD, rd); dStar = d; }
        }
        if (th.fixM > 0) mStar = max(1, min(th.fixM, L));
        if (th.fixD > 0) dStar = max(1, min(th.fixD, P.K));
        mStarR = max(1, mStar / dStar);
        if (th.fixMR > 0) mStarR = th.fixMR;
    }
    inline void planChunks(int i) {
        double L = lin_[i];
        double pp = T.c[C_PPROC].at(L);
        procTotal[i] = pp;
        tdrCost[i] = 3 * P.S + T.c[C_PPRE].at(L) + pp + T.c[C_PPOST].at(L)
                   + 2 * (P.lat + uPerToken * L);
        int c = (int)llround(pp / pieceTargetMs);
        c = max(1, min(P.numLayers, c));
        chunkStep[i] = max(1, (P.numLayers + c - 1) / c);
    }
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
    inline double lastPreFin(bool up) const {
        double best = -1;
        for (auto& p : (up ? upQ : downQ)) if (!p.dec) best = max(best, p.fin);
        return best;
    }
    inline int peekPProc(int j) {
        if (!th.sjfProc || qPProc[j].size() == 1) return qPProc[j].front();
        size_t bk = 0;
        for (size_t k = 1; k < qPProc[j].size(); k++)
            if (procTotal[qPProc[j][k]] < procTotal[qPProc[j][bk]]) bk = k;
        return qPProc[j][bk];
    }
    inline int pickRemote() {
        int active = 0;
        for (int j = 0; j < P.K; j++) if (load[j] > 0) active++;
        int best = -1;
        double bestCost = 1e300;
        for (int j = 0; j < P.K; j++) {
            if (load[j] == 0 && active >= dStar) continue;
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
    inline double nextDownAfterProc() const {
        double best = 1e300;
        for (int j = 0; j < P.K; j++) {
            if (!rBusy[j] || rRec[j].step != ST_DPROC) continue;
            double fin = max(rFreeAt[j], downFreeAt) + P.lat + uPerToken * (double)rRec[j].ids.size();
            best = min(best, fin);
        }
        return best;
    }
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
    void ingest(double t, const Frame& f) {
        curT = t;
        if (t0 < 0) t0 = t;
        for (const Event& e : f.evs) {
            if (e.type == EV_FIN) {
                int i = e.a;
                finished[i] = 1; st[i] = R_DONE;
                load[rem[i]]--; decLoad[rem[i]]--; activeDecode--;
            }
        }
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
        double prefillWait = 0;
        if (!qPPre.empty())  prefillWait = max(prefillWait, t - arrT[qPPre.front()]);
        if (!qPPost.empty()) prefillWait = max(prefillWait, t - arrT[qPPost.front()]);
        bool prefillUrgent = activeDecode < th.poolTarget || prefillWait > th.prefillUrgency * P.SLO1;
        retarget();
        bool inFlight = pendingTransfers > 0;
        for (int j = 0; j < P.K && !inFlight; j++) inFlight = rBusy[j];
        bool holdPost = false;
        if (inFlight && !qDPost.empty() && (int)qDPost.size() < mStar && batchingHelpsBottleneck(mStar)) {
            double t1 = min(nextDecAt(false, -1), nextDownAfterProc());
            double budget = waitBudget(th.waitPost) * mergeSaving(T.c[C_DPOST], (double)qDPost.size(), P.S);
            if (holdPostSince < 0) holdPostSince = t;
            if (t1 - t <= budget && t - holdPostSince <= th.holdCap * budget) holdPost = true;
        }
        if (!holdPost) holdPostSince = -1;
        bool holdPPre = false;
        bool tpotRoom = th.tpotMargin > 0 && P.SLO2 > 0 && gapCnt > 0 && tpotNow() < th.tpotMargin * P.SLO2;
        if (th.jitPre && (activeDecode <= th.jitL || tpotRoom) && tdrWorthIt()
            && !qPPre.empty() && pendingTransfers > 0) {
            int i = peekPPre();
            double lim = th.jitMode ? lastPreFin(true) : upFreeAt;
            if (lim > t + th.jitSlack * (P.S + T.c[C_PPRE].at(lin_[i])) && arrivalLikely(lim - t))
                holdPPre = true;
        }
        int bestProcJ = -1;
        double bestProcLin = 1e300;
        if (th.jitProc) {
            for (int j = 0; j < P.K; j++) {
                if (rBusy[j] || qPProc[j].empty()) continue;
                int i = peekPProc(j);
                if (lin_[i] < bestProcLin) { bestProcLin = lin_[i]; bestProcJ = j; }
            }
        }
        eligDPre.clear();
        bool deferring = false;
        if (th.deferFirst && P.SLO2 > 0) {
            bool preOnLink = (lastPreFin(true) > t) || (lastPreFin(false) > t);
            bool preLeft = (th.deferFirst >= 2) ? (preOutstanding > 0) : false;
            if (preOnLink || preLeft) {
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
        if (!eBusy) {
            int choice = -1;
            const char* ord = prefillUrgent ? ordAdmitS : ordDecodeS;
            char ordBuf[8];
            if (dpreFirst) {
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
        for (int j = 0; j < P.K; j++) {
            if (rBusy[j]) continue;
            bool doDecode = !qDProc[j].empty();
            if (doDecode && inFlight && (int)qDProc[j].size() < mStarR && batchingHelpsBottleneck(mStarR)) {
                double t1 = min(nextDecAt(true, j), nextUpAfterPre(j));
                double budget = th.waitProc * mergeSaving(T.c[C_DPROC], (double)qDProc[j].size(), P.S);
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
                int i = peekPProc(j);
                int stepSz = decLoad[j] > 0 ? chunkStep[i] : P.numLayers;
                int le = min(P.numLayers, layersDone[i] + stepSz);
                bool last = (le >= P.numLayers);
                bool hold = false;
                if (last && th.jitProc && pendingTransfers > 0) {
                    double dur = (double)(le - layersDone[i]) / P.numLayers * procTotal[i];
                    double lim = th.jitMode ? lastPreFin(false) : downFreeAt;
                    bool blocked = lim > t + th.jitSlack * (P.S + dur);
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
                for (int j = 0; j < P.K; j++) {
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
        if (sc.eBusy && sc.eRec.step >= 0) {
            tasks.push_back(Task{-1, sc.eRec.step, -1, sc.eRec.ls, sc.eRec.le, sc.eRec.ids});
            pushEv(sc.eFreeAt, 1, (int)tasks.size() - 1);
        }
        for (int j = 0; j < P.K; j++)
            if (sc.rBusy[j] && sc.rRec[j].step >= 0) {
                tasks.push_back(Task{j, sc.rRec[j].step, j, sc.rRec[j].ls, sc.rRec[j].le, sc.rRec[j].ids});
                pushEv(sc.rFreeAt[j], 1, (int)tasks.size() - 1);
            }
        for (auto& x : sc.upQ) {
            xfers.push_back(Xfer{DIR_UP, x.remote, x.dec ? KIND_DEC : KIND_PRE, x.ids});
            pushEv(x.fin, 2, (int)xfers.size() - 1);
        }
        for (auto& x : sc.downQ) {
            xfers.push_back(Xfer{DIR_DOWN, x.remote, x.dec ? KIND_DEC : KIND_PRE, x.ids});
            pushEv(x.fin, 2, (int)xfers.size() - 1);
        }
    }
    Pred run(Sched sc, clock_t deadline) {
        Pred res;
        Frame f;
        Response out;
        long long guard = 0;
        sc.decide(out);
        for (int k = 0; k < out.n; k++) startTask(sc.curT, out.a[k]);
        while (finishedCount < R) {
            if (pq.empty()) return res;
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
struct Belief {
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
    static void project(const Sched& sc, int scenario, Future& fu) {
        if (truth(fu)) return;
        int n = (int)sc.st.size();
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
                double lam = (double)(n - 1) / span;
                int extra = min(n, (int)llround(lam * span));
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
        double Ttot = totalTokens(sc, fu.arr, fu.Lin);
        long long emitted = 0, fixed = 0;
        for (int i = 0; i < n; i++) if (sc.finished[i]) fixed += max(1, sc.tokCnt[i]);
        for (int i = 0; i < n; i++) emitted += sc.tokCnt[i];
        (void)emitted;
        int maxTok = 1, nDec = 0;
        for (int i = 0; i < n; i++) if (sc.tokCnt[i] > 0) { nDec++; maxTok = max(maxTok, sc.tokCnt[i]); }
        double M = max(2.0, nDec ? (double)maxTok * (nDec + 1.0) / nDec : 2.0);
        double wsum = 0;
        static vector<double> w;
        w.assign(R, 0.0);
        for (int i = 0; i < R; i++) {
            if (i < n && sc.finished[i]) continue;
            int k = (i < n) ? sc.tokCnt[i] : 0;
            w[i] = max(1.0, M - k);
            wsum += w[i];
        }
        long long owed = 0;
        for (int i = 0; i < R; i++) if (!(i < n && sc.finished[i])) owed++;
        Ttot = max(Ttot, (double)(sc.tokensOut + owed));
        double rem = max(0.0, Ttot - (double)fixed);
        for (int i = 0; i < R; i++) {
            if (i < n && sc.finished[i]) { fu.Lout[i] = max(1, sc.tokCnt[i]); continue; }
            int k = (i < n) ? sc.tokCnt[i] : 0;
            double share = wsum > 0 ? rem * w[i] / wsum : 1.0;
            fu.Lout[i] = max(k + 1, (int)llround(share));
        }
    }
    static bool arrivalsLikelyOver(const Sched& sc) {
        int n = (int)sc.st.size();
        if (n < 4 || sc.firstArr < 0) return false;
        double lastArr = 0;
        for (int i = 0; i < n; i++) lastArr = max(lastArr, sc.arrT[i]);
        double meanGap = (lastArr - sc.firstArr) / (n - 1);
        return meanGap > 0 && (sc.curT - lastArr) > 3.0 * meanGap;
    }
};
struct Selector {
    vector<Theta> cands;
    vector<const char*> names;
    int chosen = 0;
    long long nextTok = 4;
    long long nFrames = 0, nextFrame = 4;
    int rounds = 0;
    bool arrSel = false;
    bool on = true, dbg = false, predOnly = false;
    clock_t totalBudget = 0, runBudget = 0, spent = 0, wallCeiling = 0;
    int maxRounds = 6, keep = 6;
    double margin = 1.0, shakyMargin = 15.0;
    int freezeAfter = 5, baseAnchor = 1, minArr = 8;
    void build(const Theta& base, const Params& P) {
        cands.clear(); names.clear();
        auto add = [&](const char* nm, Theta th) { cands.push_back(th); names.push_back(nm); };
        add("base", base);
        for (const char* o : {"3102", "1320", "0132", "3012"}) {
            Theta th = base; snprintf(th.ordA, sizeof th.ordA, "%s", o); add(o, th);
        }
        for (int v : {0, 1, 4, 1000}) { Theta th = base; th.jitL = v; add("jitl", th); }
        { Theta th = base; th.jitPre = 0; add("jitp0", th); }
        for (double v : {1.0, 2.0, 4.0, 8.0}) { Theta th = base; th.waitPost = v; add("wp", th); }
        for (double v : {1.0, 4.0}) { Theta th = base; th.waitProc = v; add("wr", th); }
        for (int v : {0, 3}) { Theta th = base; th.holdPreToo = v; add("hp", th); }
        { Theta th = base; th.deferFirst = 2; add("defer2", th); }
        for (double v : {0.0, 0.2, 0.5}) { Theta th = base; th.dTol = v; add("dtol", th); }
        { Theta th = base; th.decW = 0; add("decw0", th); }
        { Theta th = base; th.tdrGuard = 0; add("tdrg0", th); }
        { Theta th = base; th.poolTarget = 4; add("pool4", th); }
        for (double v : {1.0, 4.0}) { Theta th = base; th.pieceMul = v; add("piece", th); }
        for (int d : {1, 2, 4, 8}) {
            if (d > P.K) break;
            for (int m : {1, 2, 4, 8, 16}) {
                Theta th = base; th.fixD = d; th.fixM = m; add("dm", th);
            }
        }
    }
    void init(const Theta& base, const Params& P) {
        on = envD("CF_SEL", 1.0) > 0.5;
        dbg = envD("CF_SELDBG", 0.0) > 0.5;
        predOnly = envD("CF_SELPRED", 0.0) > 0.5;
        maxRounds = (int)envD("CF_SELR", 6);
        keep = (int)envD("CF_SELK", 6);
        margin = envD("CF_SELM", 1.0);
        shakyMargin = envD("CF_SELSM", 15.0);
        freezeAfter = (int)envD("CF_SELFZ", 5);
        baseAnchor = (int)envD("CF_SELBA", 1);
        minArr = (int)envD("CF_SELMA", 8);
        nextTok = (long long)envD("CF_SELT", 4);
        double budgetSec = envD("CF_SELB", 4.0);
        totalBudget = (clock_t)(budgetSec * CLOCKS_PER_SEC);
        wallCeiling = (clock_t)(envD("CF_SELWC", 9.0) * CLOCKS_PER_SEC);
        chosen = 0;
        rounds = 0;
        arrSel = false;
        nFrames = 0;
        nextFrame = (long long)envD("CF_SELF", 4);
        spent = 0;
        build(base, P);
        double runSec = envD("CF_SELRB", 0.0);
        runBudget = runSec > 0 ? (clock_t)(runSec * CLOCKS_PER_SEC)
                               : max((clock_t)(0.01 * CLOCKS_PER_SEC),
                                     (clock_t)(totalBudget / (2 * (clock_t)max<size_t>(1, cands.size()))));
    }
    void maybeSelect(Sched& sc) {
        if (!on || rounds >= maxRounds || cands.size() < 2) return;
        if (sc.st.empty()) return;
        bool arrOver = Belief::arrivalsLikelyOver(sc);
        bool trig = false;
        if (arrOver && !arrSel) { arrSel = true; trig = true; }
        if (sc.tokensOut >= nextTok) { nextTok *= 4; trig = true; }
        if (++nFrames >= nextFrame) { nextFrame *= 16; trig = true; }
        if (!trig) return;
        if (spent >= totalBudget) return;
        if (clock() > wallCeiling) return;
        clock_t start = clock();
        clock_t hardStop = start + min(totalBudget - spent, (clock_t)(4 * runBudget) + runBudget * (clock_t)cands.size());
        static Engine eng;
        static Future fu;
        int primary = arrOver ? 0 : 1;
        int secondary = arrOver ? -1 : 0;
        double needMargin = margin + (arrOver ? 0.0 : shakyMargin);
        if (predOnly) {
            Belief::project(sc, 0, fu);
            Sched clone = sc;
            clone.setTheta(cands[chosen]);
            eng.seed(sc, fu);
            Pred p = eng.run(std::move(clone), start + runBudget);
            fprintf(stderr, "[pred] tok=%lld T=%d pred=%.3f tp=%.6g tdr=%.4g tpot=%.4g ok=%d\n",
                    sc.tokensOut, fu.R ? (int)accumulate(fu.Lout.begin(), fu.Lout.end(), 0LL) : 0,
                    p.score, p.tp, p.tdr, p.tpot, (int)p.ok);
            rounds++;
            spent += clock() - start;
            return;
        }
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
            else complete = false;
        }
        if (sc1.empty() || !complete) { rounds++; spent += clock() - start; return; }
        sort(sc1.begin(), sc1.end(), [](const pair<double,int>& a, const pair<double,int>& b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
        });
        int best = sc1[0].second;
        double bestScore = sc1[0].first;
        if (secondary >= 0 && (int)sc1.size() > 1) {
            Belief::project(sc, secondary, fu);
            double bestMean = -1;
            int bestIdx = -1;
            int lim = min((int)sc1.size(), keep);
            for (int r = 0; r < lim; r++) {
                if (clock() >= hardStop) break;
                int c = sc1[r].second;
                clock_t dl = min(hardStop, clock() + runBudget);
                Sched clone = sc;
                clone.setTheta(cands[c]);
                eng.seed(sc, fu);
                Pred p = eng.run(std::move(clone), dl);
                double mean = p.ok ? 0.5 * (sc1[r].first + p.score) : sc1[r].first;
                if (mean > bestMean + 1e-12) { bestMean = mean; bestIdx = c; }
            }
            if (bestIdx >= 0) { best = bestIdx; bestScore = bestMean; }
        }
        double incumbent = -1, baseScore = -1;
        for (auto& pr : sc1) {
            if (pr.second == chosen) incumbent = pr.first;
            if (pr.second == 0) baseScore = pr.first;
        }
        bool enoughSeen = arrOver || (int)sc.st.size() >= minArr;
        if (rounds < freezeAfter && enoughSeen) {
            int want = baseAnchor ? ((bestScore > baseScore + needMargin) ? best : 0) : best;
            if (want != chosen && (want == 0 ? baseScore > incumbent - 1e-9
                                             : bestScore > incumbent + needMargin)) {
                chosen = want;
                sc.setTheta(cands[chosen]);
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
}
#ifndef LOCAL_SIM
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
        if (tok[0] == 'E') break;
        f.clear();
        f.t = strtod(tok, nullptr);
        int ne = rInt();
        for (int q = 0; q < ne; q++) {
            readTok();
            if (tok[0] == 'A') {
                Event e; e.type = EV_ARR; e.a = rInt(); e.b = rInt();
                f.evs.push_back(e);
            } else if (tok[0] == 'T') {
                Event e; e.type = EV_TDN;
                readTok();
                e.a = (tok[0] == 'E') ? -1 : pInt(tok + 1);
                readTok();
                char cls = tok[0];
                readTok();
                char kind = tok[1];
                if (cls == 'P') {
                    if (tok[0] == 'P' && tok[1] == 'R' && tok[2] == 'E') { rInt(); rInt(); }
                    else if (tok[2] == 'O' && tok[3] == 'C') { rInt(); rInt(); rInt(); rInt(); }
                    else { rInt(); rInt(); }
                } else {
                    rInt();
                    int m = rInt();
                    for (int z = 0; z < m; z++) rInt();
                }
                (void)kind;
                readTok();
                f.evs.push_back(e);
            } else if (tok[0] == 'X') {
                Event e; e.type = EV_XDN;
                readTok(); e.a = (tok[0] == 'U') ? DIR_UP : DIR_DOWN;
                rInt();
                readTok();
                readTok(); e.b = (tok[0] == 'P') ? KIND_PRE : KIND_DEC;
                int m = rInt();
                e.off = (int)f.ids.size(); e.cnt = m;
                for (int z = 0; z < m; z++) f.ids.push_back(rInt());
                f.evs.push_back(e);
            } else {
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
