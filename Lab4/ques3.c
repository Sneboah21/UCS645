// max_min.c
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    srand(time(NULL) + rank);  // different seed per process

    int nums[10];
    printf("Process %d generated: ", rank);
    for (int i = 0; i < 10; i++) {
        nums[i] = rand() % 1001;
        printf("%d ", nums[i]);
    }
    printf("\n");

    int local_max = nums[0], local_min = nums[0];
    for (int i = 1; i < 10; i++) {
        if (nums[i] > local_max) local_max = nums[i];
        if (nums[i] < local_min) local_min = nums[i];
    }

    // Use struct {value, rank} for MAXLOC/MINLOC
    struct { int val; int rank; } local_max_loc = {local_max, rank};
    struct { int val; int rank; } local_min_loc = {local_min, rank};
    struct { int val; int rank; } global_max_loc, global_min_loc;

    MPI_Reduce(&local_max_loc, &global_max_loc, 1, MPI_2INT, MPI_MAXLOC, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_min_loc, &global_min_loc, 1, MPI_2INT, MPI_MINLOC, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("\nGlobal Maximum: %d (found on Process %d)\n", global_max_loc.val, global_max_loc.rank);
        printf("Global Minimum: %d (found on Process %d)\n", global_min_loc.val, global_min_loc.rank);
    }

    MPI_Finalize();
    return 0;
}