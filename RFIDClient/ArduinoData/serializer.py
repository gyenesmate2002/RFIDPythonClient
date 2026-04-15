from rest_framework import serializers
from .models import (
    RFIDRequestLog,
    IoTStressTestSummary,
    IoTSimulationSummary,
    IoTRequestLog
)


# -------------------------------
# Serializer for RFIDRequestLog
# -------------------------------
class RFIDRequestLogSerializer(serializers.ModelSerializer):
    class Meta:
        model = RFIDRequestLog
        fields = '__all__'


# -------------------------------
# Serializer for IoTStressTestSummary
# -------------------------------
class IoTStressTestSummarySerializer(serializers.ModelSerializer):
    class Meta:
        model = IoTStressTestSummary
        fields = '__all__'


# -------------------------------
# Serializer for IoTSimulationSummary
# -------------------------------
class IoTSimulationSummarySerializer(serializers.ModelSerializer):
    class Meta:
        model = IoTSimulationSummary
        fields = '__all__'


# -------------------------------
# Serializer for IoTRequestLog
# -------------------------------
class IoTRequestLogSerializer(serializers.ModelSerializer):
    class Meta:
        model = IoTRequestLog
        fields = '__all__'