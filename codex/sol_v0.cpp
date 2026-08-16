// CF 2251A - independent Codex scheduler.
#include <bits/stdc++.h>
using namespace std;

#ifndef CODEX_WIDTH_FEATURES
#define CODEX_WIDTH_FEATURES 15
#endif

#ifndef CODEX_PRESSURE_MAX_WTP
#define CODEX_PRESSURE_MAX_WTP 2.0
#endif

#ifndef CODEX_ANALYTIC_MIN_WTP
#define CODEX_ANALYTIC_MIN_WTP 2.0
#endif

#ifndef CODEX_FIXED_WIDTH
#define CODEX_FIXED_WIDTH -1
#endif

#ifndef CODEX_HIGH_GROUP_MULT
#define CODEX_HIGH_GROUP_MULT 2.5
#endif

#ifndef CODEX_HIGH_POP_COEFF
#define CODEX_HIGH_POP_COEFF 3.75
#endif

#ifndef CODEX_SATURATION_POST_FIRST
#define CODEX_SATURATION_POST_FIRST 0
#endif

#ifndef CODEX_ULTRA_GROUP_MULT
#define CODEX_ULTRA_GROUP_MULT 1.50
#endif

#ifndef CODEX_ULTRA_POP_COEFF
#define CODEX_ULTRA_POP_COEFF 2.25
#endif

#ifndef CODEX_POST_WAIT_MULT
#define CODEX_POST_WAIT_MULT 32.0
#endif

#ifndef CODEX_PROC_WAIT_MULT
#define CODEX_PROC_WAIT_MULT 14.0
#endif

#ifndef CODEX_ULTRA_POST_WAIT_MULT
#define CODEX_ULTRA_POST_WAIT_MULT CODEX_POST_WAIT_MULT
#endif

#ifndef CODEX_ULTRA_PROC_WAIT_MULT
#define CODEX_ULTRA_PROC_WAIT_MULT CODEX_PROC_WAIT_MULT
#endif

#ifndef CODEX_IGNORE_SLACK_WHEN_TP_ONLY
#define CODEX_IGNORE_SLACK_WHEN_TP_ONLY 0
#endif

#ifndef CODEX_ULTRA_SLACK_MULT
#define CODEX_ULTRA_SLACK_MULT 1.0
#endif

struct Params {
    int K = 1;
    double S = 1, lat = 1, bw = 1, bytesPerToken = 1;
    int numLayers = 1;
    double SLO1 = 1, SLO2 = 1, tpUB = 1, tpBase = 0, distBase = 0;
    double wTp = 0.5, wC = 0.5;
};

struct PLCurve {
    vector<double> xs, ys;
    void add(double x, double y) { xs.push_back(x); ys.push_back(y); }
    void finish() {
        vector<int> order(xs.size());
        iota(order.begin(), order.end(), 0);
        sort(order.begin(), order.end(), [&](int a, int b) { return xs[a] < xs[b]; });
        vector<double> nx, ny;
        for (int i : order) { nx.push_back(xs[i]); ny.push_back(ys[i]); }
        xs.swap(nx); ys.swap(ny);
    }
    double at(double x) const {
        if (xs.empty()) return 0.0;
        if (x <= xs.front()) return ys.front();
        if (x >= xs.back()) return ys.back();
        int hi = int(lower_bound(xs.begin(), xs.end(), x) - xs.begin());
        int lo = hi - 1;
        double span = xs[hi] - xs[lo];
        if (span <= 0) return ys[hi];
        return ys[lo] + (x - xs[lo]) / span * (ys[hi] - ys[lo]);
    }
};

enum { C_PPRE = 0, C_PPROC, C_PPOST, C_DPRE, C_DPROC, C_DPOST };
struct Table { PLCurve c[6]; };

enum { ST_PPRE = 0, ST_PPROC, ST_PPOST, ST_DPRE, ST_DPROC, ST_DPOST };
enum { EV_ARR = 0, EV_TDN, EV_XDN, EV_FIN };
enum { DIR_UP = 0, DIR_DOWN };
enum { KIND_PRE = 0, KIND_DEC };

struct Event {
    int type = 0;
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
    int server = -1, step = -1, remote = -1;
    int ls = 0, le = 0;
    vector<int> ids;
};

struct Response {
    int n = 0;
    Assign a[16];
};

