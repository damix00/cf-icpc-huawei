#define LOCAL_SIM
#include "sol_v0.cpp"

#include <cassert>
#include <cstdio>
#include <fstream>

static Params simple_params(int k = 1) {
    Params p;
    p.K = k;
    p.S = 1.0;
    p.lat = 2.0;
    p.bw = 1.0;
    p.bytesPerToken = 125000.0;
    p.numLayers = 4;
    p.SLO1 = 30.0;
    p.SLO2 = 15.0;
    p.tpUB = 0.0625;
    p.tpBase = 0.022222222;
    p.distBase = 1.0;
    p.wTp = 0.5;
    p.wC = 0.5;
    return p;
}

static Table simple_table() {
    Table t;
    const double values[6] = {3.0, 10.0, 2.0, 1.0, 4.0, 1.0};
    for (int c = 0; c < 6; ++c) {
        t.c[c].add(1.0, values[c]);
        t.c[c].add(4.0, values[c]);
        t.c[c].finish();
    }
    return t;
}

static Table size_sensitive_table() {
    Table t = simple_table();
    t.c[C_PPROC].xs.clear();
    t.c[C_PPROC].ys.clear();
    t.c[C_PPROC].add(1.0, 1.0);
    t.c[C_PPROC].add(4.0, 20.0);
    t.c[C_PPROC].finish();
    t.c[C_DPROC].xs.clear();
    t.c[C_DPROC].ys.clear();
    t.c[C_DPROC].add(1.0, 1.0);
    t.c[C_DPROC].add(4.0, 20.0);
    t.c[C_DPROC].finish();
    return t;
}

static std::pair<Params, Table> load_startup_model(const char* path) {
    std::ifstream in(path);
    assert(in);
    Params p;
    in >> p.K >> p.S >> p.lat >> p.bw >> p.bytesPerToken >> p.numLayers;
    in >> p.SLO1 >> p.SLO2 >> p.tpUB >> p.tpBase >> p.distBase >> p.wTp >> p.wC;
    int rows;
    in >> rows;
    Table table;
    for (int r = 0; r < rows; ++r) {
        double batchSize;
        in >> batchSize;
        for (int c = 0; c < 6; ++c) {
            double value;
            in >> value;
            if (value >= 0) table.c[c].add(batchSize, value);
        }
    }
    for (int c = 0; c < 6; ++c) table.c[c].finish();
    return {p, table};
}

static void load_initial_workload(const char* path) {
    std::ifstream in(path);
    assert(in);
    Params p;
    in >> p.K >> p.S >> p.lat >> p.bw >> p.bytesPerToken >> p.numLayers;
    in >> p.SLO1 >> p.SLO2 >> p.tpUB >> p.tpBase >> p.distBase >> p.wTp >> p.wC;
    int rows;
    in >> rows;
    Table table;
    for (int r = 0; r < rows; ++r) {
        double batchSize;
        in >> batchSize;
        for (int c = 0; c < 6; ++c) {
            double value;
            in >> value;
            if (value >= 0) table.c[c].add(batchSize, value);
        }
    }
    for (int c = 0; c < 6; ++c) table.c[c].finish();

    int requestCount;
    in >> requestCount;
    Frame first;
    first.t = 0.0;
    for (int i = 0; i < requestCount; ++i) {
        double arrival;
        int id, lin;
        in >> arrival >> id >> lin;
        Event event;
        event.type = EV_ARR;
        event.a = id;
        event.b = lin;
        first.evs.push_back(event);
    }
    Sch::schedInit(p, table);
    Response out;
    Sch::schedFrame(first.t, first, out);
}

static void test_lone_arrival_dispatches_prefill_pre() {
    Sch::schedInit(simple_params(), simple_table());

    Frame f;
    f.t = 0.0;
    Event arrival;
    arrival.type = EV_ARR;
    arrival.a = 0;
    arrival.b = 4;
    f.evs.push_back(arrival);

    Response out;
    Sch::schedFrame(f.t, f, out);

    assert(out.n == 1);
    assert(out.a[0].server == -1);
    assert(out.a[0].step == ST_PPRE);
    assert(out.a[0].remote == 0);
    assert(out.a[0].ids == std::vector<int>({0}));
}

