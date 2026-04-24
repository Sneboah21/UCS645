// array_sum.c
#include <mpi.h>
#include <stdio.h>

#define ARRAY_SIZE 100

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int chunk = ARRAY_SIZE / size;
    int array[ARRAY_SIZE];
    int local_chunk[chunk];

    if (rank == 0) {
        for (int i = 0; i < ARRAY_SIZE; i++)
            array[i] = i + 1;
    }

    MPI_Scatter(array, chunk, MPI_INT, local_chunk, chunk, MPI_INT, 0, MPI_COMM_WORLD);

    int local_sum = 0;
    for (int i = 0; i < chunk; i++)
        local_sum += local_chunk[i];

    printf("Process %d local sum: %d\n", rank, local_sum);

    int global_sum = 0;
    MPI_Reduce(&local_sum, &global_sum, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("\nGlobal Sum: %d\n", global_sum);
        printf("Average: %.2f\n", (double)global_sum / ARRAY_SIZE);
    }

    MPI_Finalize();
    return 0;
}