namespace Sch {

Params P;
Table T;

enum ReqState {
    R_NEED_PPRE = 0, R_RUN_PPRE, R_WAIT_PRE_UP,
    R_NEED_PPROC, R_RUN_PPROC, R_WAIT_PRE_DOWN,
    R_NEED_PPOST, R_RUN_PPOST,
    R_NEED_DPRE, R_RUN_DPRE, R_WAIT_DEC_UP,
    R_NEED_DPROC, R_RUN_DPROC, R_WAIT_DEC_DOWN,
    R_NEED_DPOST, R_RUN_DPOST, R_DONE
};

struct Request {
    int state = R_DONE;
    int lin = 1;
    int remote = 0;
    int layersDone = 0;
    int tokens = 0;
    int waveSize = 1;
    int waveId = -1;
    bool finished = false;
    double arrival = 0;
};

struct BusyRecord {
    int step = -1;
    int ls = 0, le = 0;
    vector<int> ids;
};

vector<Request> req;
deque<int> qPPre, qPPost;
vector<int> qDPre, qDPost;
vector<deque<int>> qPProc;
vector<vector<int>> qDProc;
bool eBusy = false;
vector<char> rBusy;
BusyRecord eRecord;
vector<BusyRecord> rRecord;
vector<int> remoteLoad;
vector<double> remoteCommitted;
vector<double> remoteFreeAt;
double linkTimePerToken = 0;
int activeRequests = 0;
double currentTime = 0;
double upFreeAt = 0, downFreeAt = 0;
long long pendingTransfers = 0;
int activeDecode = 0;
long long observedGaps = 0;
double gapSum = 0;
vector<double> lastTokenTime;
double completedTdrSum = 0;
long long completedTdrCount = 0;
double prefillPathCost(int id);
int nextWaveId = 0;

struct TransferForecast {
    double finish = 0;
    bool prefill = false;
    int remote = -1;
};

deque<TransferForecast> upForecast, downForecast;

void forecastTransfer(bool up, double tokens, bool prefill, int remote = -1) {
    double& freeAt = up ? upFreeAt : downFreeAt;
    freeAt = max(currentTime, freeAt) + P.lat + linkTimePerToken * tokens;
    (up ? upForecast : downForecast).push_back({freeAt, prefill, remote});
    pendingTransfers++;
}

double nextDecodeFinish(bool up, int remote = -1) {
    for (const TransferForecast& transfer : (up ? upForecast : downForecast)) {
        if (!transfer.prefill && (remote < 0 || transfer.remote == remote)) return transfer.finish;
    }
    return 1e300;
}

double predictedDownFinish(double taskFinish, double linkFree, double latency,
                           double timePerToken, int groupSize) {
    return max(taskFinish, linkFree) + latency + timePerToken * groupSize;
}

double predictedQueuedProcDownFinish(double now, double remoteFree, double linkFree,
                                     double procRuntime, double latency,
                                     double timePerToken, int groupSize) {
    double procFinish = max(now, remoteFree) + procRuntime;
    return predictedDownFinish(procFinish, linkFree, latency, timePerToken, groupSize);
}

double nextDownAfterRunningProc() {
    double finish = 1e300;
    for (int remote = 0; remote < P.K; ++remote) {
        if (!rBusy[remote] || rRecord[remote].step != ST_DPROC) continue;
        finish = min(finish, predictedDownFinish(remoteFreeAt[remote], downFreeAt,
                                                 P.lat, linkTimePerToken,
                                                 (int)rRecord[remote].ids.size()));
    }
    return finish;
}

double nextMissingWaveMember(const vector<int>& readyIds) {
    unordered_set<int> waves;
    for (int id : readyIds) if (req[id].waveId >= 0) waves.insert(req[id].waveId);
    double finish = 1e300;
    for (int id = 0; id < (int)req.size(); ++id) {
        const Request& r = req[id];
        if (!waves.count(r.waveId) || r.finished || r.state == R_NEED_DPOST
            || r.state == R_RUN_DPOST || r.state == R_DONE) continue;
        int remote = r.remote;
        if (r.state == R_WAIT_DEC_DOWN) {
            finish = min(finish, nextDecodeFinish(false, remote));
        } else if (r.state == R_RUN_DPROC) {
            int group = max(1, (int)rRecord[remote].ids.size());
            finish = min(finish, predictedDownFinish(remoteFreeAt[remote], downFreeAt,
                                                     P.lat, linkTimePerToken, group));
        } else if (r.state == R_NEED_DPROC) {
            int group = max(1, (int)qDProc[remote].size());
            double runtime = P.S + T.c[C_DPROC].at(group);
            finish = min(finish, predictedQueuedProcDownFinish(currentTime, remoteFreeAt[remote],
                                                               downFreeAt, runtime, P.lat,
                                                               linkTimePerToken, group));
        } else if (r.state == R_WAIT_DEC_UP) {
            double upFinish = nextDecodeFinish(true, remote);
            int group = max(1, (int)qDProc[remote].size() + 1);
            double runtime = P.S + T.c[C_DPROC].at(group);
            finish = min(finish, predictedQueuedProcDownFinish(upFinish, remoteFreeAt[remote],
                                                               downFreeAt, runtime, P.lat,
                                                               linkTimePerToken, group));
        }
    }
    return finish;
}

double lastPrefillFinish(bool up) {
    double finish = -1;
    for (const TransferForecast& transfer : (up ? upForecast : downForecast)) {
        if (transfer.prefill) finish = transfer.finish;
    }
    return finish;
}

bool linkReorderingSafe(int decodeCount, long long gapCount, double measuredTpot) {
    if (decodeCount <= 2) return true;
    return gapCount > 0 && P.SLO2 > 0 && measuredTpot < 0.75 * P.SLO2;
}

bool tdrReorderingWorth(double projectedMeanTdr) {
    return P.SLO1 > 0 && projectedMeanTdr > 0.5 * P.SLO1;
}

bool downlinkReleaseBlocked(double prefillFinish, double now, double taskTime, bool hasShorterRival) {
    return hasShorterRival && prefillFinish > now + 3.0 * taskTime;
}

bool shouldHoldForMerge(int currentSize, int targetSize, double nextFinish,
                        double now, double budget, bool hasInFlight) {
    return hasInFlight && currentSize < targetSize && nextFinish - now <= budget;
}

bool shouldHoldInitialDecodeFanIn(int currentSize, int targetSize,
                                  int decodeCount, int requestCount,
                                  bool allFirstTokens, bool hasInFlight,
                                  bool batchingPays) {
    return hasInFlight && batchingPays && allFirstTokens && decodeCount < requestCount
        && currentSize < targetSize;
}

bool futureEventGuaranteed(long long transferCount, bool computerBusy) {
    return transferCount > 0 || computerBusy;
}

bool anyWorkInFlight() {
    bool computerBusy = eBusy;
    for (char busy : rBusy) computerBusy = computerBusy || busy;
    return futureEventGuaranteed(pendingTransfers, computerBusy);
}

bool batchingWorthHolding(bool linkIsBottleneck, double latency,
                          double payloadTimePerToken, int groupSize) {
    if (!linkIsBottleneck) return true;
    return latency > 0.05 * (latency + payloadTimePerToken * max(1, groupSize));
}

int remoteBottleneckWidth(double remoteWork, double localWork, double linkWork, int computerCount) {
    return remoteWork > max(localWork, linkWork) ? computerCount : 1;
}

int computePipelineWidth(double remoteWork, double localWork, double linkWork, int computerCount) {
    return linkWork < max(remoteWork, localWork) ? computerCount : 1;
}

int representativeGroupSize(int live) {
    live = max(1, live);
    return live < 32 ? live : int(ceil(3.75 * sqrt(double(live)) - 1e-12));
}

int prefillPressureWidth(double remoteWork, double localWork, double linkWork,
                         double latency, double remoteUnitWork, int computerCount) {
    double competingWork = max(localWork, linkWork);
    if (remoteWork <= competingWork) return 1;
    if (latency < 0.5 * remoteUnitWork) return computerCount;
    if (competingWork <= 0) return computerCount;
    int needed = int(ceil(remoteWork / competingWork - 1e-12));
    return min(computerCount, max(1, needed));
}

int jointDecodeWidth(int live) {
    live = max(1, live);
    int bestWidth = 1;
    double bestRate = -1.0;
    for (int width = 1; width <= min(P.K, live); ++width) {
        for (int group = 1; group <= live; ++group) {
            int used = min(width, group);
            double perRemote = ceil(double(group) / used);
            double local = 2.0 * P.S + T.c[C_DPRE].at(group) + T.c[C_DPOST].at(group);
            double link = used * P.lat + linkTimePerToken * group;
            double remote = P.S + T.c[C_DPROC].at(perRemote);
            double roundTrip = local + 2.0 * link + remote;
            double rate = min({double(live) / roundTrip, double(group) / local,
                               double(group) / link, double(group) / remote});
            if (rate > bestRate + 1e-12) {
                bestRate = rate;
                bestWidth = width;
            }
        }
    }
    return bestWidth;
}

bool holdFitsTokenSlack(double currentGap, double wait, double slo, bool tokenWindowOpen) {
    return !tokenWindowOpen || slo <= 0 || currentGap + wait <= 0.75 * slo;
}

double tokenSlackFraction(double throughputWeight) {
    return throughputWeight >= 0.75 ? 1.0 : 0.75;
}

bool preferPrefillProc(int decodeCount, int requestCount, double oldestPrefillAge) {
    return decodeCount < requestCount || (P.SLO1 > 0 && oldestPrefillAge > 0.5 * P.SLO1);
}

int chooseLocalKind(bool hasDPost, bool hasPPost, bool hasDPre, bool hasPPre,
                    bool saturationMode) {
    const int balanced[] = {1, 0, 3, 2};
    const int saturationPre[] = {3, 1, 2, 0};
    const int saturationPost[] = {3, 1, 0, 2};
    const int* saturation = CODEX_SATURATION_POST_FIRST ? saturationPost : saturationPre;
    const int* order = saturationMode ? saturation : balanced;
    const bool available[] = {hasDPost, hasPPost, hasDPre, hasPPre};
    for (int k = 0; k < 4; ++k) if (available[order[k]]) return order[k];
    return -1;
}

double projectedMeanTdr() {
    double sum = completedTdrSum;
    long long count = completedTdrCount;
    for (int id = 0; id < (int)req.size(); ++id) {
        if (req[id].finished || req[id].state < R_NEED_PPRE || req[id].state > R_RUN_PPOST) continue;
        sum += max(currentTime - req[id].arrival, prefillPathCost(id));
        count++;
    }
    return count ? sum / count : 0.0;
}

void ensureRequest(int id) {
    if ((int)req.size() <= id) req.resize(id + 1);
}

int bestRemoteCount() {
    int live = max(1, activeRequests);
    int bestCount = 1;
    double bestRate = -1.0;
    for (int count = 1; count <= min(P.K, live); ++count) {
        int used = min(count, live);
        double perRemote = ceil(double(live) / used);
        double local = 2.0 * P.S + T.c[C_DPRE].at(live) + T.c[C_DPOST].at(live);
        double link = used * P.lat + linkTimePerToken * live;
        double remote = P.S + T.c[C_DPROC].at(perRemote);
        double roundTrip = local + 2.0 * link + remote;
        double rate = min({double(live) / roundTrip, double(live) / local,
                           double(live) / link, double(live) / remote});
        if (rate > bestRate + 1e-12) {
            bestRate = rate;
            bestCount = count;
        }
    }
    if constexpr (CODEX_FIXED_WIDTH > 0) return min({CODEX_FIXED_WIDTH, P.K, live});
    if (P.wTp >= CODEX_ANALYTIC_MIN_WTP) {
        return live <= 64 ? jointDecodeWidth(live) : bestCount;
    }
    int useful = min(P.K, live);
    double wideLink = useful * P.lat + linkTimePerToken * live;
    double wideRemote = P.S + T.c[C_DPROC].at(ceil(double(live) / useful));
    double wideLocal = 2.0 * P.S + T.c[C_DPRE].at(live) + T.c[C_DPOST].at(live);
    if constexpr (CODEX_WIDTH_FEATURES & 1) {
        bestCount = max(bestCount,
                        computePipelineWidth(wideRemote, wideLocal, wideLink, useful));
    }

    double remoteOne = P.S + T.c[C_DPROC].at(1.0);
    double localOne = 2.0 * P.S + T.c[C_DPRE].at(1.0) + T.c[C_DPOST].at(1.0);
    double linkOne = P.lat + linkTimePerToken;
    int bottleneckWidth = remoteBottleneckWidth(remoteOne, localOne, linkOne, useful);
    if (bottleneckWidth > 1) {
        if (wideLink >= max(wideRemote, wideLocal)) bottleneckWidth = 1;
    }
    if constexpr (CODEX_WIDTH_FEATURES & 2) {
        bestCount = max(bestCount, bottleneckWidth);
    }

    double prefillRemoteWork = 0.0;
    double prefillLocalWork = 0.0;
    double prefillLinkWork = 0.0;
    for (const Request& r : req) {
        if (r.finished || r.state < R_NEED_PPRE || r.state > R_RUN_PPOST) continue;
        if (r.state <= R_RUN_PPRE) {
            prefillLocalWork += P.S + T.c[C_PPRE].at(r.lin);
        }
        if (r.state <= R_RUN_PPROC) {
            prefillRemoteWork += P.S + T.c[C_PPROC].at(r.lin);
        }
        if (r.state <= R_WAIT_PRE_UP) {
            prefillLinkWork += P.lat + linkTimePerToken * r.lin;
        }
        if (r.state <= R_WAIT_PRE_DOWN) {
            prefillLinkWork += P.lat + linkTimePerToken * r.lin;
        }
        if (r.state <= R_RUN_PPOST) {
            prefillLocalWork += P.S + T.c[C_PPOST].at(r.lin);
        }
    }
    if constexpr (CODEX_WIDTH_FEATURES & 4) {
      if (P.wTp <= CODEX_PRESSURE_MAX_WTP) {
        bestCount = max(bestCount,
                        prefillPressureWidth(prefillRemoteWork, prefillLocalWork,
                                             prefillLinkWork, P.lat, remoteOne, useful));
      }
    }

    int representativeGroup = representativeGroupSize(live);
    if constexpr (CODEX_WIDTH_FEATURES & 8) {
      if (!batchingWorthHolding(true, P.lat, linkTimePerToken, representativeGroup)) {
        bestCount = useful;
      }
    }
    return bestCount;
}

int bestDecodeGroupSize() {
    int live = max(1, activeDecode);
    int remoteCount = bestRemoteCount();
    int bestSize = 1;
    double bestRate = -1;
    for (int size = 1; size <= live; ++size) {
        int used = min({remoteCount, size, P.K});
        double perRemote = ceil(double(size) / used);
        double local = 2.0 * P.S + T.c[C_DPRE].at(size) + T.c[C_DPOST].at(size);
        double link = used * P.lat + linkTimePerToken * size;
        double remote = P.S + T.c[C_DPROC].at(perRemote);
        double roundTrip = local + 2.0 * link + remote;
        double rate = min({double(live) / roundTrip, double(size) / local,
                           double(size) / link, double(size) / remote});
        if (rate > bestRate + 1e-12) {
            bestRate = rate;
            bestSize = size;
        }
    }
    return bestSize;
}

int expandedGroupTarget(int modelTarget, int live) {
    if (modelTarget <= 1) return 1;
    return min(live, (5 * modelTarget + 3) / 4);
}

int scoreAwareGroupTarget(int modelTarget, int live, double throughputWeight) {
    int target = expandedGroupTarget(modelTarget, live);
    if (modelTarget > 1 && throughputWeight >= 0.75) {
        double multiplier = throughputWeight >= 0.999 ? CODEX_ULTRA_GROUP_MULT
                                                      : CODEX_HIGH_GROUP_MULT;
        target = min(live, int(ceil(multiplier * modelTarget - 1e-12)));
    }
    if (live >= 32 && throughputWeight >= 0.75 && CODEX_HIGH_POP_COEFF > 0) {
        double coefficient = throughputWeight >= 0.999 ? CODEX_ULTRA_POP_COEFF
                                                       : CODEX_HIGH_POP_COEFF;
        int populationTarget = int(ceil(coefficient * sqrt(double(live)) - 1e-12));
        target = min(live, max(target, populationTarget));
    }
    return target;
}

int fanInTarget(int modelTarget, const vector<int>& readyWaveSizes, int live) {
    int target = modelTarget;
    for (int size : readyWaveSizes) target = max(target, size);
    return min(max(1, live), target);
}

bool predictedLinkBottleneck(int groupSize) {
    int size = max(1, groupSize);
    int used = min({bestRemoteCount(), size, P.K});
    double link = used * P.lat + linkTimePerToken * size;
    double local = 2.0 * P.S + T.c[C_DPRE].at(size) + T.c[C_DPOST].at(size);
    double remote = P.S + T.c[C_DPROC].at(ceil(double(size) / used));
    return link >= local && link >= remote;
}

double mergeSaving(const PLCurve& curve, int currentSize) {
    double size = max(1, currentSize);
    return P.S + max(0.0, 2.0 * curve.at(size) - curve.at(2.0 * size));
}

int chooseRemote() {
    int count = bestRemoteCount();
    return int(min_element(remoteCommitted.begin(), remoteCommitted.begin() + count)
               - remoteCommitted.begin());
}

double prefillPathCost(int id) {
    double lin = req[id].lin;
    return 3.0 * P.S + T.c[C_PPRE].at(lin) + T.c[C_PPROC].at(lin) + T.c[C_PPOST].at(lin)
         + 2.0 * (P.lat + linkTimePerToken * lin);
}

int popBestPrefill() {
    auto best = qPPre.begin();
    for (auto it = next(qPPre.begin()); it != qPPre.end(); ++it) {
        if (prefillPathCost(*it) < prefillPathCost(*best)) best = it;
    }
    int id = *best;
    qPPre.erase(best);
    return id;
}

int bestPrefillProc(int remote) {
    int best = qPProc[remote].front();
    for (int id : qPProc[remote]) {
        if (prefillPathCost(id) < prefillPathCost(best)) best = id;
    }
    return best;
}

bool hasShorterUpstream(int id) {
    for (int other = 0; other < (int)req.size(); ++other) {
        if (other == id || req[other].finished) continue;
        if (req[other].state >= R_RUN_PPRE && req[other].state <= R_RUN_PPROC
            && req[other].lin < req[id].lin) return true;
    }
    return false;
}

Assign& addAssignment(Response& out, int server, int step, int remote);
void remember(BusyRecord& record, const Assign& a);

bool startPrefillProc(int remote, double now, Response& out) {
    if (qPProc[remote].empty()) return false;
    int id = bestPrefillProc(remote);
    double taskTime = P.S + T.c[C_PPROC].at(req[id].lin);
    bool hold = pendingTransfers > 0
             && linkReorderingSafe(activeDecode, observedGaps,
                                   observedGaps ? gapSum / observedGaps : 0.0)
             && tdrReorderingWorth(projectedMeanTdr())
             && downlinkReleaseBlocked(lastPrefillFinish(false), now, taskTime,
                                       hasShorterUpstream(id));
    if (hold) return false;
    auto it = find(qPProc[remote].begin(), qPProc[remote].end(), id);
    qPProc[remote].erase(it);
    Assign& a = addAssignment(out, remote, ST_PPROC, remote);
    a.ls = req[id].layersDone;
    a.le = P.numLayers;
    a.ids.push_back(id);
    req[id].state = R_RUN_PPROC;
    remember(rRecord[remote], a);
    rBusy[remote] = true;
    return true;
}

Assign& addAssignment(Response& out, int server, int step, int remote) {
    Assign& a = out.a[out.n++];
    a = Assign();
    a.server = server;
    a.step = step;
    a.remote = remote;
    return a;
}

void remember(BusyRecord& record, const Assign& a) {
    record.step = a.step;
    record.ls = a.ls;
    record.le = a.le;
    record.ids = a.ids;
    if (a.server >= 0) {
        double duration = 0;
        if (a.step == ST_PPROC) {
            int id = a.ids[0];
            duration = double(a.le - a.ls) / max(1, P.numLayers) * T.c[C_PPROC].at(req[id].lin);
        } else {
            duration = T.c[C_DPROC].at((double)a.ids.size());
        }
        remoteFreeAt[a.server] = currentTime + P.S + duration;
    }
}

void finishTask(int server) {
    BusyRecord& record = server < 0 ? eRecord : rRecord[server];
    if (server < 0) eBusy = false;
    else rBusy[server] = false;

    if (record.step == ST_PPRE) {
        int id = record.ids[0];
        req[id].state = R_WAIT_PRE_UP;
        forecastTransfer(true, req[id].lin, true, req[id].remote);
    } else if (record.step == ST_PPROC) {
        int id = record.ids[0];
        double fraction = double(record.le - record.ls) / max(1, P.numLayers);
        remoteCommitted[server] = max(0.0, remoteCommitted[server]
                                      - P.S - fraction * T.c[C_PPROC].at(req[id].lin));
        req[id].layersDone = record.le;
        req[id].state = R_WAIT_PRE_DOWN;
        forecastTransfer(false, req[id].lin, true, req[id].remote);
    } else if (record.step == ST_PPOST) {
        int id = record.ids[0];
        req[id].state = R_NEED_DPRE;
        qDPre.push_back(id);
        activeDecode++;
        completedTdrSum += currentTime - req[id].arrival;
        completedTdrCount++;
    } else if (record.step == ST_DPRE) {
        vector<int> counts(P.K, 0);
        for (int id : record.ids) {
            req[id].state = R_WAIT_DEC_UP;
            counts[req[id].remote]++;
        }
        for (int remote = 0; remote < P.K; ++remote) {
            if (counts[remote]) forecastTransfer(true, counts[remote], false, remote);
        }
    } else if (record.step == ST_DPROC) {
        for (int id : record.ids) req[id].state = R_WAIT_DEC_DOWN;
        forecastTransfer(false, record.ids.size(), false, server);
    } else if (record.step == ST_DPOST) {
        for (int id : record.ids) {
            if (req[id].tokens > 0) {
                gapSum += currentTime - lastTokenTime[id];
                observedGaps++;
            }
            lastTokenTime[id] = currentTime;
            req[id].tokens++;
            if (!req[id].finished) {
                req[id].state = R_NEED_DPRE;
                qDPre.push_back(id);
            }
        }
    }
    record = BusyRecord();
}

void finishTransfer(const Event& e, const vector<int>& ids) {
    deque<TransferForecast>& forecast = e.a == DIR_UP ? upForecast : downForecast;
    if (!forecast.empty()) forecast.pop_front();
    pendingTransfers--;
    if (e.b == KIND_PRE) {
        int id = ids[e.off];
        if (e.a == DIR_UP) {
            req[id].state = R_NEED_PPROC;
            qPProc[req[id].remote].push_back(id);
        } else {
            req[id].state = R_NEED_PPOST;
            qPPost.push_back(id);
        }
    } else if (e.a == DIR_UP) {
        for (int k = 0; k < e.cnt; ++k) {
            int id = ids[e.off + k];
            req[id].state = R_NEED_DPROC;
            qDProc[req[id].remote].push_back(id);
        }
    } else {
        for (int k = 0; k < e.cnt; ++k) {
            int id = ids[e.off + k];
            req[id].state = R_NEED_DPOST;
            qDPost.push_back(id);
        }
    }
}

void schedInit(const Params& p, const Table& t) {
    P = p;
    T = t;
    req.clear();
    qPPre.clear(); qPPost.clear(); qDPre.clear(); qDPost.clear();
    qPProc.assign(P.K, {});
    qDProc.assign(P.K, {});
    eBusy = false;
    rBusy.assign(P.K, false);
    eRecord = BusyRecord();
    rRecord.assign(P.K, BusyRecord());
    remoteLoad.assign(P.K, 0);
    remoteCommitted.assign(P.K, 0.0);
    remoteFreeAt.assign(P.K, 0.0);
    linkTimePerToken = 8.0 * P.bytesPerToken / (P.bw * 1e6);
    activeRequests = 0;
    currentTime = 0;
    upFreeAt = downFreeAt = 0;
    pendingTransfers = 0;
    upForecast.clear(); downForecast.clear();
    activeDecode = 0;
    observedGaps = 0;
    gapSum = 0;
    lastTokenTime.clear();
    completedTdrSum = 0;
    completedTdrCount = 0;
    nextWaveId = 0;
}

void schedFrame(double t, const Frame& f, Response& out) {
    out.n = 0;
    currentTime = t;

    for (const Event& e : f.evs) {
        if (e.type != EV_FIN) continue;
        ensureRequest(e.a);
        Request& r = req[e.a];
        r.finished = true;
        r.state = R_DONE;
        if (r.remote >= 0 && r.remote < P.K) remoteLoad[r.remote]--;
        activeRequests--;
        activeDecode--;
    }

    for (const Event& e : f.evs) {
        if (e.type == EV_ARR) {
            ensureRequest(e.a);
            Request& r = req[e.a];
            r = Request();
            r.state = R_NEED_PPRE;
            r.lin = e.b;
            r.arrival = t;
            qPPre.push_back(e.a);
            activeRequests++;
            if ((int)lastTokenTime.size() <= e.a) lastTokenTime.resize(e.a + 1, 0.0);
        } else if (e.type == EV_TDN) {
            finishTask(e.a);
        } else if (e.type == EV_XDN) {
            finishTransfer(e, f.ids);
        }
    }

    bool holdDPost = false;
    if (!qDPost.empty()) {
        vector<int> waveSizes;
        waveSizes.reserve(qDPost.size());
        for (int id : qDPost) waveSizes.push_back(req[id].waveSize);
        int population = max(1, activeRequests);
        int target = fanInTarget(scoreAwareGroupTarget(bestDecodeGroupSize(), population, P.wTp),
                                 waveSizes, population);
        double nextFinish = min({nextDecodeFinish(false), nextDownAfterRunningProc(),
                                 nextMissingWaveMember(qDPost)});
        double wait = nextFinish - t;
        double waitMultiplier = P.wTp >= 0.999 ? CODEX_ULTRA_POST_WAIT_MULT
                                               : CODEX_POST_WAIT_MULT;
        double budget = waitMultiplier * mergeSaving(T.c[C_DPOST], qDPost.size());
        bool safe = true;
        double slackMultiplier = P.wTp >= 0.999 ? CODEX_ULTRA_SLACK_MULT : 1.0;
        double effectiveSlo = P.SLO2 * tokenSlackFraction(P.wTp) * slackMultiplier / 0.75;
        if (!(CODEX_IGNORE_SLACK_WHEN_TP_ONLY && P.wC <= 1e-12)) {
            for (int id : qDPost) {
                double currentGap = req[id].tokens > 0 ? t - lastTokenTime[id] : 0.0;
                if (!holdFitsTokenSlack(currentGap, wait, effectiveSlo, req[id].tokens > 0)) {
                    safe = false;
                    break;
                }
            }
        }
        int representative = representativeGroupSize(population);
        holdDPost = safe
                 && batchingWorthHolding(predictedLinkBottleneck(representative), P.lat,
                                         linkTimePerToken, representative)
                 && shouldHoldForMerge((int)qDPost.size(), target, nextFinish,
                                       t, budget, anyWorkInFlight());
    }

    if (!eBusy) {
        bool ppreReady = !qPPre.empty()
                       && !(pendingTransfers > 0
                            && linkReorderingSafe(activeDecode, observedGaps,
                                                  observedGaps ? gapSum / observedGaps : 0.0)
                            && tdrReorderingWorth(projectedMeanTdr())
                            && lastPrefillFinish(true) > t + 3.0 * (P.S + T.c[C_PPRE].at(req[qPPre.front()].lin)));
        bool holdDPre = false;
        if (!qDPre.empty() && P.wTp >= 0.75 && activeRequests >= 32) {
            bool allFirstTokens = true;
            for (int id : qDPre) allFirstTokens = allFirstTokens && req[id].tokens == 0;
            int target = scoreAwareGroupTarget(bestDecodeGroupSize(), activeRequests, P.wTp);
            int representative = representativeGroupSize(activeRequests);
            bool batchingPays = batchingWorthHolding(predictedLinkBottleneck(representative),
                                                      P.lat, linkTimePerToken,
                                                      representative);
            holdDPre = shouldHoldInitialDecodeFanIn((int)qDPre.size(), target,
                                                    activeDecode, activeRequests,
                                                    allFirstTokens, anyWorkInFlight(),
                                                    batchingPays);
        }
        int choice = chooseLocalKind(!qDPost.empty() && !holdDPost, !qPPost.empty(),
                                     !qDPre.empty() && !holdDPost && !holdDPre, ppreReady,
                                     P.wTp >= 0.75);
        if (choice == 1) {
            int id = qPPost.front(); qPPost.pop_front();
            Assign& a = addAssignment(out, -1, ST_PPOST, req[id].remote);
            a.ids.push_back(id);
            req[id].state = R_RUN_PPOST;
            remember(eRecord, a); eBusy = true;
        } else if (choice == 0) {
            Assign& a = addAssignment(out, -1, ST_DPOST, -1);
            a.ids.swap(qDPost);
            for (int id : a.ids) req[id].state = R_RUN_DPOST;
            remember(eRecord, a); eBusy = true;
        } else if (choice == 3) {
            int id = popBestPrefill();
            int remote = chooseRemote();
            req[id].remote = remote;
            remoteLoad[remote]++;
            remoteCommitted[remote] += P.S + T.c[C_PPROC].at(req[id].lin);
            Assign& a = addAssignment(out, -1, ST_PPRE, remote);
            a.ids.push_back(id);
            req[id].state = R_RUN_PPRE;
            remember(eRecord, a); eBusy = true;
        } else if (choice == 2) {
            Assign& a = addAssignment(out, -1, ST_DPRE, -1);
            a.ids.swap(qDPre);
            int waveId = nextWaveId++;
            for (int id : a.ids) {
                req[id].state = R_RUN_DPRE;
                req[id].waveSize = (int)a.ids.size();
                req[id].waveId = waveId;
            }
            remember(eRecord, a); eBusy = true;
        }
    }

    for (int remote = 0; remote < P.K; ++remote) {
        if (rBusy[remote]) continue;
        double oldestPrefillAge = 0;
        for (int id : qPProc[remote]) oldestPrefillAge = max(oldestPrefillAge, t - req[id].arrival);
        bool prefillFirst = !qPProc[remote].empty()
                         && preferPrefillProc(activeDecode, activeRequests, oldestPrefillAge);
        if (prefillFirst && startPrefillProc(remote, t, out)) continue;

        bool runDecode = !qDProc[remote].empty();
        if (runDecode) {
            int waveTarget = scoreAwareGroupTarget(bestDecodeGroupSize(),
                                                   max(1, activeRequests), P.wTp);
            int target = max(1, (waveTarget + bestRemoteCount() - 1) / bestRemoteCount());
            int representative = representativeGroupSize(activeRequests);
            double waitMultiplier = P.wTp >= 0.999 ? CODEX_ULTRA_PROC_WAIT_MULT
                                                   : CODEX_PROC_WAIT_MULT;
            double budget = waitMultiplier * mergeSaving(T.c[C_DPROC], qDProc[remote].size());
            if (batchingWorthHolding(predictedLinkBottleneck(representative), P.lat,
                                     linkTimePerToken, representative)
                && shouldHoldForMerge((int)qDProc[remote].size(), target,
                                   nextDecodeFinish(true, remote), t, budget,
                                   anyWorkInFlight())) runDecode = false;
        }
        if (runDecode) {
            Assign& a = addAssignment(out, remote, ST_DPROC, remote);
            a.ids.swap(qDProc[remote]);
            for (int id : a.ids) req[id].state = R_RUN_DPROC;
            remember(rRecord[remote], a); rBusy[remote] = true;
        } else if (!qPProc[remote].empty()) {
            startPrefillProc(remote, t, out);
        }
    }
}

}  // namespace Sch

