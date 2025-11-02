## Mac M1

| Implementation | Duration | Throughput (ops/sec) | Throughput (M ops/sec) | Relative Performance |
|-----------|----------|---------------------|------------------|--------------------|
| Lock | 4.063s | 39,379,768 | 39.38 | 1.00x |
| Simple CAS | 8.620s | 18,561,484 | 18.56 | 0.47x |
| Combiner | 6.766s | 23,647,650 | 23.65 | 0.60x |

---

## AMD 8-Core Processor

| Implementation | Duration | Throughput (ops/sec) | Throughput (M ops/sec) | Relative Performance |
|--------------|----------|---------------------|------------------|---------------------|
| Lock | 5.133s | 31,170,855 | 31.17 | 0.28x |
| Simple CAS | 1.427s | 112,123,335 | 112.12 | 1.00x |
| Combiner | 4.183s | 38,250,059 | 38.25 | 0.34x |

---

## AMD 48-Core Processor

| Implementation | Duration | Throughput (ops/sec) | Throughput (M ops/sec) | Relative Performance |
|------------|----------|---------------------|------------------|---------------------|
| Lock | 4.653s | 20,631,850 | 20.63 | 0.34x |
| Simple CAS | 1.601s | 59,962,523 | 59.96 | 1.00x |
| Combiner | 3.545s | 27,080,394 | 27.08 | 0.45x |

