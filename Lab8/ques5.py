import time
import torch
import torch.nn as nn
import torch.nn.functional as F
import torchvision
import torchvision.transforms as transforms
from torch.cuda.amp import autocast, GradScaler

DEVICE = torch.device("cuda" if torch.cuda.is_available() else "cpu")
print(f"Using device: {DEVICE}")

class MnistCNN(nn.Module):
    def __init__(self, use_bn=True, use_dropout=True):
        super().__init__()
        # B1: Conv layers
        self.conv1 = nn.Conv2d(1, 32, kernel_size=3, padding=1)
        self.bn1   = nn.BatchNorm2d(32) if use_bn else nn.Identity()
        self.conv2 = nn.Conv2d(32, 64, kernel_size=3, padding=1)
        self.bn2   = nn.BatchNorm2d(64) if use_bn else nn.Identity()
        self.pool  = nn.MaxPool2d(2, 2)

        # B2: FC layers
        self.fc1     = nn.Linear(64 * 7 * 7, 128)
        self.dropout = nn.Dropout(0.5) if use_dropout else nn.Identity()
        self.fc2     = nn.Linear(128, 10)

    def forward(self, x):
        # B3: Forward pass
        x = self.pool(F.relu(self.bn1(self.conv1(x))))  # [N,32,14,14]
        x = self.pool(F.relu(self.bn2(self.conv2(x))))  # [N,64,7,7]
        x = x.view(x.size(0), -1)                        # [N,3136]
        x = F.relu(self.fc1(x))
        x = self.dropout(x)
        return self.fc2(x)                               # [N,10]

class BaselineCNN(nn.Module):
    def __init__(self):
        super().__init__()
        self.conv1 = nn.Conv2d(1, 32, 3, padding=1)
        self.conv2 = nn.Conv2d(32, 64, 3, padding=1)
        self.pool  = nn.MaxPool2d(2, 2)
        self.fc1   = nn.Linear(64*7*7, 128)
        self.fc2   = nn.Linear(128, 10)
    def forward(self, x):
        x = self.pool(F.relu(self.conv1(x)))
        x = self.pool(F.relu(self.conv2(x)))
        x = x.view(x.size(0), -1)
        return self.fc2(F.relu(self.fc1(x)))


def get_loaders(augment=False, batch_size=256):
    if augment:
        train_tf = transforms.Compose([
            transforms.RandomRotation(10),           # Part C
            transforms.RandomAffine(0, shear=10),    # Part C
            transforms.ToTensor(),
            transforms.Normalize((0.1307,), (0.3081,)),
            transforms.RandomErasing(p=0.1),         # Part C
        ])
    else:
        train_tf = transforms.Compose([
            transforms.ToTensor(),
            transforms.Normalize((0.1307,), (0.3081,)),
        ])
    test_tf = transforms.Compose([
        transforms.ToTensor(),
        transforms.Normalize((0.1307,), (0.3081,)),
    ])
    train_ds = torchvision.datasets.MNIST('./data', train=True,  download=True, transform=train_tf)
    test_ds  = torchvision.datasets.MNIST('./data', train=False, download=True, transform=test_tf)
    train_loader = torch.utils.data.DataLoader(train_ds, batch_size=batch_size, shuffle=True,  num_workers=2)
    test_loader  = torch.utils.data.DataLoader(test_ds,  batch_size=batch_size, shuffle=False, num_workers=2)
    return train_loader, test_loader

def build_optimizer(model, opt_name='adam', lr=1e-3):
    if opt_name == 'adam':
        return torch.optim.Adam(model.parameters(), lr=lr)
    elif opt_name == 'sgd':
        return torch.optim.SGD(model.parameters(), lr=lr, momentum=0.9, weight_decay=1e-4)
    raise ValueError(f"Unknown optimizer: {opt_name}")


def build_scheduler(optimizer, sched_name='none', epochs=10):
    if sched_name == 'cosine':
        return torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=epochs)
    elif sched_name == 'step':
        return torch.optim.lr_scheduler.StepLR(optimizer, step_size=3, gamma=0.5)
    return None


def evaluate(model, loader):
    model.eval()
    correct = total = 0
    with torch.no_grad():
        for data, target in loader:
            data, target = data.to(DEVICE), target.to(DEVICE)  # C1: data to GPU
            output = model(data)
            pred = output.argmax(dim=1)
            correct += pred.eq(target).sum().item()
            total   += target.size(0)
    return 100.0 * correct / total


