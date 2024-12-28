// This is a shortcut so that all the necessary include files are translated at once for Zig
#pragma once
#define UACPI_SIZED_FREES
#define UACPI_KERNEL_INITIALIZATION
#include <uacpi/acpi.h>
#include <uacpi/context.h>
#include <uacpi/event.h>
#include <uacpi/helpers.h>
#include <uacpi/io.h>
#include <uacpi/kernel_api.h>
#include <uacpi/namespace.h>
#include <uacpi/notify.h>
#include <uacpi/opregion.h>
#include <uacpi/osi.h>
#include <uacpi/resources.h>
#include <uacpi/sleep.h>
#include <uacpi/status.h>
#include <uacpi/tables.h>
#include <uacpi/types.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>