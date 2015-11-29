#ifndef CLANGTOOLBAR_H
#define CLANGTOOLBAR_H

#include "clangpluginapi.h"
#include <sdk.h>
#include <wx/choice.h>

class ClangToolbar : public ClangPluginComponent {
public:
    ClangToolbar();
    virtual ~ClangToolbar();

    void OnAttach( IClangPlugin* pClangPlugin );
    void OnRelease( IClangPlugin* pClangPlugin );

public: // Code::Blocks events
    void OnEditorActivate(CodeBlocksEvent& event);
    void OnEditorClose(CodeBlocksEvent& event);
    void OnEditorHook(cbEditor* ed, wxScintillaEvent& event);
public: // Clang events
    void OnTranslationUnitCreated( ClangEvent& event );
    void OnReparseFinished( ClangEvent& /*event*/);

public: // Command events
    void OnUpdateSelection( wxCommandEvent& evt );
    void OnUpdateContents( wxCommandEvent& evt );
    void OnScope( wxCommandEvent& evt );
    void OnFunction( wxCommandEvent& evt );
public:
    bool BuildToolBar(wxToolBar* toolBar);
        /** enable the two wxChoices */
    void EnableToolbarTools(bool enable = true);

    // Updates the toolbar
    void UpdateToolBar();

    // Updates the functions when the scope has changed
    void UpdateFunctions( const wxString& scopeItem );

    ClTranslUnitId GetCurrentTranslationUnitId();

private:
    ClTranslUnitId m_TranslUnitId;
    int m_EditorHookId;
    int m_CurrentEditorLine;
private:
    /** the CC's toolbar */
    wxToolBar*              m_ToolBar;
    /** function choice control of CC's toolbar, it is the second choice */
    wxChoice*               m_Function;
    /** namespace/scope choice control, it is the first choice control */
    wxChoice*               m_Scope;
};

#endif
