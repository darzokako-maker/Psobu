#include <Windows.h>
#include <fstream>
#include <string>
#include <vector>
#include <Psapi.h>
#include <shlobj.h>

#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "shell32.lib")

// UE4 Yapıları
struct FName {
    uint32_t Index;
    uint32_t Number;
};

struct UObject {
    void* VTable;
    int32_t Flags;
    int32_t InternalIndex;
    UObject* ClassPrivate;
    FName NamePrivate;
    UObject* OuterPrivate;
};

struct FUObjectItem {
    UObject* Object;
    int32_t Flags;
    int32_t ClusterIndex;
    int32_t SerialNumber;
};

struct TUObjectArray {
    FUObjectItem** Objects;
    FUObjectItem* PreAllocatedObjects;
    int32_t MaxElements;
    int32_t NumElements;
    int32_t MaxChunks;
    int32_t NumChunks;
};

uintptr_t g_Base = 0;
uintptr_t g_Size = 0;
std::ofstream g_Log;

bool IsValid(uintptr_t addr) {
    if (addr < 0x10000 || addr > 0x7FFFFFFFFFFF) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) return false;
    return (mbi.State == MEM_COMMIT && mbi.Protect != PAGE_NOACCESS && mbi.Protect != PAGE_GUARD);
}

template<typename T>
T Read(uintptr_t addr, T def = T()) {
    if (!IsValid(addr)) return def;
    __try { return *(T*)addr; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return def; }
}

uintptr_t FindPattern(const BYTE* pat, const char* mask, size_t len) {
    for (uintptr_t i = g_Base; i < g_Base + g_Size - len; i++) {
        for (size_t j = 0; j < len; j++) {
            if (mask[j] != '?' && *(BYTE*)(i + j) != pat[j]) break;
            if (j == len - 1) return i;
        }
    }
    return 0;
}

uintptr_t GetAbsoluteAddress(uintptr_t instruction, int offset, int size) {
    return instruction + Read<int32_t>(instruction + offset) + size;
}

void Log(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsprintf_s(buf, fmt, args);
    va_end(args);
    g_Log << buf;
}

