#include <Windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <TlHelp32.h>
#include <Psapi.h>
#include <shlobj.h>

#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "shell32.lib")

// Pro Soccer Online - UE4 Özel Yapıları
struct FNameEntry {
    uint32_t ComparisonIndex;
    uint32_t Number;
    char AnsiName[256];
    wchar_t WideName[256];
};

struct FNamePool {
    uint32_t CurrentBlock;
    uint32_t CurrentByteCursor;
    uint8_t* Blocks[8192];
    uint32_t MaxBlocks;
    uint32_t BlockSize;
};

struct TUObjectArray {
    FUObjectItem** Objects;
    FUObjectItem* PreAllocatedObjects;
    uint32_t MaxElements;
    uint32_t NumElements;
    uint32_t MaxChunks;
    uint32_t NumChunks;
};

struct FUObjectItem {
    UObject* Object;
    int32_t Flags;
    int32_t ClusterIndex;
    int32_t SerialNumber;
    int32_t Reserved;
};

// UObject Temel Yapısı
struct UObject {
    void* VTable;
    int32_t ObjectFlags;
    int32_t InternalIndex;
    UClass* ClassPrivate;
    FName NamePrivate;
    UObject* OuterPrivate;
};

struct UClass : UObject {
    UObject* SuperStruct;
    UObject* Children;
    uint32_t PropertySize;
};

struct FName {
    uint32_t ComparisonIndex;
    uint32_t Number;
};

// Pro Soccer Online Offsetleri (Güncel 2024)
namespace PSOOffsets {
    // Ana offsetler - Pattern scan ile bulunacak
    uintptr_t GObjects = 0x0;
    uintptr_t GNames = 0x0;
    uintptr_t UWorld = 0x0;
    uintptr_t GEngine = 0x0;
    
    // Sabit offsetler
    const uintptr_t NAME_PRIVATE_INDEX = 0x18;
    const uintptr_t CLASS_PRIVATE = 0x10;
    const uintptr_t OUTER_PRIVATE = 0x20;
    const uintptr_t OBJECT_FLAGS = 0x8;
    const uintptr_t SUPER_STRUCT = 0x28;
    const uintptr_t CHILDREN = 0x30;
}

class ProSoccerDumper {
private:
    std::ofstream logFile;
    std::string logPath;
    uintptr_t baseAddress;
    uintptr_t moduleSize;
    std::map<uintptr_t, std::string> objectCache;
    int totalObjects;
    int totalNames;
    bool gObjectsFound;
    bool gNamesFound;

public:
    ProSoccerDumper() {
        baseAddress = (uintptr_t)GetModuleHandleA(NULL);
        
        // Module size al
        MODULEINFO modInfo;
        GetModuleInformation(GetCurrentProcess(), (HMODULE)baseAddress, &modInfo, sizeof(modInfo));
        moduleSize = modInfo.SizeOfImage;
        
        // Masaüstü path
        char desktopPath[MAX_PATH];
        SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, desktopPath);
        
