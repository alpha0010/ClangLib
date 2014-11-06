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

        /*-- Public interface --*/

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

        // Build menu bar
        virtual void BuildMenu(wxMenuBar* menuBar);
        // Build popup menu
        virtual void BuildModuleMenu(const ModuleType type, wxMenu* menu, const FileTreeData* data = nullptr);

    protected:
        virtual void OnAttach();
        virtual void OnRelease(bool appShutDown);

    private:
        /**
         * Compute the locations of STL headers for the given compiler (cached)
         *
         * @param compId The id of the compiler
         * @return Include search flags pointing to said locations
         */
        wxString GetCompilerInclDirs(const wxString& compId);
        /**
         * Search for the source file associated with a given header
         *
         * @param ed The editor representing the header file
         * @return Full path to presumed source file
         */
        wxString GetSourceOf(cbEditor* ed);
        /**
         * Find the most likely source file from a list, corresponding to a given header
         *
         * @param candidateFilesArray List of possibilities to check
         * @param activeFile Header file to compare to
         * @param[out] isCandidate Set to true if returned file may not be best match
         * @return File determined to be source (or invalid)
         */
        wxFileName FindSourceIn(const wxArrayString& candidateFilesArray, const wxFileName& activeFile, bool& isCandidate);
        /**
         * Test if a candidate source file matches a header file
         *
         * @param candidateFile The potential source file
         * @param activeFile The header file
         * @param[out] isCandidate Set to true if match is not exact
         * @return true if files match close enough
         */
        bool IsSourceOf(const wxFileName& candidateFile, const wxFileName& activeFile, bool& isCandidate);

        /// Start up parsing timers
        void OnEditorOpen(CodeBlocksEvent& event);
        /// Start up parsing timers
        void OnEditorActivate(CodeBlocksEvent& event);
        /// Generic handler for various timers
        void OnTimer(wxTimerEvent& event);
        /// Start re-parse and highlight timers
        void OnEditorHook(cbEditor* ed, wxScintillaEvent& event);
        /// Resolve the token under the cursor and open the relevant location
        void OnGotoDeclaration(wxCommandEvent& event);

        enum DiagnosticLevel { dlMinimal, dlFull };
        /**
         * Update editor diagnostic mark up
         *
         * @param ed The editor to diagnose
         * @param diagLv Update only the highlights, or highlights and text annotations
         */
        void DiagnoseEd(cbEditor* ed, DiagnosticLevel diagLv);
        /**
         * Semantically highlight all occurrences of the token under the cursor
         * within the editor
         *
         * @param ed The editor to work in
         */
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
