from django.contrib import admin

from .models import User


def _get_field_names(model):
    """
    Return list of model field names (used for CSV headers and list_display).
    """
    return [f.name for f in model._meta.fields]


class UserAdmin(admin.ModelAdmin):
    list_display = _get_field_names(User)


admin.site.register(User, UserAdmin)