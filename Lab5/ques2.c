// assign2_bcast.c
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

#define MSG_LEN 10000000

void LinearBcast(double *buf, int n, int root, MPI_Comm comm) {
    int rank, size;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);
    if (rank == root) {
        for (int dest = 0; dest < size; dest++)
            if (dest != root)
                MPI_Send(buf, n, MPI_DOUBLE, dest, 99, comm);
    } else {
        MPI_Recv(buf, n, MPI_DOUBLE, root, 99, comm, MPI_STATUS_IGNORE);
    }
}

int main(int argc, char *argv[]) {
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    double *data = malloc(MSG_LEN * sizeof(double));
    if (rank == 0)
        for (int i = 0; i < MSG_LEN; i++) data[i] = i * 0.001;

    MPI_Barrier(MPI_COMM_WORLD);
    double t1 = MPI_Wtime();
    LinearBcast(data, MSG_LEN, 0, MPI_COMM_WORLD);
    MPI_Barrier(MPI_COMM_WORLD);
    double linear_t = MPI_Wtime() - t1;

    if (rank == 0)
        for (int i = 0; i < MSG_LEN; i++) data[i] = i * 0.001;

    MPI_Barrier(MPI_COMM_WORLD);
    double t2 = MPI_Wtime();
    MPI_Bcast(data, MSG_LEN, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Barrier(MPI_COMM_WORLD);
    double mpi_t = MPI_Wtime() - t2;

    if (rank == 0) {
        printf("=== Broadcast Race | Procs: %d ===\n", size);
        printf("  LinearBcast : %.6f s  [O(p)   complexity]\n", linear_t);
        printf("  MPI_Bcast   : %.6f s  [O(logp) complexity]\n", mpi_t);
        printf("  Advantage   : %.4fx faster with MPI_Bcast\n", linear_t / mpi_t);
    }

    free(data);
    MPI_Finalize();
    return 0;
}