#ifndef CLANGPLUGIN_H
#define CLANGPLUGIN_H

#include <cbplugin.h>
#include <wx/imaglist.h>
#include <wx/timer.h>

#include "clangproxy.h"
#include "tokendatabase.h"

class ClangPlugin : public cbCodeCompletionPlugin
{
    public:
        ClangPlugin();
        virtual ~ClangPlugin();

        // Does this plugin handle code completion for the editor ed?
        virtual CCProviderStatus GetProviderStatusFor(cbEditor* ed);
        // Supply content for the autocompletion list.
        virtual std::vector<CCToken> GetAutocompList(bool isAuto, cbEditor* ed, int& tknStart, int& tknEnd);
        // Supply html formatted documentation for the passed token.
        virtual wxString GetDocumentation(const CCToken& token);
        // Supply content for the calltip at the specified location.
        virtual std::vector<CCCallTip> GetCallTips(int pos, int style, cbEditor* ed, int& argsPos);
        // Supply the definition of the token at the specified location.
        virtual std::vector<CCToken> GetTokenAt(int pos, cbEditor* ed, bool& allowCallTip);
        // Handle documentation link event.
        virtual wxString OnDocumentationLink(wxHtmlLinkEvent& event, bool& dismissPopup);
        // Callback for inserting the selected autocomplete entry into the editor.
        virtual void DoAutocomplete(const CCToken& token, cbEditor* ed);

        virtual void BuildMenu(wxMenuBar* menuBar);
        virtual void BuildModuleMenu(const ModuleType type, wxMenu* menu, const FileTreeData* data = nullptr);

    protected:
        virtual void OnAttach();
        virtual void OnRelease(bool appShutDown);

    private:
        wxString GetCompilerInclDirs(const wxString& compId);
        wxString GetSourceOf(cbEditor* ed);
        wxFileName FindSourceIn(const wxArrayString& candidateFilesArray, const wxFileName& activeFile, bool& isCandidate);
        bool IsSourceOf(const wxFileName& candidateFile, const wxFileName& activeFile, bool& isCandidate);

        void OnEditorOpen(CodeBlocksEvent& event);
        void OnEditorActivate(CodeBlocksEvent& event);
        void OnTimer(wxTimerEvent& event);
        void OnEditorHook(cbEditor* ed, wxScintillaEvent& event);
        void OnGotoDeclaration(wxCommandEvent& event);

        enum DiagnosticLevel { dlMinimal, dlFull };
        void DiagnoseEd(cbEditor* ed, DiagnosticLevel diagLv);
        void HighlightOccurrences(cbEditor* ed);

        TokenDatabase m_Database;
        wxStringVec m_CppKeywords;
        ClangProxy m_Proxy;
        wxImageList m_ImageList;
        wxTimer m_EdOpenTimer;
        wxTimer m_ReparseTimer;
        wxTimer m_DiagnosticTimer;
        wxTimer m_HightlightTimer;
        std::map<wxString, wxString> m_compInclDirs;
        cbEditor* m_pLastEditor;
        int m_TranslUnitId;
        int m_EditorHookId;
        int m_LastCallTipPos;
        std::vector<wxStringVec> m_LastCallTips;
};

#endif // CLANGPLUGIN_H
