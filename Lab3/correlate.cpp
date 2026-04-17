#include <cmath>
#include <vector>
#include <omp.h>

// Normalize each row: subtract mean, divide by std dev
static void normalize(int ny,int nx,const float* data,std::vector<double>& norm) {
    norm.resize(ny * nx);
    for (int y = 0; y < ny; y++)
    {
        double mean = 0;
        for (int x = 0; x < nx; x++) mean += data[x + y * nx];
        mean /= nx;

        double sq = 0;
        for (int x = 0; x < nx; x++) {
            double v = data[x + y * nx] - mean;
            norm[x + y * nx] = v;
            sq += v * v;
        }

        double inv = (sq > 0) ? 1.0 / sqrt(sq) : 0.0;
        for (int x = 0; x < nx; x++) norm[x + y * nx] *= inv;
    }
}

// Step 1: Sequential
void correlate_sequential(int ny, int nx, const float* data, float* result) {
    std::vector<double> norm;
    normalize(ny, nx, data, norm);
    for (int i = 0; i < ny; i++)
        for (int j = 0; j <= i; j++) {
            double sum = 0;
            for (int x = 0; x < nx; x++)
                sum += norm[x + i * nx] * norm[x + j * nx];
            result[i + j * ny] = (float)sum;
        }
}

// Step 2: OpenMP parallel
void correlate_openmp(int ny, int nx, const float* data, float* result) {
    std::vector<double> norm;
    normalize(ny, nx, data, norm);
    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < ny; i++)
        for (int j = 0; j <= i; j++) {
            double sum = 0;
            for (int x = 0; x < nx; x++)
                sum += norm[x + i * nx] * norm[x + j * nx];
            result[i + j * ny] = (float)sum;
        }
}

// Step 3: Optimized (OpenMP + SIMD hint)
void correlate(int ny, int nx, const float* data, float* result) {
    std::vector<double> norm;
    normalize(ny, nx, data, norm);
    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < ny; i++)
        for (int j = 0; j <= i; j++) {
            double sum = 0;
            #pragma omp simd reduction(+:sum)
            for (int x = 0; x < nx; x++)
                sum += norm[x + i * nx] * norm[x + j * nx];
            result[i + j * ny] = (float)sum;
        }
}