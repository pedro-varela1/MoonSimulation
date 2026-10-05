"""CSV tables of linna2.exe -> parquet next to them: the catalog, the S2 truth (truth, sun, arcs) and the window truth files.
Usage: python to_parquet.py <folder> [<folder> ...]   (the output_dir and/or sample_dir of config.ini)
A table is converted again only when its CSV is newer than its parquet. The CSVs stay (linna2.exe reads the catalog one)."""
import glob
import os
import sys

import pandas as pd

for root in sys.argv[1:]:
    tables = [os.path.join(root, "catalog", "robbins_linna2.csv")] + glob.glob(os.path.join(root, "S2", "truth", "*.csv")) + \
        glob.glob(os.path.join(root, "S2", "windows", "*", "*", "window_*.csv"))
    for csv in tables:
        pq = csv[:-4] + ".parquet"
        if not os.path.exists(csv) or (os.path.exists(pq) and os.path.getmtime(pq) >= os.path.getmtime(csv)):
            continue
        df = pd.read_csv(csv, dtype={"crater_id": str})
        df.to_parquet(pq, index=False)
        print(f"{pq}: {len(df)} rows, {os.path.getsize(pq) / 1e6:.1f} MB (CSV {os.path.getsize(csv) / 1e6:.1f} MB)")