static Response send_event(double t, Event event, std::vector<int> ids = {}) {
    Frame f;
    f.t = t;
    f.ids = std::move(ids);
    f.evs.push_back(event);
    Response out;
    Sch::schedFrame(t, f, out);
    return out;
}

static void assert_single(const Response& out, int server, int step, int remote, int rid) {
    assert(out.n == 1);
    assert(out.a[0].server == server);
    assert(out.a[0].step == step);
    assert(out.a[0].remote == remote);
    assert(out.a[0].ids == std::vector<int>({rid}));
}

static void test_request_walks_through_all_six_stages() {
    Sch::schedInit(simple_params(), simple_table());

    Event e;
    e.type = EV_ARR; e.a = 7; e.b = 4;
    assert_single(send_event(0.0, e), -1, ST_PPRE, 0, 7);

    e = Event(); e.type = EV_TDN; e.a = -1;
    assert(send_event(4.0, e).n == 0);

    e = Event(); e.type = EV_XDN; e.a = DIR_UP; e.b = KIND_PRE; e.off = 0; e.cnt = 1;
    Response out = send_event(10.0, e, {7});
    assert_single(out, 0, ST_PPROC, 0, 7);
    assert(out.a[0].ls == 0 && out.a[0].le == 4);

    e = Event(); e.type = EV_TDN; e.a = 0;
    assert(send_event(21.0, e).n == 0);

    e = Event(); e.type = EV_XDN; e.a = DIR_DOWN; e.b = KIND_PRE; e.off = 0; e.cnt = 1;
    assert_single(send_event(27.0, e, {7}), -1, ST_PPOST, 0, 7);

    e = Event(); e.type = EV_TDN; e.a = -1;
    assert_single(send_event(30.0, e), -1, ST_DPRE, -1, 7);

    e = Event(); e.type = EV_TDN; e.a = -1;
    assert(send_event(32.0, e).n == 0);

    e = Event(); e.type = EV_XDN; e.a = DIR_UP; e.b = KIND_DEC; e.off = 0; e.cnt = 1;
    assert_single(send_event(35.0, e, {7}), 0, ST_DPROC, 0, 7);

    e = Event(); e.type = EV_TDN; e.a = 0;
    assert(send_event(40.0, e).n == 0);

    e = Event(); e.type = EV_XDN; e.a = DIR_DOWN; e.b = KIND_DEC; e.off = 0; e.cnt = 1;
    assert_single(send_event(43.0, e, {7}), -1, ST_DPOST, -1, 7);

    Frame done;
    done.t = 45.0;
    e = Event(); e.type = EV_TDN; e.a = -1; done.evs.push_back(e);
    e = Event(); e.type = EV_FIN; e.a = 7; done.evs.push_back(e);
    Sch::schedFrame(done.t, done, out);
    assert(out.n == 0);
}

static void test_admission_selects_shortest_predicted_prefill_path() {
    Sch::schedInit(simple_params(), simple_table());
    Frame f;
    f.t = 0.0;
    Event a; a.type = EV_ARR; a.a = 0; a.b = 4; f.evs.push_back(a);
    a.a = 1; a.b = 1; f.evs.push_back(a);

    Response out;
    Sch::schedFrame(f.t, f, out);

    assert_single(out, -1, ST_PPRE, 0, 1);
}

