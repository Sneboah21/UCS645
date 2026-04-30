#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <cuda_runtime.h>

#define CUDA_CHECK(call) {                                      
    cudaError_t err = call;                                     
    if(err != cudaSuccess){                                     
        fprintf(stderr,"CUDA error %s:%d: %s\n",                
            __FILE__,__LINE__,cudaGetErrorString(err));         
        exit(EXIT_FAILURE);                                     
    }                                                           
}

#define BLOCK 256
#define EPS 1e-7f


__global__ void sigmoidKernel(const float* x, float* y, int N){
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < N) y[i] = 1.0f / (1.0f + expf(-x[i]));
}

__global__ void tanhKernel(const float* x, float* y, int N){
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < N) y[i] = tanhf(x[i]);
}

__global__ void leakyReluKernel(const float* x, float* y, float alpha, int N){
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < N) y[i] = (x[i] >= 0.0f) ? x[i] : alpha * x[i];
}

__global__ void reluBackwardKernel(const float* x, const float* grad_out,
                                    float* grad_in, int N){
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < N) grad_in[i] = (x[i] > 0.0f) ? grad_out[i] : 0.0f;
}


__global__ void bceLossKernel(const float* pred, const float* target,
                               float* loss, int N){
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < N){
        float p = fmaxf(EPS, fminf(1.0f - EPS, pred[i]));
        loss[i] = -(target[i] * logf(p) + (1.0f - target[i]) * logf(1.0f - p));
    }
}


__global__ void crossEntropyKernel(const float* logits, const int* labels,
                                    float* loss, int N, int C){
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < N){
        const float* row = logits + i * C;
        // Find max for numerical stability
        float max_val = row[0];
        for(int c = 1; c < C; c++) max_val = fmaxf(max_val, row[c]);

        // Compute log-sum-exp
        float sum_exp = 0.0f;
        for(int c = 0; c < C; c++) sum_exp += expf(row[c] - max_val);
        float log_sum_exp = max_val + logf(sum_exp);

        loss[i] = log_sum_exp - row[labels[i]];
    }
}

__global__ void crossEntropyGradKernel(const float* logits, const int* labels,
                                        float* grad, int N, int C){
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < N){
        const float* row = logits + i * C;
        float* grad_row  = grad + i * C;

        float max_val = row[0];
        for(int c = 1; c < C; c++) max_val = fmaxf(max_val, row[c]);
        float sum_exp = 0.0f;
        for(int c = 0; c < C; c++) sum_exp += expf(row[c] - max_val);

        for(int c = 0; c < C; c++){
            float softmax_c = expf(row[c] - max_val) / sum_exp;
            grad_row[c] = softmax_c - (float)(c == labels[i]);
        }
    }
}


__global__ void adamUpdateKernel(float* param, float* grad,
                                  float* m, float* v,
                                  float lr, float beta1, float beta2,
                                  float eps, float beta1_t, float beta2_t,
                                  int N){
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < N){
        m[i] = beta1 * m[i] + (1.0f - beta1) * grad[i];
        v[i] = beta2 * v[i] + (1.0f - beta2) * grad[i] * grad[i];
        float m_hat = m[i] / (1.0f - beta1_t);
        float v_hat = v[i] / (1.0f - beta2_t);
        param[i] -= lr * m_hat / (sqrtf(v_hat) + eps);
    }
}

