import random

import requests
import threading
import time
import statistics

# Global lock for thread-safe operations on shared resources
lock = threading.Lock()


def worker(url, count, results, errors):
    """
    Worker function representing a single client.

    Sends a fixed number of HTTP GET requests to the target URL
    as fast as possible (no delay between requests).

    Args:
        url (str): Target endpoint
        count (int): Number of requests to send
        results (list): Shared list storing latency values (ms)
        errors (list): Shared list (size 1) tracking error count
    """

    # Use a session to reuse TCP connections (faster, more realistic)
    session = requests.Session()

    for _ in range(count):

        start = time.perf_counter()

        # Generate a random complexity value between 1 and 10
        complexity = random.randint(1, 10)

        try:
            # Send HTTP GET request with timeout
            response = session.get(url, timeout=3)

            # Calculate latency in milliseconds
            latency = (time.perf_counter() - start) * 1000

            # Combine latency with the response JSON
            response_data = response.json()
            response_data["latency"] = latency
            response_data["complexity"] = complexity

            # Safely store results
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
            # Count all request failures (timeouts, connection errors, etc.)
            with lock:
                results.append({"error": True, "error_type": "exception"})
                errors[0] += 1


def run_stress_test(url, clients=1, req_per_client=50):
    """
    Executes a concurrent stress test against a given URL.

    Creates multiple threads (clients), each sending a fixed number
    of requests without delay to simulate burst traffic.

    Args:
        url (str): Endpoint to test
        clients (int): Number of concurrent clients
        req_per_client (int): Requests per client

    Returns:
        dict: Aggregated performance metrics or None if no data collected
    """

    # Shared data structures
    results = []   # Stores latency values (ms)
    errors = [0]   # Mutable container for error counting

    threads = []

    # Track total execution time
    start_time = time.perf_counter()

    # Create and start client threads
    for _ in range(clients):
        thread = threading.Thread(
            target=worker,
            args=(url, req_per_client, results, errors)
        )

        threads.append(thread)
        thread.start()

    # Wait for all threads to complete
    for thread in threads:
        thread.join()

    # If no successful requests were recorded
    if len(results) == 0:
        return None

    total_time = time.perf_counter() - start_time

    latencies = [r.get("latency") for r in results if r.get("latency") is not None]

    percentiles = statistics.quantiles(latencies, n=100)

    # Build and return performance summary
    return {
        "total_requests": len(results),
        "avg_latency": statistics.mean(latencies) if latencies else 0,
        "min_latency": min(latencies) if latencies else 0,
        "max_latency": max(latencies) if latencies else 0,
        "p50_latency": percentiles[49],  # median
        "p95_latency": percentiles[94],
        "p99_latency": percentiles[98],
        "requests_per_sec": len(latencies) / total_time if total_time else 0,
        "error_count": errors[0],
        "request_logs": results
    }
