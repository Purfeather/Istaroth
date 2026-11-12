#include <windows.h>

__declspec(dllexport) volatile float GSTTestTimeScale = 1.0f;
__declspec(dllexport) volatile double GSTTestClocks[4] = {0};

int wmain(int argc, wchar_t **argv)
{
    LARGE_INTEGER frequency, started, previous, now;
    if (argc > 2 || !QueryPerformanceFrequency(&frequency) ||
        !QueryPerformanceCounter(&started))
        return 1;
    if (argc == 2 && !LoadLibraryW(argv[1]))
        return 2;
    previous = started;
    for (;;) {
        double elapsed, total;
        float scale;
        Sleep(5);
        if (!QueryPerformanceCounter(&now))
            return 3;
        elapsed = (double)(now.QuadPart - previous.QuadPart) / frequency.QuadPart;
        total = (double)(now.QuadPart - started.QuadPart) / frequency.QuadPart;
        scale = GSTTestTimeScale;
        GSTTestClocks[0] += elapsed * scale;
        GSTTestClocks[1] += elapsed;
        GSTTestClocks[2] += elapsed * scale;
        GSTTestClocks[3] += elapsed;
        previous = now;
        if (total >= 40.0)
            break;
    }
    /* The component owns its loader reference and releases it on stop. */
    return 0;
}
