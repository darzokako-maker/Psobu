#include <Windows.h>
#include <fstream>
#include <string>
#include <Psapi.h>
#include <shlobj.h>

#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "shell32.lib")

// UE4 Temel Yapılar
struct FName {
    uint32_t Index;
    uint32_t Number;
};

struct UObject {
    void* VTable;
    int32_t Flags;
    int32_t InternalIndex;
    UObject* Class;
    FName Name;
    UObject* Outer;
};

struct FUObjectItem {
    UObject* Object;
    int32_t Flags;
    int32_t ClusterIndex;
    int32_t SerialNumber;
};

struct TUObjectArray {
    FUObjectItem** Items;
    int32_t MaxElements;
    int32_t NumElements;
    int32_t MaxChunks;
    int32_t NumChunks;
};

// Global Değişkenler
uintptr_t g_BaseAddress = 0;
uintptr_t g_ModuleSize = 0;
std::ofstream g_LogFile;

// Yardımcı Fonksiyonlar
bool IsValidPtr(uintptr_t addr) {
    if (addr < 0x10000 || addr > 0x7FFFFFFFFFFF) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) return false;
    return (mbi.State == MEM_COMMIT && mbi.Protect != PAGE_NOACCESS);
}

template<typename T>
T Read(uintptr_t addr, T def = T()) {
    if (!IsValidPtr(addr)) return def;
    __try { return *(T*)addr; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return def; }
}

uintptr_t PatternScan(const BYTE* pattern, const char* mask, size_t len) {
    for (uintptr_t i = g_BaseAddress; i < g_BaseAddress + g_ModuleSize - len; i++) {
        bool found = true;
        for (size_t j = 0; j < len; j++) {
            if (mask[j] == '?' || *(BYTE*)(i + j) == pattern[j]) continue;
            found = false;
            break;
        }
        if (found) return i;
    }
    return 0;
}

// Offset Bulma
void FindOffsets(uintptr_t& gobjects, uintptr_t& gnames) {
    BYTE pat1[] = { 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x0C, 0xC8 };
    BYTE pat2[] = { 0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00 };
    
    uintptr_t addr = PatternScan(pat1, "xxx????xx", 11);
    if (addr) {
        gobjects = (addr + Read<int32_t>(addr + 3) + 7) - g_BaseAddress;
        g_LogFile << "[+] GObjects: 0x" << std::hex << gobjects << std::dec << "\n";
    }
    
    addr = PatternScan(pat2, "xxx????x????", 12);
    if (addr) {
        gnames = (addr + Read<int32_t>(addr + 3) + 7) - g_BaseAddress;
        g_LogFile << "[+] GNames: 0x" << std::hex << gnames << std::dec << "\n";
    }
}

// GNames Dump
void DumpNames(uintptr_t gnames) {
    g_LogFile << "\n[GNAMES]\n==========================\n";
    if (!gnames) { g_LogFile << "Bulunamadi!\n"; return; }
    
    uintptr_t pool = g_BaseAddress + gnames;
    int count = 0;
    
    for (int block = 0; block < 100 && count < 50000; block++) {
        uintptr_t blockPtr = Read<uintptr_t>(pool + block * 8);
        if (!blockPtr) continue;
        
        for (int i = 0; i < 65536 && count < 50000; i++) {
            uintptr_t entry = blockPtr + i * 2;
            if (!IsValidPtr(entry)) break;
            
            uint32_t idx = Read<uint32_t>(entry);
            char* name = (char*)(entry + 4);
            
            if (idx > 0 && idx < 10000000 && IsValidPtr((uintptr_t)name) && name[0]) {
                g_LogFile << "[" << idx << "] " << name << "\n";
                count++;
            }
        }
    }
    g_LogFile << "\nToplam: " << count << "\n";
}

// GObjects Dump
void DumpObjects(uintptr_t gobjects) {
    g_LogFile << "\n[GOBJECTS]\n==========================\n";
    if (!gobjects) { g_LogFile << "Bulunamadi!\n"; return; }
    
    TUObjectArray* arr = (TUObjectArray*)(g_BaseAddress + gobjects);
    int count = Read<int32_t>((uintptr_t)&arr->NumElements);
    uintptr_t items = Read<uintptr_t>((uintptr_t)&arr->Items);
    
    if (count <= 0 || count > 500000) {
        g_LogFile << "Gecersiz sayi: " << count << "\n";
        return;
    }
    
    g_LogFile << "Nesne Sayisi: " << count << "\n\n";
    
    for (int i = 0; i < count && i < 10000; i++) {
        FUObjectItem* item = (FUObjectItem*)(items + i * sizeof(FUObjectItem));
        UObject* obj = Read<UObject*>((uintptr_t)&item->Object);
        if (!obj) continue;
        
        uintptr_t objPtr = (uintptr_t)obj;
        g_LogFile << "[" << i << "] 0x" << std::hex << objPtr << std::dec;
        g_LogFile << " | Name:" << Read<FName>(objPtr + 0x18).Index;
        g_LogFile << " | Flags:" << Read<int32_t>(objPtr + 0x08);
        g_LogFile << "\n";
    }
}

// Ana Dump
void FullDump() {
    char desktop[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, desktop);
    
    SYSTEMTIME st;
    GetLocalTime(&st);
    char path[512];
    sprintf_s(path, "%s\\PSO_Dump_%02d%02d%02d_%02d%02d.txt", 
              desktop, st.wDay, st.wMonth, st.wYear % 100, st.wHour, st.wMinute);
    
    g_LogFile.open(path);
    g_LogFile << "PSO Dumper v1.0\n==================\n\n";
    
    uintptr_t gobjects = 0, gnames = 0;
    FindOffsets(gobjects, gnames);
    DumpNames(gnames);
    DumpObjects(gobjects);
    
    g_LogFile << "\n[Dump Tamamlandi]\n";
    g_LogFile.close();
    
    MessageBoxA(0, "Dump tamamlandi!", "PSO Dumper", MB_OK);
}

// DLL Entry
DWORD WINAPI MainThread(LPVOID param) {
    g_BaseAddress = (uintptr_t)GetModuleHandleA(NULL);
    MODULEINFO mi;
    GetModuleInformation(GetCurrentProcess(), (HMODULE)g_BaseAddress, &mi, sizeof(mi));
    g_ModuleSize = mi.SizeOfImage;
    
    AllocConsole();
    freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
    
    printf("PSO Dumper v1.0\nDump basliyor...\n");
    FullDump();
    printf("Tamamlandi!\nEnter'a basin...\n");
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
