# BBQ Queue - Enqueue Performance Data (5 runs)

bbq_enqueue_throughput = [17242.46, 16134.21, 17085.61, 17085.61, 17498.25]  # items/sec

bbq_enqueue_latency_avg = [57.93, 61.92, 58.48, 58.48, 57.10]  # microseconds


# David Queue - Enqueue Performance Data (5 runs)

david_enqueue_throughput = [17842.90, 16474.16, 17450.12, 17117.31, 16711.32]  # items/sec

david_enqueue_latency_avg = [56.00, 60.65, 57.26, 58.37, 59.79]  # microseconds


# c-FFQ Queue - Enqueue Performance Data (5 runs)

cffq_enqueue_throughput = [6021.60, 5823.03, 5647.15, 5920.98, 5605.54]  # items/sec

cffq_enqueue_latency_avg = [166.02, 171.65, 177.02, 168.84, 178.33]  # microseconds


# r-FFQ Queue - Enqueue Performance Data (5 runs)

rffq_enqueue_throughput = [5844.97, 5926.72, 5927.66, 5615.14, 5734.09]  # items/sec

rffq_enqueue_latency_avg = [171.04, 168.68, 168.65, 178.03, 174.35]  # microseconds


# Calculate averages
import numpy as np
import matplotlib.pyplot as plt

bbq_avg_throughput = np.mean(bbq_enqueue_throughput)
david_avg_throughput = np.mean(david_enqueue_throughput)
cffq_avg_throughput = np.mean(cffq_enqueue_throughput)
rffq_avg_throughput = np.mean(rffq_enqueue_throughput)

bbq_avg_latency = np.mean(bbq_enqueue_latency_avg)
david_avg_latency = np.mean(david_enqueue_latency_avg)
cffq_avg_latency = np.mean(cffq_enqueue_latency_avg)
rffq_avg_latency = np.mean(rffq_enqueue_latency_avg)

# Data for plotting
queues = ['BBQ', 'David', 'c-FFQ', 'r-FFQ']
avg_throughputs = [bbq_avg_throughput, david_avg_throughput, cffq_avg_throughput, rffq_avg_throughput]
avg_latencies = [bbq_avg_latency, david_avg_latency, cffq_avg_latency, rffq_avg_latency]

# Create figure with 2 subplots
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))

# Plot 1: Throughput
colors = ['#2E86AB', '#A23B72', '#F18F01', '#C73E1D']
bars1 = ax1.bar(queues, avg_throughputs, color=colors, alpha=0.8, edgecolor='black', linewidth=1.5)
ax1.set_ylabel('Throughput (items/sec)', fontsize=12, fontweight='bold')
ax1.set_title('Average Enqueue Throughput Comparison', fontsize=14, fontweight='bold')
ax1.grid(axis='y', alpha=0.3, linestyle='--')
ax1.set_ylim(0, max(avg_throughputs) * 1.15)

# Add value labels on bars
for bar in bars1:
    height = bar.get_height()
    ax1.text(bar.get_x() + bar.get_width()/2., height,
             f'{height:.2f}',
             ha='center', va='bottom', fontsize=10, fontweight='bold')

# Plot 2: Latency
bars2 = ax2.bar(queues, avg_latencies, color=colors, alpha=0.8, edgecolor='black', linewidth=1.5)
ax2.set_ylabel('Latency (μs)', fontsize=12, fontweight='bold')
ax2.set_title('Average Enqueue Latency Comparison', fontsize=14, fontweight='bold')
ax2.grid(axis='y', alpha=0.3, linestyle='--')
ax2.set_ylim(0, max(avg_latencies) * 1.15)

# Add value labels on bars
for bar in bars2:
    height = bar.get_height()
    ax2.text(bar.get_x() + bar.get_width()/2., height,
             f'{height:.2f}',
             ha='center', va='bottom', fontsize=10, fontweight='bold')

plt.tight_layout()
plt.savefig('enqueue_comparison.png', dpi=300, bbox_inches='tight')
plt.show()

# Print summary
print("\n" + "="*60)
print("ENQUEUE PERFORMANCE SUMMARY")
print("="*60)
print(f"\nAverage Throughput (items/sec):")
print(f"  BBQ:        {bbq_avg_throughput:>10.2f}")
print(f"  David:      {david_avg_throughput:>10.2f}")
print(f"  c-FFQ:      {cffq_avg_throughput:>10.2f}")
print(f"  r-FFQ:      {rffq_avg_throughput:>10.2f}")
print(f"\nAverage Latency (μs):")
print(f"  BBQ:        {bbq_avg_latency:>10.2f}")
print(f"  David:      {david_avg_latency:>10.2f}")
print(f"  c-FFQ:      {cffq_avg_latency:>10.2f}")
print(f"  r-FFQ:      {rffq_avg_latency:>10.2f}")
print("="*60)