        // Pro Soccer Online özel dosya adı
        SYSTEMTIME st;
        GetLocalTime(&st);
        char filename[256];
        sprintf(filename, "\\PSO_Dump_%04d%02d%02d_%02d%02d%02d.txt",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        
        logPath = std::string(desktopPath) + filename;
        logFile.open(logPath, std::ios::out | std::ios::app);
        
        totalObjects = 0;
        totalNames = 0;
        gObjectsFound = false;
        gNamesFound = false;
        
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

    // Güvenli bellek okuma
    template<typename T>
    T SafeRead(uintptr_t address, T defaultVal = T()) {
        __try {
            if (!IsValidPointer(address)) return defaultVal;
            return *(T*)address;
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

    // Pattern Scanner (Gelişmiş)
    uintptr_t PatternScan(const char* pattern, const char* mask, int offset = 0) {
        for (uintptr_t i = baseAddress; i < baseAddress + moduleSize; i++) {
            bool found = true;
            for (int j = 0; j < strlen(mask); j++) {
                if (mask[j] == '?') continue;
                if (*(uint8_t*)(i + j) != (uint8_t)pattern[j]) {
                    found = false;
                    break;
                }
            }
            if (found) return i + offset;
        }
        return 0;
    }

    // Pro Soccer Online'a özel offset bulma
    void FindPSOOffsets() {
        logFile << "[OFFSET TARAMA]\n";
        logFile << "-----------------------------------------\n";
        
        // Method 1: GObjects için pattern tarama (PSO özel)
        uintptr_t gobjects_patterns[][2] = {
            // Pattern 1: En yaygın UE4 GObjects patterni
            { (uintptr_t)"\x48\x8B\x05\x00\x00\x00\x00\x48\x8B\x0C\xC8\x48\x8D\x04\xD1",
              (uintptr_t)"xxx????xxxxxxx" },
            // Pattern 2: Alternatif
            { (uintptr_t)"\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x48\x8B\x45",
              (uintptr_t)"xxx????x????xxx" },
            // Pattern 3: Pro Soccer Online özel
            { (uintptr_t)"\x48\x8B\x1D\x00\x00\x00\x00\x48\x85\xDB\x74\x00\x48\x8B\x03",
              (uintptr_t)"xxx????xxxx?xxx" }
        };

        for (int i = 0; i < 3; i++) {
            uintptr_t addr = PatternScan((const char*)gobjects_patterns[i][0], 
                                        (const char*)gobjects_patterns[i][1]);
            if (addr) {
                int32_t relOffset = SafeRead<int32_t>(addr + 3);
                PSOOffsets::GObjects = (addr + relOffset + 7) - baseAddress;
                gObjectsFound = true;
                logFile << "[+] GObjects Bulundu (Method " << i+1 << "): 0x" 
                       << std::hex << PSOOffsets::GObjects << std::dec << "\n";
                break;
            }
        }

        // Method 2: Manuel GObjects arama (yapı tabanlı)
        if (!gObjectsFound) {
            logFile << "[*] Pattern basarisiz, manuel arama yapiliyor...\n";
            FindGObjectsManual();
        }

        // GNames için pattern tarama
        uintptr_t gnames_patterns[][2] = {
            { (uintptr_t)"\x48\x8D\x35\x00\x00\x00\x00\xEB\x00\x48\x8D\x0D",
              (uintptr_t)"xxx????x?xxx" },
            { (uintptr_t)"\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\xC6",
              (uintptr_t)"xxx????x????x" }
        };

        for (int i = 0; i < 2; i++) {
            uintptr_t addr = PatternScan((const char*)gnames_patterns[i][0], 
                                        (const char*)gnames_patterns[i][1]);
            if (addr) {
                int32_t relOffset = SafeRead<int32_t>(addr + 3);
                PSOOffsets::GNames = (addr + relOffset + 7) - baseAddress;
                gNamesFound = true;
                logFile << "[+] GNames Bulundu (Method " << i+1 << "): 0x" 
                       << std::hex << PSOOffsets::GNames << std::dec << "\n";
                break;
            }
        }

        // UWorld offseti
        uintptr_t uworld_pattern = PatternScan("\x48\x8B\x1D\x00\x00\x00\x00\x48\x85\xDB\x74\x00\x48\x8B\x4B", 
                                               "xxx????xxxx?xxx");
        if (uworld_pattern) {
            int32_t relOffset = SafeRead<int32_t>(uworld_pattern + 3);
            PSOOffsets::UWorld = (uworld_pattern + relOffset + 7) - baseAddress;
            logFile << "[+] UWorld Bulundu: 0x" << std::hex << PSOOffsets::UWorld << std::dec << "\n";
        }

        logFile << "\n";
    }

    // Manuel GObjects bulma
    void FindGObjectsManual() {
        // TUObjectArray yapısını memory'de ara
        for (uintptr_t i = baseAddress; i < baseAddress + moduleSize - 0x30; i += 8) {
            __try {
                uint32_t maxElements = *(uint32_t*)(i + 0x8);
                uint32_t numElements = *(uint32_t*)(i + 0xC);
                
                // Gerçekçi değerler kontrolü
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
        
        if (!gNamesFound) {
            logFile << "[!] GNames bulunamadi, dump atlaniyor...\n";
            return;
        }

        FNamePool* namePool = (FNamePool*)(baseAddress + PSOOffsets::GNames);
        logFile << "NamePool Adresi: 0x" << std::hex << (uintptr_t)namePool << std::dec << "\n\n";

        int nameCount = 0;
        std::map<uint32_t, std::string> nameMap;

        // Method 1: Block tabanlı okuma
        for (int blockIdx = 0; blockIdx < 8192 && nameCount < 100000; blockIdx++) {
            uint8_t* block = SafeRead<uintptr_t>((uintptr_t)&namePool->Blocks[blockIdx]);
            if (!block) continue;

            for (int i = 0; i < 65536; i++) {
                __try {
                    uint32_t comparisonIndex = *(uint32_t*)(block + (i * 2) + 0);
                    if (comparisonIndex == 0 || comparisonIndex > 10000000) continue;

                    char* name = (char*)(block + (i * 2) + 4);
                    if (name[0] == '\0' || !isprint(name[0])) continue;

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

        // Sıralı dump
        for (auto& pair : nameMap) {
            logFile << "[" << pair.first << "] " << pair.second << "\n";
        }

        totalNames = nameCount;
        logFile << "\nToplam Isim: " << nameCount << "\n";
    }

    // GObjects Dump - Sağlam versiyon
    void DumpGObjects() {
        logFile << "\n[GOBJECTS DUMP]\n";
        logFile << "=========================================\n\n";
        
        if (!gObjectsFound) {
            logFile << "[!] GObjects bulunamadi, alternatif metod deneniyor...\n";
            DumpObjectsAlternative();
            return;
        }

        TUObjectArray* objArray = (TUObjectArray*)(baseAddress + PSOOffsets::GObjects);
        
        if (!IsValidPointer((uintptr_t)objArray)) {
            logFile << "[!] GObjects adresi gecersiz!\n";
            DumpObjectsAlternative();
            return;
        }

        uint32_t numElements = SafeRead<uint32_t>((uintptr_t)&objArray->NumElements);
        uintptr_t objects = SafeRead<uintptr_t>((uintptr_t)&objArray->Objects);
        
        logFile << "NumElements: " << numElements << "\n";
        logFile << "Objects Ptr: 0x" << std::hex << objects << std::dec << "\n\n";

        if (numElements == 0 || numElements > 500000) {
            logFile << "[!] Gecersiz NumElements, alternatif metod deneniyor...\n";
            DumpObjectsAlternative();
            return;
        }

        int objCount = 0;
        for (uint32_t i = 0; i < numElements && objCount < 100000; i++) {
            __try {
                uintptr_t objectPtr = 0;
                
                // Güvenli object pointer okuma
                if (objects) {
                    FUObjectItem* item = (FUObjectItem*)objects + i;
                    if (!IsValidPointer((uintptr_t)item)) continue;
                    objectPtr = SafeRead<uintptr_t>((uintptr_t)&item->Object);
                }

                if (!IsValidPointer(objectPtr) || objectPtr < 0x10000) continue;

                // Object bilgilerini al
                int32_t internalIndex = SafeRead<int32_t>(objectPtr + PSOOffsets::CLASS_PRIVATE - 0x8);
                uint32_t nameIndex = SafeRead<uint32_t>(objectPtr + PSOOffsets::CLASS_PRIVATE - 0x4);
                uintptr_t classPtr = SafeRead<uintptr_t>(objectPtr + PSOOffsets::CLASS_PRIVATE);
                uintptr_t outerPtr = SafeRead<uintptr_t>(objectPtr + PSOOffsets::OUTER_PRIVATE);

                logFile << "[" << objCount << "] ";
                logFile << "Addr: 0x" << std::hex << objectPtr << std::dec << " | ";
                logFile << "NameIdx: " << nameIndex << " | ";
                logFile << "InternalIdx: " << internalIndex << " | ";
                
                if (classPtr) {
                    uint32_t classNameIdx = SafeRead<uint32_t>(classPtr + PSOOffsets::CLASS_PRIVATE - 0x4);
                    logFile << "ClassIdx: " << classNameIdx << " | ";
                }
                
                if (outerPtr) {
                    uint32_t outerNameIdx = SafeRead<uint32_t>(outerPtr + PSOOffsets::CLASS_PRIVATE - 0x4);
                    logFile << "OuterIdx: " << outerNameIdx;
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

    // Alternatif obje dump metodu
    void DumpObjectsAlternative() {
        logFile << "[ALTERNATIF OBJE DUMP]\n";
        logFile << "-----------------------------------------\n";
        
        // Heap taraması ile UObject bulma
        MEMORY_BASIC_INFORMATION mbi;
        uintptr_t address = 0x10000;
        int objCount = 0;
        std::vector<uintptr_t> potentialObjects;

        // İlk geçiş: Potansiyel objeleri topla
        while (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) && objCount < 50000) {
            if (mbi.State == MEM_COMMIT && 
                (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) {
                
                uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
                
                for (uintptr_t scan = (uintptr_t)mbi.BaseAddress; 
                     scan < regionEnd - 0x30; scan += 8) {
                    
                    __try {
                        // VTable kontrolü (UObject göstergesi)
                        uintptr_t vtable = *(uintptr_t*)scan;
                        if (vtable > baseAddress && vtable < baseAddress + moduleSize) {
                            int32_t flags = *(int32_t*)(scan + PSOOffsets::OBJECT_FLAGS);
                            int32_t internalIndex = *(int32_t*)(scan + PSOOffsets::CLASS_PRIVATE - 0x8);
                            
                            // Geçerli obje kontrolü
                            if ((flags & 0x00000003) != 0 && internalIndex >= 0 && internalIndex < 10000000) {
                                potentialObjects.push_back(scan);
                                objCount++;
                                
                                if (objCount >= 10000) goto scanDone;
                            }
                        }
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER) {
                        continue;
                    }
                }
            }
            address = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        }
        
        scanDone:
        logFile << "Potansiyel Obje Sayisi: " << potentialObjects.size() << "\n\n";
        
        // Objeleri dump et
        for (int i = 0; i < min(5000, (int)potentialObjects.size()); i++) {
            uintptr_t objPtr = potentialObjects[i];
            
            __try {
                int32_t internalIndex = SafeRead<int32_t>(objPtr + PSOOffsets::CLASS_PRIVATE - 0x8);
                uint32_t nameIndex = SafeRead<uint32_t>(objPtr + PSOOffsets::CLASS_PRIVATE - 0x4);
                uintptr_t classPtr = SafeRead<uintptr_t>(objPtr + PSOOffsets::CLASS_PRIVATE);
                uintptr_t outerPtr = SafeRead<uintptr_t>(objPtr + PSOOffsets::OUTER_PRIVATE);
                
                logFile << "[" << i << "] 0x" << std::hex << objPtr << std::dec;
                logFile << " | NameIdx:" << nameIndex;
                logFile << " | Flags:" << SafeRead<int32_t>(objPtr + PSOOffsets::OBJECT_FLAGS);
                
                if (classPtr && IsValidPointer(classPtr)) {
                    uint32_t classNameIdx = SafeRead<uint32_t>(classPtr + PSOOffsets::CLASS_PRIVATE - 0x4);
                    logFile << " | ClassIdx:" << classNameIdx;
                }
                
                logFile << "\n";
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }
        }
        
        totalObjects = potentialObjects.size();
    }

    // Önemli sınıfları bul ve dump et
    void DumpImportantClasses() {
        logFile << "\n[ONEMLI SINIFLAR]\n";
        logFile << "=========================================\n\n";
        
        // Önemli sınıf isimleri (Pro Soccer Online)
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
        
        // Burada GObjects içinde bu sınıfları ara
        // Offset'leri bul ve kaydet
    }

    // Full dump başlat
    void FullDump() {
        logFile << "[*] Pro Soccer Online Full Dump Baslatiliyor...\n\n";
        
        FindPSOOffsets();
        
        if (gObjectsFound || true) {  // Her zaman dene
            DumpImportantClasses();
            DumpGNames();
            DumpGObjects();
        }
        
        // Ek bilgiler
        DumpGameInfo();
        
        logFile << "\n[*] Dump Tamamlandi!\n";
    }

    void DumpGameInfo() {
        logFile << "\n[OYUN BILGILERI]\n";
        logFile << "=========================================\n\n";
        
        logFile << "Oyun: Pro Soccer Online\n";
        logFile << "Engine: Unreal Engine 4\n";
        logFile << "Platform: Steam\n\n";
        
        // Process bilgileri
        HANDLE hProcess = GetCurrentProcess();
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
            logFile << "Memory Kullanimi: " << pmc.WorkingSetSize / 1024 / 1024 << " MB\n";
            logFile << "PageFile Kullanimi: " << pmc.PagefileUsage / 1024 / 1024 << " MB\n";
        }
    }
};

// DLL Main Thread
DWORD WINAPI DumperThread(LPVOID lpParam) {
    // Konsol opsiyonel - hata ayıklama için
    #ifdef _DEBUG
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    std::cout << "PSO Dumper Debug Mode\n";
    #endif
    
    // Ana dumper'ı çalıştır
    ProSoccerDumper* dumper = new ProSoccerDumper();
    dumper->FullDump();
    delete dumper;
    
    #ifdef _DEBUG
    std::cout << "\nDump tamamlandi. Cikmak icin Enter'a basin...\n";
    std::cin.get();
    #endif
    
    FreeLibraryAndExitThread((HMODULE)lpParam, 0);
    return 0;
}

// DLL Entry
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        
        // Anti-anti dump için thread
        HANDLE hThread = CreateThread(NULL, 0, DumperThread, hModule, 0, NULL);
        if (hThread) CloseHandle(hThread);
    }
    return TRUE;
}