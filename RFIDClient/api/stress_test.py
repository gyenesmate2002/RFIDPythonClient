import requests
import threading
import time
import statistics

results = []
errors = 0


def worker(url, count):
    global errors

    for _ in range(count): # Forloop is infinitely running
        start = time.time()

        try:
            r = requests.get(url, timeout=3)  # Request doesnt go out and errors increases
            print(r)

            latency = (time.time() - start) * 1000
            results.append(latency)

        except Exception as e:
            errors += 1
            print("ERROR in stress-test worker:", e)


def run_stress_test(url, clients=1, requests_per_client=50):

    threads = []

    start_time = time.time()

    for _ in range(clients):
        t = threading.Thread(
            target=worker,
            args=(url, requests_per_client)
        )
        threads.append(t)
        t.start()
    print(threads)
    for t in threads:
        print(t)
        t.join()
    print("After forloops")
    total_time = time.time() - start_time

    return {
        "requests_total": clients * requests_per_client,
        "avg_latency": statistics.mean(results),
        "min_latency": min(results),
        "max_latency": max(results),
        "errors": errors,
        "duration": total_time,
        "req_per_sec": len(results) / total_time
    }