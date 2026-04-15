from django.db import models


# -------------------------------
# Model for RFIDRequestLog
# -------------------------------
class RFIDRequestLog(models.Model):
    platform = models.CharField(
        max_length=20,
        choices=[
            ('arduino_r4', 'Arduino Uno R4'),
            ('esp32', 'ESP32')
        ]
    )

    test_id = models.CharField(max_length=50)

    rfid_reaction_time = models.FloatField(null=True, blank=True)
    http_latency = models.FloatField(null=True, blank=True)
    led_response_time = models.FloatField(null=True, blank=True)
    total_time = models.FloatField(null=True, blank=True)

    up_time = models.FloatField(null=True, blank=True)

    wifi_rssi = models.IntegerField(null=True, blank=True)

    status = models.CharField(
        max_length=10,
        choices=[('accepted', 'Accepted'), ('denied', 'Denied'), ('error', 'Error')],
        default='error'
    )

    free_ram = models.IntegerField(null=True, blank=True)
    min_free_ram = models.IntegerField(null=True, blank=True)

    wifi_reconnects = models.IntegerField(null=True, blank=True)

    error = models.BooleanField(default=False)
    error_type = models.CharField(
        max_length=50,
        null=True,
        blank=True
    )

    created_at = models.DateTimeField(auto_now_add=True)


# -------------------------------
# Model for IoTStressTestMeasurement
# -------------------------------
class IoTStressTestSummary(models.Model):
    platform = models.CharField(
        max_length=20,
        choices=[
            ('arduino_r4', 'Arduino Uno R4'),
            ('esp32', 'ESP32')
        ]
    )

    test_id = models.CharField(max_length=50, unique=True)

    client_count = models.IntegerField()
    req_per_client = models.IntegerField()

    total_requests = models.IntegerField()

    avg_latency = models.FloatField(null=True, blank=True)
    min_latency = models.FloatField(null=True, blank=True)
    max_latency = models.FloatField(null=True, blank=True)

    p50_latency = models.FloatField(null=True, blank=True)
    p95_latency = models.FloatField(null=True, blank=True)
    p99_latency = models.FloatField(null=True, blank=True)

    requests_per_sec = models.FloatField(null=True, blank=True)

    error_count = models.IntegerField(null=True, blank=True)

    created_at = models.DateTimeField(auto_now_add=True)


# -------------------------------
# Model for IoTSimulationMeasurement
# -------------------------------
class IoTSimulationSummary(models.Model):
    platform = models.CharField(
        max_length=20,
        choices=[
            ('arduino_r4', 'Arduino Uno R4'),
            ('esp32', 'ESP32')
        ]
    )

    test_id = models.CharField(max_length=50, unique=True)

    devices = models.IntegerField()

    interval = models.FloatField()   # sec
    duration = models.IntegerField()  # sec

    total_requests = models.IntegerField(null=True, blank=True)

    avg_latency = models.FloatField(null=True, blank=True)
    min_latency = models.FloatField(null=True, blank=True)
    max_latency = models.FloatField(null=True, blank=True)

    p50_latency = models.FloatField(null=True, blank=True)
    p95_latency = models.FloatField(null=True, blank=True)
    p99_latency = models.FloatField(null=True, blank=True)

    requests_per_sec = models.FloatField(null=True, blank=True)

    error_count = models.IntegerField(null=True, blank=True)

    created_at = models.DateTimeField(auto_now_add=True)


# -------------------------------
# Model for IoTRequestLog
# -------------------------------
class IoTRequestLog(models.Model):
    platform = models.CharField(
        max_length=20,
        choices=[
            ('arduino_r4', 'Arduino Uno R4'),
            ('esp32', 'ESP32')
        ]
    )

    test_id = models.CharField(max_length=50)

    latency = models.FloatField(null=True, blank=True)

    error = models.BooleanField(default=False)

    error_type = models.CharField(
        max_length=50,
        null=True,
        blank=True
    )

    free_ram = models.IntegerField(null=True, blank=True)
    min_free_ram = models.IntegerField(null=True, blank=True)

    computation_time = models.FloatField(null=True, blank=True)

    complexity = models.IntegerField(null=True, blank=True)

    wifi_rssi = models.IntegerField(null=True, blank=True)

    created_at = models.DateTimeField(auto_now_add=True)
