# DLC cache comparison, 2026-10-27

**Outcome (2026-10-28):** the persisted flat-library cache was removed. Scan
threads remain. Refresh Songs now routes through native discovery and rescans
the host library on a worker before the guest enumerates it. The measurements
below document why the old cache was retired.

## Scope and method

The former production host loader was measured directly with the now-retired
`tools/dlc_cache_benchmark.cpp`. The library
contained 1,777 packages. Each trial used a new cache-file path and the game's
configured four scan threads. The benchmark ran three trials, placing the
cache-off scan first in trials 1 and 3 and last in trial 2. Raw results are in
[`dlc-cache-comparison-2026-10-27.csv`](dlc-cache-comparison-2026-10-27.csv).

Historical reproduction command (the benchmark tool was retired with the cache):

```powershell
clang++ -O2 -std=c++23 -Isrc tools/dlc_cache_benchmark.cpp -o out/dlc-cache-compare/dlc_cache_benchmark.exe
.\out\dlc-cache-compare\dlc_cache_benchmark.exe 'D:\Games\Rock Band DLCs' 'out\dlc-cache-compare\new-trial' 4
```

`first_cache_write` means **no application cache file exists**. It still scans
every package, verifies the tree again, and writes a cache. `cache_hit` is the
next load using that file. `forced_refresh` skips reading an existing cache,
scans and rewrites it. These conditions do not flush Windows' file cache or
reset antivirus state.

## Host timings

| Condition | Trial 1 | Trial 2 | Trial 3 | Warm median |
| --- | ---: | ---: | ---: | ---: |
| Cache off | 20,751.7 ms | 93.7 ms | 94.9 ms | 94.3 ms (trials 2–3) |
| First cache write | 179.5 ms | 130.2 ms | 138.2 ms | 138.2 ms |
| Cache hit | 8.9 ms | 12.7 ms | 18.5 ms | 12.7 ms |
| Forced refresh | 143.6 ms | 137.9 ms | 157.9 ms | 143.6 ms |

On this warm library, a hit saved about **81.6 ms** against the two warm
cache-off scans. Creating the cache for the first time was about **43.9 ms**
slower than a warm cache-off scan (median first write versus mean of warm
cache-off trials). These are small fractions of the game's loading time.

The benchmark's first scan took **20.75 s**. The same library scanned in about
94 ms later in the run, consistent with an expensive first file access. A cache
hit can avoid that header-open cost on a later boot, but this experiment has
only **one** slow first scan and did not explicitly flush the OS file cache.
It does not establish how often normal boots encounter that cost. A first boot
without an application cache cannot avoid the scan.

## Game boots

The existing game logs show 3,554 guest enumeration items in each run:

| Log | Host cache | Renderer / scale | Host library step | Menu load |
| --- | --- | --- | ---: | ---: |
| `rb_blitz_401.log` | Off | Vulkan / 1 | 76.0 ms | 16.64 s |
| `rb_blitz_402.log` | Hit | Vulkan / 2 | 20.6 ms | 45.42 s |
| `rb_blitz_403.log` | Hit | D3D12 / 1 | 207.2 ms | 35.27 s |
| `rb_blitz_404.log` | Hit | D3D12 / 1 | 4.8 ms | 25.71 s |

These four runs are **not** a matched end-to-end cache comparison: renderer and render
size changed between Off and On. In `403`, D3D12 also rejected 403 stored PSO
blobs and spent 9.56 s preparing graphics. `404` used the same backend and
scale with no rejected blobs and spent 1.40 s preparing graphics. The host
cache alone cannot explain the multi-second differences between these runs.

Three subsequent boots used the same build, profile, library and `-SafeGraphics`
setting: D3D12, 1280×720 guest render base, scale 1. The D3D12 pipeline cache
was warm. Each run found 1,777 packages and completed 3,554 guest enumeration
items with no failures. The `dlc_load` timer starts after the host library step
and ends when the usable main menu appears.

| Log | Cache state | Host library step | `dlc_load` | Host step + `dlc_load` |
| --- | --- | ---: | ---: | ---: |
| `rb_blitz_405.log` | Off, scan | 110.9 ms | 24,439.4 ms | 24,550.3 ms |
| `rb_blitz_406.log` | On, forced rebuild | 122.9 ms | 24,548.8 ms | 24,671.7 ms |
| `rb_blitz_407.log` | On, hit | 4.8 ms | 24,478.6 ms | 24,483.5 ms |

The last column adds two measured stages; it is **not** a continuous boot timer
because other startup work occurs between them. The cache hit removed 106.1 ms
of host work relative to Off. The three `dlc_load` results span 109.4 ms, with
the hit's guest time 39.2 ms **slower** than Off. Those differences are small
relative to the 24.5 s game loading phase and do not show a guest-side speedup
or slowdown caused by the cache. A forced rebuild was 12.0 ms slower than Off
in the host step; a true first boot without a cache file also has to scan and
write before it can produce a later hit. Graphics preparation was 803, 813 and
798 ms respectively, with no rejected cached PSO blobs in these runs.

## Assessment

For a warm library, this cache saves about **0.08–0.11 s** of host work and has
no demonstrated noticeable effect on reaching the main menu. It does **not**
fulfill a promise to make every launch's DLC loading visibly faster. It can
avoid a much larger first-access header scan, observed once at **20.75 s** in
the host benchmark; how often that condition recurs has not been measured.
The feature does not cache the guest's enumeration, mounting or song metadata.

The cache-hit path itself took 4.8 ms in the matched game run, so optimizing
that path cannot materially shorten the 24.5 s guest phase. A meaningful
further speedup would require changing the guest-side enumeration or rendering
during discovery, which is a separate, more invasive effort. The large
historical reduction from removing emulated mount latency now applies whether
the cache is on or off. Removing the cache toggle also currently affects the
`Refresh Songs` menu behavior, so that behavior would need an independent
decision if the cache feature were removed.
