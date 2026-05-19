from django.contrib import admin
from django.http import HttpResponse
import csv
from datetime import datetime
from django.utils import timezone
from datetime import timedelta

from .models import (
    RFIDRequestLog,
    IoTRequestLog,
    IoTSimulationSummary,
    IoTStressTestSummary,
    User
)


def _get_field_names(model):
    """
    Return list of model field names (used for CSV headers and list_display).
    """
    return [f.name for f in model._meta.fields]


def export_as_csv(modeladmin, request, queryset):
    """
    Admin action to export the given queryset to CSV.
    Returns an HttpResponse with a CSV attachment.
    """
    model = modeladmin.model
    field_names = _get_field_names(model)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = f"{model.__name__}_{timestamp}.csv"

    response = HttpResponse(content_type="text/csv")
    response["Content-Disposition"] = f'attachment; filename="{filename}"'

    writer = csv.writer(response)
    writer.writerow(field_names)

    for obj in queryset:
        row = []
        for field in field_names:
            value = getattr(obj, field)
            # Convert non-string values to str for CSV safety
            row.append(str(value) if value is not None else "")
        writer.writerow(row)

    return response


export_as_csv.short_description = "Export selected as CSV"


def export_all_as_csv(modeladmin, request, queryset):
    """
    Admin action to export ALL objects for this model (ignores selection).
    Useful when you want the whole table.
    """
    all_qs = modeladmin.model.objects.all()
    return export_as_csv(modeladmin, request, all_qs)


export_all_as_csv.short_description = "Export all as CSV"


def delete_all(modeladmin, request, queryset):
    """
    Admin action to delete ALL objects for this model (ignores selection).
    """
    modeladmin.model.objects.all().delete()


delete_all.short_description = "⚠️ Delete ALL records"


def delete_yesterday(modeladmin, request, queryset):
    """
    Deletes all records created yesterday (00:00–23:59).
    """
    now = timezone.now()

    # Start of today
    today_start = now.replace(hour=0, minute=0, second=0, microsecond=0)

    # Start and end of yesterday
    yesterday_start = today_start - timedelta(days=1)
    yesterday_end = today_start

    # Change 'created_at' to your actual datetime field
    modeladmin.model.objects.filter(
        created_at__gte=yesterday_start,
        created_at__lt=yesterday_end
    ).delete()

    modeladmin.message_user(request, "Yesterday's records deleted.")


delete_yesterday.short_description = "Delete yesterday's records"


class UserAdmin(admin.ModelAdmin):
    list_display = _get_field_names(User)
    actions = [export_as_csv, export_all_as_csv, delete_all]


class RFIDRequestLogAdmin(admin.ModelAdmin):
    list_display = _get_field_names(RFIDRequestLog)
    actions = [export_as_csv, export_all_as_csv, delete_all]


class IoTRequestLogAdmin(admin.ModelAdmin):
    list_display = _get_field_names(IoTRequestLog)
    actions = [export_as_csv, export_all_as_csv, delete_all, delete_yesterday]


class IoTSimulationSummaryAdmin(admin.ModelAdmin):
    list_display = _get_field_names(IoTSimulationSummary)
    actions = [export_as_csv, export_all_as_csv, delete_all]


class IoTStressTestSummaryAdmin(admin.ModelAdmin):
    list_display = _get_field_names(IoTStressTestSummary)
    actions = [export_as_csv, export_all_as_csv, delete_all]


# Register your models here.
admin.site.register(User, UserAdmin)
admin.site.register(RFIDRequestLog, RFIDRequestLogAdmin)
admin.site.register(IoTRequestLog, IoTRequestLogAdmin)
admin.site.register(IoTSimulationSummary, IoTSimulationSummaryAdmin)
admin.site.register(IoTStressTestSummary, IoTStressTestSummaryAdmin)