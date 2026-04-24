// assign5_abundant.c
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

#define LIMIT 100000
#define WORK 20
#define STOP -1
#define STOP_TAG 21

int is_abundant(int n)
{
  if (n < 2)
    return 0;
  int total = 1;
  for (int d = 2; d * d <= n; d++)
  {
    if (n % d == 0)
    {
      total += d;
      if (d != n / d)
        total += n / d;
    }
  }
  return total > n;
}

int main(int argc, char *argv[])
{
  int rank, size;
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  double t0 = MPI_Wtime();

  if (rank == 0)
  {
    int found = 0, cur = 2, alive = size - 1;
    MPI_Status st;
    int result;
    while (alive > 0)
    {
      MPI_Recv(&result, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &st);
      int src = st.MPI_SOURCE;
      if (result > 0)
        found++;
      if (cur <= LIMIT)
      {
        MPI_Send(&cur, 1, MPI_INT, src, WORK, MPI_COMM_WORLD);
        cur++;
      }
      else
      {
        int sig = STOP;
        MPI_Send(&sig, 1, MPI_INT, src, STOP_TAG, MPI_COMM_WORLD);
        alive--;
      }
    }
    double elapsed = MPI_Wtime() - t0;
    printf("=== Abundant Number Finder | Procs=%d ===\n", size);
    printf("  Abundant numbers up to %d : %d\n", LIMIT, found);
    printf("  Time: %.4f s\n", elapsed);
  }
  else
  {
    int req = 0;
    MPI_Send(&req, 1, MPI_INT, 0, WORK, MPI_COMM_WORLD);
    MPI_Status st;
    while (1)
    {
      int num;
      MPI_Recv(&num, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &st);
      if (st.MPI_TAG == STOP_TAG)
        break;
      int res = is_abundant(num) ? num : -num;
      MPI_Send(&res, 1, MPI_INT, 0, WORK, MPI_COMM_WORLD);
    }
  }

  MPI_Finalize();
  return 0;
}