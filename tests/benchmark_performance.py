import os
import sys
import time
import subprocess
import concurrent.futures

def run_headless_instance(instance_id, frames, rom_path, bios_path, exe_path):
    cmd = [
        exe_path,
        "--no-window",
        "--quiet",
        "--frames", str(frames),
        "--rom", rom_path,
        "--bios", bios_path
    ]
    t0 = time.perf_counter()
    res = subprocess.run(cmd, capture_output=True, text=True)
    elapsed = time.perf_counter() - t0
    fps = frames / elapsed if elapsed > 0 else 0
    return {
        "id": instance_id,
        "returncode": res.returncode,
        "frames": frames,
        "elapsed_sec": elapsed,
        "fps": fps
    }

def benchmark(concurrency_levels=[1, 2, 4], frames_per_run=300):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    exe_path = os.path.join(root, "KHCOMRecomp.exe")
    rom_path = os.path.join(root, "roms", "Kingdom Hearts - Chain of Memories (U)(Venom).gba")
    bios_path = os.path.join(root, "bios", "gba_bios.bin")

    if not os.path.exists(exe_path):
        print(f"Error: {exe_path} not found.")
        sys.exit(1)
    if not os.path.exists(rom_path):
        print(f"Error: {rom_path} not found.")
        sys.exit(1)
    if not os.path.exists(bios_path):
        print(f"Error: {bios_path} not found.")
        sys.exit(1)

    print("================================================================")
    print("      KHCOMRecomp Parallel Automated Performance Benchmark      ")
    print("================================================================")
    print(f"Executable: {exe_path}")
    print(f"Frames per Instance: {frames_per_run}")
    print()

    results_table = []

    for workers in concurrency_levels:
        print(f"Running benchmark with {workers} parallel instance(s)...")
        start_time = time.perf_counter()

        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
            futures = [
                executor.submit(run_headless_instance, i, frames_per_run, rom_path, bios_path, exe_path)
                for i in range(workers)
            ]
            results = [f.result() for f in futures]

        total_elapsed = time.perf_counter() - start_time
        total_frames = sum(r["frames"] for r in results)
        aggregate_fps = total_frames / total_elapsed if total_elapsed > 0 else 0
        avg_fps_per_worker = sum(r["fps"] for r in results) / len(results) if results else 0

        all_passed = all(r["returncode"] in (0, 1) for r in results)

        print(f"  Completed in {total_elapsed:.3f}s")
        print(f"  Aggregate Throughput: {aggregate_fps:.1f} FPS")
        print(f"  Average Worker Speed: {avg_fps_per_worker:.1f} FPS ({1000.0/avg_fps_per_worker:.3f} ms/frame)")
        print(f"  Status: {'PASS' if all_passed else 'FAIL'}")
        print()

        results_table.append({
            "workers": workers,
            "total_frames": total_frames,
            "elapsed_sec": total_elapsed,
            "aggregate_fps": aggregate_fps,
            "avg_worker_fps": avg_fps_per_worker,
            "ms_per_frame": 1000.0 / avg_fps_per_worker if avg_fps_per_worker > 0 else 0
        })

    print("================================================================")
    print("                 Benchmark Summary Matrix                       ")
    print("================================================================")
    print("Workers | Total Frames | Elapsed (s) | Aggregate FPS | ms/frame")
    print("--------+--------------+-------------+---------------+---------")
    for row in results_table:
        print(f"   {row['workers']:<4} | {row['total_frames']:<12} | {row['elapsed_sec']:<11.3f} | {row['aggregate_fps']:<13.1f} | {row['ms_per_frame']:.3f} ms")
    print("================================================================")

if __name__ == "__main__":
    benchmark(concurrency_levels=[1, 2, 4], frames_per_run=300)
