from django.urls import path
from .views import get_users, create_user, user_detail, login_request, rfid_scan, login_status, get_requests

urlpatterns = [
    path('users/get', get_users, name='get_user'),
    path('users/create', create_user, name='create_user'),
    path('users/get/<int:pk>', user_detail, name='user_detail'),
    path('login_requests/get', get_requests, name="get_requests"),

    path('login/request/', login_request, name="login_request"),
    path('rfid/scan/', rfid_scan, name="rfid_scan"),
    path('login/status/', login_status, name='login_status'),
]