static void test_remote_placement_uses_committed_work_not_request_count() {
    Params p = simple_params(2);
    p.lat = 0.001;
    p.bytesPerToken = 1.0;
    p.bw = 100.0;
    Sch::schedInit(p, size_sensitive_table());

    Event e; e.type = EV_ARR; e.a = 0; e.b = 4;
    assert_single(send_event(0.0, e), -1, ST_PPRE, 0, 0);

    Frame second;
    second.t = 4.0;
    e = Event(); e.type = EV_TDN; e.a = -1; second.evs.push_back(e);
    e = Event(); e.type = EV_ARR; e.a = 1; e.b = 1; second.evs.push_back(e);
    Response out;
    Sch::schedFrame(second.t, second, out);
    assert_single(out, -1, ST_PPRE, 1, 1);

    Frame third;
    third.t = 8.0;
    e = Event(); e.type = EV_TDN; e.a = -1; third.evs.push_back(e);
    e = Event(); e.type = EV_ARR; e.a = 2; e.b = 1; third.evs.push_back(e);
    Sch::schedFrame(third.t, third, out);
    assert_single(out, -1, ST_PPRE, 1, 2);
}

static void test_high_link_latency_concentrates_decode_homes() {
    Params p = simple_params(4);
    p.lat = 100.0;
    p.bytesPerToken = 1.0;
    p.bw = 100.0;
    Sch::schedInit(p, size_sensitive_table());

    Frame first;
    first.t = 0.0;
    Event e; e.type = EV_ARR; e.a = 0; e.b = 1; first.evs.push_back(e);
    e.a = 1; first.evs.push_back(e);
    Response out;
    Sch::schedFrame(first.t, first, out);
    assert_single(out, -1, ST_PPRE, 0, 0);

    e = Event(); e.type = EV_TDN; e.a = -1;
    out = send_event(4.0, e);
    assert(out.n == 0);
    Frame release;
    release.t = 95.0;
    Sch::schedFrame(release.t, release, out);
    assert_single(out, -1, ST_PPRE, 0, 1);
}

static void test_prefill_release_waits_until_uplink_needs_work() {
    Params p = simple_params();
    p.lat = 100.0;
    p.bytesPerToken = 1.0;
    p.bw = 100.0;
    Sch::schedInit(p, simple_table());

    Event e; e.type = EV_ARR; e.a = 0; e.b = 4;
    assert_single(send_event(0.0, e), -1, ST_PPRE, 0, 0);

    Frame blocked;
    blocked.t = 4.0;
    e = Event(); e.type = EV_TDN; e.a = -1; blocked.evs.push_back(e);
    e = Event(); e.type = EV_ARR; e.a = 1; e.b = 1; blocked.evs.push_back(e);
    Response out;
    Sch::schedFrame(blocked.t, blocked, out);
    assert(out.n == 0);

    Frame justInTime;
    justInTime.t = 95.0;
    Sch::schedFrame(justInTime.t, justInTime, out);
    assert_single(out, -1, ST_PPRE, 0, 1);
}

static void test_link_reordering_gate_protects_open_token_windows() {
    Sch::schedInit(simple_params(), simple_table());
    assert(Sch::linkReorderingSafe(2, 0, 0.0));
    assert(Sch::linkReorderingSafe(3, 10, 0.50 * simple_params().SLO2));
    assert(!Sch::linkReorderingSafe(3, 10, 0.90 * simple_params().SLO2));
}

static void test_tdr_reordering_gate_respects_zero_excess_region() {
    Sch::schedInit(simple_params(), simple_table());
    assert(!Sch::tdrReorderingWorth(0.49 * simple_params().SLO1));
    assert(Sch::tdrReorderingWorth(0.51 * simple_params().SLO1));
}

static void test_downlink_release_waits_only_for_a_shorter_rival() {
    assert(Sch::downlinkReleaseBlocked(100.0, 0.0, 10.0, true));
    assert(!Sch::downlinkReleaseBlocked(100.0, 0.0, 10.0, false));
    assert(!Sch::downlinkReleaseBlocked(25.0, 0.0, 10.0, true));
}

static void test_merge_hold_requires_known_nearby_work() {
    assert(Sch::shouldHoldForMerge(1, 4, 10.0, 0.0, 15.0, true));
    assert(!Sch::shouldHoldForMerge(4, 4, 10.0, 0.0, 15.0, true));
    assert(!Sch::shouldHoldForMerge(1, 4, 30.0, 0.0, 15.0, true));
    assert(!Sch::shouldHoldForMerge(1, 4, 10.0, 0.0, 15.0, false));
}

