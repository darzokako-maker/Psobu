#include <Windows.h>
#include <fstream>
#include <string>
#include <vector>
#include <Psapi.h>
#include <shlobj.h>

#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "shell32.lib")

// ============================================
// UE5 Yapıları (Güncel 5.3/5.4)
// ============================================

struct FName {
    uint32_t ComparisonIndex;
    uint32_t Number;
    uint32_t DisplayIndex; // UE5 eklemesi
};

struct FNameEntry {
    uint16_t bIsWide : 1;
    uint16_t LowercaseProbeHash : 5;
    uint16_t Len : 10;
    char AnsiName[256];
};

struct UObject {
    void* VTable;
    int32_t ObjectFlags;
    int32_t InternalIndex;
    UObject* ClassPrivate;
    FName NamePrivate;
    UObject* OuterPrivate;
};

struct FUObjectItem {
    UObject* Object;
    int32_t Flags;
    int32_t ClusterRootIndex;
    int32_t SerialNumber;
    int32_t Reserved;
};

struct TUObjectArray {
    FUObjectItem** Objects;
    FUObjectItem* PreAllocatedObjects;
    int32_t MaxElements;
    int32_t NumElements;
    int32_t MaxChunks;
    int32_t NumChunks;
};

// UE5 FNamePool
struct FNamePool {
    uint8_t Lock[8];
    uint32_t CurrentBlock;
    uint32_t CurrentByteCursor;
    uint8_t* Blocks[8192];
};

// Global
uintptr_t g_Base = 0;
uintptr_t g_Size = 0;
std::ofstream g_Log;
int g_FoundCount = 0;

// Gelişmiş bellek kontrolü
bool IsValidPtr(uintptr_t addr) {
    if (addr < 0x10000 || addr > 0x7FFFFFFFFFFFULL) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) return false;
    return (mbi.State == MEM_COMMIT && 
            mbi.Protect != PAGE_NOACCESS && 
            mbi.Protect != PAGE_GUARD);
}

template<typename T>
T ReadSafe(uintptr_t addr, T def = T()) {
    if (!IsValidPtr(addr)) return def;
    __try { return *(T*)addr; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return def; }
}

uintptr_t ReadPtr(uintptr_t addr) {
    return ReadSafe<uintptr_t>(addr, 0);
}

// Pattern Scanner
uintptr_t PatternScan(const BYTE* pattern, const char* mask, size_t len) {
    for (uintptr_t i = g_Base; i < g_Base + g_Size - len; i++) {
        bool found = true;
        for (size_t j = 0; j < len; j++) {
            if (mask[j] != '?' && *(BYTE*)(i + j) != pattern[j]) {
                found = false;
                break;
            }
        }
        if (found) return i;
    }
    return 0;
}

uintptr_t ResolveRVA(uintptr_t addr, int offset) {
    return addr + ReadSafe<int32_t>(addr + offset) + offset + 4;
}

