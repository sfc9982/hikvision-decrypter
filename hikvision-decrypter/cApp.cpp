#include "cApp.h"
#include "cMain.h"

wxIMPLEMENT_APP(cApp);

bool cApp::OnInit()
{
    if (!wxApp::OnInit()) return false;
    auto* frame = new cMain(localization::ReadPreference());
    SetTopWindow(frame);
    frame->Show();
    return true;
}
