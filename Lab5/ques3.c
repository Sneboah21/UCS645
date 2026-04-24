// assign3_inner.c
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

#define N_TOTAL 50000000LL

int main(int argc, char *argv[]) {
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    double scale = 0.0;
    if (rank == 0) scale = 4.0;
    MPI_Bcast(&scale, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);

    long long local_n = N_TOTAL / size;
    double *u = malloc(local_n * sizeof(double));
    double *v = malloc(local_n * sizeof(double));

    for (long long i = 0; i < local_n; i++) {
        u[i] = 1.0 * scale;
        v[i] = 0.5;
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double t_start = MPI_Wtime();

    double local_sum = 0.0;
    for (long long i = 0; i < local_n; i++)
        local_sum += u[i] * v[i];

    double global_sum = 0.0;
    MPI_Reduce(&local_sum, &global_sum, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    MPI_Barrier(MPI_COMM_WORLD);
    double t_local = MPI_Wtime() - t_start;

    double t_max;
    MPI_Reduce(&t_local, &t_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("=== Inner Product | Procs=%d ===\n", size);
        printf("  Result : %.2f\n", global_sum);
        printf("  Time   : %.6f s\n", t_max);
        printf("  [Compute speedup and efficiency by comparing T1]\n");
    }

    free(u); free(v);
    MPI_Finalize();
    return 0;
}