// UE5 GObjects Bulma
uintptr_t FindGObjectsUE5() {
    g_Log << "[UE5 GObjects Tarama]\n";
    
    // UE5.0-5.4 patternleri
    struct { BYTE* pat; const char* mask; size_t len; const char* name; } patterns[] = {
        { (BYTE*)"\x48\x8B\x05\x00\x00\x00\x00\x48\x8B\x0C\xC8\x48\x8D\x04\xD1", "xxx????xxxxxxx", 15, "UE5.0 Standard" },
        { (BYTE*)"\x48\x8B\x1D\x00\x00\x00\x00\x48\x85\xDB\x74\x00\x48\x8B\x03", "xxx????xxxx?xxx", 15, "UE5.1 GObjects" },
        { (BYTE*)"\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x48\x8B\x45\x00", "xxx????x????xxx?", 16, "UE5.2 Pattern" },
        { (BYTE*)"\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x74\x00\x48\x8B\x01", "xxx????xxxx?xxx", 15, "UE5.3 Pattern" },
        { (BYTE*)"\x48\x8B\x05\x00\x00\x00\x00\x48\x63\x0C\xD0\x48\x8D\x04\xD1", "xxx????xxxxxxx", 15, "UE5.4 Pattern" },
    };
    
    for (auto& p : patterns) {
        uintptr_t addr = PatternScan(p.pat, p.mask, p.len);
        if (addr) {
            uintptr_t resolved = ResolveRVA(addr, 3);
            if (IsValidPtr(resolved)) {
                g_Log << "  [+] " << p.name << ": 0x" << std::hex << (resolved - g_Base) << std::dec << "\n";
                g_FoundCount++;
                return resolved - g_Base;
            }
        }
    }
    
    // Derinlemesine yapısal tarama
    g_Log << "  [*] Derin yapisal tarama...\n";
    
    for (uintptr_t i = g_Base; i < g_Base + g_Size; i += 8) {
        __try {
            uintptr_t objectsPtr = *(uintptr_t*)i;
            int32_t maxElements = *(int32_t*)(i + 8);
            int32_t numElements = *(int32_t*)(i + 12);
            
            if (maxElements > 50000 && maxElements < 1000000 && 
                numElements > 10000 && numElements <= maxElements &&
                objectsPtr > g_Base && IsValidPtr(objectsPtr)) {
                
                // İlk 10 objeyi doğrula
                int validObjs = 0;
                for (int j = 0; j < 10; j++) {
                    uintptr_t itemAddr = objectsPtr + j * sizeof(uintptr_t);
                    uintptr_t item = ReadPtr(itemAddr);
                    if (item && IsValidPtr(item)) {
                        uintptr_t obj = ReadPtr(item);
                        if (obj > g_Base && IsValidPtr(obj)) {
                            uintptr_t vtable = ReadPtr(obj);
                            if (vtable > g_Base && vtable < g_Base + g_Size) {
                                validObjs++;
                            }
                        }
                    }
                }
                
                if (validObjs >= 8) {
                    g_Log << "  [+] Yapisal Bulundu: 0x" << std::hex << (i - g_Base) << std::dec;
                    g_Log << " (Max:" << maxElements << " Num:" << numElements << " Valid:" << validObjs << "/10)\n";
                    g_FoundCount++;
                    return i - g_Base;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
    }
    
    g_Log << "  [-] GObjects bulunamadi!\n";
    return 0;
}

// UE5 GNames Bulma
uintptr_t FindGNamesUE5() {
    g_Log << "[UE5 GNames Tarama]\n";
    
    struct { BYTE* pat; const char* mask; size_t len; const char* name; } patterns[] = {
        { (BYTE*)"\x48\x8D\x35\x00\x00\x00\x00\xEB\x00\x48\x8D\x0D", "xxx????x?xxx", 12, "UE5.0 GNames" },
        { (BYTE*)"\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\xC6\x05", "xxx????x????xx", 14, "UE5.1 GNames" },
        { (BYTE*)"\x48\x8D\x3D\x00\x00\x00\x00\x48\x8D\x35", "xxx????xxx", 10, "UE5.2 GNames" },
        { (BYTE*)"\x4C\x8D\x35\x00\x00\x00\x00\x48\x8D\x0D", "xxx????xxx", 10, "UE5.3 GNames" },
        { (BYTE*)"\x48\x8D\x0D\x00\x00\x00\x00\x48\x8D\x1D", "xxx????xxx", 10, "UE5.4 GNames" },
    };
    
    for (auto& p : patterns) {
        uintptr_t addr = PatternScan(p.pat, p.mask, p.len);
        if (addr) {
            uintptr_t resolved = ResolveRVA(addr, 3);
            if (IsValidPtr(resolved)) {
                // FNamePool doğrulaması
                uintptr_t block0 = ReadPtr(resolved);
                if (block0 > g_Base && IsValidPtr(block0)) {
                    char* firstName = (char*)(block0 + 2);
                    if (IsValidPtr((uintptr_t)firstName) && firstName[0]) {
                        g_Log << "  [+] " << p.name << ": 0x" << std::hex << (resolved - g_Base) << std::dec;
                        g_Log << " (Ilk isim: " << firstName << ")\n";
                        g_FoundCount++;
                        return resolved - g_Base;
                    }
                }
            }
        }
    }
    
    // Manuel FNamePool arama
    g_Log << "  [*] Manuel FNamePool taramasi...\n";
    
    for (uintptr_t i = g_Base; i < g_Base + g_Size - 0x20; i += 8) {
        __try {
            uint32_t currentBlock = *(uint32_t*)(i + 8);
            uint32_t currentCursor = *(uint32_t*)(i + 12);
            
            if (currentBlock < 1000 && currentCursor < 1000000) {
                uintptr_t block0 = *(uintptr_t*)i;
                if (block0 > g_Base && IsValidPtr(block0)) {
                    char* name = (char*)(block0 + 2);
                    if (IsValidPtr((uintptr_t)name) && name[0]) {
                        // "None" veya ilk geçerli ismi kontrol et
                        if (isprint((unsigned char)name[0]) && strlen(name) < 100) {
                            g_Log << "  [+] Manuel FNamePool: 0x" << std::hex << (i - g_Base) << std::dec;
                            g_Log << " (Isim: " << name << ")\n";
                            g_FoundCount++;
                            return i - g_Base;
                        }
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
    }
    
    g_Log << "  [-] GNames bulunamadi!\n";
    return 0;
}

// UE5 UWorld Bulma
uintptr_t FindUWorldUE5() {
    g_Log << "[UE5 UWorld Tarama]\n";
    
    BYTE patterns[][16] = {
        { 0x48, 0x8B, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x48, 0x85, 0xDB, 0x74, 0x00, 0x48, 0x8B, 0x4B },
        { 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x88, 0x00, 0x00, 0x00, 0x00, 0x48 },
        { 0x48, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x00, 0x48, 0x8B, 0x01 },
    };
    
    const char* masks[] = {
        "xxx????xxxx?xxx",
        "xxx????xxx????x",
        "xxx????xxxx?xxx",
    };
    
    for (int i = 0; i < 3; i++) {
        uintptr_t addr = PatternScan(patterns[i], masks[i], 15);
        if (addr) {
            uintptr_t resolved = ResolveRVA(addr, 3);
            g_Log << "  [+] UWorld Pattern " << (i+1) << ": 0x" << std::hex << (resolved - g_Base) << std::dec << "\n";
            g_FoundCount++;
            return resolved - g_Base;
        }
    }
    
    g_Log << "  [-] UWorld bulunamadi!\n";
    return 0;
}

// GNames Dump
void DumpGNames(uintptr_t offset) {
    g_Log << "\n========================================\n";
    g_Log << "[GNAMES DUMP]\n";
    g_Log << "========================================\n\n";
    
    if (!offset) return;
    
    uintptr_t poolAddr = g_Base + offset;
    FNamePool* pool = (FNamePool*)poolAddr;
    
    g_Log << "FNamePool Adresi: 0x" << std::hex << poolAddr << std::dec << "\n";
    g_Log << "CurrentBlock: " << ReadSafe<uint32_t>(poolAddr + 8) << "\n";
    g_Log << "CurrentCursor: " << ReadSafe<uint32_t>(poolAddr + 12) << "\n\n";
    
    int totalNames = 0;
    
    for (int blockIdx = 0; blockIdx < 8192 && totalNames < 100000; blockIdx++) {
        uintptr_t blockPtr = ReadPtr(poolAddr + 16 + blockIdx * 8);
        if (!blockPtr || !IsValidPtr(blockPtr)) continue;
        
        for (int i = 0; i < 65536 && totalNames < 100000; i++) {
            uintptr_t entry = blockPtr + i * 2;
            if (!IsValidPtr(entry)) break;
            
            uint16_t header = ReadSafe<uint16_t>(entry);
            uint16_t len = header >> 6;
            
            if (len > 0 && len < 256) {
                char* name = (char*)(entry + 2);
                if (IsValidPtr((uintptr_t)name) && name[0]) {
                    if (totalNames < 500) {
                        g_Log << "[" << totalNames << "] " << name << "\n";
                    }
                    totalNames++;
                }
            }
        }
    }
    
    g_Log << "\nToplam Isim: " << totalNames << "\n";
}

// GObjects Dump
void DumpGObjects(uintptr_t offset) {
    g_Log << "\n========================================\n";
    g_Log << "[GOBJECTS DUMP]\n";
    g_Log << "========================================\n\n";
    
    if (!offset) return;
    
    uintptr_t arrAddr = g_Base + offset;
    g_Log << "TUObjectArray Adresi: 0x" << std::hex << arrAddr << std::dec << "\n";
    
    int32_t maxElements = ReadSafe<int32_t>(arrAddr + 8);
    int32_t numElements = ReadSafe<int32_t>(arrAddr + 12);
    uintptr_t objectsPtr = ReadPtr(arrAddr);
    
    g_Log << "MaxElements: " << maxElements << "\n";
    g_Log << "NumElements: " << numElements << "\n";
    g_Log << "Objects Ptr: 0x" << std::hex << objectsPtr << std::dec << "\n\n";
    
    if (!objectsPtr || numElements <= 0 || numElements > 1000000) {
        g_Log << "[!] Gecersiz GObjects verisi!\n";
        return;
    }
    
    int totalObjects = 0;
    
    for (int i = 0; i < numElements && totalObjects < 10000; i++) {
        uintptr_t itemAddr = objectsPtr + i * sizeof(uintptr_t);
        uintptr_t item = ReadPtr(itemAddr);
        if (!item || !IsValidPtr(item)) continue;
        
        uintptr_t obj = ReadPtr(item);
        if (!obj || !IsValidPtr(obj)) continue;
        
        FName objName = ReadSafe<FName>(obj + 0x18);
        int32_t objFlags = ReadSafe<int32_t>(obj + 8);
        int32_t internalIndex = ReadSafe<int32_t>(obj + 12);
        uintptr_t classPtr = ReadPtr(obj + 0x10);
        uintptr_t outerPtr = ReadPtr(obj + 0x20);
        
        if (totalObjects < 100) {
            g_Log << "[" << totalObjects << "] ";
            g_Log << "Addr: 0x" << std::hex << obj << std::dec << " | ";
            g_Log << "NameIdx: " << objName.ComparisonIndex << " | ";
            g_Log << "Flags: 0x" << std::hex << objFlags << std::dec << " | ";
            g_Log << "InternalIdx: " << internalIndex;
            
            if (classPtr && IsValidPtr(classPtr)) {
                FName className = ReadSafe<FName>(classPtr + 0x18);
                g_Log << " | ClassIdx: " << className.ComparisonIndex;
            }
            
            if (outerPtr && IsValidPtr(outerPtr)) {
                FName outerName = ReadSafe<FName>(outerPtr + 0x18);
                g_Log << " | OuterIdx: " << outerName.ComparisonIndex;
            }
            
            g_Log << "\n";
        }
        totalObjects++;
    }
    
    g_Log << "\nToplam Nesne: " << totalObjects << " (Ilk 100 gosterildi)\n";
}

// World Dump
void DumpWorld(uintptr_t worldOffset) {
    g_Log << "\n========================================\n";
    g_Log << "[UWORLD DUMP]\n";
    g_Log << "========================================\n\n";
    
    if (!worldOffset) return;
    
    uintptr_t worldPtr = ReadPtr(g_Base + worldOffset);
    if (!worldPtr || !IsValidPtr(worldPtr)) return;
    
    g_Log << "UWorld Adresi: 0x" << std::hex << worldPtr << std::dec << "\n\n";
    
    // UE5 World offsets
    uintptr_t persistentLevel = ReadPtr(worldPtr + 0x30);
    uintptr_t gameState = ReadPtr(worldPtr + 0x158);
    uintptr_t gameInstance = ReadPtr(worldPtr + 0x1D0);
    uintptr_t owningGameInstance = ReadPtr(worldPtr + 0x1C8);
    
    g_Log << "PersistentLevel: 0x" << std::hex << persistentLevel << std::dec << "\n";
    g_Log << "GameState: 0x" << std::hex << gameState << std::dec << "\n";
    g_Log << "GameInstance: 0x" << std::hex << gameInstance << std::dec << "\n";
    g_Log << "OwningGameInstance: 0x" << std::hex << owningGameInstance << std::dec << "\n";
    
    // Actor listesi
    if (persistentLevel && IsValidPtr(persistentLevel)) {
        uintptr_t actors = ReadPtr(persistentLevel + 0x98);
        int32_t actorCount = ReadSafe<int32_t>(persistentLevel + 0xA0);
        
        g_Log << "\n[Actor Listesi]\n";
        g_Log << "Actor Sayisi: " << actorCount << "\n";
        
        if (actors && actorCount > 0 && actorCount < 50000) {
            int shown = 0;
            for (int i = 0; i < actorCount && shown < 50; i++) {
                uintptr_t actor = ReadPtr(actors + i * 8);
                if (actor && IsValidPtr(actor)) {
                    FName actorName = ReadSafe<FName>(actor + 0x18);
                    uintptr_t actorClass = ReadPtr(actor + 0x10);
                    
                    g_Log << "  [" << i << "] 0x" << std::hex << actor << std::dec;
                    g_Log << " | NameIdx: " << actorName.ComparisonIndex;
                    
                    if (actorClass) {
                        FName className = ReadSafe<FName>(actorClass + 0x18);
                        g_Log << " | ClassIdx: " << className.ComparisonIndex;
                    }
                    
                    g_Log << "\n";
                    shown++;
                }
            }
        }
    }
}

// Ana Dump Fonksiyonu
void FullDump() {
    char desktop[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, desktop);
    
    SYSTEMTIME st;
    GetLocalTime(&st);
    char path[512];
    sprintf_s(path, "%s\\UE5_Dump_%02d%02d_%02d%02d.txt", 
              desktop, st.wDay, st.wMonth, st.wHour, st.wMinute);
    
    g_Log.open(path);
    
    g_Log << "========================================\n";
    g_Log << "  UNREAL ENGINE 5 - FULL DUMPER v3.0\n";
    g_Log << "========================================\n\n";
    g_Log << "Base Address: 0x" << std::hex << g_Base << std::dec << "\n";
    g_Log << "Module Size: 0x" << std::hex << g_Size << std::dec << "\n";
    g_Log << "Module Size (MB): " << (g_Size / 1024 / 1024) << "\n\n";
    
    // Tüm offsetleri bul
    g_Log << "========================================\n";
    g_Log << "[OFFSET TARAMA BASLIYOR]\n";
    g_Log << "========================================\n\n";
    
    uintptr_t gobjects = FindGObjectsUE5();
    uintptr_t gnames = FindGNamesUE5();
    uintptr_t uworld = FindUWorldUE5();
    
    g_Log << "\n========================================\n";
    g_Log << "[TARAMA SONUCU]\n";
    g_Log << "========================================\n";
    g_Log << "Bulunan Offset Sayisi: " << g_FoundCount << "/3\n";
    g_Log << "GObjects: 0x" << std::hex << gobjects << std::dec << " " << (gobjects ? "[OK]" : "[FAIL]") << "\n";
    g_Log << "GNames: 0x" << std::hex << gnames << std::dec << " " << (gnames ? "[OK]" : "[FAIL]") << "\n";
    g_Log << "UWorld: 0x" << std::hex << uworld << std::dec << " " << (uworld ? "[OK]" : "[FAIL]") << "\n";
    
    // Dump işlemleri
    DumpWorld(uworld);
    DumpGNames(gnames);
    DumpGObjects(gobjects);
    
    g_Log << "\n========================================\n";
    g_Log << "[DUMP TAMAMLANDI]\n";
    g_Log << "========================================\n";
    
    g_Log.close();
    
    char msg[512];
    sprintf_s(msg, "UE5 Dump Tamamlandi!\n%d/3 offset bulundu.\n\n%s", g_FoundCount, path);
    MessageBoxA(0, msg, "UE5 Dumper v3.0", MB_ICONINFORMATION);
}

// DLL Thread
DWORD WINAPI MainThread(LPVOID param) {
    g_Base = (uintptr_t)GetModuleHandleA(NULL);
    
    MODULEINFO mi;
    GetModuleInformation(GetCurrentProcess(), (HMODULE)g_Base, &mi, sizeof(mi));
    g_Size = mi.SizeOfImage;
    
    AllocConsole();
    freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
    
    printf("===================================\n");
    printf("  UE5 Full Dumper v3.0\n");
    printf("===================================\n");
    printf("Base: 0x%llX\n", g_Base);
    printf("Size: %lld MB\n\n", g_Size / 1024 / 1024);
    printf("[*] Dump basliyor...\n");
    
    FullDump();
    
    printf("\n[*] Tamamlandi! Enter'a basin...\n");
    getchar();
    
    FreeConsole();
    FreeLibraryAndExitThread((HMODULE)param, 0);
    return 0;
}

// DLL Entry
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(0, 0, MainThread, hModule, 0, 0);
    }
    return TRUE;
}
