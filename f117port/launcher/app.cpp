// app.cpp -- the F-117A Enhanced launcher: the game folder and the options,
// then f117.
#include <wx/app.h>

#include "launcher.h"
#include "settings.h"

class LauncherApp : public wxApp {
public:
    bool OnInit() override {
        SetAppName("F-117A Enhanced");
        SetVendorName("Krzysztof Kania");
        // Headless: --settings-dir DIR (use DIR instead of the user's settings) --print-command FILE (write the
        // command line Play would run into FILE, then exit; used by the ctest).
        wxString printTo;
        for (int i = 1; i < argc; ++i) {
            if (wxString(argv[i]) == "--settings-dir" && i + 1 < argc) settings::UseFolder(argv[++i]);
            else if (wxString(argv[i]) == "--print-command" && i + 1 < argc) printTo = argv[++i];
        }
        if (printTo.empty() && !wxApp::OnInit()) return false;  // it rejects the two headless options above
        auto* dialog = new LauncherDialog;
        if (!printTo.empty()) {
            dialog->PrintCommand(printTo);  // the ctest reads the file
            dialog->Destroy();
            return false;  // no event loop: the process ends here
        }
        SetTopWindow(dialog);
        dialog->Show();
        return true;
    }
};

wxIMPLEMENT_APP(LauncherApp);