#ifndef LOCAL_SIM
#if defined(_WIN32)
#include <io.h>
#define SOL_READ(buf, n) _read(0, (buf), (unsigned int)(n))
#else
#include <unistd.h>
#define SOL_READ(buf, n) read(0, (buf), (size_t)(n))
#endif

static char inputBuffer[1 << 16];
static int inputPos = 0, inputLen = 0;

static inline int nextChar() {
    if (inputPos == inputLen) {
        inputPos = 0;
        do {
            inputLen = (int)SOL_READ(inputBuffer, sizeof(inputBuffer));
        } while (inputLen < 0 && errno == EINTR);
        if (inputLen <= 0) { inputLen = 0; return -1; }
    }
    return (unsigned char)inputBuffer[inputPos++];
}

static char tokenBuffer[64];

static inline bool readToken() {
    int c = nextChar();
    while (c == ' ' || c == '\n' || c == '\r' || c == '\t') c = nextChar();
    if (c < 0) return false;
    int n = 0;
    while (c > ' ') {
        if (n < 63) tokenBuffer[n++] = (char)c;
        c = nextChar();
    }
    tokenBuffer[n] = 0;
    return n > 0;
}

static inline int parseInt(const char* s) {
    int sign = 1;
    if (*s == '+') ++s;
    else if (*s == '-') { sign = -1; ++s; }
    int value = 0;
    while (*s >= '0' && *s <= '9') value = value * 10 + (*s++ - '0');
    return sign * value;
}