int main(){
    printf("===== Assignment 8 Problem 3: ML Primitives =====\n");
    int N = 10000000; // 10^7
    size_t bytes = N * sizeof(float);

    float *hX = (float*)malloc(bytes);
    float *hY = (float*)malloc(bytes);
    for(int i = 0; i < N; i++) hX[i] = ((float)rand()/RAND_MAX) * 8.0f - 4.0f;

    float *dX, *dY;
    CUDA_CHECK(cudaMalloc(&dX, bytes));
    CUDA_CHECK(cudaMalloc(&dY, bytes));
    CUDA_CHECK(cudaMemcpy(dX, hX, bytes, cudaMemcpyHostToDevice));

    int blocks = (N + BLOCK - 1) / BLOCK;
    cudaEvent_t eS, eE;
    CUDA_CHECK(cudaEventCreate(&eS)); CUDA_CHECK(cudaEventCreate(&eE));

    const char* names[] = {"Sigmoid", "Tanh", "LeakyReLU", "ReLU-Bwd"};
    float times[4];

    CUDA_CHECK(cudaEventRecord(eS));
    sigmoidKernel<<<blocks, BLOCK>>>(dX, dY, N);
    CUDA_CHECK(cudaEventRecord(eE)); CUDA_CHECK(cudaEventSynchronize(eE));
    CUDA_CHECK(cudaEventElapsedTime(&times[0], eS, eE));

    CUDA_CHECK(cudaEventRecord(eS));
    tanhKernel<<<blocks, BLOCK>>>(dX, dY, N);
    CUDA_CHECK(cudaEventRecord(eE)); CUDA_CHECK(cudaEventSynchronize(eE));
    CUDA_CHECK(cudaEventElapsedTime(&times[1], eS, eE));

    CUDA_CHECK(cudaEventRecord(eS));
    leakyReluKernel<<<blocks, BLOCK>>>(dX, dY, 0.01f, N);
    CUDA_CHECK(cudaEventRecord(eE)); CUDA_CHECK(cudaEventSynchronize(eE));
    CUDA_CHECK(cudaEventElapsedTime(&times[2], eS, eE));

    float *dGrad; CUDA_CHECK(cudaMalloc(&dGrad, bytes));
    CUDA_CHECK(cudaMemcpy(dGrad, dX, bytes, cudaMemcpyDeviceToDevice));
    CUDA_CHECK(cudaEventRecord(eS));
    reluBackwardKernel<<<blocks, BLOCK>>>(dX, dGrad, dY, N);
    CUDA_CHECK(cudaEventRecord(eE)); CUDA_CHECK(cudaEventSynchronize(eE));
    CUDA_CHECK(cudaEventElapsedTime(&times[3], eS, eE));

    printf("\n=== Activation Function Benchmarks (N=10^7) ===\n");
    printf("%-15s %-12s %-12s\n", "Kernel", "Time(ms)", "BW(GB/s)");
    for(int k = 0; k < 4; k++){
        float bw = (2.0f * bytes / 1e9f) / (times[k] / 1e3f);
        printf("%-15s %-12.3f %-12.2f\n", names[k], times[k], bw);
    }

    int C = 10, Nb = 1000;
    float *h_logits = (float*)malloc(Nb * C * sizeof(float));
    int   *h_labels = (int*)malloc(Nb * sizeof(int));
    float *h_loss   = (float*)malloc(Nb * sizeof(float));
    for(int i = 0; i < Nb * C; i++) h_logits[i] = ((float)rand()/RAND_MAX)*4 - 2;
    for(int i = 0; i < Nb; i++) h_labels[i] = rand() % C;

    float *d_logits, *d_loss; int *d_labels;
    CUDA_CHECK(cudaMalloc(&d_logits, Nb * C * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_labels, Nb * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_loss, Nb * sizeof(float)));
    CUDA_CHECK(cudaMemcpy(d_logits, h_logits, Nb*C*sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_labels, h_labels, Nb*sizeof(int), cudaMemcpyHostToDevice));

    int lb = (Nb + BLOCK - 1) / BLOCK;
    crossEntropyKernel<<<lb, BLOCK>>>(d_logits, d_labels, d_loss, Nb, C);
    CUDA_CHECK(cudaMemcpy(h_loss, d_loss, Nb*sizeof(float), cudaMemcpyDeviceToHost));

    float mean_loss = 0.0f;
    for(int i = 0; i < Nb; i++) mean_loss += h_loss[i];
    printf("\nCross-Entropy mean loss: %.4f (expected ~2.3 for random 10-class)\n",
           mean_loss / Nb);

    // Adam test
    int Np = 1000;
    float *h_p = (float*)malloc(Np * sizeof(float));
    float *h_g = (float*)malloc(Np * sizeof(float));
    for(int i = 0; i < Np; i++){ h_p[i] = 0.5f; h_g[i] = 0.01f; }

    float *d_p, *d_g, *d_m, *d_v;
    CUDA_CHECK(cudaMalloc(&d_p, Np*sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_g, Np*sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_m, Np*sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_v, Np*sizeof(float)));
    CUDA_CHECK(cudaMemcpy(d_p, h_p, Np*sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_g, h_g, Np*sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemset(d_m, 0, Np*sizeof(float)));
    CUDA_CHECK(cudaMemset(d_v, 0, Np*sizeof(float)));

    float lr=0.001f, b1=0.9f, b2=0.999f, adam_eps=1e-8f;
    float b1t=b1, b2t=b2;
    int pb = (Np + BLOCK - 1) / BLOCK;
    for(int step = 0; step < 100; step++){
        adamUpdateKernel<<<pb, BLOCK>>>(d_p, d_g, d_m, d_v,
                                        lr, b1, b2, adam_eps, b1t, b2t, Np);
        b1t *= b1; b2t *= b2;
    }
    float p_final;
    CUDA_CHECK(cudaMemcpy(&p_final, d_p, sizeof(float), cudaMemcpyDeviceToHost));
    printf("Adam 100-step result (param[0]): %.6f\n", p_final);

    free(hX); free(hY); free(h_logits); free(h_labels); free(h_loss);
    free(h_p); free(h_g);
    CUDA_CHECK(cudaFree(dX)); CUDA_CHECK(cudaFree(dY)); CUDA_CHECK(cudaFree(dGrad));
    CUDA_CHECK(cudaFree(d_logits)); CUDA_CHECK(cudaFree(d_labels)); CUDA_CHECK(cudaFree(d_loss));
    CUDA_CHECK(cudaFree(d_p)); CUDA_CHECK(cudaFree(d_g));
    CUDA_CHECK(cudaFree(d_m)); CUDA_CHECK(cudaFree(d_v));
    return 0;
}