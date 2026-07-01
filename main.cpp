#include <windows.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <psapi.h>
#include <shlobj.h>

// Antivirüs motorlarını yanıltmak ve imzayı bozmak için junk döngüsü
void PerformJunkAnalysis() {
    volatile int junkCalc = 0;
    for (int i = 0; i < 300; i++) {
        junkCalc += (i * 2) / (i + 1);
        if (junkCalc % 5 == 0) junkCalc ^= 0x3F;
    }
}

// Çökmeleri engelleyen en üst düzey Güvenli Okuma Makrosu
#define SAFE_READ(addr, type, default_val) \
    ([](uintptr_t a) -> type { \
        type val = default_val; \
        __try { if (a > 0x10000 && a < 0x7FFFFFFEFFFF) val = *(type*)a; } \
        __except (EXCEPTION_EXECUTE_HANDLER) { } \
        return val; \
    })(addr)

struct FNameEntry {
    uint16_t Info;
    char AnsiName[1024];

    std::string GetName() {
        uint32_t Len = Info >> 1;
        if (Len <= 0 || Len > 1024) return "None";
        return std::string(AnsiName, Len);
    }
};

struct FUObjectItem {
    void* Object;
    int32_t Flags;
    int32_t ClusterRootIndex;
    int32_t Pad;
};

struct TUObjectArray {
    FUObjectItem* Objects;
    int32_t MaxElements;
    int32_t NumElements;
};

uintptr_t BaseAddress = 0;
uintptr_t FNamePoolAddr = 0;
TUObjectArray* GObjectArray = nullptr;

// Dumper-7 kalitesinde byte tarayıcı
uintptr_t FindPattern(const unsigned char* pattern, const char* mask) {
    MODULEINFO modInfo = { 0 };
    GetModuleInformation(GetCurrentProcess(), GetModuleHandleA(NULL), &modInfo, sizeof(modInfo));
    uintptr_t start = (uintptr_t)modInfo.lpBaseOfDll;
    uintptr_t size = (uintptr_t)modInfo.SizeOfImage;
    size_t patternLen = strlen(mask);

    for (uintptr_t i = 0; i < size - patternLen; i++) {
        bool found = true;
        for (size_t j = 0; j < patternLen; j++) {
            if (mask[j] != '?' && pattern[j] != *(unsigned char*)(start + i + j)) {
                found = false;
                break;
            }
        }
        if (found) return start + i;
    }
    return 0;
}

std::string GetObjectName(void* UObject) {
    if (!UObject) return "nullptr";
    uint32_t NameIndex = SAFE_READ((uintptr_t)UObject + 0x18, uint32_t, 0);
    if (NameIndex == 0) return "None";

    uint32_t Block = NameIndex >> 16;
    uint32_t Offset = NameIndex & 0xFFFF;

    uintptr_t BlockPtr = SAFE_READ(FNamePoolAddr + 0x10 + (Block * 8), uintptr_t, 0);
    if (!BlockPtr) return "None";

    FNameEntry* Entry = (FNameEntry*)(BlockPtr + (Offset * 2));
    return Entry->GetName();
}

DWORD WINAPI DumperThread(LPVOID lpParam) {
    PerformJunkAnalysis();
    BaseAddress = (uintptr_t)GetModuleHandleA(NULL);

    // Çıktı klasörünü masaüstü yapıyoruz (UAC/Yönetici İzni Engeli Olmaz)
    char desktopPath[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, desktopPath);
    std::string fullPath = std::string(desktopPath) + "\\ProSoccerSDK.txt";

    std::ofstream dump(fullPath);
    if (!dump.is_open()) {
        MessageBoxA(NULL, "Masaustunde dosya olusturulamadi!", "Hata", MB_OK | MB_ICONERROR);
        return 0;
    }

    dump << "==================================================\n";
    dump << "         NOVA POWERFUL DYNAMIC SDK DUMPER         \n";
    dump << "==================================================\n";
    dump << "Base Address: 0x" << std::hex << BaseAddress << "\n";

    // UE4.27 Evrensel GObjectArray İmzası (Dumper-7 Altyapısı)
    const unsigned char GObjPattern[] = { 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x4B, 0x8D, 0x0C, 0x40, 0x48, 0x8B, 0x01 };
    const char* GObjMask = "xxx????xxxxxxx";

    uintptr_t GObjectSig = FindPattern(GObjPattern, GObjMask);
    
    if (GObjectSig) {
        int32_t Offset = SAFE_READ(GObjectSig + 3, int32_t, 0);
        GObjectArray = (TUObjectArray*)(GObjectSig + 7 + Offset);
        dump << "GObjectArray Durumu: DINAMIK IMZA ILE BULUNDU -> 0x" << std::hex << (uintptr_t)GObjectArray << "\n";
    } else {
        // İmza patlarsa hardcoded yedek devreye girer
        GObjectArray = (TUObjectArray*)(BaseAddress + 0x41E9D50);
        dump << "GObjectArray Durumu: IMZA BULUNAMADI! YEDEK STATIK OFSET DENENIYOR -> 0x" << std::hex << (uintptr_t)GObjectArray << "\n";
    }

    // FNamePool konumunu çöz
    FNamePoolAddr = (uintptr_t)GObjectArray - 0x2C4A0;
    dump << "FNamePool Adresi: 0x" << std::hex << FNamePoolAddr << "\n";

    // Eleman sayısını doğrula
    int32_t ElementsCount = SAFE_READ((uintptr_t)&GObjectArray->NumElements, int32_t, 0);
    dump << "NumElements (Nesne Sayisi): " << std::dec << ElementsCount << "\n";
    dump << "==================================================\n\n";

    if (ElementsCount <= 0 || ElementsCount > 500000) {
        dump << "[KRITIK HATA] Nesne sayisi gecersiz veya bellek korumali!\n";
        dump << "Lütfen bu log dosyasini kontrol edin.\n";
        dump.close();
        MessageBoxA(NULL, "Bellek okuma hatasi! Detaylar masaustundeki .txt icinde.", "Hata", MB_OK | MB_ICONERROR);
        return 0;
    }

    // Listeyi taramaya başla
    uintptr_t ObjectsList = SAFE_READ((uintptr_t)&GObjectArray->Objects, uintptr_t, 0);
    if (ObjectsList) {
        for (int32_t i = 0; i < ElementsCount; ++i) {
            uintptr_t ItemAddr = ObjectsList + (i * sizeof(FUObjectItem));
            void* UObject = SAFE_READ(ItemAddr, void*, nullptr);
            if (!UObject) continue;

            std::string Name = GetObjectName(UObject);
            void* ClassPrivate = SAFE_READ((uintptr_t)UObject + 0x10, void*, nullptr);
            std::string ClassName = GetObjectName(ClassPrivate);

            dump << "[" << i << "] " << Name << " (Class: " << ClassName << ")\n";
        }
    }

    dump.close();
    MessageBoxA(NULL, "SDK Basariyla Söküldü ve Masaustune Kaydedildi!", "Basarili", MB_OK | MB_ICONINFORMATION);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        PerformJunkAnalysis();
        CreateThread(NULL, 0, DumperThread, NULL, 0, NULL);
    }
    return TRUE;
}
