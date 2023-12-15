#include <ntifs.h>
#include <stdio.h>
#include <ntddk.h>
#include <ntstrsafe.h>
#include "Common.h"
#include "ProcMon.h"

Globals g_Globals;

#define DRIVER_PREFIX "[PRCMON] " __FUNCTION__ ": "
#define MY_DEVICE_NAME L"PrcMon"
#define MY_DEVICE L"\\Device\\PrcMon"
#define MY_DRIVER_LINK L"\\??\\PrcMon"

auto g_registredcallback = false;

static void PushItem(_In_ PLIST_ENTRY Entry)
{
	AutoLock<FastMutex> lock(g_Globals.Mutex);
	if (g_Globals.ItemCount > 1000) {
		auto head = RemoveHeadList(&g_Globals.ItemsHead);
		g_Globals.ItemCount--;
		auto item = CONTAINING_RECORD(head, FullItem<ItemHeader>, Entry);
		ExFreePool(item);
	}
	InsertTailList(&g_Globals.ItemsHead, Entry);
	g_Globals.ItemCount++;
}

static void OnProcessNotify(_Inout_ PEPROCESS Process, _In_ HANDLE ProcessId, _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo)
{
	UNREFERENCED_PARAMETER(Process);
	if (CreateInfo) {
		//process created:
		USHORT allocSize = sizeof(FullItem<ProcessCreateInfo>);
		USHORT commandLineSize = CreateInfo->CommandLine ? CreateInfo->CommandLine->Length : 0;
		allocSize += commandLineSize;
		auto info = (FullItem<ProcessCreateInfo>*)ExAllocatePool2(POOL_FLAG_PAGED, allocSize, 'prcm');
		if (info == nullptr) {
			DbgPrint(DRIVER_PREFIX "Failed to allocate memory for process create info\n");
			return;
		}
		auto& item = info->Data;
		KeQuerySystemTime(&item.Time);
		item.Type = ItemType::ProcessCreate;
		item.Size = sizeof(ProcessCreateInfo) + commandLineSize;
		item.ProcessId = HandleToULong(ProcessId);
		item.ParentProcessId = HandleToULong(CreateInfo->ParentProcessId);
		if (commandLineSize > 0) {
			RtlCopyMemory((UCHAR*)&item + sizeof(item), CreateInfo->CommandLine->Buffer, commandLineSize);
			item.CommandLineLength = commandLineSize / sizeof(WCHAR);
			item.CommandLineOffset = sizeof(item);
		}
		else {
			item.CommandLineLength = 0;
			item.CommandLineOffset = 0;
		}
		PushItem(&info->Entry);
	}
	else {
		//process exited:
		auto info = (FullItem<ProcessExitInfo>*)ExAllocatePool2(POOL_FLAG_PAGED, sizeof(FullItem<ProcessExitInfo>), 'prcm');
		if (info == nullptr) {
			DbgPrint(DRIVER_PREFIX "Failed to allocate memory for process exit info\n");
			return;
		}
		auto& item = info->Data;
		KeQuerySystemTime(&item.Time);
		item.Type = ItemType::ProcessExit;
		item.Size = sizeof(ProcessExitInfo);
		item.ProcessId = HandleToULong(ProcessId);
		PushItem(&info->Entry);
	}
}

static NTSTATUS HandleCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	NTSTATUS openStatus = STATUS_SUCCESS;

	Irp->IoStatus.Status = openStatus;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return openStatus;
}

static NTSTATUS HandleRead(PDEVICE_OBJECT, PIRP Irp)
{
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto length = stack->Parameters.Read.Length;
	auto status = STATUS_SUCCESS;
	auto count = 0;
	NT_ASSERT(Irp->MdlAddress == nullptr);

	auto buffer = (UCHAR*)MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
	if (buffer == nullptr) {
		DbgPrint(DRIVER_PREFIX "Failed to lock buffer\n");
		status = STATUS_INSUFFICIENT_RESOURCES;
	}
	else {
		while (true) {
			if (IsListEmpty(&g_Globals.ItemsHead)) {
				break;
			}
			auto entry = RemoveHeadList(&g_Globals.ItemsHead);
			auto info = CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry);
			auto size = info->Data.Size;
			if (length < size) {
				InsertHeadList(&g_Globals.ItemsHead, entry);
				break;
			}
			g_Globals.ItemCount--;
			RtlCopyMemory(buffer, &info->Data, size);
			buffer += size;
			length -= size;
			count += size;
			ExFreePool(info);
		}
	}
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = count;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

static void MyDriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(MY_DRIVER_LINK);
	DbgPrint(DRIVER_PREFIX "unloading driver...\n");
	// unregister process callback
	if (g_registredcallback) {
		PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE); //unregister
	}

	DbgPrint(DRIVER_PREFIX "deleting items...\n");

	// free items
	while (!IsListEmpty(&g_Globals.ItemsHead)) {
		auto head = RemoveHeadList(&g_Globals.ItemsHead);
		ExFreePool(CONTAINING_RECORD(head, FullItem<ItemHeader>, Entry));
	}
	
	DbgPrint(DRIVER_PREFIX "deleting symbolic link...\n");

	// delete symbolic link
	IoDeleteSymbolicLink(&symLink);
	// delete device
	IoDeleteDevice(DriverObject->DeviceObject);
	DbgPrint(DRIVER_PREFIX "driver unloaded!\n");
}


static NTSTATUS _InitializeDriver(_In_ PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING devName = RTL_CONSTANT_STRING(MY_DEVICE);

    PDEVICE_OBJECT DeviceObject = nullptr;
    NTSTATUS status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
    if (!NT_SUCCESS(status)) {
        DbgPrint(DRIVER_PREFIX "Failed to create device (0x%08X)\n", status);
        return status;
    }
	DeviceObject->Flags |= DO_DIRECT_IO;

    UNICODE_STRING symLink = RTL_CONSTANT_STRING(MY_DRIVER_LINK);
    status = IoCreateSymbolicLink(&symLink, &devName);
    if (!NT_SUCCESS(status)) {
        DbgPrint(DRIVER_PREFIX "Failed to create symbolic link (0x%08X)\n", status);
        return status;
    }

    status = PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, FALSE);
    if (!NT_SUCCESS(status)) {
        DbgPrint(DRIVER_PREFIX "failed to register process callback (0x%08X)\n", status);
        return status;
    }
	g_registredcallback = true;

    return STATUS_SUCCESS;
}

extern "C"
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);
	InitializeListHead(&g_Globals.ItemsHead);
	g_Globals.Mutex.Init();

	// check version:
	RTL_OSVERSIONINFOW version = { 0 };
	RtlGetVersion(&version);
	DbgPrint(DRIVER_PREFIX "OS Version: %d.%d.%d\n", version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber);

	if (!RtlIsNtDdiVersionAvailable(NTDDI_WIN7)) {
		DbgPrint(DRIVER_PREFIX "Windows < 7 is not supported!\n");
		return STATUS_NOT_SUPPORTED;
	}

	DriverObject->DriverUnload = MyDriverUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = HandleCreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = HandleCreateClose;
	DriverObject->MajorFunction[IRP_MJ_READ] = HandleRead;

	DbgPrint(DRIVER_PREFIX "driver loaded!\n");

	NTSTATUS status = _InitializeDriver(DriverObject);

	if (NT_SUCCESS(status)) {
		DbgPrint(DRIVER_PREFIX "DriverEntry completed successfully\n");
	}
	else {
		MyDriverUnload(DriverObject);
	}
	return status;
}
