#include "clangcc.h"
#include <cbstyledtextctrl.h>
#include <editor_hooks.h>
#include <cbcolourmanager.h>

//#ifndef CB_PRECOMP
#include <cbeditor.h>
#include <cbproject.h>
#include <compilerfactory.h>
#include <configmanager.h>
#include <editorcolourset.h>
#include <editormanager.h>
#include <logmanager.h>
#include <macrosmanager.h>
#include <projectfile.h>
#include <projectmanager.h>

#include <algorithm>
#include <wx/dir.h>
#include <wx/tokenzr.h>
#include <wx/choice.h>
//#endif // CB_PRECOMP

const int idReparseTimer    = wxNewId();
const int idDiagnosticTimer = wxNewId();
const int idHighlightTimer = wxNewId();

#define REPARSE_DELAY 9000
#define DIAGNOSTIC_DELAY 3000
#define HIGHTLIGHT_DELAY 1700

//DEFINE_EVENT_TYPE(clEVT_COMMAND_UPDATETOOLBARSELECTION)
//DEFINE_EVENT_TYPE(clEVT_COMMAND_UPDATETOOLBARCONTENTS)

ClangCodeCompletion::ClangCodeCompletion() :
    ClangPluginComponent(),
    m_TranslUnitId(-1),
    m_EditorHookId(-1),
    m_ReparseTimer(this, idReparseTimer),
    m_DiagnosticTimer(this, idDiagnosticTimer),
    m_CCOutstanding(0),
    m_CCOutstandingLastMessageTime(0),
    m_CCOutstandingPos(-1)

{

}

ClangCodeCompletion::~ClangCodeCompletion()
{

}

void ClangCodeCompletion::OnAttach(IClangPlugin* pClangPlugin)
{
    ClangPluginComponent::OnAttach(pClangPlugin);
    typedef cbEventFunctor<ClangCodeCompletion, CodeBlocksEvent> CBCCEvent;
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_ACTIVATED, new CBCCEvent(this, &ClangCodeCompletion::OnEditorActivate));
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_CLOSE,     new CBCCEvent(this, &ClangCodeCompletion::OnEditorClose));

    Connect(idDiagnosticTimer, wxEVT_TIMER, wxTimerEventHandler(ClangCodeCompletion::OnTimer));
    Connect(idHighlightTimer,  wxEVT_TIMER, wxTimerEventHandler(ClangCodeCompletion::OnTimer));

    typedef cbEventFunctor<ClangCodeCompletion, ClangEvent> ClCCEvent;
    pClangPlugin->RegisterEventSink(clEVT_TRANSLATIONUNIT_CREATED, new ClCCEvent(this, &ClangCodeCompletion::OnTranslationUnitCreated) );
    pClangPlugin->RegisterEventSink(clEVT_REPARSE_FINISHED, new ClCCEvent(this, &ClangCodeCompletion::OnReparseFinished) );
    pClangPlugin->RegisterEventSink(clEVT_GETCODECOMPLETE_FINISHED, new ClCCEvent(this, &ClangCodeCompletion::OnCodeCompleteFinished) );

    fprintf(stdout,"==== Registered event %d\n", (int)clEVT_GETCODECOMPLETE_FINISHED);

    m_EditorHookId = EditorHooks::RegisterHook(new EditorHooks::HookFunctor<ClangCodeCompletion>(this, &ClangCodeCompletion::OnEditorHook));
}

void ClangCodeCompletion::OnRelease(IClangPlugin* pClangPlugin)
{
    pClangPlugin->RemoveAllEventSinksFor(this);
    EditorHooks::UnregisterHook(m_EditorHookId);
    Manager::Get()->RemoveAllEventSinksFor(this);

    ClangPluginComponent::OnRelease(pClangPlugin);
}

void ClangCodeCompletion::OnEditorActivate(CodeBlocksEvent& event)
{
    event.Skip();
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinEditor(event.GetEditor());
    if (ed)
    {
        wxString fn = ed->GetFilename();

        ClTranslUnitId id = m_pClangPlugin->GetTranslationUnitId(fn);
        if (m_TranslUnitId != id )
        {
        }
        m_TranslUnitId = id;
    }
}

