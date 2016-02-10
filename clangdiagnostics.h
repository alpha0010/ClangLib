#ifndef CLANGDIAGNOSTICS_H
#define CLANGDIAGNOSTICS_H

#include <cbplugin.h>
#include <wx/timer.h>

#include "clangpluginapi.h"

class ClangDiagnostics : public ClangPluginComponent
{
public:
    ClangDiagnostics();
    virtual ~ClangDiagnostics();

    static const wxString SettingName;

    void OnAttach( IClangPlugin* pClangPlugin );
    void OnRelease( IClangPlugin* pClangPlugin );
    void BuildMenu( wxMenuBar* menuBar );

public: // Command handlers
    void OnGotoNextDiagnostic( wxCommandEvent& WXUNUSED(event) );
    void OnGotoPrevDiagnostic( wxCommandEvent& WXUNUSED(event) );

public: // Code::Blocks events
    void OnEditorActivate(CodeBlocksEvent& event);
    void OnEditorClose(CodeBlocksEvent& event);

public: // Clang events
    void OnDiagnostics( ClangEvent& event );

public:
    ClTranslUnitId GetCurrentTranslationUnitId();

private:
    ClTranslUnitId m_TranslUnitId;
    int m_EditorHookId;
    std::vector<ClDiagnostic> m_Diagnostics;

    wxTimer m_DiagnosticTimer;
};


#endif