static void test_busy_computer_guarantees_a_future_frame() {
    assert(Sch::futureEventGuaranteed(1, false));
    assert(Sch::futureEventGuaranteed(0, true));
    assert(!Sch::futureEventGuaranteed(0, false));
}

static void test_payload_dominated_link_disables_merge_holds() {
    assert(!Sch::batchingWorthHolding(true, 1.0, 100.0, 5));
    assert(Sch::batchingWorthHolding(true, 10.0, 1.0, 5));
    assert(Sch::batchingWorthHolding(false, 1.0, 100.0, 5));
}

static void test_merge_hold_respects_remaining_token_slack() {
    assert(Sch::holdFitsTokenSlack(10.0, 5.0, 20.0, true));
    assert(!Sch::holdFitsTokenSlack(14.0, 2.0, 20.0, true));
    assert(Sch::holdFitsTokenSlack(100.0, 5.0, 20.0, false));
}

static void test_saturation_mode_can_use_full_token_slo() {
    assert(std::abs(Sch::tokenSlackFraction(0.50) - 0.75) < 1e-9);
    assert(std::abs(Sch::tokenSlackFraction(0.75) - 1.00) < 1e-9);
}

static void test_remote_compute_fills_decode_pool_before_recycling_tokens() {
    Sch::schedInit(simple_params(), simple_table());
    assert(Sch::preferPrefillProc(1, 2, 0.0));
    assert(!Sch::preferPrefillProc(2, 2, 0.0));
    assert(Sch::preferPrefillProc(2, 2, simple_params().SLO1));
}

static void test_remote_bottleneck_override_uses_parallel_computers() {
    assert(Sch::remoteBottleneckWidth(30.0, 10.0, 1.0, 6) == 6);
    assert(Sch::remoteBottleneckWidth(10.0, 30.0, 1.0, 6) == 1);
    assert(Sch::remoteBottleneckWidth(10.0, 1.0, 30.0, 6) == 1);
}

static void test_compute_bound_pipeline_uses_parallel_computers() {
    assert(Sch::computePipelineWidth(10.0, 30.0, 1.0, 6) == 6);
    assert(Sch::computePipelineWidth(30.0, 10.0, 1.0, 6) == 6);
    assert(Sch::computePipelineWidth(10.0, 1.0, 30.0, 6) == 1);
}

static void test_large_payload_workload_does_not_overexpand_remote_width() {
    load_initial_workload("hold/h_8_12.txt");
    assert(Sch::bestRemoteCount() == 2);
    for (int id = 0; id < (int)Sch::req.size(); ++id) {
        Sch::req[id].state = id % 2 ? Sch::R_RUN_PPROC : Sch::R_NEED_DPRE;
    }
    assert(Sch::bestRemoteCount() == 2);

    load_initial_workload("val/v_0_22.txt");
    assert(Sch::bestRemoteCount() == 8);

    load_initial_workload("tests/g_8_2.txt");
    assert(Sch::bestRemoteCount() == 7);

    load_initial_workload("judge/j_34.txt");
    assert(Sch::bestRemoteCount() == 1);

    load_initial_workload("judge/j_66.txt");
    assert(Sch::bestRemoteCount() == 5);
}

static void test_pipeline_target_compensates_for_unmodelled_overlap() {
    assert(Sch::expandedGroupTarget(1, 10) == 1);
    assert(Sch::expandedGroupTarget(3, 11) == 4);
    assert(Sch::expandedGroupTarget(8, 8) == 8);
}

static void test_saturation_group_target_amortizes_local_fixed_costs() {
    assert(Sch::representativeGroupSize(2000) == 168);
    assert(Sch::representativeGroupSize(49) == 27);
    assert(Sch::representativeGroupSize(25) == 25);
    assert(Sch::scoreAwareGroupTarget(10, 49, 0.75) == 27);
    assert(Sch::scoreAwareGroupTarget(10, 49, 1.00) == 16);
    assert(Sch::scoreAwareGroupTarget(1, 2000, 1.00) == 101);
    assert(Sch::scoreAwareGroupTarget(3, 11, 0.50) == 4);
    assert(Sch::scoreAwareGroupTarget(1, 25, 1.00) == 1);
}

