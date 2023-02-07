#include "hook.hpp"
#include "imports.hpp"

typedef NTSTATUS(*FNtCreateFile)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
FNtCreateFile g_NtCreateFile = 0;
NTSTATUS MyNtCreateFile(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength)
{
	// NtCreateFile �ĵ��÷������� IRQL = PASSIVE_LEVEL�� �����������ں� APC �����������
	if (KeGetCurrentIrql() != PASSIVE_LEVEL) return g_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
	if (ExGetPreviousMode() == KernelMode) return g_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
	if (PsGetProcessSessionId(IoGetCurrentProcess()) == 0) return g_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);

	if (ObjectAttributes &&
		ObjectAttributes->ObjectName &&
		ObjectAttributes->ObjectName->Buffer)
	{
		wchar_t* name = (wchar_t*)ExAllocatePool(NonPagedPool, ObjectAttributes->ObjectName->Length + sizeof(wchar_t));
		if (name)
		{
			RtlZeroMemory(name, ObjectAttributes->ObjectName->Length + sizeof(wchar_t));
			RtlCopyMemory(name, ObjectAttributes->ObjectName->Buffer, ObjectAttributes->ObjectName->Length);

			if (wcsstr(name, L"My\\Certificates") && !wcsstr(name, L".ini"))
			{
				// DbgPrintEx(0, 0, "Call %ws \n", name);

				ExFreePool(name);
				return STATUS_ACCESS_DENIED;
			}

			ExFreePool(name);
		}
	}

	return g_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
}

void __fastcall ssdt_call_back(unsigned long ssdt_index, void** ssdt_address)
{
	// https://hfiref0x.github.io/
	UNREFERENCED_PARAMETER(ssdt_index);

	if (*ssdt_address == g_NtCreateFile) *ssdt_address = MyNtCreateFile;
}

VOID DriverUnload(PDRIVER_OBJECT driver)
{
	UNREFERENCED_PARAMETER(driver);

	k_hook::stop();

	// ������Ҫע��,ȷ��ϵͳ��ִ�е��Ѿ����ٵ�ǰ����������
	// ���統ǰ����ж�ص���,������ҹ���MyNtCreateFile����ִ��for����,��Ȼ������
	// ���������10���ֶο���ֱ�ӸĽ�
	LARGE_INTEGER integer{ 0 };
	integer.QuadPart = -10000;
	integer.QuadPart *= 10000;
	KeDelayExecutionThread(KernelMode, FALSE, &integer);
}

EXTERN_C
NTSTATUS
DriverEntry(
	PDRIVER_OBJECT driver,
	PUNICODE_STRING registe)
{
	UNREFERENCED_PARAMETER(registe);

	driver->DriverUnload = DriverUnload;

	UNICODE_STRING str;
	WCHAR name[256]{ L"NtCreateFile" };
	RtlInitUnicodeString(&str, name);
	g_NtCreateFile = (FNtCreateFile)MmGetSystemRoutineAddress(&str);

	// ��ʼ�����ҹ�
	return k_hook::initialize(ssdt_call_back) && k_hook::start() ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}