void ClangCodeCompletion::OnEditorClose(CodeBlocksEvent& event)
{
    EditorManager* edm = Manager::Get()->GetEditorManager();
    if (!edm)
    {
        event.Skip();
        return;
    }
    event.Skip();
}

void ClangCodeCompletion::OnEditorHook(cbEditor* ed, wxScintillaEvent& event)
{
    event.Skip();
    bool clearIndicator = false;
    bool reparse = false;
    //if (!m_pClangPlugin->IsProviderFor(ed))
    //    return;
    cbStyledTextCtrl* stc = ed->GetControl();
    if (event.GetEventType() == wxEVT_SCI_MODIFIED)
    {
        m_ReparseTimer.Stop();
        if (event.GetModificationType() & (wxSCI_MOD_INSERTTEXT | wxSCI_MOD_DELETETEXT))
        {
            reparse = true;
            clearIndicator = true;
        }
    }
    else if (event.GetEventType() == wxEVT_SCI_UPDATEUI)
    {
        //fprintf(stdout,"wxEVT_SCI_UPDATEUI\n");
        if (event.GetUpdated() & wxSCI_UPDATE_SELECTION)
        {
            m_HightlightTimer.Stop();
            m_HightlightTimer.Start(HIGHTLIGHT_DELAY, wxTIMER_ONE_SHOT);
        }
        clearIndicator = true;
    }
    else if (event.GetEventType() == wxEVT_SCI_CHANGE)
    {
        //fprintf(stdout,"wxEVT_SCI_CHANGE\n");
    }
    if (clearIndicator)
    {
        const int theIndicator = 16;
        stc->SetIndicatorCurrent(theIndicator);
        stc->IndicatorClearRange(0, stc->GetLength());
    }
    if (reparse)
    {
        RequestReparse();
    }
}

void ClangCodeCompletion::OnTimer(wxTimerEvent& event)
{
    if (!IsAttached())
    {
        return;
    }
    const int evId = event.GetId();
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
    {
        return;
    }

    if (evId == idReparseTimer) // m_ReparseTimer
    {
        //wxCommandEvent evt(cbEVT_COMMAND_REPARSE, idReparse);
        //AddPendingEvent(evt);
        m_pClangPlugin->RequestReparse( GetCurrentTranslationUnitId(), ed->GetFilename() );

    }
    else if (evId == idHighlightTimer)
    {
        //if (m_TranslUnitId == wxNOT_FOUND)
        //{
        //    return;
        //}
        HighlightOccurrences(ed);
    }
    else if (evId == idDiagnosticTimer)
    {
        //wxCommandEvent evt(cbEVT_COMMAND_DIAGNOSEED, idDiagnoseEd);
        //AddPendingEvent(evt);
    }
    else
    {
        event.Skip();
    }
}

ClTranslUnitId ClangCodeCompletion::GetCurrentTranslationUnitId()
{
    if (m_TranslUnitId == -1 )
    {
        cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
        if (!ed)
        {
            return -1;
        }
        wxString filename = ed->GetFilename();
        m_TranslUnitId = m_pClangPlugin->GetTranslationUnitId( filename );
    }
    return m_TranslUnitId;
}

cbCodeCompletionPlugin::CCProviderStatus ClangCodeCompletion::GetProviderStatusFor( cbEditor* ed )
{
    if (ed->GetLanguage() == ed->GetColourSet()->GetHighlightLanguage(wxT("C/C++")))
        return cbCodeCompletionPlugin::ccpsActive;
    return cbCodeCompletionPlugin::ccpsInactive;
}

struct PrioritySorter
{
    bool operator()(const cbCodeCompletionPlugin::CCToken& a, const cbCodeCompletionPlugin::CCToken& b)
    {
        return a.weight < b.weight;
    }
};

static wxString GetActualName(const wxString& name)
{
    const int idx = name.Find(wxT(':'));
    if (idx == wxNOT_FOUND)
        return name;
    return name.Mid(0, idx);
}