static void test_initial_decode_wave_waits_for_the_known_population_target() {
    assert(Sch::shouldHoldInitialDecodeFanIn(4, 168, 4, 2000, true, true, true));
    assert(!Sch::shouldHoldInitialDecodeFanIn(168, 168, 168, 2000, true, true, true));
    assert(!Sch::shouldHoldInitialDecodeFanIn(4, 168, 4, 2000, false, true, true));
    assert(!Sch::shouldHoldInitialDecodeFanIn(4, 168, 4, 2000, true, false, true));
    assert(!Sch::shouldHoldInitialDecodeFanIn(4, 168, 4, 2000, true, true, false));
}

static void test_saturation_mode_prioritizes_pipeline_feeding() {
    assert(Sch::chooseLocalKind(true, true, true, true, false) == 1);
    assert(Sch::chooseLocalKind(true, true, true, true, true) == 3);
    assert(Sch::chooseLocalKind(true, false, true, false, false) == 0);
    assert(Sch::chooseLocalKind(true, false, true, false, true) == 2);
}

static void test_decode_fan_in_preserves_originating_wave_size() {
    assert(Sch::fanInTarget(3, std::vector<int>({6, 6, 6}), 10) == 6);
    assert(Sch::fanInTarget(5, std::vector<int>({3, 3}), 10) == 5);
    assert(Sch::fanInTarget(8, std::vector<int>({12}), 9) == 9);
}

static void test_downlink_forecast_looks_through_running_remote_compute() {
    assert(std::abs(Sch::predictedDownFinish(100.0, 80.0, 2.0, 1.0, 3) - 105.0) < 1e-9);
    assert(std::abs(Sch::predictedDownFinish(100.0, 120.0, 2.0, 1.0, 3) - 125.0) < 1e-9);
}

static void test_downlink_forecast_looks_through_queued_remote_compute() {
    assert(std::abs(Sch::predictedQueuedProcDownFinish(0.0, 10.0, 5.0, 20.0,
                                                       2.0, 1.0, 3) - 35.0) < 1e-9);
}

int main() {
    test_lone_arrival_dispatches_prefill_pre();
    test_request_walks_through_all_six_stages();
    test_admission_selects_shortest_predicted_prefill_path();
    test_remote_placement_uses_committed_work_not_request_count();
    test_high_link_latency_concentrates_decode_homes();
    test_prefill_release_waits_until_uplink_needs_work();
    test_link_reordering_gate_protects_open_token_windows();
    test_tdr_reordering_gate_respects_zero_excess_region();
    test_downlink_release_waits_only_for_a_shorter_rival();
    test_merge_hold_requires_known_nearby_work();
    test_busy_computer_guarantees_a_future_frame();
    test_payload_dominated_link_disables_merge_holds();
    test_merge_hold_respects_remaining_token_slack();
    test_saturation_mode_can_use_full_token_slo();
    test_remote_compute_fills_decode_pool_before_recycling_tokens();
    test_remote_bottleneck_override_uses_parallel_computers();
    test_compute_bound_pipeline_uses_parallel_computers();
    test_large_payload_workload_does_not_overexpand_remote_width();
    test_pipeline_target_compensates_for_unmodelled_overlap();
    test_saturation_group_target_amortizes_local_fixed_costs();
    test_initial_decode_wave_waits_for_the_known_population_target();
    test_saturation_mode_prioritizes_pipeline_feeding();
    test_decode_fan_in_preserves_originating_wave_size();
    test_downlink_forecast_looks_through_running_remote_compute();
    test_downlink_forecast_looks_through_queued_remote_compute();
    std::puts("scheduler tests passed: 24");
    return 0;
}
