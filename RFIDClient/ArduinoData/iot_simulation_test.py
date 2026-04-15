import requests
import threading
import time
import statistics
import random

# Global lock for thread-safe operations on shared resources
lock = threading.Lock()


def worker(url, interval, duration, results, errors):
    """
    Simulates a single IoT device.

    Sends periodic HTTP GET requests to the target URL for the given duration.

    Args:
        url (str): Target endpoint
        interval (float): Delay between requests (seconds)
        duration (int): Total runtime for this worker (seconds)
        results (list): Shared list to store response data with latency
        errors (list): Shared list (size 1) to count errors
    """
    session = requests.Session()
    end_time = time.perf_counter() + duration

    while time.perf_counter() < end_time:
        start = time.perf_counter()

        # Generate a random complexity value between 1 and 10
        complexity = random.randint(1, 10)

        try:
            response = session.get(f"{url}?complexity={complexity}", timeout=3)
            latency = (time.perf_counter() - start) * 1000

            response_data = response.json()
            response_data["latency"] = latency
            response_data["complexity"] = complexity

            with lock:
                if response.status_code != 200 or response_data.get("error", False):
                    response_data["error"] = True
                    response_data["error_type"] = "non_200"
                    errors[0] += 1

                results.append(response_data)

        except requests.exceptions.Timeout:
            with lock:
                results.append({"error": True, "error_type": "timeout"})
                errors[0] += 1

        except Exception:
            with lock:
                results.append({"error": True, "error_type": "exception"})
                errors[0] += 1

        time.sleep(interval)


def run_iot_simulation(url, devices=50, interval=5, duration=300):
    """
    Runs a multi-device IoT simulation test.

    Args:
        url (str): Endpoint to test
        devices (int): Number of simulated devices
        interval (float): Request interval per device (seconds)
        duration (int): Total test duration (seconds)

    Returns:
        dict: Performance metrics or None if no data collected
    """
    results = []
    errors = [0]
    threads = []

    start_time = time.perf_counter()

    for _ in range(devices):
        thread = threading.Thread(target=worker, args=(url, interval, duration, results, errors))
        threads.append(thread)
        thread.start()

    for thread in threads:
        thread.join()

    if not results:
        return None

    total_time = time.perf_counter() - start_time

    latencies = [r.get("latency") for r in results if r.get("latency") is not None]

    percentiles = statistics.quantiles(latencies, n=100)

    return {
        "total_requests": len(results),
        "avg_latency": statistics.mean(latencies) if latencies else 0,
        "min_latency": min(latencies) if latencies else 0,
        "max_latency": max(latencies) if latencies else 0,
        "p50_latency": percentiles[49],
        "p95_latency": percentiles[94],
        "p99_latency": percentiles[98],
        "requests_per_sec": len(latencies) / total_time if total_time else 0,
        "error_count": errors[0],
        "request_logs": results,
    }
