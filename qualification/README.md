# Windows depth-copy check

The Windows runner reconstructs the pinned SDK using the standalone depth-copy commit in `windows.py`. It compiles the actual draw engine and uses the runtime translator and pinned glslang to compile both shader stages and compare their entry-point interfaces.

The job uses source only and records results in the log and step summary. Windows gameplay and full game linking require a separate check.