static inline int readInt() { readToken(); return parseInt(tokenBuffer); }
static inline double readDouble() { readToken(); return strtod(tokenBuffer, nullptr); }

static string outputBuffer;

static inline void appendInt(int value) {
    char digits[12];
    int n = 0;
    if (value < 0) { outputBuffer += '-'; value = -value; }
    do { digits[n++] = char('0' + value % 10); value /= 10; } while (value);
    while (n) outputBuffer += digits[--n];
}

static void emit(const Response& response) {
    outputBuffer.clear();
    appendInt(response.n); outputBuffer += '\n';
    for (int k = 0; k < response.n; ++k) {
        const Assign& a = response.a[k];
        if (a.server < 0) outputBuffer += 'E';
        else { outputBuffer += 'C'; appendInt(a.server); }
        outputBuffer += ' ';
        switch (a.step) {
            case ST_PPRE:
                outputBuffer += "P PRE "; appendInt(a.remote); outputBuffer += ' '; appendInt(a.ids[0]);
                break;
            case ST_PPROC:
                outputBuffer += "P PROC "; appendInt(a.ls); outputBuffer += ' '; appendInt(a.le);
                outputBuffer += ' '; appendInt(a.remote); outputBuffer += ' '; appendInt(a.ids[0]);
                break;
            case ST_PPOST:
                outputBuffer += "P POST "; appendInt(a.remote); outputBuffer += ' '; appendInt(a.ids[0]);
                break;
            case ST_DPRE:
                outputBuffer += "D PRE -1 "; appendInt((int)a.ids.size());
                for (int id : a.ids) { outputBuffer += ' '; appendInt(id); }
                break;
            case ST_DPROC:
                outputBuffer += "D PROC "; appendInt(a.remote); outputBuffer += ' '; appendInt((int)a.ids.size());
                for (int id : a.ids) { outputBuffer += ' '; appendInt(id); }
                break;
            case ST_DPOST:
                outputBuffer += "D POST -1 "; appendInt((int)a.ids.size());
                for (int id : a.ids) { outputBuffer += ' '; appendInt(id); }
                break;
        }
        outputBuffer += '\n';
    }
    fwrite(outputBuffer.data(), 1, outputBuffer.size(), stdout);
    fflush(stdout);
}

