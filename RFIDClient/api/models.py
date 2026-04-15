from django.db import models


class User(models.Model):
    name = models.CharField(max_length=100, unique=True)
    rfid_uid = models.CharField(max_length=32, unique=True)


class LoginRequest(models.Model):
    username = models.CharField(max_length=100)
    status = models.CharField(max_length=20, default="waiting")
    created_at = models.DateTimeField(auto_now_add=True)
