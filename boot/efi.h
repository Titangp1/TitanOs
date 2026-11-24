/* Définitions UEFI minimales utilisées par le chargeur de TitanOs.
 * Compilé pour la cible x86_64-unknown-windows : la convention d'appel Microsoft x64
 * est celle d'UEFI, aucun attribut n'est requis. */
#pragma once

typedef unsigned long long UINTN, UINT64, EFI_PHYSICAL_ADDRESS;
typedef unsigned int UINT32;
typedef unsigned short UINT16, CHAR16;
typedef unsigned char UINT8, BOOLEAN;
typedef UINTN EFI_STATUS;
typedef void *EFI_HANDLE;

#define EFI_SUCCESS          0
#define EFI_ERROR_BIT        0x8000000000000000ull
#define EFI_UNSUPPORTED      (EFI_ERROR_BIT | 3)
#define EFI_BUFFER_TOO_SMALL (EFI_ERROR_BIT | 5)
#define EFI_NOT_FOUND        (EFI_ERROR_BIT | 14)
#define EFI_ERROR(s)         (((s) & EFI_ERROR_BIT) != 0)

typedef struct { UINT32 Data1; UINT16 Data2, Data3; UINT8 Data4[8]; } EFI_GUID;
typedef struct { UINT64 Signature; UINT32 Revision, HeaderSize, CRC32, Reserved; } EFI_TABLE_HEADER;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS (*Reset)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, BOOLEAN);
    EFI_STATUS (*OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, CHAR16 *);
    void *TestString, *QueryMode, *SetMode, *SetAttribute;
    EFI_STATUS (*ClearScreen)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *);
    void *SetCursorPosition, *EnableCursor, *Mode;
};

typedef struct { UINT8 Type, SubType, Length[2]; } EFI_DEVICE_PATH;

enum { EfiLoaderData = 2 };

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL, *RestoreTPL;
    void *AllocatePages, *FreePages, *GetMemoryMap;
    EFI_STATUS (*AllocatePool)(UINT32 PoolType, UINTN Size, void **Buffer);
    EFI_STATUS (*FreePool)(void *Buffer);
    void *CreateEvent, *SetTimer, *WaitForEvent, *SignalEvent, *CloseEvent, *CheckEvent;
    void *InstallProtocolInterface, *ReinstallProtocolInterface, *UninstallProtocolInterface;
    EFI_STATUS (*HandleProtocol)(EFI_HANDLE, EFI_GUID *, void **);
    void *Reserved, *RegisterProtocolNotify, *LocateHandle, *LocateDevicePath, *InstallConfigurationTable;
    EFI_STATUS (*LoadImage)(BOOLEAN BootPolicy, EFI_HANDLE Parent, EFI_DEVICE_PATH *Path, void *Src, UINTN Size, EFI_HANDLE *Image);
    EFI_STATUS (*StartImage)(EFI_HANDLE Image, UINTN *ExitDataSize, CHAR16 **ExitData);
    void *Exit, *UnloadImage, *ExitBootServices;
    void *GetNextMonotonicCount;
    EFI_STATUS (*Stall)(UINTN Microseconds);
    EFI_STATUS (*SetWatchdogTimer)(UINTN Timeout, UINT64 WatchdogCode, UINTN DataSize, CHAR16 *Data);
    void *ConnectController, *DisconnectController;
    void *OpenProtocol, *CloseProtocol, *OpenProtocolInformation;
    void *ProtocolsPerHandle, *LocateHandleBuffer;
    EFI_STATUS (*LocateProtocol)(EFI_GUID *Protocol, void *Registration, void **Interface);
    EFI_STATUS (*InstallMultipleProtocolInterfaces)(EFI_HANDLE *Handle, ...);
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct {
    UINT32 Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    EFI_DEVICE_PATH *FilePath;
    void *Reserved;
    UINT32 LoadOptionsSize;
    void *LoadOptions;
    void *ImageBase;
    UINT64 ImageSize;
    UINT32 ImageCodeType, ImageDataType;
    void *Unload;
} EFI_LOADED_IMAGE;

typedef struct EFI_FILE EFI_FILE;
struct EFI_FILE {
    UINT64 Revision;
    EFI_STATUS (*Open)(EFI_FILE *, EFI_FILE **, CHAR16 *, UINT64 Mode, UINT64 Attr);
    EFI_STATUS (*Close)(EFI_FILE *);
    void *Delete;
    EFI_STATUS (*Read)(EFI_FILE *, UINTN *Size, void *Buffer);
    void *Write;
    EFI_STATUS (*GetPosition)(EFI_FILE *, UINT64 *);
    EFI_STATUS (*SetPosition)(EFI_FILE *, UINT64);
};

typedef struct EFI_SIMPLE_FS EFI_SIMPLE_FS;
struct EFI_SIMPLE_FS {
    UINT64 Revision;
    EFI_STATUS (*OpenVolume)(EFI_SIMPLE_FS *, EFI_FILE **);
};

typedef struct EFI_LOAD_FILE2 EFI_LOAD_FILE2;
struct EFI_LOAD_FILE2 {
    EFI_STATUS (*LoadFile)(EFI_LOAD_FILE2 *, EFI_DEVICE_PATH *, BOOLEAN BootPolicy, UINTN *Size, void *Buffer);
};

typedef struct { UINT32 RedMask, GreenMask, BlueMask, ReservedMask; } EFI_PIXEL_BITMASK;
typedef struct {
    UINT32 Version, HorizontalResolution, VerticalResolution, PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    UINT32 PixelsPerScanLine;
} EFI_GOP_MODE_INFO;
typedef struct {
    UINT32 MaxMode, Mode;
    EFI_GOP_MODE_INFO *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GOP_MODE;
typedef struct EFI_GOP EFI_GOP;
struct EFI_GOP { void *QueryMode, *SetMode, *Blt; EFI_GOP_MODE *Mode; };

#define LOADED_IMAGE_GUID { 0x5B1B31A1, 0x9562, 0x11d2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }
#define SIMPLE_FS_GUID    { 0x964E5B22, 0x6459, 0x11d2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }
#define DEVICE_PATH_GUID  { 0x09576E91, 0x6D3F, 0x11d2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }
#define LOAD_FILE2_GUID   { 0x4006C0C1, 0xFCB3, 0x403E, { 0x99, 0x6D, 0x4A, 0x6C, 0x87, 0x24, 0xE0, 0x6D } }
#define GOP_GUID          { 0x9042A9DE, 0x23DC, 0x4A38, { 0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A } }
#define LINUX_INITRD_GUID { 0x5568E427, 0x68FC, 0x4F3D, { 0xAC, 0x74, 0xCA, 0x55, 0x52, 0x31, 0xCC, 0x68 } }
