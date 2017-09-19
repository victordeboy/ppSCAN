#include "GraphParallelExp.h"

#include <cstring>
#include <cmath>

#include <iostream>
#include <algorithm>
#include <numeric>

#include "../playground/pretty_print.h"
#include "../ThreadPool.h"

using namespace std::chrono;

GraphParallelExp::GraphParallelExp(const char *dir_string, const char *eps_s, int min_u, ui thread_num) {
    thread_num_ = thread_num;

    io_helper_ptr = yche::make_unique<InputOutput>(dir_string);
    io_helper_ptr->ReadGraph();

    auto tmp_start = high_resolution_clock::now();
    // 1st: parameter
    std::tie(eps_a2, eps_b2) = io_helper_ptr->ParseEps(eps_s);
    this->min_u = min_u;

    // 2nd: GraphParallelExp
    // csr representation
    n = static_cast<ui>(io_helper_ptr->n);
    out_edge_start = std::move(io_helper_ptr->offset_out_edges);
    out_edges = std::move(io_helper_ptr->out_edges);

    // edge properties
    min_cn = vector<int>(io_helper_ptr->m);
    std::fill(min_cn.begin(), min_cn.end(), NOT_SURE);

    // vertex properties
    degree = std::move(io_helper_ptr->degree);
    is_core_lst = vector<char>(n, FALSE);
    is_non_core_lst = vector<char>(n, FALSE);
    // 3rd: disjoint-set, make-set at the beginning
    disjoint_set_ptr = yche::make_unique<DisjointSet>(n);
    auto all_end = high_resolution_clock::now();
    cout << "other construct time:" << duration_cast<milliseconds>(all_end - tmp_start).count()
         << " ms\n";
}

void GraphParallelExp::Output(const char *eps_s, const char *miu) {
    io_helper_ptr->Output(eps_s, miu, noncore_cluster, is_core_lst, cluster_dict, disjoint_set_ptr->parent);
}

ui GraphParallelExp::BinarySearch(vector<int> &array, ui offset_beg, ui offset_end, int val) {
    auto mid = static_cast<ui>((static_cast<unsigned long>(offset_beg) + offset_end) / 2);
    if (array[mid] == val) { return mid; }
    return val < array[mid] ? BinarySearch(array, offset_beg, mid, val) : BinarySearch(array, mid + 1, offset_end, val);
}

int GraphParallelExp::ComputeCnLowerBound(int du, int dv) {
    auto c = (int) (sqrtl((((long double) du) * ((long double) dv) * eps_a2) / eps_b2));
    if (((long long) c) * ((long long) c) * eps_b2 < ((long long) du) * ((long long) dv) * eps_a2) { ++c; }
    return c;
}

void GraphParallelExp::Prune() {
    auto batch_size = 8192u;
    ThreadPool pool(thread_num_);
    for (auto v_i = 0; v_i < n; v_i += batch_size) {
        int my_start = v_i;
        int my_end = min(n, my_start + batch_size);

        pool.enqueue([this](int i_start, int i_end) {
            for (auto u = i_start; u < i_end; u++) {
                for (auto edge_idx = out_edge_start[u]; edge_idx < out_edge_start[u + 1]; edge_idx++) {
                    auto v = out_edges[edge_idx];
                    int deg_a = degree[u], deg_b = degree[v];
                    if (deg_a > deg_b) { swap(deg_a, deg_b); }
                    if (((long long) deg_a) * eps_b2 < ((long long) deg_b) * eps_a2) {
                        min_cn[edge_idx] = NOT_DIRECT_REACHABLE;
                    } else {
                        int c = ComputeCnLowerBound(deg_a, deg_b);
                        min_cn[edge_idx] = c <= 2 ? DIRECT_REACHABLE : c;
                    }
                }
            }
        }, my_start, my_end);
    }
}

int GraphParallelExp::IntersectNeighborSets(int u, int v, int min_cn_num) {
    int cn = 2; // count for self and v, count for self and u
    int du = out_edge_start[u + 1] - out_edge_start[u] + 2, dv =
            out_edge_start[v + 1] - out_edge_start[v] + 2; // count for self and v, count for self and u

    auto offset_nei_u = out_edge_start[u], offset_nei_v = out_edge_start[v];

    // correctness guaranteed by two pruning previously in computing min_cn
    while (cn < min_cn_num) {
        while (out_edges[offset_nei_u] < out_edges[offset_nei_v]) {
            --du;
            if (du < min_cn_num) { return NOT_DIRECT_REACHABLE; }
            ++offset_nei_u;
        }
        while (out_edges[offset_nei_u] > out_edges[offset_nei_v]) {
            --dv;
            if (dv < min_cn_num) { return NOT_DIRECT_REACHABLE; }
            ++offset_nei_v;
        }
        if (out_edges[offset_nei_u] == out_edges[offset_nei_v]) {
            ++cn;
            ++offset_nei_u;
            ++offset_nei_v;
        }
    }
    return cn >= min_cn_num ? DIRECT_REACHABLE : NOT_DIRECT_REACHABLE;
}

