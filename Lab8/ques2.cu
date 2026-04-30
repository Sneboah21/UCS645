#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <cuda_runtime.h>

#define CUDA_CHECK(call) {                                       \
    cudaError_t err = call;                                      \
    if(err != cudaSuccess){                                      \
        fprintf(stderr,"CUDA error %s:%d: %s\n",                \
            __FILE__,__LINE__,cudaGetErrorString(err));          \
        exit(EXIT_FAILURE);                                      \
    }                                                            \
}

#define BLOCK_SIZE 256
#define NUM_BINS   256


float naiveCPUSum(const float* A, int N){
    float sum = 0.0f;
    for(int i = 0; i < N; i++) sum += A[i];
    return sum;
}

__global__ void sharedMemReduceSum(const float* input, float* output, int N){
    __shared__ float sdata[BLOCK_SIZE];
    int tid = threadIdx.x;
    int i   = blockIdx.x * blockDim.x + threadIdx.x;

    sdata[tid] = (i < N) ? input[i] : 0.0f;
    __syncthreads();

    // Tree reduction
    for(int stride = blockDim.x / 2; stride > 0; stride >>= 1){
        if(tid < stride)
            sdata[tid] += sdata[tid + stride];
        __syncthreads();
    }
    if(tid == 0) output[blockIdx.x] = sdata[0];
}

__global__ void sharedMemReduceMax(const float* input, float* output, int N){
    __shared__ float sdata[BLOCK_SIZE];
    int tid = threadIdx.x;
    int i   = blockIdx.x * blockDim.x + threadIdx.x;

    sdata[tid] = (i < N) ? input[i] : -1e30f;
    __syncthreads();

    for(int stride = blockDim.x / 2; stride > 0; stride >>= 1){
        if(tid < stride)
            sdata[tid] = fmaxf(sdata[tid], sdata[tid + stride]);
        __syncthreads();
    }
    if(tid == 0) output[blockIdx.x] = sdata[0];
}

__inline__ __device__ float warpReduceSum(float val){
    for(int offset = 16; offset > 0; offset >>= 1)
        val += __shfl_down_sync(0xFFFFFFFF, val, offset);
    return val;
}

__global__ void warpShuffleReduceSum(const float* input, float* output, int N){
    int i   = blockIdx.x * blockDim.x + threadIdx.x;
    float val = (i < N) ? input[i] : 0.0f;

    val = warpReduceSum(val);

    __shared__ float shared[32]; // one per warp
    int lane    = threadIdx.x % 32;
    int warpId  = threadIdx.x / 32;

    if(lane == 0) shared[warpId] = val;
    __syncthreads();

    val = (threadIdx.x < blockDim.x / 32) ? shared[lane] : 0.0f;
    if(warpId == 0) val = warpReduceSum(val);
    if(threadIdx.x == 0) output[blockIdx.x] = val;
}

float gpuReduceSum(const float* d_input, int N, int strategy){
    int blocks = (N + BLOCK_SIZE - 1) / BLOCK_SIZE;
    float *d_partial;
    CUDA_CHECK(cudaMalloc(&d_partial, blocks * sizeof(float)));

    if(strategy == 2)
        sharedMemReduceSum<<<blocks, BLOCK_SIZE>>>(d_input, d_partial, N);
    else
        warpShuffleReduceSum<<<blocks, BLOCK_SIZE>>>(d_input, d_partial, N);

    float *d_result;
    CUDA_CHECK(cudaMalloc(&d_result, sizeof(float)));
    if(strategy == 2)
        sharedMemReduceSum<<<1, BLOCK_SIZE>>>(d_partial, d_result, blocks);
    else
        warpShuffleReduceSum<<<1, BLOCK_SIZE>>>(d_partial, d_result, blocks);

    float result;
    CUDA_CHECK(cudaMemcpy(&result, d_result, sizeof(float), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaFree(d_partial)); CUDA_CHECK(cudaFree(d_result));
    return result;
}


__global__ void bankConflictKernel(float* output, int stride, int N){
    __shared__ float sdata[1024];
    int tid = threadIdx.x;
    int idx = (tid * stride) % 1024;
    sdata[idx] = (float)tid;
    __syncthreads();
    if(tid < N) output[tid] = sdata[tid];
}

__global__ void paddedSharedMem(float* output, int N){
    __shared__ float tile[16][17]; 
    int tx = threadIdx.x % 16;
    int ty = threadIdx.x / 16;
    tile[ty][tx] = (float)(blockIdx.x * blockDim.x + threadIdx.x);
    __syncthreads();
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < N) output[i] = tile[ty][tx];
}


__global__ void histogramGlobal(const int* input, int* histo, int N){
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < N) atomicAdd(&histo[input[i]], 1);
}

__global__ void histogramShared(const int* input, int* histo, int N){
    __shared__ int local_histo[NUM_BINS];

    for(int i = threadIdx.x; i < NUM_BINS; i += blockDim.x)
        local_histo[i] = 0;
    __syncthreads();

    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < N) atomicAdd(&local_histo[input[i]], 1);
    __syncthreads();

    for(int i = threadIdx.x; i < NUM_BINS; i += blockDim.x)
        atomicAdd(&histo[i], local_histo[i]);
}