int main() {
    Params p;
    p.K = readInt(); p.S = readDouble(); p.lat = readDouble(); p.bw = readDouble();
    p.bytesPerToken = readDouble(); p.numLayers = readInt();
    p.SLO1 = readDouble(); p.SLO2 = readDouble(); p.tpUB = readDouble(); p.tpBase = readDouble();
    p.distBase = readDouble(); p.wTp = readDouble(); p.wC = readDouble();

    Table table;
    int rows = readInt();
    for (int r = 0; r < rows; ++r) {
        double batchSize = readDouble();
        for (int c = 0; c < 6; ++c) {
            double value = readDouble();
            if (value >= 0) table.c[c].add(batchSize, value);
        }
    }
    for (int c = 0; c < 6; ++c) table.c[c].finish();
    Sch::schedInit(p, table);

    Frame frame;
    static Response response;
    while (true) {
        if (!readToken()) break;
        if (tokenBuffer[0] == 'E') break;
        frame.clear();
        frame.t = strtod(tokenBuffer, nullptr);
        int eventCount = readInt();
        for (int q = 0; q < eventCount; ++q) {
            readToken();
            if (tokenBuffer[0] == 'A') {
                Event e; e.type = EV_ARR; e.a = readInt(); e.b = readInt();
                frame.evs.push_back(e);
            } else if (tokenBuffer[0] == 'T') {
                Event e; e.type = EV_TDN;
                readToken();
                e.a = tokenBuffer[0] == 'E' ? -1 : parseInt(tokenBuffer + 1);
                readToken();
                char taskClass = tokenBuffer[0];
                readToken();
                if (taskClass == 'P') {
                    if (tokenBuffer[0] == 'P' && tokenBuffer[1] == 'R' && tokenBuffer[2] == 'E') {
                        readInt(); readInt();
                    } else if (tokenBuffer[2] == 'O' && tokenBuffer[3] == 'C') {
                        readInt(); readInt(); readInt(); readInt();
                    } else {
                        readInt(); readInt();
                    }
                } else {
                    readInt();
                    int count = readInt();
                    for (int z = 0; z < count; ++z) readInt();
                }
                readToken();
                frame.evs.push_back(e);
            } else if (tokenBuffer[0] == 'X') {
                Event e; e.type = EV_XDN;
                readToken(); e.a = tokenBuffer[0] == 'U' ? DIR_UP : DIR_DOWN;
                readInt();
                readToken();
                readToken(); e.b = tokenBuffer[0] == 'P' ? KIND_PRE : KIND_DEC;
                int count = readInt();
                e.off = (int)frame.ids.size(); e.cnt = count;
                for (int z = 0; z < count; ++z) frame.ids.push_back(readInt());
                frame.evs.push_back(e);
            } else {
                Event e; e.type = EV_FIN; e.a = readInt();
                frame.evs.push_back(e);
            }
        }
        Sch::schedFrame(frame.t, frame, response);
        emit(response);
    }
    return 0;
}
#endif
