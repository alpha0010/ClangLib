#ifndef CLANGDIAGNOSTICS_H
#define CLANGDIAGNOSTICS_H

#include <cbplugin.h>
#include <wx/timer.h>

#include "clangpluginapi.h"

class ClangDiagnostics : public ClangPluginComponent {
public:
    ClangDiagnostics();
    virtual ~ClangDiagnostics();

    void OnAttach( IClangPlugin* pClangPlugin );
    void OnRelease( IClangPlugin* pClangPlugin );

public: // Code::Blocks events
    void OnEditorActivate(CodeBlocksEvent& event);
    void OnEditorClose(CodeBlocksEvent& event);
    void OnTimer(wxTimerEvent& event);

public: // Clang events
    void OnDiagnostics( ClangEvent& event );

public:
    ClTranslUnitId GetCurrentTranslationUnitId();

private:
    ClTranslUnitId m_TranslUnitId;
    int m_EditorHookId;

    wxTimer m_DiagnosticTimer;
};


#endif

