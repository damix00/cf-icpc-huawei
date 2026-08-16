#define LOCAL_SIM
#include "sol_v0.cpp"

struct LocalCase {
    Params p;
    Table t;
    int requests = 0;
    double meanLin = 0;
    double meanLout = 0;
    std::vector<int> inputLengths;
};

static bool loadCase(const char* path, LocalCase& tc) {
    std::ifstream in(path);
    if (!in) return false;
    in >> tc.p.K >> tc.p.S >> tc.p.lat >> tc.p.bw >> tc.p.bytesPerToken >> tc.p.numLayers;
    in >> tc.p.SLO1 >> tc.p.SLO2 >> tc.p.tpUB >> tc.p.tpBase >> tc.p.distBase
       >> tc.p.wTp >> tc.p.wC;
    int rows = 0;
    in >> rows;
    for (int r = 0; r < rows; ++r) {
        double size = 0;
        in >> size;
        for (int c = 0; c < 6; ++c) {
            double value = 0;
            in >> value;
            if (value >= 0) tc.t.c[c].add(size, value);
        }
    }
    for (int c = 0; c < 6; ++c) tc.t.c[c].finish();
    in >> tc.requests;
    double sumLin = 0, sumLout = 0;
    for (int i = 0; i < tc.requests; ++i) {
        double arrival = 0;
        int lin = 0, lout = 0;
        in >> arrival >> lin >> lout;
        tc.inputLengths.push_back(lin);
        sumLin += lin;
        sumLout += lout;
    }
    tc.meanLin = sumLin / std::max(1, tc.requests);
    tc.meanLout = sumLout / std::max(1, tc.requests);
    return true;
}

static int workloadWidth(const LocalCase& tc, double meanOutput) {
    const Params& p = tc.p;
    const Table& t = tc.t;
    double u = 8.0 * p.bytesPerToken / (p.bw * 1e6);
    double prefillRemote = 0, prefillLocal = 0, prefillLink = 0;
    for (int lin : tc.inputLengths) {
        prefillRemote += p.S + t.c[C_PPROC].at(lin);
        prefillLocal += 2.0 * p.S + t.c[C_PPRE].at(lin) + t.c[C_PPOST].at(lin);
        prefillLink += p.lat + u * lin;
    }
    double tokens = tc.requests * meanOutput;
    int bestWidth = 1;
    double bestSpan = 1e300;
    for (int width = 1; width <= std::min(p.K, tc.requests); ++width) {
        double widthBest = 1e300;
        for (int group = 1; group <= tc.requests; ++group) {
            int used = std::min(width, group);
            double perRemote = std::ceil(double(group) / used);
            double rounds = std::ceil(tokens / group);
            double local = prefillLocal + rounds *
                         (2.0 * p.S + t.c[C_DPRE].at(group) + t.c[C_DPOST].at(group));
            double remote = prefillRemote / width + rounds *
                          (p.S + t.c[C_DPROC].at(perRemote));
            double link = prefillLink + rounds * (used * p.lat + u * group);
            double startup = 3.0 * p.S + t.c[C_DPRE].at(group)
                           + t.c[C_DPROC].at(perRemote) + t.c[C_DPOST].at(group)
                           + 2.0 * (used * p.lat + u * group);
            widthBest = std::min(widthBest, std::max({local, remote, link}) + startup);
        }
        if (widthBest < bestSpan - 1e-9) {
            bestSpan = widthBest;
            bestWidth = width;
        }
    }
    return bestWidth;
}

static int analyticWidth(int live) {
    int best = 1;
    double bestRate = -1;
    for (int width = 1; width <= std::min(Sch::P.K, live); ++width) {
        double perRemote = std::ceil(double(live) / width);
        double local = 2.0 * Sch::P.S + Sch::T.c[C_DPRE].at(live)
                     + Sch::T.c[C_DPOST].at(live);
        double link = width * Sch::P.lat + Sch::linkTimePerToken * live;
        double remote = Sch::P.S + Sch::T.c[C_DPROC].at(perRemote);
        double roundTrip = local + 2.0 * link + remote;
        double rate = std::min({double(live) / roundTrip, double(live) / local,
                                double(live) / link, double(live) / remote});
        if (rate > bestRate + 1e-12) {
            bestRate = rate;
            best = width;
        }
    }
    return best;
}

static int jointWidth(int live) {
    int bestWidth = 1;
    double bestRate = -1;
    for (int width = 1; width <= std::min(Sch::P.K, live); ++width) {
        for (int group = 1; group <= live; ++group) {
            int used = std::min(width, group);
            double perRemote = std::ceil(double(group) / used);
            double local = 2.0 * Sch::P.S + Sch::T.c[C_DPRE].at(group)
                         + Sch::T.c[C_DPOST].at(group);
            double link = used * Sch::P.lat + Sch::linkTimePerToken * group;
            double remote = Sch::P.S + Sch::T.c[C_DPROC].at(perRemote);
            double roundTrip = local + 2.0 * link + remote;
            double rate = std::min({double(live) / roundTrip, double(group) / local,
                                    double(group) / link, double(group) / remote});
            if (rate > bestRate + 1e-12) {
                bestRate = rate;
                bestWidth = width;
            }
        }
    }
    return bestWidth;
}

int main(int argc, char** argv) {
    for (int a = 1; a < argc; ++a) {
        LocalCase tc;
        if (!loadCase(argv[a], tc)) continue;
        Sch::schedInit(tc.p, tc.t);
        Sch::req.assign(tc.requests, Sch::Request());
        Sch::activeRequests = tc.requests;
        Sch::activeDecode = tc.requests;
        for (auto& r : Sch::req) {
            r.state = Sch::R_NEED_DPRE;
            r.lin = int(std::lround(tc.meanLin));
        }
        int base = analyticWidth(std::max(1, tc.requests));
        int full = Sch::bestRemoteCount();
        int group = Sch::bestDecodeGroupSize();
        std::printf("%s K=%d R=%d wTp=%.2f S=%.4g lat=%.4g u=%.4g Lin=%.1f Lout=%.1f width=%d->%d joint=%d group=%d work[8,32,64,128]=%d,%d,%d,%d actual=%d\n",
                    argv[a], tc.p.K, tc.requests, tc.p.wTp, tc.p.S, tc.p.lat,
                    Sch::linkTimePerToken, tc.meanLin, tc.meanLout, base, full,
                    jointWidth(tc.requests), group,
                    workloadWidth(tc, 8), workloadWidth(tc, 32), workloadWidth(tc, 64),
                    workloadWidth(tc, 128), workloadWidth(tc, tc.meanLout));
    }
}
