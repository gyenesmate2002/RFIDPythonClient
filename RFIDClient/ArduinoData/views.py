from django.utils import timezone
from rest_framework.decorators import api_view, authentication_classes
from django.views.decorators.csrf import csrf_exempt
from rest_framework.response import Response

from .models import (
    RFIDRequestLog,
    IoTStressTestSummary,
    IoTSimulationSummary,
    IoTRequestLog
)
from .serializer import (
    RFIDRequestLogSerializer,
    IoTStressTestSummarySerializer,
    IoTSimulationSummarySerializer,
    IoTRequestLogSerializer
)

from .iot_simulation_test import run_iot_simulation
from .stress_test import run_stress_test


ALLOWED_UID = "23E661F5"
IOT_TEST_ARDUINO_URL = "http://192.168.0.89/benchmark"

LAST_SCAN_FOR_FRONTEND = None


# --- RFID ---
# Endpoint for the RFID test
@api_view(['GET'])
def rfid_scan(request):
    global LAST_SCAN_FOR_FRONTEND

    uid = request.GET.get("uid")

    print(f"Received RFID scan with UID: {uid} at {timezone.now()}")

    if uid is None:
        return Response(status=400, headers={"X-Status": "error"})

    status = "accepted" if uid == ALLOWED_UID else "denied"

    LAST_SCAN_FOR_FRONTEND = {
        "status": status,
        "created_at": timezone.now()
    }

    return Response(status=200, headers={"X-Status": status})


# RFID last scan display endpoint for Frontend
@api_view(['GET'])
def rfid_latest(request):
    global LAST_SCAN_FOR_FRONTEND

    if LAST_SCAN_FOR_FRONTEND:
        return Response(LAST_SCAN_FOR_FRONTEND)
    else:
        return Response({
            "status": "none",
            "created_at": None
        })


# Endpoint for saving RFID test diagnostics in database from the MCU (after every 10 requests)
@api_view(['POST'])
def rfid_save_measurements(request):
    """
    Diagnostics endpoint for RFID benchmark measurements.
    Expects JSON payload matching the RFIDRequestLog model fields.
    The created_at field is automatically set by the model.
    """

    data = request.data

    print(data)

    # Required fields based on the RFIDRequestLog model
    required_fields = [
        "platform", "test_id"
    ]
    missing = [f for f in required_fields if f not in data]
    if missing:
        return Response({"status": "error", "message": f"Missing fields: {', '.join(missing)}"}, status=400)

    serializer = RFIDRequestLogSerializer(data=data)

    if serializer.is_valid():
        measurement = serializer.save()
        return Response({
            "status": "success",
            "id": measurement.id,
            "created_at": measurement.created_at
        })

    return Response({"status": "error", "errors": serializer.errors}, status=400)


# Endpoint for displaying RFID test diagnostics from database
@api_view(['GET'])
def rfid_get_measurements(request):
    test_id = request.GET.get("test_id")  # Retrieve test_id from the request

    if not test_id:
        return Response({"status": "error", "message": "test_id is required"}, status=400)

    measurements = RFIDRequestLog.objects.filter(test_id=test_id).order_by('-created_at')

    serializer = RFIDRequestLogSerializer(measurements, many=True)

    return Response(serializer.data)


# --- IoT gateway ---
# Run stress-test endpoint
@api_view(['POST'])
@authentication_classes([])
def iot_stress_test(request):
    """
    Execute IoT gateway stress test and save results, including request logs.
    """

    platform = request.data.get("platform", "arduino_r4")
    test_id = request.data.get("test_id", f"stress_test_{timezone.now().isoformat()}")
    clients = int(request.data.get("clients", 1))
    req_per_client = int(request.data.get("requests", 50))

    result = run_stress_test(IOT_TEST_ARDUINO_URL, clients, req_per_client)

    if result is None:
        return Response({
            "status": "error",
            "message": "No results generated"
        }, status=500)

    # Save request_logs individually
    request_logs = result.get("request_logs", [])
    for log in request_logs:
        log_data = {
            "platform": platform,
            "test_id": test_id,
            "latency": float(log.get("latency")) if log.get("latency") is not None else None,
            "error": bool(log.get("error", True)),
            "error_type": log.get("error_type", None),
            "free_ram": int(log.get("free_ram")) if log.get("free_ram") is not None else None,
            "min_free_ram": int(log.get("min_free_ram")) if log.get("min_free_ram") is not None else None,
            "computation_time": int(log.get("computation_time")) if log.get("computation_time") is not None else None,
            "complexity": int(log.get("complexity")) if log.get("complexity") is not None else None,
            "wifi_rssi": int(log.get("wifi_rssi")) if log.get("wifi_rssi") is not None else None,
        }

        serializer = IoTRequestLogSerializer(data=log_data)
        if serializer.is_valid():
            serializer.save()
        else:
            print(serializer.errors)  # Log errors for debugging

    # Save summary results
    result.pop("request_logs", None)  # Remove detailed logs from summary
    summary_data = {
        "platform": platform,
        "test_id": test_id,
        "client_count": clients,
        "req_per_client": req_per_client,
        **result
    }

    serializer = IoTStressTestSummarySerializer(data=summary_data)
    if serializer.is_valid():
        measurement = serializer.save()
        return Response({
            "status": "success",
            "id": measurement.id,
            "created_at": measurement.created_at,
            "data": serializer.data
        })

    return Response({
        "status": "error",
        "errors": serializer.errors
    }, status=400)