std::vector<cbCodeCompletionPlugin::CCToken> ClangCodeCompletion::GetAutocompList(bool isAuto, cbEditor* ed, int& tknStart, int& tknEnd)
{
#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    fprintf(stdout,"%s isAuto=%d\n", __PRETTY_FUNCTION__,(int)isAuto);
#endif
    std::vector<cbCodeCompletionPlugin::CCToken> tokens;

    int CCOutstanding = m_CCOutstanding;
    if ((CCOutstanding > 0)&&(m_CCOutstandingPos != ed->GetControl()->GetCurrentPos()))
    {
        CCOutstanding = 0;
    }

    m_CCOutstanding = 0;
    ClTranslUnitId translUnitId = m_TranslUnitId;
    if( translUnitId != GetCurrentTranslationUnitId() )
        return tokens;
    if (translUnitId == wxNOT_FOUND)
    {
        Manager::Get()->GetLogManager()->LogWarning(wxT("ClangLib: m_TranslUnitId == wxNOT_FOUND, "
                "cannot complete in file ") + ed->GetFilename());
        return tokens;
    }

    cbStyledTextCtrl* stc = ed->GetControl();
    const int style = stc->GetStyleAt(tknEnd);
    const wxChar curChar = stc->GetCharAt(tknEnd - 1);
    if (isAuto) // filter illogical cases of auto-launch
    {
        if ((curChar == wxT(':') // scope operator
                && stc->GetCharAt(tknEnd - 2) != wxT(':') )
                || ( curChar == wxT('>') // '->'
                        && stc->GetCharAt(tknEnd - 2) != wxT('-') )
                || ( wxString(wxT("<\"/")).Find(curChar) != wxNOT_FOUND // #include directive (TODO: enumerate completable include files)
                        && !stc->IsPreprocessor(style)))
        {
            return tokens;
        }
    }

    const int line = stc->LineFromPosition(tknStart);
/*
    std::map<wxString, wxString> unsavedFiles;
    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    for (int i = 0; i < edMgr->GetEditorsCount(); ++i)
    {
        cbEditor* editor = edMgr->GetBuiltinEditor(i);
        if (editor && editor->GetModified())
            unsavedFiles.insert(std::make_pair(editor->GetFilename(), editor->GetControl()->GetText()));
    }
*/
    const int lnStart = stc->PositionFromLine(line);
    int column = tknStart - lnStart;
    for (; column > 0; --column)
    {
        if (!wxIsspace(stc->GetCharAt(lnStart + column - 1))
                || (column != 1 && !wxIsspace(stc->GetCharAt(lnStart + column - 2))))
        {
            break;
        }
    }

    const wxString& prefix = stc->GetTextRange(tknStart, tknEnd).Lower();
    bool includeCtors = true; // sometimes we get a lot of these
    for (int i = tknStart - 1; i > 0; --i)
    {
        wxChar chr = stc->GetCharAt(i);
        if (!wxIsspace(chr))
        {
            if (chr == wxT(';') || chr == wxT('}')) // last non-whitespace character
                includeCtors = false; // filter out ctors (they are unlikely to be wanted in this situation)
            break;
        }
    }

    std::vector<ClToken> tknResults;
    if ((CCOutstanding == 0)||(m_CCOutstandingResults.size()==0))
    {
        ClTokenPosition loc(line+1, column+1);
        //ClangProxy::CodeCompleteAtJob job( cbEVT_CLANG_SYNCTASK_FINISHED, idClangCodeCompleteTask, isAuto, ed->GetFilename(), loc, m_TranslUnitId, unsavedFiles);
        //m_Proxy.AppendPendingJob(job);
        unsigned long timeout = 40;
        if( !isAuto )
        {
            timeout = 500;
        }
        if( wxCOND_TIMEOUT == m_pClangPlugin->GetCodeCompletionAt(translUnitId, ed->GetFilename(), loc, timeout, tknResults))
        {
            if (wxGetLocalTime() - m_CCOutstandingLastMessageTime > 10)
            {
                //InfoWindow::Display(_("Code completion"), _("Busy parsing the document"), 1000);
                m_CCOutstandingLastMessageTime = wxGetLocalTime();
            }
            //std::cout<<"Timeout waiting for code completion"<<std::endl;
            m_CCOutstanding++;
            m_CCOutstandingPos = ed->GetControl()->GetCurrentPos();
            m_CCOutstandingResults.clear();
            return tokens;
        }
    }
    else
    {
        tknResults = m_CCOutstandingResults;
    }

    //m_Proxy.CodeCompleteAt(isAuto, ed->GetFilename(), line + 1, column + 1,
    //        m_TranslUnitId, unsavedFiles, tknResults);
    if (prefix.Length() > 3) // larger context, match the prefix at any point in the token
    {
        for (std::vector<ClToken>::const_iterator tknIt = tknResults.begin();
                tknIt != tknResults.end(); ++tknIt)
        {
            if (tknIt->name.Lower().Find(prefix) != wxNOT_FOUND && (includeCtors || tknIt->category != tcCtorPublic))
                tokens.push_back(cbCodeCompletionPlugin::CCToken(tknIt->id, tknIt->name, tknIt->name, tknIt->weight, tknIt->category));
        }
    }
    else if (prefix.IsEmpty())
    {
        for (std::vector<ClToken>::const_iterator tknIt = tknResults.begin();
                tknIt != tknResults.end(); ++tknIt)
        {
            // it is rather unlikely for an operator to be the desired completion
            if (!tknIt->name.StartsWith(wxT("operator")) && (includeCtors || tknIt->category != tcCtorPublic))
                tokens.push_back(cbCodeCompletionPlugin::CCToken(tknIt->id, tknIt->name, tknIt->name, tknIt->weight, tknIt->category));
        }
    }
    else // smaller context, only allow matches of the prefix at the beginning of the token
    {
        for (std::vector<ClToken>::const_iterator tknIt = tknResults.begin();
                tknIt != tknResults.end(); ++tknIt)
        {
            if (tknIt->name.Lower().StartsWith(prefix) && (includeCtors || tknIt->category != tcCtorPublic))
                tokens.push_back(cbCodeCompletionPlugin::CCToken(tknIt->id, tknIt->name, tknIt->name, tknIt->weight, tknIt->category));
        }
    }

    if (!tokens.empty())
    {
        if (prefix.IsEmpty() && tokens.size() > 1500) // reduce to give only top matches
        {
            std::partial_sort(tokens.begin(), tokens.begin() + 1000, tokens.end(), PrioritySorter());
            tokens.erase(tokens.begin() + 1000, tokens.end());
        }
        const int imgCount = m_pClangPlugin->GetImageList(translUnitId).GetImageCount();
        for (int i = 0; i < imgCount; ++i)
            stc->RegisterImage(i, m_pClangPlugin->GetImageList(translUnitId).GetBitmap(i));
        bool isPP = stc->GetLine(line).Strip(wxString::leading).StartsWith(wxT("#"));
        std::set<int> usedWeights;
        for (std::vector<cbCodeCompletionPlugin::CCToken>::iterator tknIt = tokens.begin();
                tknIt != tokens.end(); ++tknIt)
        {
            wxStringVec keywords = m_pClangPlugin->GetKeywords(translUnitId);
            usedWeights.insert(tknIt->weight);
            switch (tknIt->category)
            {
            case tcNone:
                if (isPP)
                    tknIt->category = tcMacroDef;
                else if (std::binary_search(keywords.begin(), keywords.end(), GetActualName(tknIt->name)))
                    tknIt->category = tcLangKeyword;
                break;

            case tcClass:
            case tcCtorPublic:
            case tcDtorPublic:
            case tcFuncPublic:
            case tcVarPublic:
            case tcEnum:
            case tcTypedef:
                // TODO
                //m_Proxy.RefineTokenType(m_TranslUnitId, tknIt->id, tknIt->category);
                break;

            default:
                break;
            }
        }
        // Clang sometimes gives many weight values, which can make completion more difficult
        // because results are less alphabetical. Use a compression map on the lower priority
        // values (higher numbers) to reduce the total number of weights used.
        if (usedWeights.size() > 3)
        {
            std::vector<int> weightsVec(usedWeights.begin(), usedWeights.end());
            std::map<int, int> weightCompr;
            weightCompr[weightsVec[0]] = weightsVec[0];
            weightCompr[weightsVec[1]] = weightsVec[1];
            int factor = (weightsVec.size() > 7 ? 3 : 2);
            for (size_t i = 2; i < weightsVec.size(); ++i)
                weightCompr[weightsVec[i]] = weightsVec[(i - 2) / factor + 2];
            for (std::vector<cbCodeCompletionPlugin::CCToken>::iterator tknIt = tokens.begin();
                    tknIt != tokens.end(); ++tknIt)
            {
                tknIt->weight = weightCompr[tknIt->weight];
            }
        }
    }

    std::cout<<"CodeCompletion finished"<<std::endl;
    return tokens;
}