def train(model, train_loader, test_loader, optimizer, scheduler=None,
          epochs=10, use_amp=False, tag=""):
    criterion = nn.CrossEntropyLoss()
    scaler = GradScaler() if use_amp else None
    history = []

    for epoch in range(1, epochs+1):
        model.train()
        t0 = time.time()
        running_loss = 0.0
        correct = total = 0

        for data, target in train_loader:
            data, target = data.to(DEVICE), target.to(DEVICE)  # C1

            optimizer.zero_grad()
            if use_amp:
                with autocast():                      # D: AMP
                    output = model(data)
                    loss = criterion(output, target)
                scaler.scale(loss).backward()
                scaler.step(optimizer)
                scaler.update()
            else:
                output = model(data)                  # C2: forward
                loss = criterion(output, target)
                loss.backward()                       # C2: backward
                optimizer.step()                      # C2: optimizer step

            running_loss += loss.item() * data.size(0)
            pred = output.argmax(dim=1)
            correct += pred.eq(target).sum().item()
            total   += data.size(0)

        if scheduler: scheduler.step()

        epoch_loss  = running_loss / total
        train_acc   = 100.0 * correct / total
        test_acc    = evaluate(model, test_loader)   # C3
        elapsed     = time.time() - t0

        if DEVICE.type == 'cuda':
            mem_mb = torch.cuda.memory_allocated() / 1e6
        else:
            mem_mb = 0.0

        print(f"[{tag}] Epoch {epoch:2d} | Loss:{epoch_loss:.4f} | "
              f"Train:{train_acc:.2f}% | Test:{test_acc:.2f}% | "
              f"Time:{elapsed:.1f}s | Mem:{mem_mb:.0f}MB")
        history.append({'epoch': epoch, 'loss': epoch_loss,
                        'train_acc': train_acc, 'test_acc': test_acc,
                        'time': elapsed})
    return history


def run_part_a():
    print("\n" + "="*60)
    print("PART A: Full 10-Epoch Training (BN + Dropout + Adam)")
    print("="*60)
    train_loader, test_loader = get_loaders(batch_size=256)
    model = MnistCNN(use_bn=True, use_dropout=True).to(DEVICE)
    optimizer = build_optimizer(model, 'adam')
    scheduler = build_scheduler(optimizer, 'cosine', epochs=10)
    return train(model, train_loader, test_loader, optimizer, scheduler,
                 epochs=10, tag="PartA")


def run_ablation():
    print("\n" + "="*60)
    print("PART B: Ablation Study (5 epochs each)")
    print("="*60)
    train_loader, test_loader = get_loaders(batch_size=256)
    results = []

    configs = [
        ("Baseline (no BN, no Drop, Adam)",
         lambda: BaselineCNN().to(DEVICE), 'adam', 'none'),
        ("+ BatchNorm (Adam)",
         lambda: MnistCNN(use_bn=True, use_dropout=False).to(DEVICE), 'adam', 'none'),
        ("+ Dropout(0.5) (Adam)",
         lambda: MnistCNN(use_bn=False, use_dropout=True).to(DEVICE), 'adam', 'none'),
        ("SGD+Momentum+CosineAnnealingLR",
         lambda: MnistCNN(use_bn=True, use_dropout=True).to(DEVICE), 'sgd', 'cosine'),
    ]

    for name, model_fn, opt_name, sched_name in configs:
        print(f"\n--- Config: {name} ---")
        model = model_fn()
        lr = 0.01 if opt_name == 'sgd' else 1e-3
        opt = build_optimizer(model, opt_name, lr=lr)
        sched = build_scheduler(opt, sched_name, epochs=5)
        t_start = time.time()
        hist = train(model, train_loader, test_loader, opt, sched, epochs=5, tag=name[:20])
        total_time = time.time() - t_start
        best_test = max(h['test_acc'] for h in hist)
        epoch_95 = next((h['epoch'] for h in hist if h['test_acc'] >= 95.0), None)
        results.append({
            'Config': name, 'TestAcc': best_test,
            'Epoch95': epoch_95 if epoch_95 else ">5",
            'TrainTime': f"{total_time:.1f}s"
        })

    print("\n=== Ablation Table ===")
    print(f"{'Configuration':<40} {'Test Acc%':>10} {'Epoch@95%':>10} {'Time':>10}")
    print("-" * 72)
    for r in results:
        print(f"{r['Config']:<40} {r['TestAcc']:>10.2f} {str(r['Epoch95']):>10} {r['TrainTime']:>10}")

    return results


