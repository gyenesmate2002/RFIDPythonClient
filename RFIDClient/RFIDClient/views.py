from django.shortcuts import render


def iot_stress_test_dashboard(request):
    return render(request, 'IoTStressTestDashboard.html')


def rfid_scan_dashboard(request):
    return render(request, 'RFIDScanDashboard.html')


def iot_simulation_test_dashboard(request):
    return render(request, 'IoTSimulationTestDashboard.html')