int GraphParallelExp::EvalReachable(int u, ui edge_idx) {
    int v = out_edges[edge_idx];
    return IntersectNeighborSets(u, v, min_cn[edge_idx]);
}

bool GraphParallelExp::IsDefiniteCoreVertex(int u) {
    return is_core_lst[u] == TRUE;
}

void GraphParallelExp::CheckCoreFirstBSP(int u) {
    auto sd = 0;
    auto ed = degree[u] - 1;
    for (auto edge_idx = out_edge_start[u]; edge_idx < out_edge_start[u + 1]; edge_idx++) {
        auto v = out_edges[edge_idx];
        // be careful, the next line can only be commented when memory load/store of min_cn is atomic
//        if (u <= v) {
        if (min_cn[edge_idx] == DIRECT_REACHABLE) {
            ++sd;
            if (sd >= min_u) {
                is_core_lst[u] = TRUE;
                return;
            }
        } else if (min_cn[edge_idx] == NOT_DIRECT_REACHABLE) {
            --ed;
            if (ed < min_u) {
                is_non_core_lst[u] = TRUE;
                return;
            }
        }
//        }
    }

    for (auto edge_idx = out_edge_start[u]; edge_idx < out_edge_start[u + 1]; edge_idx++) {
        auto v = out_edges[edge_idx];
        if (u <= v && min_cn[edge_idx] > 0) {
            min_cn[edge_idx] = EvalReachable(u, edge_idx);
            min_cn[BinarySearch(out_edges, out_edge_start[v], out_edge_start[v + 1], u)] = min_cn[edge_idx];
            if (min_cn[edge_idx] == DIRECT_REACHABLE) {
                ++sd;
                if (sd >= min_u) {
                    is_core_lst[u] = TRUE;
                    return;
                }
            } else {
                --ed;
                if (ed < min_u) {
                    is_non_core_lst[u] = TRUE;
                    return;
                }
            }
        }
    }
}

void GraphParallelExp::CheckCoreSecondBSP(int u) {
    if (is_core_lst[u] == FALSE && is_non_core_lst[u] == FALSE) {
        auto sd = 0;
        auto ed = degree[u] - 1;
        for (auto edge_idx = out_edge_start[u]; edge_idx < out_edge_start[u + 1]; edge_idx++) {
            if (min_cn[edge_idx] == DIRECT_REACHABLE) {
                ++sd;
                if (sd >= min_u) {
                    is_core_lst[u] = TRUE;
                    return;
                }
            }
            if (min_cn[edge_idx] == NOT_DIRECT_REACHABLE) {
                --ed;
                if (ed < min_u) {
                    return;
                }
            }
        }

        for (auto edge_idx = out_edge_start[u]; edge_idx < out_edge_start[u + 1]; edge_idx++) {
            auto v = out_edges[edge_idx];
            if (min_cn[edge_idx] > 0) {
                min_cn[edge_idx] = EvalReachable(u, edge_idx);
                min_cn[BinarySearch(out_edges, out_edge_start[v], out_edge_start[v + 1], u)] = min_cn[edge_idx];
                if (min_cn[edge_idx] == DIRECT_REACHABLE) {
                    ++sd;
                    if (sd >= min_u) {
                        is_core_lst[u] = TRUE;
                        return;
                    }
                } else {
                    --ed;
                    if (ed < min_u) {
                        return;
                    }
                }
            }
        }
    }
}

void GraphParallelExp::ClusterCoreFirstPhase(int u) {
    for (auto j = out_edge_start[u]; j < out_edge_start[u + 1]; j++) {
        auto v = out_edges[j];
        if (u < v && IsDefiniteCoreVertex(v) && !disjoint_set_ptr->IsSameSet(u, v)) {
            if (min_cn[j] == DIRECT_REACHABLE) {
                disjoint_set_ptr->Union(u, v);
            }
        }
    }
}

void GraphParallelExp::ClusterCoreSecondPhase(int u) {
    for (auto edge_idx = out_edge_start[u]; edge_idx < out_edge_start[u + 1]; edge_idx++) {
        auto v = out_edges[edge_idx];
        if (u < v && IsDefiniteCoreVertex(v) && !disjoint_set_ptr->IsSameSet(u, v)) {
            if (min_cn[edge_idx] > 0) {
                min_cn[edge_idx] = EvalReachable(u, edge_idx);
                if (min_cn[edge_idx] == DIRECT_REACHABLE) {
                    disjoint_set_ptr->Union(u, v);
                }
            }
        }
    }
}

void GraphParallelExp::MarkClusterMinEleAsId() {
    cluster_dict = vector<int>(n);
    std::fill(cluster_dict.begin(), cluster_dict.end(), n);

    for (auto i = 0; i < n; i++) {
        if (IsDefiniteCoreVertex(i)) {
            // after this, root must be left nodes' parent since disjoint_set_ptr->FindRoot(i)
            int x = disjoint_set_ptr->FindRoot(i);
            if (i < cluster_dict[x]) { cluster_dict[x] = i; }
        }
    }
}

