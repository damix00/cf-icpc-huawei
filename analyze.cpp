// Offline analysis: the true per-test ceiling, and how much of the gap to it we are leaving.
//
//   g++ -O2 -std=gnu++17 -o analyze.exe analyze.cpp && ./analyze.exe tests/g_5_2.txt [achieved_tp]
//
// Two independent lower bounds on elapsed time:
//   latency floor  each request's steps are strictly serial, so request i cannot finish before
//                  arr[i] + (uncontended prefill round trip) + Lout[i] * (best decode round trip);
//   work floor     no schedule beats the busiest resource's total unavoidable work.
// The larger of the two caps tp, and hence caps the score.  Chasing "headroom" above this ceiling
// is chasing a calibration artefact, not points.
#include <bits/stdc++.h>
using namespace std;

struct PL {
    vector<double> xs, ys;
    void add(double x, double y) { xs.push_back(x); ys.push_back(y); }
    void finish() {
        vector<int> id(xs.size()); iota(id.begin(), id.end(), 0);
        sort(id.begin(), id.end(), [&](int a, int b) { return xs[a] < xs[b]; });
        vector<double> nx, ny;
        for (int i : id) { nx.push_back(xs[i]); ny.push_back(ys[i]); }
        xs.swap(nx); ys.swap(ny);
    }
    double at(double x) const {
        if (xs.empty()) return 0;
        if (x <= xs.front()) return ys.front();
        if (x >= xs.back()) return ys.back();
        int hi = int(lower_bound(xs.begin(), xs.end(), x) - xs.begin()), lo = hi - 1;
        double d = xs[hi] - xs[lo];
        return d <= 0 ? ys[hi] : ys[lo] + (x - xs[lo]) / d * (ys[hi] - ys[lo]);
    }
};

int main(int argc, char** argv) {
    FILE* fp = fopen(argv[1], "r");
    if (!fp) { fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    double achieved = argc > 2 ? atof(argv[2]) : -1;
    double achTdr   = argc > 3 ? atof(argv[3]) : -1;
    double achTpot  = argc > 4 ? atof(argv[4]) : -1;

    int K, numLayers; double S, lat, bw, bpt;
    if (fscanf(fp, "%d %lf %lf %lf %lf %d", &K, &S, &lat, &bw, &bpt, &numLayers) != 6) return 2;
    double SLO1, SLO2, tpUB, tpBase, distBase, wTp, wC;
    if (fscanf(fp, "%lf %lf %lf %lf %lf %lf %lf", &SLO1, &SLO2, &tpUB, &tpBase, &distBase, &wTp, &wC) != 7) return 2;
    int N; if (fscanf(fp, "%d", &N) != 1) return 2;
    PL c[6];
    for (int r = 0; r < N; r++) {
        double bs; if (fscanf(fp, "%lf", &bs) != 1) return 2;
        for (int k = 0; k < 6; k++) { double v; if (fscanf(fp, "%lf", &v) != 1) return 2; if (v >= 0) c[k].add(bs, v); }
    }
    for (int k = 0; k < 6; k++) c[k].finish();
    int R; if (fscanf(fp, "%d", &R) != 1) return 2;
    vector<double> arr(R); vector<int> Lin(R), Lout(R);
    long long sumOut = 0;
    for (int i = 0; i < R; i++) {
        if (fscanf(fp, "%lf %d %d", &arr[i], &Lin[i], &Lout[i]) != 3) return 2;
        sumOut += Lout[i];
    }
    fclose(fp);

    const double u = 8.0 * bpt / (bw * 1e6);
    auto xfer = [&](double len) { return lat + u * len; };

    // best achievable decode round trip for one request, over the group size it rides in
    double Cdec = 1e300; int mBest = 1;
    for (int m = 1; m <= max(1, R); m++) {
        double v = 3 * S + c[3].at(m) + c[4].at(m) + c[5].at(m) + 2 * xfer(m);
        if (v < Cdec) { Cdec = v; mBest = m; }
    }
    // uncontended prefill round trip (one full piece)
    auto Cpre = [&](int L) { return 3 * S + c[0].at(L) + c[1].at(L) + c[2].at(L) + 2 * xfer(L); };

    double t0 = *min_element(arr.begin(), arr.end());
    double latFloor = 0, tdrFloor = 0;
    for (int i = 0; i < R; i++) {
        latFloor = max(latFloor, arr[i] - t0 + Cpre(Lin[i]) + Lout[i] * Cdec);
        tdrFloor += Cpre(Lin[i]);
    }
    tdrFloor /= R;

    // work floor: busiest resource, optimised over group size m and remotes used k
    double sumPProc = 0, sumPLocal = 0, sumLinXfer = 0;
    for (int i = 0; i < R; i++) {
        sumPProc   += c[1].at(Lin[i]);
        sumPLocal  += c[0].at(Lin[i]) + c[2].at(Lin[i]) + 2 * S;
        sumLinXfer += xfer(Lin[i]);
    }
    double workFloor = 1e300;
    for (int k = 1; k <= K; k++)
        for (int m = 1; m <= max(1, min(4096, R)); m = m < 4 ? m + 1 : m * 2) {
            double mj = max(1.0, (double)m / k), rounds = (double)sumOut / m;
            double loc  = sumPLocal + rounds * (2 * S + c[3].at(m) + c[5].at(m));
            double rem  = (sumPProc + R * S + rounds * k * (S + c[4].at(mj))) / K;
            double link = sumLinXfer + rounds * (k * lat + u * m);
            workFloor = min(workFloor, max(max(loc, rem), link));
        }

    double floorT = max(latFloor, workFloor);
    double tpCap  = sumOut / floorT;
    auto clamp01  = [](double x) { return max(0.0, min(1.0, x)); };
    double compCap = tpUB > tpBase ? clamp01((tpCap - tpBase) / (tpUB - tpBase)) : 0.0;

    double exTdr = max(0.0, (tdrFloor - SLO1) / SLO1), exTpot = max(0.0, (Cdec - SLO2) / SLO2);
    double distF = sqrt(exTdr * exTdr + exTpot * exTpot);
    double waitCap = distBase > 0 ? max(0.0, 1.0 - distF / distBase) : (distF == 0 ? 1.0 : 0.0);
    double scoreCap = 1000 * (wTp * compCap + wC * waitCap);

    printf("%-20s K=%d S=%5.2f lat=%8.4f u=%9.3g R=%4d sumOut=%6lld  wtp=%.2f\n",
           argv[1], K, S, lat, u, R, sumOut, wTp);
    printf("   Cdec=%.2f(m=%d)  floors: latency=%.4g work=%.4g -> elapsed>=%.4g   tp<=%.6g (tp_UB=%.6g)\n",
           Cdec, mBest, latFloor, workFloor, floorT, tpCap, tpUB);
    printf("   CEILING score<=%.1f  (tp comp<=%.4f, wait comp<=%.4f)", scoreCap, compCap, waitCap);
    if (achieved > 0) {
        double comp = tpUB > tpBase ? clamp01((achieved - tpBase) / (tpUB - tpBase)) : 0.0;
        printf("   | achieved tp=%.6g comp=%.4f -> %.0f%% of the reachable tp",
               achieved, comp, 100 * achieved / tpCap);
    }
    printf("\n");
    if (achTdr > 0)
        printf("   tdr %.4g vs floor %.4g (SLO1 %.4g) | tpot %.4g vs floor %.2f (SLO2 %.4g)\n",
               achTdr, tdrFloor, SLO1, achTpot, Cdec, SLO2);
    return 0;
}
