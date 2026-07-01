#include <Windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <TlHelp32.h>
#include <Psapi.h>
#include <shlobj.h>
#include <cctype>

#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "advapi32.lib")

// Build bilgileri (GitHub Actions tarafından tanımlanır)
#ifndef BUILD_VERSION
#define BUILD_VERSION "1.0.0"
#endif

#ifndef BUILD_DATE
#define BUILD_DATE __DATE__ " " __TIME__
#endif

#ifndef COMMIT_HASH
#define COMMIT_HASH "unknown"
#endif

// ============================================
// Pro Soccer Online - UE4 Yapı Tanımlamaları
// ============================================

// Forward declarations
struct UObject;
struct UClass;
struct FName;

// FName yapısı
struct FName {
    uint32_t ComparisonIndex;
    uint32_t Number;
    
    FName() : ComparisonIndex(0), Number(0) {}
};

// FNameEntry yapısı
struct FNameEntry {
    uint32_t ComparisonIndex;
    uint32_t Number;
    char AnsiName[256];
    wchar_t WideName[256];
};

// FNamePool yapısı
struct FNamePool {
    uint32_t CurrentBlock;
    uint32_t CurrentByteCursor;
    uint8_t* Blocks[8192];
    uint32_t MaxBlocks;
    uint32_t BlockSize;
};

// FUObjectItem yapısı
struct FUObjectItem {
    UObject* Object;
    int32_t Flags;
    int32_t ClusterIndex;
    int32_t SerialNumber;
    int32_t Reserved;
};

// TUObjectArray yapısı
struct TUObjectArray {
    FUObjectItem** Objects;
    FUObjectItem* PreAllocatedObjects;
    uint32_t MaxElements;
    uint32_t NumElements;
    uint32_t MaxChunks;
    uint32_t NumChunks;
};

// UObject temel yapısı
struct UObject {
    void* VTable;                    // 0x00
    int32_t ObjectFlags;             // 0x08
    int32_t InternalIndex;           // 0x0C
    UClass* ClassPrivate;            // 0x10
    FName NamePrivate;               // 0x18
    UObject* OuterPrivate;           // 0x20
};

// UClass yapısı
struct UClass : public UObject {
    UObject* SuperStruct;            // 0x28
    UObject* Children;               // 0x30
    uint32_t PropertySize;           // 0x38
};

// ============================================
// Offset Yapılandırması
// ============================================
namespace PSOOffsets {
    // Ana offsetler - Pattern scan ile bulunacak
    uintptr_t GObjects = 0x0;
    uintptr_t GNames = 0x0;
    uintptr_t UWorld = 0x0;
    uintptr_t GEngine = 0x0;
    
    // Sabit UE4 offsetleri
    const uintptr_t NAME_PRIVATE_OFFSET = 0x18;    // UObject::NamePrivate
    const uintptr_t CLASS_PRIVATE_OFFSET = 0x10;   // UObject::ClassPrivate
    const uintptr_t OUTER_PRIVATE_OFFSET = 0x20;   // UObject::OuterPrivate
    const uintptr_t OBJECT_FLAGS_OFFSET = 0x08;    // UObject::ObjectFlags
    const uintptr_t INTERNAL_INDEX_OFFSET = 0x0C;  // UObject::InternalIndex
    const uintptr_t SUPER_STRUCT_OFFSET = 0x28;    // UClass::SuperStruct
    const uintptr_t CHILDREN_OFFSET = 0x30;        // UClass::Children
}

