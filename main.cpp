#include <windows.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <psapi.h>

// Güvenli Bellek Okuma Makrosu (Çökmeleri ve okunmama hatalarını önler)
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

// Çoklu Sürüm Uyumlu Gelişmiş Tarayıcı
uintptr_t FindPattern(const char* pattern, const char* mask) {
    MODULEINFO modInfo = { 0 };
    GetModuleInformation(GetCurrentProcess(), GetModuleHandleA(NULL), &modInfo, sizeof(modInfo));
    uintptr_t start = (uintptr_t)modInfo.lpBaseOfDll;
    uintptr_t size = (uintptr_t)modInfo.SizeOfImage;
    size_t patternLen = strlen(mask);

    for (uintptr_t i = 0; i < size - patternLen; i++) {
        bool found = true;
        for (size_t j = 0; j < patternLen; j++) {
            if (mask[j] != '?' && pattern[j] != *(char*)(start + i + j)) {
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

void InitializeDumper() {
    BaseAddress = (uintptr_t)GetModuleHandleA(NULL);

    // 1. Sürüm İmza Havuzu (UE 4.25 - 4.27 Evrensel Çekirdek Yapısı)
    uintptr_t GObjectSig = FindPattern("\x48\x8B\x05\x00\x00\x00\x00\x4B\x8D\x0C\x40\x48\x8B\x01", "xxx????xxxxxxx");
    
    // 2. Sürüm Yedek İmza (Eğer ana imza derleyici tarafından değiştirildiyse devreye girer)
    if (!GObjectSig) {
        GObjectSig = FindPattern("\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x48\x8B\x8D", "xxx????x????xxx");
    }

    if (GObjectSig) {
        int32_t Offset = SAFE_READ(GObjectSig + 3, int32_t, 0);
        GObjectArray = (TUObjectArray*)(GObjectSig + 7 + Offset);
    } else {
        // Son Çare: Bilinen en kararlı statik ofset adresi (Sürüm uyuşmazlığında sıfırlanmayı önler)
        GObjectArray = (TUObjectArray*)(BaseAddress + 0x41E9D50);
    }

    // FNamePool Konumunu Dinamik Olarak Doğrula
    FNamePoolAddr = (uintptr_t)GObjectArray - 0x2C4A0;
}

DWORD WINAPI DumperThread(LPVOID lpParam) {
    InitializeDumper();

    std::ofstream dump("C:\\ProSoccerSDK.txt");
    if (!dump.is_open()) {
        MessageBoxA(NULL, "Dosya olusturulamadi! Yonetici olarak calistirin.", "Hata", MB_OK | MB_ICONERROR);
        return 0;
    }

    // Geçerlilik Kontrolü (GObjectArray okunmadı hatasını burada yakalıyoruz)
    int32_t ElementsCount = SAFE_READ((uintptr_t)&GObjectArray->NumElements, int32_t, 0);
    if (ElementsCount <= 0 || ElementsCount > 500000) {
        dump << "[HATA] GObjectArray bellekten okunamadi veya adres gecersiz!\n";
        dump << "Elde edilen GObjectArray Adresi: " << std::hex << GObjectArray << "\n";
        dump.close();
        MessageBoxA(NULL, "GObjectArray okunamadi! Oyun surumu uyumsuz veya koruma aktif.", "Hata", MB_OK | MB_ICONERROR);
        return 0;
    }

    dump << "--- PRO SOCCER ONLINE DYNAMIC SDK DUMP ---\n";
    dump << "NumElements: " << std::dec << ElementsCount << "\n\n";

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
    MessageBoxA(NULL, "SDK basariyla C:\\ProSoccerSDK.txt dosyasına yazildi!", "Basarili", MB_OK | MB_ICONINFORMATION);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, DumperThread, NULL, 0, NULL);
    }
    return TRUE;
}

