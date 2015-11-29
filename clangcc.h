#ifndef CLANGCC_H
#define CLANGCC_H

#include <cbplugin.h>
#include <wx/imaglist.h>
#include <wx/timer.h>

#include "clangpluginapi.h"

class ClangCodeCompletion : public ClangPluginComponent {
public:
    ClangCodeCompletion();
    virtual ~ClangCodeCompletion();

    void OnAttach( IClangPlugin* pClangPlugin );
    void OnRelease( IClangPlugin* pClangPlugin );

    cbCodeCompletionPlugin::CCProviderStatus GetProviderStatusFor( cbEditor* ed );
    std::vector<cbCodeCompletionPlugin::CCToken> GetAutocompList(bool isAuto, cbEditor* ed, int& tknStart, int& tknEnd);
    wxString GetDocumentation( const cbCodeCompletionPlugin::CCToken& token );
public: // Code::Blocks events
    void OnEditorActivate(CodeBlocksEvent& event);
    void OnEditorClose(CodeBlocksEvent& event);
    void OnEditorHook(cbEditor* ed, wxScintillaEvent& event);
    void OnTimer(wxTimerEvent& event);

public: // Clang events
    void OnTranslationUnitCreated( ClangEvent& event );
    void OnReparseFinished( ClangEvent& event );
    void OnCodeCompleteFinished( ClangEvent& event );
public:
    ClTranslUnitId GetCurrentTranslationUnitId();
    void RequestReparse();
        /**
     * Semantically highlight all occurrences of the token under the cursor
     * within the editor
     *
     * @param ed The editor to work in
     */
    void HighlightOccurrences(cbEditor* ed);

private:
    ClTranslUnitId m_TranslUnitId;
    int m_EditorHookId;

    wxTimer m_ReparseTimer;
    wxTimer m_DiagnosticTimer;
    wxTimer m_HightlightTimer;

    unsigned int m_CCOutstanding;
    long m_CCOutstandingLastMessageTime;
    int m_CCOutstandingPos;
    std::vector<ClToken> m_CCOutstandingResults;

};


#endif