void GraphParallelExp::ClusterNonCores() {
    noncore_cluster = std::vector<pair<int, int>>();
    noncore_cluster.reserve(n);

    MarkClusterMinEleAsId();

    auto tmp_start = high_resolution_clock::now();
    {
        ThreadPool pool(thread_num_);

        auto batch_size = 64u;
        for (auto v_i = 0; v_i < cores.size(); v_i += batch_size) {
            int my_start = v_i;
            int my_end = min(static_cast<ui>(cores.size()), my_start + batch_size);
            pool.enqueue([this](int i_start, int i_end) {
                for (auto i = i_start; i < i_end; i++) {
                    auto u = cores[i];
                    for (auto j = out_edge_start[u]; j < out_edge_start[u + 1]; j++) {
                        auto v = out_edges[j];
                        if (!IsDefiniteCoreVertex(v)) {
                            if (min_cn[j] > 0) {
                                min_cn[j] = EvalReachable(u, j);
                            }
                        }
                    }
                }
            }, my_start, my_end);
        }
    }
    auto tmp_end = high_resolution_clock::now();
    cout << "4th: eval cost in cluster-non-core:" << duration_cast<milliseconds>(tmp_end - tmp_start).count()
         << " ms\n";

    for (auto u : cores) {
        for (auto j = out_edge_start[u]; j < out_edge_start[u + 1]; j++) {
            auto v = out_edges[j];
            if (!IsDefiniteCoreVertex(v)) {
                auto root_of_u = disjoint_set_ptr->FindRoot(u);
                if (min_cn[j] == DIRECT_REACHABLE) {
                    noncore_cluster.emplace_back(cluster_dict[root_of_u], v);
                }
            }
        }
    }
}

void GraphParallelExp::pSCANFirstPhasePrune() {
    auto prune_start = high_resolution_clock::now();
    Prune();
    auto prune_end = high_resolution_clock::now();
    cout << "1st: prune execution time:" << duration_cast<milliseconds>(prune_end - prune_start).count() << " ms\n";
}

void GraphParallelExp::pSCANSecondPhaseCheckCore() {
    auto find_core_start = high_resolution_clock::now();
    {
        ThreadPool pool(thread_num_);
        auto batch_size = 32u;
        for (auto v_i = 0; v_i < n; v_i += batch_size) {
            int my_start = v_i;
            int my_end = min(n, my_start + batch_size);
            pool.enqueue([this](int i_start, int i_end) {
                for (auto i = i_start; i < i_end; i++) { CheckCoreFirstBSP(i); }
            }, my_start, my_end);
        }
    }
    auto first_bsp_end = high_resolution_clock::now();
    cout << "2nd: check core first-phase bsp time:"
         << duration_cast<milliseconds>(first_bsp_end - find_core_start).count() << " ms\n";

    {
        ThreadPool pool(thread_num_);

        auto batch_size = 64u;
        for (auto v_i = 0; v_i < n; v_i += batch_size) {
            int my_start = v_i;
            int my_end = min(n, my_start + batch_size);
            pool.enqueue([this](int i_start, int i_end) {
                for (auto i = i_start; i < i_end; i++) {
                    CheckCoreSecondBSP(i);
                }
            }, my_start, my_end);
        }
    }

    auto second_bsp_end = high_resolution_clock::now();
    cout << "2nd: check core second-phase bsp time:"
         << duration_cast<milliseconds>(second_bsp_end - first_bsp_end).count() << " ms\n";
}

void GraphParallelExp::pSCANThirdPhaseClusterCore() {
    auto tmp_start = high_resolution_clock::now();

    for (auto i = 0; i < n; i++) {
        if (IsDefiniteCoreVertex(i)) { cores.emplace_back(i); }
    }
    // prepare data
    auto tmp_end0 = high_resolution_clock::now();
    cout << "3rd: copy time: " << duration_cast<milliseconds>(tmp_end0 - tmp_start).count() << " ms\n";

    // cluster-core 1st phase
    for (auto core:cores) { ClusterCoreFirstPhase(core); }

    auto tmp_end = high_resolution_clock::now();
    cout << "3rd: prepare time: " << duration_cast<milliseconds>(tmp_end - tmp_start).count() << " ms\n";

    // cluster-core 2nd phase
    for (auto core:cores) { ClusterCoreSecondPhase(core); }

    auto end_core_cluster = high_resolution_clock::now();
    cout << "3rd: core clustering time:" << duration_cast<milliseconds>(end_core_cluster - tmp_start).count()
         << " ms\n";
}

void GraphParallelExp::pSCANFourthPhaseClusterNonCore() {
    auto tmp_start = high_resolution_clock::now();

    ClusterNonCores();

    auto all_end = high_resolution_clock::now();
    cout << "4th: non-core clustering time:" << duration_cast<milliseconds>(all_end - tmp_start).count()
         << " ms\n";
}

void GraphParallelExp::pSCAN() {
    cout << "new algo" << endl;
    pSCANFirstPhasePrune();

    pSCANSecondPhaseCheckCore();

    pSCANThirdPhaseClusterCore();

    pSCANFourthPhaseClusterNonCore();
}