int main(){
    printf("===== Assignment 8 Problem 2: Memory Hierarchy =====\n");
    int N = 1 << 20;
    size_t bytes = N * sizeof(float);

    float *hA = (float*)malloc(bytes);
    for(int i = 0; i < N; i++) hA[i] = (float)(rand() % 100) / 10.0f;

    float *dA;
    CUDA_CHECK(cudaMalloc(&dA, bytes));
    CUDA_CHECK(cudaMemcpy(dA, hA, bytes, cudaMemcpyHostToDevice));

    cudaEvent_t eS, eE;
    CUDA_CHECK(cudaEventCreate(&eS)); CUDA_CHECK(cudaEventCreate(&eE));

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    float cpu_sum = naiveCPUSum(hA, N);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    float cpu_us = (t1.tv_sec - t0.tv_sec)*1e6 + (t1.tv_nsec - t0.tv_nsec)/1e3;

    CUDA_CHECK(cudaEventRecord(eS));
    float gpu_shared = gpuReduceSum(dA, N, 2);
    CUDA_CHECK(cudaEventRecord(eE)); CUDA_CHECK(cudaEventSynchronize(eE));
    float shared_ms; CUDA_CHECK(cudaEventElapsedTime(&shared_ms, eS, eE));
    float shared_bw = (bytes / 1e9) / (shared_ms / 1e3);

    CUDA_CHECK(cudaEventRecord(eS));
    float gpu_warp = gpuReduceSum(dA, N, 3);
    CUDA_CHECK(cudaEventRecord(eE)); CUDA_CHECK(cudaEventSynchronize(eE));
    float warp_ms; CUDA_CHECK(cudaEventElapsedTime(&warp_ms, eS, eE));
    float warp_bw = (bytes / 1e9) / (warp_ms / 1e3);

    printf("\n=== Reduction Comparison (N=2^20) ===\n");
    printf("%-20s %-12s %-12s %-12s %-12s\n",
           "Method", "Time(us)", "BW(GB/s)", "Result", "Correct?");
    printf("%-20s %-12.1f %-12s %-12.4f %-12s\n",
           "CPU Sequential", cpu_us, "N/A", cpu_sum, "REF");
    printf("%-20s %-12.1f %-12.2f %-12.4f %-12s\n",
           "Shared-Mem Tree", shared_ms*1000, shared_bw, gpu_shared,
           fabsf(gpu_shared - cpu_sum) < 0.1f ? "YES" : "NO");
    printf("%-20s %-12.1f %-12.2f %-12.4f %-12s\n",
           "Warp Shuffle", warp_ms*1000, warp_bw, gpu_warp,
           fabsf(gpu_warp - cpu_sum) < 0.1f ? "YES" : "NO");

    printf("\n=== Bank Conflict Profiling ===\n");
    printf("%-10s %-12s\n", "Stride", "Time(ms)");
    int strides[] = {1, 2, 4, 8, 16, 32};
    float* d_out;
    CUDA_CHECK(cudaMalloc(&d_out, 1024 * sizeof(float)));
    for(int i = 0; i < 6; i++){
        CUDA_CHECK(cudaEventRecord(eS));
        for(int rep = 0; rep < 1000; rep++)
            bankConflictKernel<<<1, 1024>>>(d_out, strides[i], 1024);
        CUDA_CHECK(cudaEventRecord(eE)); CUDA_CHECK(cudaEventSynchronize(eE));
        float ms; CUDA_CHECK(cudaEventElapsedTime(&ms, eS, eE));
        printf("%-10d %-12.4f\n", strides[i], ms);
    }
    CUDA_CHECK(cudaFree(d_out));

    int *h_input = (int*)malloc(N * sizeof(int));
    for(int i = 0; i < N; i++) h_input[i] = rand() % NUM_BINS;
    int *d_input; CUDA_CHECK(cudaMalloc(&d_input, N * sizeof(int)));
    CUDA_CHECK(cudaMemcpy(d_input, h_input, N * sizeof(int), cudaMemcpyHostToDevice));

    int *d_histo_global, *d_histo_shared;
    CUDA_CHECK(cudaMalloc(&d_histo_global, NUM_BINS * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_histo_shared, NUM_BINS * sizeof(int)));
    CUDA_CHECK(cudaMemset(d_histo_global, 0, NUM_BINS * sizeof(int)));
    CUDA_CHECK(cudaMemset(d_histo_shared, 0, NUM_BINS * sizeof(int)));

    int hblocks = (N + BLOCK_SIZE - 1) / BLOCK_SIZE;

    CUDA_CHECK(cudaEventRecord(eS));
    histogramGlobal<<<hblocks, BLOCK_SIZE>>>(d_input, d_histo_global, N);
    CUDA_CHECK(cudaEventRecord(eE)); CUDA_CHECK(cudaEventSynchronize(eE));
    float hg_ms; CUDA_CHECK(cudaEventElapsedTime(&hg_ms, eS, eE));

    CUDA_CHECK(cudaEventRecord(eS));
    histogramShared<<<hblocks, BLOCK_SIZE>>>(d_input, d_histo_shared, N);
    CUDA_CHECK(cudaEventRecord(eE)); CUDA_CHECK(cudaEventSynchronize(eE));
    float hs_ms; CUDA_CHECK(cudaEventElapsedTime(&hs_ms, eS, eE));

    printf("\n=== Histogram Benchmark ===\n");
    printf("Global-mem atomics: %.4f ms\n", hg_ms);
    printf("Shared-mem private: %.4f ms\n", hs_ms);
    printf("Speedup: %.2fx\n", hg_ms / hs_ms);

    free(hA); free(h_input);
    CUDA_CHECK(cudaFree(dA)); CUDA_CHECK(cudaFree(d_input));
    CUDA_CHECK(cudaFree(d_histo_global)); CUDA_CHECK(cudaFree(d_histo_shared));
    return 0;
}