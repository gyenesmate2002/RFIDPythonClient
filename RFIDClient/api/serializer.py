from rest_framework import serializers
from .models import User, LoginRequest


class UserSerializer(serializers.ModelSerializer):
    class Meta:
        model = User
        fields = '__all__'


class LoginRequestSerializer(serializers.ModelSerializer):
    class Meta:
        model = LoginRequest
        fields = '__all__'
