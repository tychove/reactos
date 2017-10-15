/*
 * PROJECT:     ReactOS Storport Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Storport helper functions
 * COPYRIGHT:   Copyright 2017 Eric Kohl (eric.kohl@reactos.org)
 */

/* INCLUDES *******************************************************************/

#include "precomp.h"

#define NDEBUG
#include <debug.h>


/* FUNCTIONS ******************************************************************/

static
NTSTATUS
NTAPI
ForwardIrpAndWaitCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    if (Irp->PendingReturned)
        KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}


NTSTATUS
ForwardIrpAndWait(
    _In_ PDEVICE_OBJECT LowerDevice,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    ASSERT(LowerDevice);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);

    IoSetCompletionRoutine(Irp, ForwardIrpAndWaitCompletion, &Event, TRUE, TRUE, TRUE);

    Status = IoCallDriver(LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        Status = KeWaitForSingleObject(&Event, Suspended, KernelMode, FALSE, NULL);
        if (NT_SUCCESS(Status))
            Status = Irp->IoStatus.Status;
    }

    return Status;
}


NTSTATUS
NTAPI
ForwardIrpAndForget(
    _In_ PDEVICE_OBJECT LowerDevice,
    _In_ PIRP Irp)
{
    ASSERT(LowerDevice);

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(LowerDevice, Irp);
}


INTERFACE_TYPE
GetBusInterface(
    PDEVICE_OBJECT DeviceObject)
{
    GUID Guid;
    ULONG Length;
    NTSTATUS Status;

    Status = IoGetDeviceProperty(DeviceObject,
                                 DevicePropertyBusTypeGuid,
                                 sizeof(Guid),
                                 &Guid,
                                 &Length);
    if (!NT_SUCCESS(Status))
        return InterfaceTypeUndefined;

    if (RtlCompareMemory(&Guid, &GUID_BUS_TYPE_PCMCIA, sizeof(GUID)) == sizeof(GUID))
        return PCMCIABus;
    else if (RtlCompareMemory(&Guid, &GUID_BUS_TYPE_PCI, sizeof(GUID)) == sizeof(GUID))
        return PCIBus;
    else if (RtlCompareMemory(&Guid, &GUID_BUS_TYPE_ISAPNP, sizeof(GUID)) == sizeof(GUID))
        return PNPISABus;

    return InterfaceTypeUndefined;
}


static
ULONG
GetResourceListSize(
    PCM_RESOURCE_LIST ResourceList)
{
    PCM_FULL_RESOURCE_DESCRIPTOR Descriptor;
    INT i;
    ULONG Size;

    DPRINT1("GetResourceListSize(%p)\n", ResourceList);

    Size = sizeof(CM_RESOURCE_LIST);
    if (ResourceList->Count == 0)
    {
        DPRINT1("Size: 0x%lx (%u)\n", Size, Size);
        return Size;
    }

    DPRINT1("ResourceList->Count: %lu\n", ResourceList->Count);

    Descriptor = &ResourceList->List[0];
    for (i = 0; i < ResourceList->Count; i++)
    {
        /* Process resources in CM_FULL_RESOURCE_DESCRIPTOR block number ix. */

        DPRINT1("PartialResourceList->Count: %lu\n", Descriptor->PartialResourceList.Count);

        /* Add the size of the current full descriptor */
        Size += sizeof(CM_FULL_RESOURCE_DESCRIPTOR) + 
                (Descriptor->PartialResourceList.Count - 1) * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);

        /* Advance to next CM_FULL_RESOURCE_DESCRIPTOR block in memory. */
        Descriptor = (PCM_FULL_RESOURCE_DESCRIPTOR)(Descriptor->PartialResourceList.PartialDescriptors + 
                                                    Descriptor->PartialResourceList.Count);
    }

    DPRINT1("Size: 0x%lx (%u)\n", Size, Size);
    return Size;
}


PCM_RESOURCE_LIST
CopyResourceList(
    POOL_TYPE PoolType,
    PCM_RESOURCE_LIST Source)
{
    PCM_RESOURCE_LIST Destination;
    ULONG Size;

    DPRINT1("CopyResourceList(%lu %p)\n",
            PoolType, Source);

    /* Get the size of the resource list */
    Size = GetResourceListSize(Source);

    /* Allocate a new buffer */
    Destination = ExAllocatePoolWithTag(PoolType,
                                        Size,
                                        TAG_RESOURCE_LIST);
    if (Destination == NULL)
        return NULL;

    /* Copy the resource list */
    RtlCopyMemory(Destination,
                  Source,
                  Size);

    return Destination;
}


NTSTATUS
QueryBusInterface(
    PDEVICE_OBJECT DeviceObject,
    PGUID Guid,
    USHORT Size,
    USHORT Version,
    PBUS_INTERFACE_STANDARD Interface,
    PVOID InterfaceSpecificData)
{
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION Stack;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                       DeviceObject,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Stack = IoGetNextIrpStackLocation(Irp);

    Stack->MajorFunction = IRP_MJ_PNP;
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = Guid;
    Stack->Parameters.QueryInterface.Size = Size;
    Stack->Parameters.QueryInterface.Version = Version;
    Stack->Parameters.QueryInterface.Interface = (PINTERFACE)Interface;
    Stack->Parameters.QueryInterface.InterfaceSpecificData = InterfaceSpecificData;

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

        Status=IoStatus.Status;
    }

    return Status;
}

/* EOF */