// ============================================
// Pro Soccer Online Dumper Ana Sınıfı
// ============================================
class ProSoccerDumper {
private:
    std::ofstream logFile;
    std::string logPath;
    uintptr_t baseAddress;
    uintptr_t moduleSize;
    int totalObjects;
    int totalNames;
    bool gObjectsFound;
    bool gNamesFound;

public:
    ProSoccerDumper() : totalObjects(0), totalNames(0), 
                       gObjectsFound(false), gNamesFound(false) {
        baseAddress = (uintptr_t)GetModuleHandleA(NULL);
        
        // Module size al
        MODULEINFO modInfo;
        if (GetModuleInformation(GetCurrentProcess(), (HMODULE)baseAddress, &modInfo, sizeof(modInfo))) {
            moduleSize = modInfo.SizeOfImage;
        } else {
            moduleSize = 0x10000000; // Fallback 256MB
        }
        
        // Masaüstü path oluştur
        char desktopPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, desktopPath))) {
            // Zaman damgası oluştur
            SYSTEMTIME st;
            GetLocalTime(&st);
            char filename[256];
            sprintf_s(filename, sizeof(filename), 
                     "\\PSO_Dump_%04d%02d%02d_%02d%02d%02d.txt",
                     st.wYear, st.wMonth, st.wDay, 
                     st.wHour, st.wMinute, st.wSecond);
            
            logPath = std::string(desktopPath) + filename;
        } else {
            // Fallback: geçici dizin
            logPath = "PSO_Dump.txt";
        }
        
        logFile.open(logPath, std::ios::out | std::ios::app);
        
        if (logFile.is_open()) {
            WriteHeader();
        }
    }

    ~ProSoccerDumper() {
        if (logFile.is_open()) {
            WriteFooter();
            logFile.close();
            
            std::string msg = "Dump Basariyla Tamamlandi!\n";
            msg += "Dosya: " + logPath + "\n";
            msg += "Nesne Sayisi: " + std::to_string(totalObjects) + "\n";
            msg += "Isim Sayisi: " + std::to_string(totalNames);
            MessageBoxA(0, msg.c_str(), "PSO Dumper v2.0", MB_ICONINFORMATION);
        }
    }

    void WriteHeader() {
        logFile << "=============================================\n";
        logFile << "  PRO SOCCER ONLINE - ADVANCED DUMPER v2.0\n";
        logFile << "=============================================\n\n";
        logFile << "Build Version: " << BUILD_VERSION << "\n";
        logFile << "Build Date: " << BUILD_DATE << "\n";
        logFile << "Commit: " << COMMIT_HASH << "\n\n";
        logFile << "Base Address: 0x" << std::hex << baseAddress << std::dec << "\n";
        logFile << "Module Size: 0x" << std::hex << moduleSize << std::dec << "\n\n";
    }

    void WriteFooter() {
        logFile << "\n=============================================\n";
        logFile << "  DUMP ISTATISTIKLERI\n";
        logFile << "=============================================\n";
        logFile << "Toplam Nesne: " << totalObjects << "\n";
        logFile << "Toplam Isim: " << totalNames << "\n";
        logFile << "GObjects: " << (gObjectsFound ? "BULUNDU" : "BULUNAMADI") << "\n";
        logFile << "GNames: " << (gNamesFound ? "BULUNDU" : "BULUNAMADI") << "\n";
    }

    // Güvenli bellek okuma - template
    template<typename T>
    T SafeRead(uintptr_t address, T defaultVal = T()) {
        if (!IsValidPointer(address)) return defaultVal;
        
        __try {
            return *(T*)address;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return defaultVal;
        }
    }

    // uint8_t* özel okuma (C2440 hatası düzeltmesi)
    uint8_t* SafeReadPointer(uintptr_t address, uint8_t* defaultVal = nullptr) {
        if (!IsValidPointer(address)) return defaultVal;
        
        __try {
            return *(uint8_t**)address;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return defaultVal;
        }
    }

    bool IsValidPointer(uintptr_t address) {
        if (address < 0x10000 || address > 0x7FFFFFFFFFFF) return false;
        
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) == 0) return false;
        
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect == PAGE_NOACCESS || mbi.Protect == PAGE_GUARD) return false;
        
        return true;
    }

    // Pattern Scanner
    uintptr_t PatternScan(const unsigned char* pattern, const char* mask, size_t patternLen) {
        for (uintptr_t i = baseAddress; i < baseAddress + moduleSize - patternLen; i++) {
            bool found = true;
            for (size_t j = 0; j < patternLen; j++) {
                if (mask[j] == '?') continue;
                if (*(unsigned char*)(i + j) != pattern[j]) {
                    found = false;
                    break;
                }
            }
            if (found) return i;
        }
        return 0;
    }

    // Pro Soccer Online'a özel offset bulma
    void FindPSOOffsets() {
        logFile << "[OFFSET TARAMA]\n";
        logFile << "-----------------------------------------\n";
        
        // GObjects patternleri
        unsigned char gobjects_pattern1[] = { 
            0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 
            0x48, 0x8B, 0x0C, 0xC8, 0x48, 0x8D, 0x04, 0xD1 
        };
        const char* gobjects_mask1 = "xxx????xxxxxxx";
        
        unsigned char gobjects_pattern2[] = { 
            0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, 
            0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x45 
        };
        const char* gobjects_mask2 = "xxx????x????xxx";
        
        // Patternleri dene
        uintptr_t addr = PatternScan(gobjects_pattern1, gobjects_mask1, sizeof(gobjects_pattern1));
        if (addr) {
            int32_t relOffset = SafeRead<int32_t>(addr + 3);
            PSOOffsets::GObjects = (addr + relOffset + 7) - baseAddress;
            gObjectsFound = true;
            logFile << "[+] GObjects Bulundu (Pattern 1): 0x" 
                   << std::hex << PSOOffsets::GObjects << std::dec << "\n";
        } else {
            addr = PatternScan(gobjects_pattern2, gobjects_mask2, sizeof(gobjects_pattern2));
            if (addr) {
                int32_t relOffset = SafeRead<int32_t>(addr + 3);
                PSOOffsets::GObjects = (addr + relOffset + 7) - baseAddress;
                gObjectsFound = true;
                logFile << "[+] GObjects Bulundu (Pattern 2): 0x" 
                       << std::hex << PSOOffsets::GObjects << std::dec << "\n";
            }
        }
        
        if (!gObjectsFound) {
            logFile << "[*] Pattern basarisiz, manuel arama yapiliyor...\n";
            FindGObjectsManual();
        }
        
        // GNames patterni
        unsigned char gnames_pattern[] = { 
            0x48, 0x8D, 0x35, 0x00, 0x00, 0x00, 0x00, 
            0xEB, 0x00, 0x48, 0x8D, 0x0D 
        };
        const char* gnames_mask = "xxx????x?xxx";
        
        addr = PatternScan(gnames_pattern, gnames_mask, sizeof(gnames_pattern));
        if (addr) {
            int32_t relOffset = SafeRead<int32_t>(addr + 3);
            PSOOffsets::GNames = (addr + relOffset + 7) - baseAddress;
            gNamesFound = true;
            logFile << "[+] GNames Bulundu: 0x" 
                   << std::hex << PSOOffsets::GNames << std::dec << "\n";
        }
        
        logFile << "\n";
    }

    // Manuel GObjects bulma
    void FindGObjectsManual() {
        for (uintptr_t i = baseAddress; i < baseAddress + moduleSize - 0x30; i += 8) {
            __try {
                uint32_t maxElements = *(uint32_t*)(i + 0x08);
                uint32_t numElements = *(uint32_t*)(i + 0x0C);
                
                if (maxElements > 1000 && maxElements < 1000000 && 
                    numElements > 0 && numElements <= maxElements) {
                    
                    uintptr_t objectsPtr = *(uintptr_t*)i;
                    if (objectsPtr > baseAddress && objectsPtr < baseAddress + moduleSize) {
                        PSOOffsets::GObjects = i - baseAddress;
                        gObjectsFound = true;
                        logFile << "[+] GObjects Manuel Bulundu: 0x" 
                               << std::hex << PSOOffsets::GObjects << std::dec << "\n";
                        return;
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }
        }
        logFile << "[-] GObjects bulunamadi!\n";
    }

    // GNames Dump - Hata toleranslı
    void DumpGNames() {
        logFile << "\n[GNAMES DUMP]\n";
        logFile << "=========================================\n\n";
        
        if (!gNamesFound || PSOOffsets::GNames == 0) {
            logFile << "[!] GNames bulunamadi, dump atlaniyor...\n";
            return;
        }

        uintptr_t namePoolAddr = baseAddress + PSOOffsets::GNames;
        logFile << "NamePool Adresi: 0x" << std::hex << namePoolAddr << std::dec << "\n\n";

        FNamePool* namePool = (FNamePool*)namePoolAddr;
        int nameCount = 0;
        std::map<uint32_t, std::string> nameMap;

        for (int blockIdx = 0; blockIdx < 8192 && nameCount < 100000; blockIdx++) {
            uint8_t* block = SafeReadPointer((uintptr_t)&namePool->Blocks[blockIdx]);
            if (!block) continue;

            for (int i = 0; i < 65536; i++) {
                __try {
                    uintptr_t entryAddr = (uintptr_t)block + (i * 2);
                    if (!IsValidPointer(entryAddr)) break;
                    
                    uint32_t comparisonIndex = *(uint32_t*)entryAddr;
                    if (comparisonIndex == 0 || comparisonIndex > 10000000) continue;

                    char* name = (char*)(entryAddr + 4);
                    if (!IsValidPointer((uintptr_t)name)) continue;
                    if (name[0] == '\0' || !isprint((unsigned char)name[0])) continue;

                    std::string nameStr(name);
                    if (nameStr.length() > 0 && nameStr.length() < 256) {
                        nameMap[comparisonIndex] = nameStr;
                        nameCount++;
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    break;
                }
            }
        }

        for (auto& pair : nameMap) {
            logFile << "[" << pair.first << "] " << pair.second << "\n";
        }

        totalNames = nameCount;
        logFile << "\nToplam Isim: " << nameCount << "\n";
    }

    // GObjects Dump
    void DumpGObjects() {
        logFile << "\n[GOBJECTS DUMP]\n";
        logFile << "=========================================\n\n";
        
        if (!gObjectsFound || PSOOffsets::GObjects == 0) {
            logFile << "[!] GObjects bulunamadi, alternatif metod deneniyor...\n";
            DumpObjectsAlternative();
            return;
        }

        uintptr_t objArrayAddr = baseAddress + PSOOffsets::GObjects;
        
        if (!IsValidPointer(objArrayAddr)) {
            logFile << "[!] GObjects adresi gecersiz!\n";
            DumpObjectsAlternative();
            return;
        }

        TUObjectArray* objArray = (TUObjectArray*)objArrayAddr;
        uint32_t numElements = SafeRead<uint32_t>((uintptr_t)&objArray->NumElements);
        uintptr_t objectsPtr = SafeRead<uintptr_t>((uintptr_t)&objArray->Objects);
        
        logFile << "NumElements: " << numElements << "\n";
        logFile << "Objects Ptr: 0x" << std::hex << objectsPtr << std::dec << "\n\n";

        if (numElements == 0 || numElements > 500000) {
            logFile << "[!] Gecersiz NumElements, alternatif metod deneniyor...\n";
            DumpObjectsAlternative();
            return;
        }

        int objCount = 0;
        FUObjectItem** objects = (FUObjectItem**)objectsPtr;
        
        for (uint32_t i = 0; i < numElements && objCount < 100000; i++) {
            __try {
                if (!IsValidPointer((uintptr_t)&objects[i])) continue;
                
                FUObjectItem* item = objects[i];
                if (!IsValidPointer((uintptr_t)item)) continue;
                
                UObject* uobject = item->Object;
                if (!IsValidPointer((uintptr_t)uobject)) continue;
                
                uintptr_t objectPtr = (uintptr_t)uobject;
                
                // Object bilgilerini al
                int32_t internalIndex = SafeRead<int32_t>(objectPtr + PSOOffsets::INTERNAL_INDEX_OFFSET);
                FName namePrivate = SafeRead<FName>(objectPtr + PSOOffsets::NAME_PRIVATE_OFFSET);
                uintptr_t classPtr = SafeRead<uintptr_t>(objectPtr + PSOOffsets::CLASS_PRIVATE_OFFSET);
                uintptr_t outerPtr = SafeRead<uintptr_t>(objectPtr + PSOOffsets::OUTER_PRIVATE_OFFSET);

                logFile << "[" << objCount << "] ";
                logFile << "Addr: 0x" << std::hex << objectPtr << std::dec << " | ";
                logFile << "NameIdx: " << namePrivate.ComparisonIndex << " | ";
                logFile << "InternalIdx: " << internalIndex << " | ";
                
                if (classPtr && IsValidPointer(classPtr)) {
                    FName className = SafeRead<FName>(classPtr + PSOOffsets::NAME_PRIVATE_OFFSET);
                    logFile << "ClassIdx: " << className.ComparisonIndex << " | ";
                }
                
                if (outerPtr && IsValidPointer(outerPtr)) {
                    FName outerName = SafeRead<FName>(outerPtr + PSOOffsets::NAME_PRIVATE_OFFSET);
                    logFile << "OuterIdx: " << outerName.ComparisonIndex;
                }
                
                logFile << "\n";
                objCount++;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }
        }

        totalObjects = objCount;
        logFile << "\nToplam Nesne: " << objCount << "\n";
    }

    // Alternatif obje dump metodu (C2712 hatası düzeltildi)
    void DumpObjectsAlternative() {
        logFile << "[ALTERNATIF OBJE DUMP]\n";
        logFile << "-----------------------------------------\n";
        
        int objCount = 0;
        std::vector<uintptr_t> potentialObjects;
        
        // Heap taraması için ayrı fonksiyon kullan (C2712 düzeltmesi)
        ScanHeapForObjects(potentialObjects, objCount);
        
        logFile << "Potansiyel Obje Sayisi: " << potentialObjects.size() << "\n\n";
        
        // Objeleri dump et
        int dumpLimit = (int)(std::min)((size_t)5000, potentialObjects.size());
        for (int i = 0; i < dumpLimit; i++) {
            DumpSingleObject(potentialObjects[i], i);
        }
        
        totalObjects = potentialObjects.size();
    }

    // Heap taraması - ayrı fonksiyon (C2712 düzeltmesi)
    void ScanHeapForObjects(std::vector<uintptr_t>& objects, int& objCount) {
        MEMORY_BASIC_INFORMATION mbi;
        uintptr_t address = 0x10000;
        
        while (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) && objCount < 50000) {
            if (mbi.State == MEM_COMMIT && 
                (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) {
                
                uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
                
                for (uintptr_t scan = (uintptr_t)mbi.BaseAddress; 
                     scan < regionEnd - 0x30; scan += 8) {
                    
                    if (IsPotentialUObject(scan)) {
                        objects.push_back(scan);
                        objCount++;
                        
                        if (objCount >= 10000) return;
                    }
                }
            }
            address = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        }
    }

    // Potansiyel UObject kontrolü - ayrı fonksiyon
    bool IsPotentialUObject(uintptr_t address) {
        __try {
            uintptr_t vtable = *(uintptr_t*)address;
            if (vtable > baseAddress && vtable < baseAddress + moduleSize) {
                int32_t flags = *(int32_t*)(address + PSOOffsets::OBJECT_FLAGS_OFFSET);
                int32_t internalIndex = *(int32_t*)(address + PSOOffsets::INTERNAL_INDEX_OFFSET);
                
                if ((flags & 0x00000003) != 0 && internalIndex >= 0 && internalIndex < 10000000) {
                    return true;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return false;
    }

    // Tek obje dump - ayrı fonksiyon
    void DumpSingleObject(uintptr_t objPtr, int index) {
        __try {
            int32_t internalIndex = SafeRead<int32_t>(objPtr + PSOOffsets::INTERNAL_INDEX_OFFSET);
            FName namePrivate = SafeRead<FName>(objPtr + PSOOffsets::NAME_PRIVATE_OFFSET);
            uintptr_t classPtr = SafeRead<uintptr_t>(objPtr + PSOOffsets::CLASS_PRIVATE_OFFSET);
            uintptr_t outerPtr = SafeRead<uintptr_t>(objPtr + PSOOffsets::OUTER_PRIVATE_OFFSET);
            int32_t flags = SafeRead<int32_t>(objPtr + PSOOffsets::OBJECT_FLAGS_OFFSET);
            
            logFile << "[" << index << "] 0x" << std::hex << objPtr << std::dec;
            logFile << " | NameIdx:" << namePrivate.ComparisonIndex;
            logFile << " | Flags:" << flags;
            
            if (classPtr && IsValidPointer(classPtr)) {
                FName className = SafeRead<FName>(classPtr + PSOOffsets::NAME_PRIVATE_OFFSET);
                logFile << " | ClassIdx:" << className.ComparisonIndex;
            }
            
            logFile << "\n";
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            // Skip invalid objects
        }
    }

    // Önemli sınıfları dump et
    void DumpImportantClasses() {
        logFile << "\n[ONEMLI SINIFLAR]\n";
        logFile << "=========================================\n\n";
        
        const char* importantClasses[] = {
            "PlayerController", "PlayerState", "GameState", "GameMode",
            "Character", "Pawn", "Actor", "World", "GameInstance",
            "PlayerCameraManager", "HUD", "Level", "Player",
            "Ball", "Goal", "Team", "Match", "Stadium"
        };
        
        logFile << "Aranan Siniflar:\n";
        for (auto className : importantClasses) {
            logFile << "  - " << className << "\n";
        }
        logFile << "\n";
    }

    // Oyun bilgilerini dump et
    void DumpGameInfo() {
        logFile << "\n[OYUN BILGILERI]\n";
        logFile << "=========================================\n\n";
        
        logFile << "Oyun: Pro Soccer Online\n";
        logFile << "Engine: Unreal Engine 4\n";
        logFile << "Platform: Steam\n\n";
        
        HANDLE hProcess = GetCurrentProcess();
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
            logFile << "Memory Kullanimi: " << pmc.WorkingSetSize / 1024 / 1024 << " MB\n";
            logFile << "PageFile Kullanimi: " << pmc.PagefileUsage / 1024 / 1024 << " MB\n";
        }
        
        CloseHandle(hProcess);
    }

    // Ana dump fonksiyonu
    void FullDump() {
        logFile << "[*] Pro Soccer Online Full Dump Baslatiliyor...\n\n";
        
        FindPSOOffsets();
        DumpImportantClasses();
        DumpGNames();
        DumpGObjects();
        DumpGameInfo();
        
        logFile << "\n[*] Dump Tamamlandi!\n";
    }
};

// ============================================
// DLL Ana Thread Fonksiyonu
// ============================================
DWORD WINAPI DumperThread(LPVOID lpParam) {
    // Konsol oluştur (debug için)
    AllocConsole();
    
    FILE* fOut;
    freopen_s(&fOut, "CONOUT$", "w", stdout);
    
    std::cout << "===================================\n";
    std::cout << "  Pro Soccer Online Dumper v2.0\n";
    std::cout << "  Build: " << BUILD_VERSION << "\n";
    std::cout << "===================================\n\n";
    std::cout << "[*] Dump baslatiliyor...\n";
    
    // Ana dumper'ı çalıştır
    ProSoccerDumper* dumper = new ProSoccerDumper();
    dumper->FullDump();
    delete dumper;
    
    std::cout << "\n[*] Dump tamamlandi!\n";
    std::cout << "[*] Dosya masaustune kaydedildi.\n";
    std::cout << "\nCikmak icin bir tusa basin...\n";
    
    // Kullanıcıdan input bekle
    std::cin.get();
    
    // Konsolu kapat
    FreeConsole();
    
    // DLL'i bellekten kaldır
    FreeLibraryAndExitThread((HMODULE)lpParam, 0);
    return 0;
}

// ============================================
// DLL Giriş Noktası
// ============================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        // Thread bildirimlerini kapat
        DisableThreadLibraryCalls(hModule);
        
        // Ana thread'i başlat
        HANDLE hThread = CreateThread(
            NULL,                   // Varsayılan güvenlik
            0,                      // Varsayılan stack boyutu
            DumperThread,           // Thread fonksiyonu
            hModule,                // Parametre
            0,                      // Hemen başlat
            NULL                    // Thread ID
        );
        
        if (hThread) {
            CloseHandle(hThread);
            return TRUE;
        }
        
        return FALSE;
    }
    
    return TRUE;
}