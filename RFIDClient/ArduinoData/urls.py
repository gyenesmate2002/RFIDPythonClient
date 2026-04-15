from django.urls import path
from . import views
urlpatterns = [

    # Test endpoints
    path("rfid_scan/", views.rfid_scan),
    path("iot-stress-test/", views.iot_stress_test),
    path("iot-simulation-test/", views.iot_simulation_test),

    # Get database measurements endpoints
    path('rfid-measurements/', views.rfid_get_measurements),
    path('iot-stress-test-results/', views.iot_stress_test_measurements),
    path("iot-simulation-results/", views.iot_simulation_measurements),


    # Frontend RFID scan display endpoint
    path('rfid-latest/', views.rfid_latest),

    # RFID test measurements save endpoint
    path('rfid-save-measurements/', views.rfid_save_measurements)

]