// GObjects'u deneysel olarak bul
uintptr_t FindGObjects() {
    // Method 1: Direkt pattern
    BYTE pat1[] = { 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x0C, 0xC8, 0x48, 0x8D, 0x04, 0xD1 };
    uintptr_t addr = FindPattern(pat1, "xxx????xxxxxxx", 15);
    if (addr) return GetAbsoluteAddress(addr, 3, 7) - g_Base;
    
    // Method 2: Alternatif pattern
    BYTE pat2[] = { 0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x45 };
    addr = FindPattern(pat2, "xxx????x????xxx", 15);
    if (addr) return GetAbsoluteAddress(addr, 3, 7) - g_Base;
    
    // Method 3: Manuel tarama - TUObjectArray yapısını bellekte ara
    for (uintptr_t i = g_Base; i < g_Base + g_Size - 0x30; i += 8) {
        __try {
            int32_t maxElements = *(int32_t*)(i + 0x08);
            int32_t numElements = *(int32_t*)(i + 0x0C);
            
            if (maxElements > 50000 && maxElements < 500000 && 
                numElements > 10000 && numElements <= maxElements) {
                
                uintptr_t objectsPtr = *(uintptr_t*)i;
                if (objectsPtr > g_Base && objectsPtr < g_Base + g_Size * 2) {
                    // Ekstra validasyon
                    uintptr_t firstItem = Read<uintptr_t>(objectsPtr);
                    if (firstItem > 0x10000 && IsValid(firstItem)) {
                        return i - g_Base;
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
    }
    
    return 0;
}

// GNames'i bul
uintptr_t FindGNames() {
    BYTE pat1[] = { 0x48, 0x8D, 0x35, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x00, 0x48, 0x8D, 0x0D };
    uintptr_t addr = FindPattern(pat1, "xxx????x?xxx", 12);
    if (addr) return GetAbsoluteAddress(addr, 3, 7) - g_Base;
    
    BYTE pat2[] = { 0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xC6, 0x05 };
    addr = FindPattern(pat2, "xxx????x????xx", 14);
    if (addr) return GetAbsoluteAddress(addr, 3, 7) - g_Base;
    
    return 0;
}

// UWorld'u bul
uintptr_t FindUWorld() {
    BYTE pat[] = { 0x48, 0x8B, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x48, 0x85, 0xDB, 0x74, 0x00, 0x48, 0x8B, 0x4B };
    uintptr_t addr = FindPattern(pat, "xxx????xxxx?xxx", 15);
    if (addr) return GetAbsoluteAddress(addr, 3, 7) - g_Base;
    return 0;
}

// GEngine bul
uintptr_t FindGEngine() {
    BYTE pat[] = { 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x88, 0x00, 0x00, 0x00, 0x00, 0x48 };
    uintptr_t addr = FindPattern(pat, "xxx????xxx????x", 15);
    if (addr) return GetAbsoluteAddress(addr, 3, 7) - g_Base;
    return 0;
}

void DumpGNames(uintptr_t gnames) {
    if (!gnames) {
        Log("[!] GNames bulunamadi!\n");
        return;
    }
    
    uintptr_t pool = g_Base + gnames;
    Log("[GNames]\nAdres: 0x%llX\n\n", pool);
    
    int count = 0;
    for (int block = 0; block < 200 && count < 100000; block++) {
        uintptr_t blockPtr = Read<uintptr_t>(pool + block * 8);
        if (!blockPtr || !IsValid(blockPtr)) continue;
        
        for (int i = 0; i < 65536 && count < 100000; i++) {
            uintptr_t entry = blockPtr + i * 2;
            if (!IsValid(entry)) break;
            
            uint32_t idx = Read<uint32_t>(entry);
            char* name = (char*)(entry + 4);
            
            if (idx > 0 && idx < 10000000 && IsValid((uintptr_t)name) && name[0]) {
                if (count < 1000) Log("[%d] %s\n", idx, name);
                count++;
            }
        }
    }
    Log("\nToplam Isim: %d\n\n", count);
}

void DumpGObjects(uintptr_t gobjects) {
    if (!gobjects) {
        Log("[!] GObjects bulunamadi!\n");
        return;
    }
    
    uintptr_t arrAddr = g_Base + gobjects;
    Log("[GObjects]\nAdres: 0x%llX\n", arrAddr);
    
    TUObjectArray* arr = (TUObjectArray*)arrAddr;
    int32_t numElements = Read<int32_t>((uintptr_t)&arr->NumElements);
    int32_t maxElements = Read<int32_t>((uintptr_t)&arr->MaxElements);
    uintptr_t objects = Read<uintptr_t>((uintptr_t)&arr->Objects);
    
    Log("NumElements: %d\n", numElements);
    Log("MaxElements: %d\n", maxElements);
    Log("ObjectsPtr: 0x%llX\n\n", objects);
    
    if (!objects || numElements <= 0 || numElements > 500000) {
        Log("[!] Gecersiz GObjects verisi!\n");
        return;
    }
    
    int count = 0;
    for (int i = 0; i < numElements && count < 5000; i++) {
        uintptr_t itemAddr = objects + i * sizeof(uintptr_t);
        FUObjectItem* item = (FUObjectItem*)Read<uintptr_t>(itemAddr);
        
        if (!IsValid((uintptr_t)item)) continue;
        
        UObject* obj = item->Object;
        if (!IsValid((uintptr_t)obj)) continue;
        
        uintptr_t objPtr = (uintptr_t)obj;
        int32_t internalIndex = Read<int32_t>(objPtr + 0x0C);
        FName name = Read<FName>(objPtr + 0x18);
        int32_t flags = Read<int32_t>(objPtr + 0x08);
        UObject* outer = Read<UObject*>(objPtr + 0x20);
        UObject* classObj = Read<UObject*>(objPtr + 0x10);
        
        if (count < 1000) {
            Log("[%d] 0x%llX | NameIdx:%d | Flags:0x%X | InternalIdx:%d", 
                i, objPtr, name.Index, flags, internalIndex);
            
            if (classObj && IsValid((uintptr_t)classObj)) {
                FName className = Read<FName>((uintptr_t)classObj + 0x18);
                Log(" | ClassIdx:%d", className.Index);
            }
            
            if (outer && IsValid((uintptr_t)outer)) {
                FName outerName = Read<FName>((uintptr_t)outer + 0x18);
                Log(" | OuterIdx:%d", outerName.Index);
            }
            
            Log("\n");
        }
        count++;
    }
    
    Log("\nToplam Nesne: %d\n\n", count);
}

void DumpUWorld(uintptr_t uworld) {
    if (!uworld) {
        Log("[!] UWorld bulunamadi!\n\n");
        return;
    }
    
    uintptr_t worldAddr = Read<uintptr_t>(g_Base + uworld);
    Log("[UWorld]\nAdres: 0x%llX\n\n", worldAddr);
    
    if (IsValid(worldAddr)) {
        uintptr_t gameState = Read<uintptr_t>(worldAddr + 0x120);
        uintptr_t gameInstance = Read<uintptr_t>(worldAddr + 0x1A0);
        uintptr_t persistentLevel = Read<uintptr_t>(worldAddr + 0x30);
        
        Log("GameState: 0x%llX\n", gameState);
        Log("GameInstance: 0x%llX\n", gameInstance);
        Log("PersistentLevel: 0x%llX\n\n", persistentLevel);
        
        // Oyuncuları bul
        if (IsValid(persistentLevel)) {
            uintptr_t actors = Read<uintptr_t>(persistentLevel + 0x98);
            int32_t actorCount = Read<int32_t>(persistentLevel + 0xA0);
            
            Log("[Oyuncular]\n");
            Log("Actor Sayisi: %d\n", actorCount);
            
            for (int i = 0; i < actorCount && i < 100; i++) {
                uintptr_t actor = Read<uintptr_t>(actors + i * 8);
                if (IsValid(actor)) {
                    FName actorName = Read<FName>(actor + 0x18);
                    Log("  [%d] 0x%llX | NameIdx:%d\n", i, actor, actorName.Index);
                }
            }
        }
    }
    Log("\n");
}

void FullDump() {
    char desktop[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, desktop);
    
    SYSTEMTIME st;
    GetLocalTime(&st);
    char path[512];
    sprintf_s(path, "%s\\PSO_FullDump_%02d%02d_%02d%02d.txt", 
              desktop, st.wDay, st.wMonth, st.wHour, st.wMinute);
    
    g_Log.open(path);
    
    Log("========================================\n");
    Log("  Pro Soccer Online - Full Dumper v2.0\n");
    Log("========================================\n\n");
    Log("Base: 0x%llX | Size: 0x%llX\n\n", g_Base, g_Size);
    
    // Offsetleri bul
    Log("[Offset Tarama]\n");
    Log("-----------------\n");
    
    uintptr_t gobjects = FindGObjects();
    uintptr_t gnames = FindGNames();
    uintptr_t uworld = FindUWorld();
    uintptr_t gengine = FindGEngine();
    
    Log("GObjects: 0x%llX %s\n", gobjects, gobjects ? "[BULUNDU]" : "[BULUNAMADI]");
    Log("GNames: 0x%llX %s\n", gnames, gnames ? "[BULUNDU]" : "[BULUNAMADI]");
    Log("UWorld: 0x%llX %s\n", uworld, uworld ? "[BULUNDU]" : "[BULUNAMADI]");
    Log("GEngine: 0x%llX %s\n\n", gengine, gengine ? "[BULUNDU]" : "[BULUNAMADI]");
    
    // Dump'ları yap
    DumpUWorld(uworld);
    DumpGNames(gnames);
    DumpGObjects(gobjects);
    
    g_Log.close();
    MessageBoxA(0, path, "Dump Tamamlandi!", MB_OK);
}

DWORD WINAPI MainThread(LPVOID param) {
    g_Base = (uintptr_t)GetModuleHandleA(NULL);
    MODULEINFO mi;
    GetModuleInformation(GetCurrentProcess(), (HMODULE)g_Base, &mi, sizeof(mi));
    g_Size = mi.SizeOfImage;
    
    AllocConsole();
    freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
    printf("PSO Full Dumper v2.0\n");
    
    FullDump();
    
    printf("\nTamamlandi! Enter'a basin...\n");
    getchar();
    FreeConsole();
    FreeLibraryAndExitThread((HMODULE)param, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(0, 0, MainThread, hModule, 0, 0);
    }
    return TRUE;
}