# Endpoint for displaying IoT gateway stress-test diagnostics from database
@api_view(['GET'])
def iot_stress_test_measurements(request):
    test_id = request.GET.get("filter_test_id")  # Retrieve test_id from the request
    print(f"Filtering IoT stress test measurements for test_id: {test_id}")

    if not test_id:
        return Response({"status": "error", "message": "test_id is required"}, status=400)

    # Retrieve the IoTStressTestSummary for the given test_id
    try:
        summary = IoTStressTestSummary.objects.get(test_id=test_id)
    except IoTStressTestSummary.DoesNotExist:
        return Response({"status": "error", "message": "No summary found for the given test_id"}, status=404)

    # Retrieve all IoTRequestLog entries for the given test_id
    request_logs = IoTRequestLog.objects.filter(test_id=test_id).order_by('-created_at')

    # Serialize the data
    summary_serializer = IoTStressTestSummarySerializer(summary)
    request_logs_serializer = IoTRequestLogSerializer(request_logs, many=True)

    # Combine the data into a single response
    return Response({
        "status": "success",
        "summary": summary_serializer.data,
        "request_logs": request_logs_serializer.data
    })


# Endpoint for IoT gateway simulation test
@api_view(['POST'])
@authentication_classes([])
def iot_simulation_test(request):
    """
    Run IoT simulation and save results, including request logs.
    """

    platform = request.data.get("platform", "arduino_r4")
    test_id = request.data.get("test_id", f"simulation_test_{timezone.now().isoformat()}")
    devices = int(request.data.get("devices", 50))
    interval = float(request.data.get("interval", 5))
    duration = int(request.data.get("duration", 300))

    result = run_iot_simulation(IOT_TEST_ARDUINO_URL, devices, interval, duration)

    if result is None:
        return Response({
            "status": "error",
            "message": "No results generated"
        }, status=500)

    # Save request_logs individually
    request_logs = result.get("request_logs", [])
    for log in request_logs:
        log_data = {
            "platform": platform,
            "test_id": test_id,
            "latency": float(log.get("latency")) if log.get("latency") is not None else None,
            "error": bool(log.get("error", True)),
            "error_type": log.get("error_type", None),
            "free_ram": int(log.get("free_ram")) if log.get("free_ram") is not None else None,
            "min_free_ram": int(log.get("min_free_ram")) if log.get("min_free_ram") is not None else None,
            "computation_time": int(log.get("computation_time")) if log.get("computation_time") is not None else None,
            "complexity": int(log.get("complexity")) if log.get("complexity") is not None else None,
            "wifi_rssi": int(log.get("wifi_rssi")) if log.get("wifi_rssi") is not None else None,
        }

        serializer = IoTRequestLogSerializer(data=log_data)
        if serializer.is_valid():
            serializer.save()
        else:
            print(serializer.errors)  # Log errors for debugging

    # Save summary results
    result.pop("request_logs", None)  # Remove detailed logs from summary
    summary_data = {
        "platform": platform,
        "test_id": test_id,
        "devices": devices,
        "interval": interval,
        "duration": duration,
        **result
    }

    serializer = IoTSimulationSummarySerializer(data=summary_data)
    if serializer.is_valid():
        measurement = serializer.save()
        return Response({
            "status": "success",
            "id": measurement.id,
            "created_at": measurement.created_at,
            "data": serializer.data
        })

    return Response({
        "status": "error",
        "errors": serializer.errors
    }, status=400)


# Endpoint for displaying IoT gateway simulation-test diagnostics from database
@api_view(['GET'])
def iot_simulation_measurements(request):
    test_id = request.GET.get("filter_test_id")  # Retrieve test_id from the request

    if not test_id:
        return Response({"status": "error", "message": "test_id is required"}, status=400)

    # Retrieve the IoTSimulationSummary for the given test_id
    try:
        summary = IoTSimulationSummary.objects.get(test_id=test_id)
    except IoTSimulationSummary.DoesNotExist:
        return Response({"status": "error", "message": "No summary found for the given test_id"}, status=404)

    # Retrieve all IoTRequestLog entries for the given test_id
    request_logs = IoTRequestLog.objects.filter(test_id=test_id).order_by('-created_at')

    # Serialize the data
    summary_serializer = IoTSimulationSummarySerializer(summary)
    request_logs_serializer = IoTRequestLogSerializer(request_logs, many=True)

    # Combine the data into a single response
    return Response({
        "status": "success",
        "summary": summary_serializer.data,
        "request_logs": request_logs_serializer.data
    })