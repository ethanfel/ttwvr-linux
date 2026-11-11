// Reproduce TTWVR's cross-process D3D11 fence import without game or VR assets.
// Build as both i686 and x86_64. Run probe32.exe <Windows path to probe64.exe>.
#include <windows.h>
#include <d3d11_4.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cstdint>
#include <io.h>

static void check(HRESULT result, const char *operation) {
    if (FAILED(result)) {
        std::fprintf(stderr, "%s failed: 0x%08lx\n", operation, (unsigned long)result);
        std::exit(2);
    }
}

static void wincheck(BOOL result, const char *operation) {
    if (!result) {
        std::fprintf(stderr, "%s failed: %lu\n", operation, GetLastError());
        std::exit(2);
    }
}

static void await_value(ID3D11Fence *fence, UINT64 value) {
    for (int i = 0; i != 500; ++i) {
        UINT64 current = fence->GetCompletedValue();
        if (current == UINT64_MAX) {
            std::fprintf(stderr, "Fence reports device loss\n");
            std::exit(3);
        }
        if (current >= value) return;
        Sleep(10);
    }
    std::fprintf(stderr, "Timed out waiting for fence value %llu\n", value);
    std::exit(3);
}

int wmain(int argc, wchar_t **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const bool child = argc == 4 && !std::wcscmp(argv[1], L"--child");
    if (!child && argc != 2) {
        std::fprintf(stderr, "Usage: probe32.exe <Windows path to probe64.exe>\n");
        return 2;
    }
    // Proton launches do not always forward console stdout. Keep each process's
    // small result log in the caller-selected working directory as well.
    if (!std::freopen(child ? "fence-probe-child.txt" : "fence-probe-parent.txt",
                      "w", stdout)) return 2;
    if (_dup2(_fileno(stdout), _fileno(stderr))) return 2;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    ID3D11Device *base = nullptr;
    ID3D11DeviceContext *context = nullptr;
    const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                           &requested, 1, D3D11_SDK_VERSION, &base, nullptr,
                           &context), "D3D11CreateDevice");
    ID3D11Device5 *device = nullptr;
    ID3D11DeviceContext4 *context4 = nullptr;
    check(base->QueryInterface(IID_PPV_ARGS(&device)), "ID3D11Device5");
    check(context->QueryInterface(IID_PPV_ARGS(&context4)), "ID3D11DeviceContext4");
    ID3D11Fence *fence = nullptr;

    if (child) {
        HANDLE process = OpenProcess(PROCESS_DUP_HANDLE, FALSE,
                                     std::wcstoul(argv[2], nullptr, 10));
        wincheck(process != nullptr, "OpenProcess(parent)");
        HANDLE remote = (HANDLE)(uintptr_t)std::wcstoull(argv[3], nullptr, 10);
        HANDLE local = nullptr;
        wincheck(DuplicateHandle(process, remote, GetCurrentProcess(), &local,
                                 0, FALSE, DUPLICATE_SAME_ACCESS), "DuplicateHandle");
        std::printf("Child (%zu-bit): importing parent's shared fence\n", sizeof(void*) * 8);
        check(device->OpenSharedFence(local, IID_PPV_ARGS(&fence)), "OpenSharedFence");
        await_value(fence, 7);
        check(context4->Signal(fence, 11), "Child Signal(11)");
        context4->Flush();
        await_value(fence, 11);
        std::puts("Child: received 7, signalled 11");
        CloseHandle(local);
        CloseHandle(process);
    } else {
        check(device->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence)),
              "CreateFence");
        HANDLE shared = nullptr;
        check(fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &shared),
              "CreateSharedHandle");
        check(context4->Signal(fence, 7), "Parent Signal(7)");
        context4->Flush();
        await_value(fence, 7);
        std::printf("Parent (%zu-bit): signalled 7\n", sizeof(void*) * 8);
        wchar_t command[32768];
        int length = std::swprintf(command, 32768, L"\"%ls\" --child %lu %llu",
                                  argv[1], GetCurrentProcessId(),
                                  (unsigned long long)(uintptr_t)shared);
        if (length < 0 || length >= 32768) return 2;
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info = {};
        wincheck(CreateProcessW(argv[1], command, nullptr, nullptr, FALSE, 0,
                                nullptr, nullptr, &startup, &info), "CreateProcess(child)");
        if (WaitForSingleObject(info.hProcess, 20000) != WAIT_OBJECT_0) {
            std::fprintf(stderr, "Probe child timed out; terminating only this test child\n");
            TerminateProcess(info.hProcess, 4);
            return 4;
        }
        DWORD exit_code = 0;
        wincheck(GetExitCodeProcess(info.hProcess, &exit_code), "GetExitCodeProcess");
        std::printf("Child exit: 0x%08lx\n", exit_code);
        if (exit_code) return 1;
        await_value(fence, 11);
        std::puts("PASS: cross-process GPU fence round trip 7 -> 11");
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
        CloseHandle(shared);
    }
    fence->Release();
    context4->Release();
    device->Release();
    context->Release();
    base->Release();
    return 0;
}
