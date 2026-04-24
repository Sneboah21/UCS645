// assign1_saxpy.c
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

#define VEC_SIZE (1 << 16)
#define SCALAR 3.7

int main(int argc, char *argv[]) {
    int pid, np;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &pid);
    MPI_Comm_size(MPI_COMM_WORLD, &np);

    int chunk = VEC_SIZE / np;
    int offset = pid * chunk;

    double *vecA = malloc(chunk * sizeof(double));
    double *vecB = malloc(chunk * sizeof(double));

    for (int i = 0; i < chunk; i++) {
        vecA[i] = (offset + i) * 1.5;
        vecB[i] = (offset + i) * 0.75;
    }

    double t_serial = 0.0;
    if (pid == 0) {
        double *sA = malloc(VEC_SIZE * sizeof(double));
        double *sB = malloc(VEC_SIZE * sizeof(double));
        for (int i = 0; i < VEC_SIZE; i++) { sA[i] = i*1.5; sB[i] = i*0.75; }
        double s0 = MPI_Wtime();
        for (int i = 0; i < VEC_SIZE; i++) sA[i] = SCALAR * sA[i] + sB[i];
        t_serial = MPI_Wtime() - s0;
        printf(">> [Serial Baseline] Time: %.6f s\n", t_serial);
        free(sA); free(sB);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double t0 = MPI_Wtime();
    for (int i = 0; i < chunk; i++)
        vecA[i] = SCALAR * vecA[i] + vecB[i];
    MPI_Barrier(MPI_COMM_WORLD);
    double elapsed = MPI_Wtime() - t0;

    double t_max;
    MPI_Reduce(&elapsed, &t_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (pid == 0) {
        double speedup    = t_serial / t_max;
        double efficiency = speedup / np * 100.0;
        printf(">> [Parallel] Procs=%d | Time=%.6f s\n", np, t_max);
        printf(">> Speedup=%.4fx | Efficiency=%.2f%%\n", speedup, efficiency);
    }

    free(vecA); free(vecB);
    MPI_Finalize();
    return 0;
}