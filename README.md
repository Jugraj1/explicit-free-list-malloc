<!--
    README.md  ·  seglist‑malloc
    ======================================================
    A lightweight, segregated‑free‑list memory allocator
    for ANU COMP2310 Assignment 1 · 2025
-->

<h1 align="center">seglist‑malloc 🗂️ (ANU Project)</h1>
<p align="center">
  <i>A 16‑byte‑metadata, constant‑time‑coalescing allocator<br>
     with an optional mark‑and‑sweep garbage collector</i>
</p>

<p align="center">
  <a href="#"><img alt="CI" src="https://img.shields.io/badge/CI-passing-brightgreen"></a>
  <a href="LICENSE"><img alt="MIT" src="https://img.shields.io/badge/license-MIT-blue"></a>
</p>

---

## Table&nbsp;of&nbsp;Contents
1. [Project Goals](#project-goals)  
2. [Features](#features)  
3. [Quick Start](#quick-start)  
4. [Public API](#public-api)  
5. [Design Overview](#design-overview)  
6. [Garbage Collector](#garbage-collector)  
7. [Repository Layout](#repository-layout)  
8. [Quality Assurance](#quality-assurance)  
9. [Benchmarks](#benchmarks)  
10. [Limitations & Roadmap](#limitations--roadmap)  
11. [Contributing](#contributing)  
12. [License](#license)  

---

## Project Goals
* **O(1) allocation/deallocation** for common sizes (segregated lists).  
* **Minimal metadata** – 16 bytes per block by using a 32‑bit relative next pointer. :contentReference[oaicite:0]{index=0}&#8203;:contentReference[oaicite:1]{index=1}  
* **Constant‑time coalescing** with boundary tags (footer on every block). :contentReference[oaicite:2]{index=2}&#8203;:contentReference[oaicite:3]{index=3}  
* **Dynamic growth** – request new chunks from the OS via `mmap`.  
* **Optional GC** – pluggable mark‑and‑sweep for leak detection in tests.  

---

## Features
| Category | Detail |
|----------|--------|
| **Allocator** | Segregated free lists (size‑class buckets) for near‑O(1) best‑fit search |
| | 16‑byte `Block` header: `size_and_flags` (8 B) + `int32_t next_offset` (4 B) + padding (4 B) |
| | Bits 0–1 encode current/prev allocation status; remaining 62 bits = size |
| | Boundary‑tag footers on **every** block → constant‑time left/right merges |
| | Fenceposts (chunk start/end) prevent out‑of‑chunk coalescing |
| **Growth** | New `Chunk`s mapped with `mmap`; each stores `base`, `end`, and first‑block ptr |
| **GC (opt‑in)** | Two‑phase mark & sweep; high bit of chunk‑index doubles as mark bit :contentReference[oaicite:4]{index=4}&#8203;:contentReference[oaicite:5]{index=5} |
| **Testing** | Unit + stress tests, Valgrind, AddressSanitizer, GitHub Actions |

---

## Quick Start
```bash
# clone
git clone https://github.com/Jugraj1/seglist-malloc.git
cd seglist-malloc

# build static library + tests
make                 # libseglist.a → build/, tests → bin/

# run self‑tests
make test

# enable garbage collector (compile‑time flag)
make CFLAGS+='-DUSE_GC'

