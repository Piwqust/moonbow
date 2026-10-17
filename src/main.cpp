#include "Trace.h"
#include "app/Gallery.h"
#include "gfx/Gpu.h"
#include "gfx/Text.h"

#include <objbase.h>
#include <shellapi.h>

#include <string>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Trace("wWinMain");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const bool gallery = argv && argc > 1 && std::wstring(argv[1]) == L"--gallery";
    LocalFree(argv);

    int code = 1;
    if (gfx::Gpu::Get().CreateFactories() && gfx::Text::Get().Init(instance)) {
        if (gallery) code = app::RunGallery();
    }
    CoUninitialize();
    return code;
}