wxString ClangCodeCompletion::GetDocumentation( const cbCodeCompletionPlugin::CCToken &token )
{
    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    cbEditor* ed = edMgr->GetBuiltinActiveEditor();
    if (ed)
    {
        return m_pClangPlugin->GetCodeCompletionTokenDocumentation( m_TranslUnitId, ed->GetFilename(), ClTokenPosition(0,0), token.id );
    }
    return wxEmptyString;
}

void ClangCodeCompletion::HighlightOccurrences(cbEditor* ed)
{
    ClTranslUnitId translId = GetCurrentTranslationUnitId();
    cbStyledTextCtrl* stc = ed->GetControl();
    int pos = stc->GetCurrentPos();
    const wxChar ch = stc->GetCharAt(pos);
    if (pos > 0
            && (wxIsspace(ch) || (ch != wxT('_') && wxIspunct(ch)))
            && !wxIsspace(stc->GetCharAt(pos - 1)))
    {
        --pos;
    }

    // chosen a high value for indicator, hoping not to interfere with the indicators used by some lexers
    // if they get updated from deprecated old style indicators someday.
    const int theIndicator = 16;
    stc->SetIndicatorCurrent(theIndicator);

    // Set Styling:
    // clear all style indications set in a previous run (is also done once after text gets unselected)
    stc->IndicatorClearRange(0, stc->GetLength());

    if (stc->GetTextRange(pos - 1, pos + 1).Strip().IsEmpty())
        return;

    // TODO: use independent key
    wxColour highlightColour(Manager::Get()->GetColourManager()->GetColour(wxT("editor_highlight_occurrence")));

    stc->IndicatorSetStyle(theIndicator, wxSCI_INDIC_HIGHLIGHT);
    stc->IndicatorSetForeground(theIndicator, highlightColour);
    stc->IndicatorSetUnder(theIndicator, true);

    const int line = stc->LineFromPosition(pos);
    ClTokenPosition loc(line + 1, pos - stc->PositionFromLine(line) + 1);

    std::vector< std::pair<int, int> > occurrences;
    m_pClangPlugin->GetOccurrencesOf( translId,  ed->GetFilename(), loc, 100, occurrences );

    for (std::vector< std::pair<int, int> >::const_iterator tkn = occurrences.begin();
            tkn != occurrences.end(); ++tkn)
    {
        stc->IndicatorFillRange(tkn->first, tkn->second);
    }
}

