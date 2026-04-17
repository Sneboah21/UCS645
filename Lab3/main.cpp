#include <iostream>
#include <vector>
#include <cstdlib>
#include <chrono>
#include <cmath>

void correlate_sequential(int ny, int nx, const float* data, float* result);
void correlate_openmp(int ny, int nx, const float* data, float* result);
void correlate(int ny, int nx, const float* data, float* result);

using namespace std;
using namespace chrono;

int main(int argc, char* argv[]) {
    int ny = 100, nx = 500;
    if (argc >= 3) { ny = atoi(argv[1]); nx = atoi(argv[2]); }

    cout << "Matrix: " << ny << " x " << nx << "\n";

    vector<float> data(ny * nx);
    for (auto& v : data) v = (float)(rand() % 1000) / 100.0f;

    vector<float> r1(ny * ny, 0), r2(ny * ny, 0), r3(ny * ny, 0);

    auto run = [&](auto fn, vector<float>& res, const char* name) {
        auto t1 = high_resolution_clock::now();
        fn(ny, nx, data.data(), res.data());
        auto t2 = high_resolution_clock::now();
        cout << name << ": " << duration<double, milli>(t2 - t1).count() << " ms\n";
    };

    run(correlate_sequential, r1, "Sequential");
    run(correlate_openmp,     r2, "OpenMP    ");
    run(correlate,            r3, "Optimized ");

    // Check accuracy
    double err = 0;
    for (int i = 0; i < ny * ny; i++) err = max(err, (double)abs(r1[i] - r3[i]));
    cout << "Max error: " << err << "\n";
}