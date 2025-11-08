# BBQ Queue - Dequeue Performance Data (5 runs)

bbq_dequeue_throughput = [7090.92, 6785.32, 6615.04, 7012.23, 6354.76]  # items/sec

bbq_dequeue_latency_avg = [281.95, 294.63, 302.25, 285.11, 314.58]  # microseconds


# David Queue - Dequeue Performance Data (5 runs)

david_dequeue_throughput = [9022.74, 9067.98, 9177.16, 9527.15, 9242.77]  # items/sec

david_dequeue_latency_avg = [221.48, 220.40, 217.80, 209.79, 216.27]  # microseconds


# c-FFQ Queue - Dequeue Performance Data (5 runs) - Batch size = 100

cffq_b100_dequeue_throughput = [336209.04, 354775.99, 331482.56, 317855.87, 320746.18]  # items/sec

cffq_b100_dequeue_latency_avg = [5.75, 5.43, 5.89, 6.06, 6.01]  # microseconds


# c-FFQ Queue - Dequeue Performance Data (5 runs) - Batch size = 10

cffq_b10_dequeue_throughput = [58144.27, 53522.59, 58106.12, 60625.17, 57428.82]  # items/sec

cffq_b10_dequeue_latency_avg = [34.16, 37.13, 34.23, 32.83, 34.59]  # microseconds


# r-FFQ Queue - Dequeue Performance Data (5 runs) - Batch size = 100

rffq_b100_dequeue_throughput = [25666.78, 26958.49, 26617.31, 27425.03, 27601.43]  # items/sec

rffq_b100_dequeue_latency_avg = [77.52, 73.82, 74.83, 72.58, 72.14]  # microseconds


# r-FFQ Queue - Dequeue Performance Data (5 runs) - Batch size = 10

rffq_b10_dequeue_throughput = [22139.74, 23114.06, 23356.16, 22787.17, 22332.07]  # items/sec

rffq_b10_dequeue_latency_avg = [90.22, 86.43, 85.49, 87.65, 89.40]  # microseconds


# Calculate averages
import numpy as np
import matplotlib.pyplot as plt

bbq_avg_throughput = np.mean(bbq_dequeue_throughput)
david_avg_throughput = np.mean(david_dequeue_throughput)
cffq_b100_avg_throughput = np.mean(cffq_b100_dequeue_throughput)
cffq_b10_avg_throughput = np.mean(cffq_b10_dequeue_throughput)
rffq_b100_avg_throughput = np.mean(rffq_b100_dequeue_throughput)
rffq_b10_avg_throughput = np.mean(rffq_b10_dequeue_throughput)

bbq_avg_latency = np.mean(bbq_dequeue_latency_avg)
david_avg_latency = np.mean(david_dequeue_latency_avg)
cffq_b100_avg_latency = np.mean(cffq_b100_dequeue_latency_avg)
cffq_b10_avg_latency = np.mean(cffq_b10_dequeue_latency_avg)
rffq_b100_avg_latency = np.mean(rffq_b100_dequeue_latency_avg)
rffq_b10_avg_latency = np.mean(rffq_b10_dequeue_latency_avg)

# Data for plotting
queues = ['BBQ', 'David', 'c-FFQ\n(b=100)', 'c-FFQ\n(b=10)', 'r-FFQ\n(b=100)', 'r-FFQ\n(b=10)']
avg_throughputs = [bbq_avg_throughput, david_avg_throughput, cffq_b100_avg_throughput, 
                   cffq_b10_avg_throughput, rffq_b100_avg_throughput, rffq_b10_avg_throughput]
avg_latencies = [bbq_avg_latency, david_avg_latency, cffq_b100_avg_latency, 
                 cffq_b10_avg_latency, rffq_b100_avg_latency, rffq_b10_avg_latency]

# Create figure with 2 subplots
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))

# Plot 1: Throughput
colors = ['#2E86AB', '#A23B72', '#F18F01', '#FFA07A', '#C73E1D', '#FF6B6B']
bars1 = ax1.bar(queues, avg_throughputs, color=colors, alpha=0.8, edgecolor='black', linewidth=1.5)
ax1.set_ylabel('Throughput (items/sec)', fontsize=12, fontweight='bold')
ax1.set_title('Average Dequeue Throughput Comparison', fontsize=14, fontweight='bold')
ax1.grid(axis='y', alpha=0.3, linestyle='--')
ax1.set_ylim(0, max(avg_throughputs) * 1.15)
ax1.tick_params(axis='x', labelsize=9)

# Add value labels on bars
for bar in bars1:
    height = bar.get_height()
    ax1.text(bar.get_x() + bar.get_width()/2., height,
             f'{height:.2f}',
             ha='center', va='bottom', fontsize=9, fontweight='bold')

# Plot 2: Latency
bars2 = ax2.bar(queues, avg_latencies, color=colors, alpha=0.8, edgecolor='black', linewidth=1.5)
ax2.set_ylabel('Latency (μs)', fontsize=12, fontweight='bold')
ax2.set_title('Average Dequeue Latency Comparison', fontsize=14, fontweight='bold')
ax2.grid(axis='y', alpha=0.3, linestyle='--')
ax2.set_ylim(0, max(avg_latencies) * 1.15)
ax2.tick_params(axis='x', labelsize=9)

# Add value labels on bars
for bar in bars2:
    height = bar.get_height()
    ax2.text(bar.get_x() + bar.get_width()/2., height,
             f'{height:.2f}',
             ha='center', va='bottom', fontsize=9, fontweight='bold')

plt.tight_layout()
plt.savefig('dequeue_comparison.png', dpi=300, bbox_inches='tight')
plt.show()

# Print summary
print("\n" + "="*70)
print("DEQUEUE PERFORMANCE SUMMARY")
print("="*70)
print(f"\nAverage Throughput (items/sec):")
print(f"  BBQ:              {bbq_avg_throughput:>12.2f}")
print(f"  David:            {david_avg_throughput:>12.2f}")
print(f"  c-FFQ (b=100):    {cffq_b100_avg_throughput:>12.2f}")
print(f"  c-FFQ (b=10):     {cffq_b10_avg_throughput:>12.2f}")
print(f"  r-FFQ (b=100):    {rffq_b100_avg_throughput:>12.2f}")
print(f"  r-FFQ (b=10):     {rffq_b10_avg_throughput:>12.2f}")
print(f"\nAverage Latency (μs):")
print(f"  BBQ:              {bbq_avg_latency:>12.2f}")
print(f"  David:            {david_avg_latency:>12.2f}")
print(f"  c-FFQ (b=100):    {cffq_b100_avg_latency:>12.2f}")
print(f"  c-FFQ (b=10):     {cffq_b10_avg_latency:>12.2f}")
print(f"  r-FFQ (b=100):    {rffq_b100_avg_latency:>12.2f}")
print(f"  r-FFQ (b=10):     {rffq_b10_avg_latency:>12.2f}")
print("="*70)