void ClangCodeCompletion::RequestReparse()
{
    m_ReparseTimer.Stop();
    m_ReparseTimer.Start(REPARSE_DELAY, wxTIMER_ONE_SHOT);
    //m_DiagnosticTimer.Stop();
    //m_DiagnosticTimer.Start(DIAGNOSTIC_DELAY, wxTIMER_ONE_SHOT);
}

void ClangCodeCompletion::OnTranslationUnitCreated( ClangEvent& event )
{
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
}

void ClangCodeCompletion::OnReparseFinished( ClangEvent& event )
{
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
}

void ClangCodeCompletion::OnCodeCompleteFinished( ClangEvent& event )
{
    //fprintf(stdout,"%s\n", __PRETTY_FUNCTION__ );
    if( event.GetTranslationUnitId() != m_TranslUnitId )
    {
        return;
    }
    if (m_CCOutstanding > 0)
    {
        EditorManager* edMgr = Manager::Get()->GetEditorManager();
        cbEditor* ed = edMgr->GetBuiltinActiveEditor();
        if (ed)
        {
            if (ed->GetControl()->GetCurrentPos() == m_CCOutstandingPos)
            {
                m_CCOutstandingResults = event.GetCodeCompletionResults();
                if ( m_CCOutstandingResults.size() > 0 )
                {
                    CodeBlocksEvent evt(cbEVT_COMPLETE_CODE);
                    evt.SetInt(1);
                    Manager::Get()->ProcessEvent(evt);
                    return;
                }
            }
        }
        m_CCOutstanding--;
    }
}
