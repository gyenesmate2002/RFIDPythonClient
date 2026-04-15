"""
URL configuration for RFIDClient project.

The `urlpatterns` list routes URLs to views. For more information please see:
    https://docs.djangoproject.com/en/4.2/topics/http/urls/
Examples:
Function views
    1. Add an import:  from my_app import views
    2. Add a URL to urlpatterns:  path('', views.home, name='home')
Class-based views
    1. Add an import:  from other_app.views import Home
    2. Add a URL to urlpatterns:  path('', Home.as_view(), name='home')
Including another URLconf
    1. Import the include() function: from django.urls import include, path
    2. Add a URL to urlpatterns:  path('blog/', include('blog.urls'))
"""
from django.contrib import admin
from django.urls import path, include
from .views import iot_stress_test_dashboard, rfid_scan_dashboard, iot_simulation_test_dashboard

urlpatterns = [
    path('admin/', admin.site.urls),
    path("api/", include('api.urls')),

    path("arduino/", include("ArduinoData.urls")),

    path('IoTSimulationTestDashboard/', iot_simulation_test_dashboard, name='IoTSimulationTestDashboard'),
    path('IoTStressTestDashboard/', iot_stress_test_dashboard, name='IoTStressTestDashboard'),
    path('RFIDScanDashboard/', rfid_scan_dashboard, name='RFIDScanDashboard'),
]

