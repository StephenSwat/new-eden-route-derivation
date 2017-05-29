#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#include "main.h"
#include "min_heap.h"
#include "universe.h"

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

#define AU_TO_M 149597870700.0
#define LY_TO_M 9460730472580800.0

static struct timespec mbt[6];
static long mba[6] = {0};

inline double __attribute__((always_inline)) entity_distance(struct entity *a, struct entity *b) {
    if (a->system != b->system) return INFINITY;

    double dx = a->x - b->x;
    double dy = a->y - b->y;
    double dz = a->z - b->z;

    return sqrt(dx * dx + dy * dy + dz * dz);
}

inline double __attribute__((always_inline)) system_distance(struct system *a, struct system *b) {
    double dx = a->x - b->x;
    double dy = a->y - b->y;
    double dz = a->z - b->z;

    return dx * dx + dy * dy + dz * dz;
}

double get_time(double distance, double v_wrp) {
    double k_accel = v_wrp;
    double k_decel = (v_wrp / 3) < 2 ? (v_wrp / 3) : 2;

    double v_max_wrp = v_wrp * AU_TO_M;

    double d_accel = AU_TO_M;
    double d_decel = v_max_wrp / k_decel;

    double d_min = d_accel + d_decel;

    double cruise_time = 0;

    if (d_min > distance) {
        v_max_wrp = distance * k_accel * k_decel / (k_accel + k_decel);
    } else {
        cruise_time = (distance - d_min) / v_max_wrp;
    }

    double t_accel = log(v_max_wrp / k_accel) / k_accel;
    double t_decel = log(v_max_wrp / 100) / k_decel;

    return cruise_time + t_accel + t_decel;
}

struct route *dijkstra(struct universe *u, struct entity *src, struct entity *dst, struct trip *parameters) {
    static double gate_cost = 10.0;
    static struct min_heap queue;

    static int prev[LIMIT_ENTITIES];
    static int step[LIMIT_ENTITIES];
    static double cost[LIMIT_ENTITIES];

    int count = u->entity_count;

    min_heap_init(&queue, count);

    double distance, cur_cost;

    double jump_range = parameters->jump_range;
    double warp_speed = parameters->warp_speed;
    double align_time = parameters->align_time;

    double sqjr = pow(jump_range * LY_TO_M, 2.0);

    for (int i = 0; i < u->entity_count; i++) {
        if (!u->entities[i]->destination && u->entities[i]->seq_id != src->seq_id && u->entities[i]->seq_id != dst->seq_id) continue;

        if (i == src->seq_id) {
            min_heap_insert(&queue, 0, i);
            prev[i] = -1;
            step[i] = 0;
            cost[i] = 0;
        } else {
            min_heap_insert(&queue, INFINITY, i);
            prev[i] = -1;
            step[i] = -1;
            cost[i] = INFINITY;
        }
    }

    int tmp, v;
    int loops = 0;

    struct system *sys, *jsys;
    struct entity *ent;

    for (int remaining = count; remaining > 0; remaining--, loops++) {
        if (verbose) {
            clock_gettime(CLOCK_MONOTONIC, &mbt[1]);
            mba[0] += time_diff(&mbt[0], &mbt[1]);
        }

        tmp = min_heap_extract(&queue);

        if (tmp == dst->seq_id || isinf(cost[tmp])) break;

        if (verbose) {
            clock_gettime(CLOCK_MONOTONIC, &mbt[2]);
            mba[1] += time_diff(&mbt[1], &mbt[2]);
        }

        // System set
        ent = u->entities[tmp];
        sys = ent->system;

        for (int i = 0; i < sys->entity_count; i++) {
            if (!sys->entities[i].destination && sys->entities[i].seq_id != src->seq_id && sys->entities[i].seq_id != dst->seq_id) continue;
            if (tmp == sys->entities[i].seq_id) continue;

            v = sys->entities[i].seq_id;
            cur_cost = cost[tmp] + align_time + get_time(entity_distance(ent, &sys->entities[i]), warp_speed);

            if (min_heap_decrease(&queue, cur_cost, v)) {
                prev[v] = tmp;
                cost[v] = cur_cost;
                step[v] = step[tmp] + 1;
            }
        }

        if (verbose) {
            clock_gettime(CLOCK_MONOTONIC, &mbt[3]);
            mba[2] += time_diff(&mbt[2], &mbt[3]);
        }

        // Gate set
        if (ent->destination) {
            v = ent->destination->seq_id;
            cur_cost = cost[tmp] + gate_cost;

            if (min_heap_decrease(&queue, cur_cost, v)) {
                prev[v] = tmp;
                cost[v] = cur_cost;
                step[v] = step[tmp] + 1;
            }
        }

        if (verbose) {
            clock_gettime(CLOCK_MONOTONIC, &mbt[4]);
            mba[3] += time_diff(&mbt[3], &mbt[4]);
        }

        // Jump set
        if (!isnan(jump_range)) {
            for (int i = 0; i < u->system_count; i++) {
                distance = system_distance(sys, &u->systems[i]);

                if (distance <= sqjr && sys->id != u->systems[i].id) {
                    jsys = u->systems + i;
                    distance = sqrt(distance) / LY_TO_M;
                    for (int j = 0; j < jsys->entity_count; j++) {
                        if (!jsys->entities[j].destination && jsys->entities[j].seq_id != src->seq_id && jsys->entities[j].seq_id != dst->seq_id) continue;

                        v = jsys->entities[j].seq_id;
                        cur_cost = cost[tmp] + 60 * (distance + 1);

                        if (min_heap_decrease(&queue, cur_cost, v)) {
                            prev[v] = tmp;
                            cost[v] = cur_cost;
                            step[v] = step[tmp] + 1;
                        }
                    }
                }
            }
        }

        if (verbose) {
            clock_gettime(CLOCK_MONOTONIC, &mbt[5]);
            mba[4] += time_diff(&mbt[4], &mbt[5]);
        }
    }

    struct route *route = malloc(sizeof(struct route));

    if (verbose) {
        fprintf(stderr, "%lu %lu %lu %lu\n", mba[1], mba[2], mba[3], mba[4]);
    }

    route->loops = loops;
    route->length = step[dst->seq_id] + 1;
    route->cost = cost[dst->seq_id];
    route->points = calloc(route->length, sizeof(struct entity *));

    for (int c = dst->seq_id, i = route->length - 1; i >= 0; c = prev[c], i--) {
        route->points[i] = u->entities[c];
    }

    min_heap_destroy(&queue);

    return route;
}
