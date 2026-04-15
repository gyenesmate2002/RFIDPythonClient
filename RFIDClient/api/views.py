from time import process_time_ns

from django.contrib.auth import user_logged_in
from rest_framework import status
from rest_framework.decorators import api_view
from rest_framework.response import Response
from .models import User, LoginRequest
from .serializer import UserSerializer, LoginRequestSerializer
from .stress_test import run_stress_test


# RFID
# Debug endpoints ---
@api_view(['GET'])
def get_requests(request):
    requests = LoginRequest.objects.all()
    serializer = LoginRequestSerializer(requests, many=True)
    return Response(serializer.data)


@api_view(['GET'])
def get_users(request):
    users = User.objects.all()
    serializer = UserSerializer(users, many=True)
    return Response(serializer.data)


@api_view(['POST'])
def create_user(request):
    serializer = UserSerializer(data=request.data)
    if serializer.is_valid():
        serializer.save()
        return Response(serializer.data, status=status.HTTP_201_CREATED)
    return Response(serializer.errors, status=status.HTTP_400_BAD_REQUEST)


@api_view(['GET', 'PUT', 'DELETE'])
def user_detail(request, pk):
    try:
        user = User.objects.get(pk=pk)
    except User.DoesNotExist:
        return Response(status=status.HTTP_404_NOT_FOUND)

    if request.method == 'GET':
        serializer = UserSerializer(user)
        return Response(serializer.data)

    elif request.method == 'PUT':
        serializer = UserSerializer(user, data=request.data)
        if serializer.is_valid():
            serializer.save()
            return Response(serializer.data)
        return Response(serializer.errors, status=status.HTTP_400_BAD_REQUEST)

    elif request.method == 'DELETE':
        user.delete()
        return Response(status=status.HTTP_204_NO_CONTENT)
# Debug endpoints ---


@api_view(['POST'])
def login_request(request):
    username = request.data.get("username")

    if username is None:
        return Response({"error": "username required"}, status=400)

    # Check user exists
    try:
        user = User.objects.get(name=username)
    except User.DoesNotExist:
        return Response({"error": "User not found"}, status=404)

    # Remove old login requests for this user
    LoginRequest.objects.filter(username=username).delete()

    # Create new login request
    LoginRequest.objects.create(username=username)

    login_req = LoginRequest.objects.filter(username=username).first()
    print(username, login_req.username)

    return Response({"status": "waiting_for_rfid"})


@api_view(['POST'])
def rfid_scan(request):
    uid = request.data.get("uid")

    if uid is None:
        return Response({"error": "UID required"}, status=400)

    # Check if UID belongs to a known user
    user = User.objects.filter(rfid_uid=uid).first()

    # Check if ANY login request is active
    login_req = LoginRequest.objects.filter(status="waiting").first()  # Or filter by device/session

    if login_req is None:
        return Response({"error": "No active login request"}, status=400)

    # If UID doesn't exist → card is simply unknown
    if user is None:
        login_req.status = "mismatch"
        login_req.save()
        return Response({"status": "mismatch", "error": "Unknown RFID card"}, status=404)

    # Compare card user with login request user
    print(user.name, login_req.username)
    if user.name != login_req.username:
        login_req.status = "mismatch"
        login_req.save()
        return Response({"status": "mismatch", "error": "Wrong card for this login"}, status=403)

    # RFID matches → success
    login_req.status = "success"
    login_req.save()

    return Response({
        "status": "login_success",
        "username": user.name
    })


@api_view(['GET'])
def login_status(request):
    username = request.GET.get('username')
    if not username:
        return Response({"error": "username required"}, status=400)

    try:
        login_req = LoginRequest.objects.filter(username=username).latest('created_at')
    except LoginRequest.DoesNotExist:
        return Response({"status": "no_request"})

    return Response({"status": login_req.status})


# IoT gateway
@api_view(['POST'])
def stress_test(request):

    url = "http://192.168.0.89/benchmark"

    clients = int(request.data.get("clients", 1))
    requests = int(request.data.get("requests", 50))
    print(f"Before stress-test")
    result = run_stress_test(url, clients, requests)
    print(f"After stress-test")

    return Response(result)