def run_augmentation():
    print("\n" + "="*60)
    print("PART C: Data Augmentation Comparison")
    print("="*60)

    for aug in [False, True]:
        label = "WITH augmentation" if aug else "WITHOUT augmentation"
        print(f"\n--- {label} ---")
        train_loader, test_loader = get_loaders(augment=aug, batch_size=256)
        model = MnistCNN(use_bn=True, use_dropout=True).to(DEVICE)
        opt   = build_optimizer(model, 'adam')
        sched = build_scheduler(opt, 'cosine', epochs=5)
        hist  = train(model, train_loader, test_loader, opt, sched, epochs=5, tag=label[:20])
        best_acc   = max(h['test_acc'] for h in hist)
        best_epoch = max(hist, key=lambda h: h['test_acc'])['epoch']
        print(f"  >> Best Test Accuracy: {best_acc:.2f}% at epoch {best_epoch}")


def run_amp():
    print("\n" + "="*60)
    print("PART D (Bonus): AMP Training")
    print("="*60)
    if not torch.cuda.is_available():
        print("  CUDA not available, skipping AMP.")
        return

    train_loader, test_loader = get_loaders(batch_size=256)
    model = MnistCNN(use_bn=True, use_dropout=True).to(DEVICE)
    opt   = build_optimizer(model, 'adam')
    sched = build_scheduler(opt, 'cosine', epochs=5)
    print("\n--- Without AMP ---")
    t0 = time.time()
    hist_no_amp = train(model, train_loader, test_loader, opt, sched,
                        epochs=5, use_amp=False, tag="NoAMP")
    no_amp_time = time.time() - t0

    model2 = MnistCNN(use_bn=True, use_dropout=True).to(DEVICE)
    opt2   = build_optimizer(model2, 'adam')
    sched2 = build_scheduler(opt2, 'cosine', epochs=5)
    print("\n--- With AMP (FP16) ---")
    torch.cuda.reset_peak_memory_stats()
    t0 = time.time()
    hist_amp = train(model2, train_loader, test_loader, opt2, sched2,
                     epochs=5, use_amp=True, tag="AMP")
    amp_time = time.time() - t0
    peak_mem = torch.cuda.max_memory_allocated() / 1e6

    print(f"\nAMP Summary:")
    print(f"  No-AMP total time: {no_amp_time:.1f}s")
    print(f"  AMP total time:    {amp_time:.1f}s  ({no_amp_time/amp_time:.2fx} speedup)")
    print(f"  Peak GPU memory (AMP): {peak_mem:.0f} MB")
    print(f"  Final Test Acc (AMP): {hist_amp[-1]['test_acc']:.2f}%")

def run_cuda_streams():
    """Demonstrate async data transfer with CUDA streams."""
    print("\n" + "="*60)
    print("PART D (Bonus): CUDA Streams — Async Transfer")
    print("="*60)
    if not torch.cuda.is_available():
        print("  CUDA not available."); return

    train_loader, _ = get_loaders(batch_size=256)
    batches = []
    for i, (data, target) in enumerate(train_loader):
        if i >= 100: break
        batches.append((data, target))

    # Synchronous
    t0 = time.time()
    for data, target in batches:
        _ = data.to(DEVICE)
        _ = target.to(DEVICE)
        torch.cuda.synchronize()
    sync_time = time.time() - t0

    # Async with CUDA stream
    stream = torch.cuda.Stream()
    t0 = time.time()
    for data, target in batches:
        with torch.cuda.stream(stream):
            _ = data.to(DEVICE, non_blocking=True)
            _ = target.to(DEVICE, non_blocking=True)
    torch.cuda.synchronize()
    async_time = time.time() - t0

    print(f"  Synchronous transfer (100 batches): {sync_time*1000:.1f} ms")
    print(f"  Async transfer (100 batches):       {async_time*1000:.1f} ms")
    print(f"  Speedup: {sync_time/async_time:.2f}x")


if __name__ == "__main__":
    run_part_a()
    run_ablation()
    run_augmentation()
    run_amp()
    run_cuda_streams()