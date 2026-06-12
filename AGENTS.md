# AGENTS.md

## Repository layout (important)

This repository is a **multi-project sandbox**. The `main` branch is an **empty placeholder**
(it only contains this file and a one-line `README.md`). All real work lives in **independent
feature branches** under the `cursor/*` namespace, and the branches are **unrelated to each other**.

There is no single "application" on `main`, and there are **no shared dependencies at the repo root**.
Each feature branch is a self-contained project. The branch types seen so far:

- **C++17 / CMake** computer-vision & charting projects (e.g. `cursor/cpp-chart-display-*`,
  `cursor/infrared-temp-extraction-*`, `cursor/ir-human-detect-*`, `cursor/thermal-human-detector-*`,
  `cursor/needle-bend-detection-*`). Some are pure C++17 (no external deps); others need OpenCV.
- **Python** thermal/CV projects with a `requirements.txt` (e.g. `cursor/thermal-human-detection-mask-*`).
- **Windows / MFC** code (e.g. `cursor/excel-file-utils-*`, `cursor/thermal-lr-stddev-*`,
  `cursor/mfc-voice-recognition-guide-*`). These target Visual Studio / MFC and **cannot be built on this Linux VM**.

When asked to "set up / run the project", first determine which branch you are on; the setup
differs per branch as described below.

## Cursor Cloud specific instructions

The base toolchain is preinstalled: `gcc`/`g++` 13, `cmake` 3.28, `make`, `python3` 3.12 (with
`python3.12-venv`), and `node` 22.

### C++ / CMake branches — non-obvious gotcha
- The default `c++` / `cc` on this VM resolves to **Clang, which is broken** (missing C++ standard
  headers and cannot link `-lstdc++`). **Always build with GCC.** A bare `cmake ..` will pick Clang
  and fail at "Check for working CXX compiler".
- Configure explicitly with GCC, then build:
  ```bash
  mkdir -p build && cd build
  cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
  make -j$(nproc)
  ```
- Projects that depend on OpenCV need it installed at the system level (e.g. `sudo apt-get install -y libopencv-dev`); pure-C++17 projects do not.
- Some binaries resolve their sample-data paths relative to the **current working directory**
  (e.g. the chart generator's `demo` mode expects `data/...` under `cwd`). Pass explicit file
  paths or run from the project root to avoid "Cannot open file" errors.

### Python branches
- This VM is an externally-managed Python (PEP 668), so install into a **virtualenv**, not system pip:
  ```bash
  python3 -m venv .venv
  ./.venv/bin/pip install -r requirements.txt   # or just the core deps below
  ./.venv/bin/python -m pytest -v
  ```
- For CV branches, the heavyweight `ultralytics` (pulls in PyTorch) is only needed for YOLO mode.
  For tests / HOG / threshold modes the core deps are enough and far faster to install:
  `opencv-python-headless`, `numpy`, `pytest` (use `opencv-python-headless` since the VM has no display libs for full `opencv-python`).

### Update script note
The startup update script is intentionally minimal because `main` has no shared dependencies.
It best-effort installs a branch's `requirements.txt` into `.venv` **only when that file exists**,
so it is a no-op on `main` and on C++/MFC branches. C++ configure/build (with the GCC flags above)
and any system packages (OpenCV, etc.) are intentionally left out of the update script and must be
run manually per the instructions above.
