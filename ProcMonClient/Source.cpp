#include <windows.h>
#include <stdio.h>

#include "Common.h"

static void OnExit() {
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    printf("Exiting...\n");
    exit(0);
}

BOOL WINAPI ConsoleHandler(DWORD CEvent) {
    switch (CEvent) {
    case CTRL_C_EVENT:
        OnExit();
        break;
    }
    return TRUE;
}

static int Error(const char* msg) {
	printf("%s (%u)\n", msg, GetLastError());
	return 1;
}

static void PrintColor(const char* msg, WORD color, ...) {
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hConsole, color);
	printf("%s", msg);
    // Reset color:
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

static void DisplayItem(const BYTE* buffer, DWORD size) {
    auto count = size;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    char printBuffer[50]{ 0 };
    while (count > 0) {
        auto item = (const ItemHeader*)buffer;
        switch (item->Type) {
        case ItemType::ProcessCreate: {
            auto info = (const ProcessCreateInfo*)buffer;
            PrintColor("ProcessCreate:\n", FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            printf("\tPID:%u\n\tPPID:%u\n\n", info->ProcessId, info->ParentProcessId);
            // sprintf_s(printBuffer, "\tPID:%u\n\tPPID:%u\n\n", info->ProcessId, info->ParentProcessId);
            // PrintColor(printBuffer, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            break;
        }
        case ItemType::ProcessExit: {
            auto info = (const ProcessExitInfo*)buffer;
            PrintColor("ProcessExit:\n", FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            printf("\tPID:%u\n\tExitStatus:%u\n\n", info->ProcessId, info->ExitStatus);
            // sprintf_s(printBuffer, "\tPID:%u\n\tExitStatus:%u\n\n", info->ProcessId, info->ExitStatus);
            // PrintColor(printBuffer, FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            break;
        }
        default:
            printf("Unknown item type: %u\n", item->Type);
            break;
        }
        buffer += item->Size;
        count -= item->Size;
    }
}

int main() {
    if (SetConsoleCtrlHandler((PHANDLER_ROUTINE)ConsoleHandler, TRUE) == FALSE) {
        printf("ERROR: Could not set control handler");
    }
    auto hfile = CreateFile(
        L"\\\\.\\PrcMon",
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (hfile == INVALID_HANDLE_VALUE) {
        return Error("Failed to open device");
    }

    BYTE buffer[1024]{};

    while (true) {
        DWORD bytes;
        if (!ReadFile(hfile, buffer, sizeof(buffer), &bytes, nullptr)) {
			return Error("Failed to read from device");
		}
        if (bytes != 0) {
			DisplayItem(buffer, bytes);
		}
        Sleep(200);
    }
    return 0;
}
