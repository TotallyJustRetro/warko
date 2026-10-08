/* winpick.c - native "Open ROM" dialog on Windows (used when the exe is started without a ROM). */
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
int gb_pick_rom(char *buf, int n) {
    OPENFILENAMEA o; memset(&o, 0, sizeof o); buf[0] = 0;
    o.lStructSize = sizeof o; o.lpstrFilter = "Game Boy ROMs\0*.gb;*.gbc\0All files\0*.*\0";
    o.lpstrFile = buf; o.nMaxFile = n; o.lpstrTitle = "Open Game Boy ROM";
    o.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&o);
}
#else
int gb_pick_rom(char *buf, int n) { (void)buf; (void)n; return 0; }
#endif
