from django.contrib import admin
from django.http import HttpResponse
import csv
from datetime import datetime

from .models import (
    RFIDRequestLog,
    IoTRequestLog,
    IoTSimulationSummary,
    IoTStressTestSummary
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


class RFIDRequestLogAdmin(admin.ModelAdmin):
    list_display = _get_field_names(RFIDRequestLog)
    actions = [export_as_csv, export_all_as_csv, delete_all]


class IoTRequestLogAdmin(admin.ModelAdmin):
    list_display = _get_field_names(IoTRequestLog)
    actions = [export_as_csv, export_all_as_csv, delete_all]


class IoTSimulationSummaryAdmin(admin.ModelAdmin):
    list_display = _get_field_names(IoTSimulationSummary)
    actions = [export_as_csv, export_all_as_csv, delete_all]


class IoTStressTestSummaryAdmin(admin.ModelAdmin):
    list_display = _get_field_names(IoTStressTestSummary)
    actions = [export_as_csv, export_all_as_csv, delete_all]


# Register your models here.
admin.site.register(RFIDRequestLog, RFIDRequestLogAdmin)
admin.site.register(IoTRequestLog, IoTRequestLogAdmin)
admin.site.register(IoTSimulationSummary, IoTSimulationSummaryAdmin)
admin.site.register(IoTStressTestSummary, IoTStressTestSummaryAdmin)