#if 0

#include <wx/wx.h>
#include <wx/filename.h>
#include "zuFile.h"

// ------------------------------------------------------------
// How to compile the test
// ------------------------------------------------------------ 
// Linux or MacOS
// g++ test_tryopenfile.cpp zuFile.cpp -o test_tryopenfile \
//    `wx-config --cxxflags --libs`

//Windows Add to a small console project:
//  test_tryopenfile.cpp
//   zuFile.cpp
//   Link against wxWidgets libraries
// 

//How to use it
//Set the correct path:
//static wxString g_dataDir = "/path/to/climatology_pi/data/";

// Run the utility:
// ./test_tryopenfile

// You will see output like:
// Testing TryOpenFile for base name: wind01
//  SUCCESS: Opened wind01 (read 16 bytes)

// Testing TryOpenFile for base name: cyclone-epa
//  SUCCESS: Opened cyclone-epa (read 16 bytes)
//If anything fails, you’ll know exactly which file is missing or unreadable.


// ------------------------------------------------------------
// CONFIGURATION: set your climatology data directory here
// ------------------------------------------------------------
static wxString g_dataDir = "/path/to/climatology_pi/data/";

// ------------------------------------------------------------
// TryOpenFile implementation (standalone version)
// ------------------------------------------------------------
ZUFILE* TryOpenFile(const wxString& basePath)
{
    // 1. Try raw file
    if (wxFileExists(basePath))
        return zu_open(basePath.mb_str().data(), "rb");

    // 2. Try .gz
    if (wxFileExists(basePath + ".gz"))
        return zu_open((basePath + ".gz").mb_str().data(), "rb");

    // 3. Try .bz2 (legacy)
    if (wxFileExists(basePath + ".bz2"))
        return zu_open((basePath + ".bz2").mb_str().data(), "rb");

    return nullptr;
}

// ------------------------------------------------------------
// Helper: test opening a single file base name
// ------------------------------------------------------------
bool TestTryOpen(const wxString& base)
{
    wxString path = g_dataDir + base;

    wxPrintf("Testing TryOpenFile for base name: %s\n", base);

    ZUFILE* f = TryOpenFile(path);

    if (!f)
    {
        wxPrintf("  FAILED: Could not open %s (checked %s, %s.gz, %s.bz2)\n",
                 base,
                 path,
                 path + ".gz",
                 path + ".bz2");
        return false;
    }

    // Try reading a few bytes to ensure the stream is valid
    unsigned char buf[16];
    size_t n = zu_read(f, buf, sizeof(buf));

    if (n == 0)
    {
        wxPrintf("  FAILED: Opened %s but could not read data\n", base);
        zu_close(f);
        return false;
    }

    wxPrintf("  SUCCESS: Opened %s (read %zu bytes)\n", base, n);
    zu_close(f);
    return true;
}

// ------------------------------------------------------------
// Full test suite for all climatology files
// ------------------------------------------------------------
void RunTryOpenFileTests()
{
    wxPrintf("=== Running TryOpenFile() Test Harness ===\n\n");

    // Wind files
    for (int i = 1; i <= 12; i++)
        TestTryOpen(wxString::Format("wind%02d", i));

    // Current files
    for (int i = 1; i <= 12; i++)
        TestTryOpen(wxString::Format("current%02d", i));

    // Scalar fields
    TestTryOpen("sealevelpressure");
    TestTryOpen("seasurfacetemperature");
    TestTryOpen("airtemperature");
    TestTryOpen("cloud");
    TestTryOpen("precipitation");
    TestTryOpen("relativehumidity");
    TestTryOpen("lightning");
    TestTryOpen("seadepth");

    // Cyclone files (must be decompressed!)
    TestTryOpen("cyclone-epa");
    TestTryOpen("cyclone-wpa");
    TestTryOpen("cyclone-spa");
    TestTryOpen("cyclone-atl");
    TestTryOpen("cyclone-nio");
    TestTryOpen("cyclone-she");

    // El Niño years (must be decompressed!)
    TestTryOpen("elnino_years.txt");

    wxPrintf("\n=== TryOpenFile() Tests Complete ===\n");
}

// ------------------------------------------------------------
// wxWidgets entry point
// ------------------------------------------------------------
class MyApp : public wxApp
{
public:
    virtual bool OnInit()
    {
        RunTryOpenFileTests();
        return false; // exit immediately
    }
};

wxIMPLEMENT_APP(MyApp);

#endif
