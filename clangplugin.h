#ifndef CLANGPLUGIN_H
#define CLANGPLUGIN_H

#include <cbplugin.h>
#include <wx/imaglist.h>
#include <wx/timer.h>

#include "clangproxy.h"

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
        virtual wxStringVec GetCallTips(int pos, int style, cbEditor* ed, int& hlStart, int& hlEnd, int& argsPos);
        // Supply the definition of the token at the specified location.
        virtual std::vector<CCToken> GetTokenAt(int pos, cbEditor* ed);
        // Handle documentation link event.
        virtual wxString OnDocumentationLink(wxHtmlLinkEvent& event, bool& dismissPopup);

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

        ClangProxy m_Proxy;
        wxImageList m_ImageList;
        wxTimer m_EdOpenTimer;
        std::map<wxString, wxString> m_compInclDirs;
        cbEditor* m_pLastEditor;
        int m_TranslUnitId;
};

#endif // CLANGPLUGIN_H
