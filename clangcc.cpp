#include "clangcc.h"
#include <cbstyledtextctrl.h>
#include <editor_hooks.h>
#include <cbcolourmanager.h>
#include <infowindow.h>

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
#include <vector>
#include <wx/dir.h>
#include <wx/tokenzr.h>
#include <wx/choice.h>
//#endif // CB_PRECOMP
#include "cclogger.h"

const int idHighlightTimer = wxNewId();

#define HIGHTLIGHT_DELAY 1700

const wxString ClangCodeCompletion::SettingName = _T("/code_completion");

ClangCodeCompletion::ClangCodeCompletion() :
    ClangPluginComponent(),
    m_TranslUnitId(-1),
    m_EditorHookId(-1),
    m_CCOutstanding(0),
    m_CCOutstandingLastMessageTime(0),
    m_CCOutstandingTokenStart(-1),
    m_CCOutstandingLoc(0,0)
{

}

ClangCodeCompletion::~ClangCodeCompletion()
{

}

void ClangCodeCompletion::OnAttach(IClangPlugin* pClangPlugin)
{
    ClangPluginComponent::OnAttach(pClangPlugin);

    ColourManager *pColours = Manager::Get()->GetColourManager();

    pColours->RegisterColour(_("Code completion"), _("Documentation popup scope text"), wxT("cc_docs_scope_fore"), *wxBLUE);

    typedef cbEventFunctor<ClangCodeCompletion, CodeBlocksEvent> CBCCEvent;
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_ACTIVATED, new CBCCEvent(this, &ClangCodeCompletion::OnEditorActivate));
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_CLOSE,     new CBCCEvent(this, &ClangCodeCompletion::OnEditorClose));

    Connect(idHighlightTimer, wxEVT_TIMER, wxTimerEventHandler(ClangCodeCompletion::OnTimer));

    typedef cbEventFunctor<ClangCodeCompletion, ClangEvent> ClCCEvent;
    pClangPlugin->RegisterEventSink(clEVT_TRANSLATIONUNIT_CREATED,  new ClCCEvent(this, &ClangCodeCompletion::OnTranslationUnitCreated));
    pClangPlugin->RegisterEventSink(clEVT_GETCODECOMPLETE_FINISHED, new ClCCEvent(this, &ClangCodeCompletion::OnCodeCompleteFinished));

    m_EditorHookId = EditorHooks::RegisterHook(new EditorHooks::HookFunctor<ClangCodeCompletion>(this, &ClangCodeCompletion::OnEditorHook));
}

void ClangCodeCompletion::OnRelease(IClangPlugin* pClangPlugin)
{
    pClangPlugin->RemoveAllEventSinksFor(this);
    Disconnect(idHighlightTimer);
    EditorHooks::UnregisterHook(m_EditorHookId);
    Manager::Get()->RemoveAllEventSinksFor(this);

    ClangPluginComponent::OnRelease(pClangPlugin);
}

void ClangCodeCompletion::OnEditorActivate(CodeBlocksEvent& event)
{
    event.Skip();
    if (!IsAttached())
        return;

    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinEditor(event.GetEditor());
    if (ed)
    {
        wxString fn = ed->GetFilename();

        ClTranslUnitId id = m_pClangPlugin->GetTranslationUnitId(fn);
        m_TranslUnitId = id;
        m_CCOutstanding = 0;
        m_CCOutstandingLastMessageTime = 0;
        m_CCOutstandingTokenStart = 0;
        m_CCOutstandingResults.clear();
        cbStyledTextCtrl* stc = ed->GetControl();
#ifndef __WXMSW__
        stc->Disconnect(wxEVT_KEY_DOWN, wxKeyEventHandler(ClangCodeCompletion::OnKeyDown));
        stc->Connect(wxID_ANY, wxEVT_KEY_DOWN, wxKeyEventHandler(ClangCodeCompletion::OnKeyDown), (wxObject*)nullptr, this);
#endif
        const int imgCount = m_pClangPlugin->GetImageList(id).GetImageCount();
        for (int i = 0; i < imgCount; ++i)
            stc->RegisterImage(i, m_pClangPlugin->GetImageList(id).GetBitmap(i));
    }
}

void ClangCodeCompletion::OnEditorClose(CodeBlocksEvent& event)
{
    event.Skip();
    if (!IsAttached())
        return;

    EditorManager* edm = Manager::Get()->GetEditorManager();
    if (!edm)
        return;
}

void ClangCodeCompletion::OnEditorHook(cbEditor* ed, wxScintillaEvent& event)
{
    event.Skip();
    if (!IsAttached())
        return;
    bool clearIndicator = false;
    //if (!m_pClangPlugin->IsProviderFor(ed))
    //    return;
    cbStyledTextCtrl* stc = ed->GetControl();
    if (event.GetEventType() == wxEVT_SCI_MODIFIED)
    {
        if (event.GetModificationType() & (wxSCI_MOD_INSERTTEXT | wxSCI_MOD_DELETETEXT))
        {
            clearIndicator = true;
        }
    }
    else if (event.GetEventType() == wxEVT_SCI_UPDATEUI)
    {
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
    else if (event.GetEventType() == wxEVT_SCI_KEY)
    {
        //fprintf(stdout,"wxEVT_SCI_KEY\n");
    }
    if (clearIndicator)
    {
        const int theIndicator = 16;
        stc->SetIndicatorCurrent(theIndicator);
        stc->IndicatorClearRange(0, stc->GetLength());
    }
}

void ClangCodeCompletion::OnTimer(wxTimerEvent& event)
{
    if (!IsAttached())
        return;
    const int evId = event.GetId();
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
        return;

    if (evId == idHighlightTimer)
        HighlightOccurrences(ed);
    else
        event.Skip();
}

void ClangCodeCompletion::OnKeyDown(wxKeyEvent& event)
{
    //fprintf(stdout,"OnKeyDown");
    if (event.GetKeyCode() == WXK_TAB)
    {
        cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
        if (ed)
        {
            cbStyledTextCtrl* stc = ed->GetControl();
            if (!stc->AutoCompActive())
            {
                int pos = stc->PositionFromLine(stc->GetCurrentLine());
                int maxPos = stc->PositionFromLine(stc->GetCurrentLine() + 1);
                for (std::vector<wxString>::iterator it = m_TabJumpArguments.begin(); it != m_TabJumpArguments.end(); ++it)
                {
                    int argPos = stc->FindText(pos, maxPos, *it);
                    if (argPos != wxNOT_FOUND)
                    {
                        stc->SetSelectionVoid(argPos, argPos + it->Length() - 1);
                        wxString value = *it;
                        it = m_TabJumpArguments.erase(it);
                        m_TabJumpArguments.push_back(value);
                        stc->EnableTabSmartJump();
                        return;
                    }
                }
            }
        }
    }
    event.Skip();
}

void ClangCodeCompletion::OnCompleteCode(CodeBlocksEvent &event)
{
    event.Skip();
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
        return;
    //wxString filename = ed->GetFilename();
    //m_pClangPlugin->RequestReparse( m_TranslUnitId, filename );
}

ClTranslUnitId ClangCodeCompletion::GetCurrentTranslationUnitId()
{
    if (m_TranslUnitId == wxNOT_FOUND)
    {
        cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
        if (!ed)
            return wxNOT_FOUND;
        wxString filename = ed->GetFilename();
        m_TranslUnitId = m_pClangPlugin->GetTranslationUnitId( filename );
    }
    return m_TranslUnitId;
}

cbCodeCompletionPlugin::CCProviderStatus ClangCodeCompletion::GetProviderStatusFor(cbEditor* ed)
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
    std::vector<cbCodeCompletionPlugin::CCToken> tokens;

    ConfigManager* cfg = Manager::Get()->GetConfigManager(wxT("ClangLib"));
    size_t maxResultCount = cfg->ReadInt(wxT("/max_matches"), 1024);

    cbStyledTextCtrl* stc = ed->GetControl();
    const int style = stc->GetStyleAt( tknEnd );
    const int lineIndentPos = stc->GetLineIndentPosition(stc->GetCurrentLine());
    const wxChar lineFirstChar = stc->GetCharAt(lineIndentPos);

    if (lineFirstChar == wxT('#'))
    {
        const int startPos = stc->WordStartPosition(lineIndentPos + 1, true);
        const int endPos = stc->WordEndPosition(lineIndentPos + 1, true);
        const wxString str = stc->GetTextRange(startPos, endPos);

        if (str == wxT("include") && (tknEnd > endPos))
            return GetAutocompListIncludes(isAuto, ed, tknStart, tknEnd);
    }

    ClTranslUnitId translUnitId = GetCurrentTranslationUnitId();
    if (translUnitId == wxNOT_FOUND)
    {
        Manager::Get()->GetLogManager()->LogWarning(wxT("ClangLib: translUnitId == wxNOT_FOUND, "
                "cannot complete in file ") + ed->GetFilename());
        if (wxGetLocalTime() + 10 > m_CCOutstandingLastMessageTime)
        {
            InfoWindow::Display(_("Code completion"), _("Busy parsing the document"), 1000);
            m_CCOutstandingLastMessageTime = wxGetLocalTime();
        }
        return tokens;
    }
    if (translUnitId != m_TranslUnitId)
        return tokens;

    const wxChar curChar = stc->GetCharAt(tknEnd - 1);
    if (isAuto) // filter illogical cases of auto-launch
    {
        if (   (   curChar == wxT(':') // scope operator
                && stc->GetCharAt(tknEnd - 2) != wxT(':') )
            || (   curChar == wxT('>') // '->'
                && stc->GetCharAt(tknEnd - 2) != wxT('-') )
            || (   wxString(wxT("<\"/")).Find(curChar) != wxNOT_FOUND // #include directive (TODO: enumerate completable include files)
                && !stc->IsPreprocessor(style) ) )
        {
            return tokens;
        }
    }

    if (stc->IsString(style)||stc->IsComment(style)||stc->IsCharacter(style))
        return tokens;

    // TODO: The outstanding bits really have become a mess: It's complicated with CC that can come in with the same position from C::B, CC request that can come in from the result and an old request that might still have been in the pipe

    //CCLogger::Get()->DebugLog( F(wxT("GetAutoCompList m_CCOutstanding=%d m_CCOutstandingTokenStart=%d"), m_CCOutstanding, m_CCOutstandingTokenStart ) );

    int CCOutstanding = m_CCOutstanding;
    if ((CCOutstanding > 0) && (m_CCOutstandingTokenStart == tknStart) && m_CCOutstandingResults.empty())
    {
        CCLogger::Get()->DebugLog(wxT("CC request allready requested"));
        return tokens;
    }
    if (m_CCOutstandingTokenStart != tknStart)
    {
        m_CCOutstandingResults.clear();
    }
    if ((CCOutstanding > 0) && (m_CCOutstandingTokenStart != tknStart))
    {
        CCOutstanding = 0;
    }

    if ((CCOutstanding > 0) && m_CCOutstandingResults.empty())
    {
        CCLogger::Get()->DebugLog( wxT("CodeCompletion allready requested, wait until it's finished!") );
        // This request is allready scheduled to be performed but is not ready yet...
        return tokens;
    }


    const int line = stc->LineFromPosition(tknStart);
    const int lnStart = stc->PositionFromLine(line);
    int column = tknStart - lnStart;
    for (; column > 0; --column)
    {
        if (   !wxIsspace(stc->GetCharAt(lnStart + column - 1))
            || (column != 1 && !wxIsspace(stc->GetCharAt(lnStart + column - 2))) )
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
    if (m_CCOutstandingResults.empty())
    {
        ClTokenPosition loc(line + 1, column + 1);
        unsigned long timeout = 20;
        if ( !isAuto )
        {
            timeout = 100;
        }
        if (wxCOND_TIMEOUT == m_pClangPlugin->GetCodeCompletionAt(translUnitId, ed->GetFilename(), loc, includeCtors, timeout, tknResults))
        {
            m_CCOutstanding++;
            m_CCOutstandingTokenStart = tknStart;
            m_CCOutstandingLoc = loc;
            m_CCOutstandingResults.clear();
            return tokens;
        }
    }
    else
    {
        tknResults = m_CCOutstandingResults;
    }
    m_CCOutstanding = 0;
    if (prefix.Length() > 3) // larger context, match the prefix at any point in the token
    {
        for (std::vector<ClToken>::const_iterator tknIt = tknResults.begin();
             tknIt != tknResults.end(); ++tknIt)
        {
            if ( (tknIt->name.Lower().Find(prefix) != wxNOT_FOUND) && (includeCtors || (tknIt->category != tcCtorPublic)) )
                tokens.push_back(cbCodeCompletionPlugin::CCToken(tknIt->id, tknIt->name, tknIt->name, tknIt->weight, tknIt->category));
        }
    }
    else if (prefix.IsEmpty())
    {
        for (std::vector<ClToken>::const_iterator tknIt = tknResults.begin();
                tknIt != tknResults.end(); ++tknIt)
        {
            // it is rather unlikely for an operator to be the desired completion
            if ( (!tknIt->name.StartsWith(wxT("operator"))) && (includeCtors || tknIt->category != tcCtorPublic) )
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
        if (prefix.IsEmpty() && (tokens.size() > maxResultCount)) // reduce to give only top matches
        {
            std::partial_sort(tokens.begin(), tokens.begin() + maxResultCount, tokens.end(), PrioritySorter());
            tokens.erase(tokens.begin() + maxResultCount, tokens.end());
        }
        //const int imgCount = m_pClangPlugin->GetImageList(translUnitId).GetImageCount();
        //for (int i = 0; i < imgCount; ++i)
        //    stc->RegisterImage(i, m_pClangPlugin->GetImageList(translUnitId).GetBitmap(i));
        bool isPP = stc->GetLine(line).Strip(wxString::leading).StartsWith(wxT("#"));
        wxStringVec keywords = m_pClangPlugin->GetKeywords(translUnitId);
        std::set<int> usedWeights;
        for (std::vector<cbCodeCompletionPlugin::CCToken>::iterator tknIt = tokens.begin();
             tknIt != tokens.end(); ++tknIt)
        {
            usedWeights.insert(tknIt->weight);
            switch (tknIt->category)
            {
            case tcNone:
                if (isPP)
                    tknIt->category = tcMacroDef;
                else if (std::binary_search(keywords.begin(), keywords.end(), GetActualName(tknIt->name)))
                    tknIt->category = tcLangKeyword;
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

    CCLogger::Get()->DebugLog( wxT("Delivering list of CC Tokens") );

    return tokens;
}

std::vector<cbCodeCompletionPlugin::CCToken> ClangCodeCompletion::GetAutocompListIncludes(bool WXUNUSED(isAuto), cbEditor* WXUNUSED(ed), int& WXUNUSED(tknStart), int& WXUNUSED(tknEnd))
{
    std::vector<cbCodeCompletionPlugin::CCToken> result;

    return result;
}

bool ClangCodeCompletion::DoAutocomplete( const cbCodeCompletionPlugin::CCToken& token, cbEditor* ed)
{
    wxString tknText = token.name;
    int idx = tknText.Find(wxT(':'));
    if (idx != wxNOT_FOUND)
        tknText.Truncate(idx);
    std::vector<std::pair<int, int> > offsetsList;
    cbStyledTextCtrl* stc = ed->GetControl();
    wxString suffix = m_pClangPlugin->GetCodeCompletionInsertSuffix(GetCurrentTranslationUnitId(),
                                                                    token.id,
                                                                    GetEOLStr(stc->GetEOLMode()) + ed->GetLineIndentString(stc->GetCurrentLine()),
                                                                    offsetsList);
    //if (offsetsList.size() == 0)
    //    offsetsList.push_back( std::make_pair<int,int>(0,0) );
    int pos = stc->GetCurrentPos();
    int startPos = std::min(stc->WordStartPosition(pos, true), std::min(stc->GetSelectionStart(),
                            stc->GetSelectionEnd()));
    int moveToPos = startPos + tknText.Length();
    stc->SetTargetStart(startPos);
    int endPos = stc->WordEndPosition(pos, true);
    if (tknText.EndsWith(stc->GetTextRange(pos, endPos)))
    {
        // Inplace function renaming. We determine here if we insert the arguments or not
        if (!suffix.IsEmpty())
        {
            if (stc->GetCharAt(endPos) == (int)suffix[0])
            {
                if ( (suffix.Length() != 2) || (stc->GetCharAt(endPos + 1) != (int)suffix[1]) )
                {
                    offsetsList.clear();
                }
            }
            else
            {
                tknText += suffix;
                if (suffix.Length() == 2)
                {
                    moveToPos += 2;
                }
            }
        }
    }
    else
    {
        endPos = pos;
        tknText += suffix;
    }
    stc->SetTargetEnd(endPos);

    stc->AutoCompCancel(); // so (wx)Scintilla does not insert the text as well

    if (stc->GetTextRange(startPos, endPos) != tknText)
        stc->ReplaceTarget(tknText);
    if (offsetsList.size() > 0)
        stc->SetSelectionVoid(moveToPos + offsetsList[0].first, moveToPos + offsetsList[0].second);
    else
        stc->SetSelectionVoid(moveToPos, moveToPos);
    stc->ChooseCaretX();
    if (m_TabJumpArguments.size() > 10 )
        m_TabJumpArguments.clear();
    for (std::vector< std::pair<int,int> >::const_iterator it = offsetsList.begin(); it != offsetsList.end(); ++it)
    {
        if (it->first != it->second)
        {
            m_TabJumpArguments.push_back( suffix.SubString(it->first, it->second) );
            stc->EnableTabSmartJump();
        }
    }
    if (offsetsList.size() > 0)
    {
        if (m_TabJumpArguments.size() > 0)
        {
            // Move the first to the last since the first is allready selected
            wxString first = m_TabJumpArguments.front();
            m_TabJumpArguments.erase(m_TabJumpArguments.begin());
            m_TabJumpArguments.push_back(first);
        }
        if (   (token.category != tcLangKeyword)
            && ((offsetsList[0].first != offsetsList[0].second) || (offsetsList[0].first == 1)) )
        {
            int tooltipMode = Manager::Get()->GetConfigManager(wxT("ccmanager"))->ReadInt(wxT("/tooltip_mode"), 1);
            if (tooltipMode != 3) // keybound only
            {
                CodeBlocksEvent evt(cbEVT_SHOW_CALL_TIP);
                Manager::Get()->ProcessEvent(evt);
            }
        }
    }
    return true;
}

wxString ClangCodeCompletion::GetDocumentation(const cbCodeCompletionPlugin::CCToken &token)
{
    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    cbEditor* ed = edMgr->GetBuiltinActiveEditor();
    if (ed)
    {
        return m_pClangPlugin->GetCodeCompletionTokenDocumentation(m_TranslUnitId, ed->GetFilename(), ClTokenPosition(0,0), token.id);
    }
    return wxEmptyString;
}

void ClangCodeCompletion::HighlightOccurrences(cbEditor* ed)
{
    ClTranslUnitId translId = GetCurrentTranslationUnitId();
    cbStyledTextCtrl* stc = ed->GetControl();
    int pos = stc->GetCurrentPos();
    const wxChar ch = stc->GetCharAt(pos);
    if (   pos > 0
        && (wxIsspace(ch) || (ch != wxT('_') && wxIspunct(ch)))
        && !wxIsspace(stc->GetCharAt(pos - 1)) )
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

void ClangCodeCompletion::OnTranslationUnitCreated( ClangEvent& event )
{
    if (event.GetTranslationUnitId() != GetCurrentTranslationUnitId())
        return;
    m_CCOutstanding = 0;
    m_CCOutstandingTokenStart = 0;
    m_CCOutstandingResults.clear();
}

void ClangCodeCompletion::OnCodeCompleteFinished(ClangEvent& event)
{
    //CCLogger::Get()->DebugLog( wxT("OnCodeCompleteFinished") );
    if (event.GetTranslationUnitId() != m_TranslUnitId)
        return;

    if (m_CCOutstanding > 0)
    {
        if (event.GetLocation() != m_CCOutstandingLoc)
        {
            CCLogger::Get()->DebugLog(wxT("Discard old CodeCompletion request result"));
            return;
        }
        EditorManager* edMgr = Manager::Get()->GetEditorManager();
        cbEditor* ed = edMgr->GetBuiltinActiveEditor();
        if (ed)
        {
            m_CCOutstandingResults = event.GetCodeCompletionResults();
            if (m_CCOutstandingResults.size() > 0)
            {
                CodeBlocksEvent evt(cbEVT_COMPLETE_CODE);
                evt.SetInt(1);
                Manager::Get()->ProcessEvent(evt);
                return;
            }
        }
        m_CCOutstanding--;
    }
}
// Sorting in GetLocalIncludeDirs()
static int CompareStringLen(const wxString& first, const wxString& second)
{
    return second.Len() - first.Len();
}

wxArrayString ClangCodeCompletion::GetLocalIncludeDirs(cbProject* project, const wxArrayString& buildTargets)
{
    wxArrayString dirs;
#if 0 //TODO
    // Do not try to operate include directories if the project is not for this platform
    if (m_CCEnablePlatformCheck && !project->SupportsCurrentPlatform())
        return dirs;
#endif
    const wxString prjPath = project->GetCommonTopLevelPath();
    GetAbsolutePath(prjPath, project->GetIncludeDirs(), dirs);

    for (size_t i = 0; i < buildTargets.GetCount(); ++i)
    {
        ProjectBuildTarget* tgt = project->GetBuildTarget(buildTargets[i]);
        // Do not try to operate include directories if the target is not for this platform
#if 0 //TODO
        if (   !m_CCEnablePlatformCheck
                || (m_CCEnablePlatformCheck && tgt->SupportsCurrentPlatform()) )
        {
#endif
            GetAbsolutePath(prjPath, tgt->GetIncludeDirs(), dirs);
#if 0 //TODO
        }
#endif
    }

    // if a path has prefix with the project's path, it is a local include search dir
    // other wise, it is a system level include search dir, we try to collect all the system dirs
    wxArrayString sysDirs;
    for (size_t i = 0; i < dirs.GetCount();)
    {
        if (dirs[i].StartsWith(prjPath))
            ++i;
        else
        {
#if 0 //TODO
            wxCriticalSectionLocker locker(m_SystemHeadersThreadCS);
            if (m_SystemHeadersMap.find(dirs[i]) == m_SystemHeadersMap.end())
                sysDirs.Add(dirs[i]);
#endif
            dirs.RemoveAt(i);
        }
    }

    if (!sysDirs.IsEmpty())
    {
#if 0 //TODO
        SystemHeadersThread* thread = new SystemHeadersThread(this, &m_SystemHeadersThreadCS, m_SystemHeadersMap, sysDirs);
        m_SystemHeadersThreads.push_back(thread);
        if (!m_SystemHeadersThreads.front()->IsRunning() && m_NativeParser.Done())
            thread->Run();
#endif
    }

    dirs.Sort(CompareStringLen);
    return dirs;
}

wxArrayString& ClangCodeCompletion::GetSystemIncludeDirs(cbProject* project, bool force)
{
    static cbProject*    lastProject = nullptr;
    static wxArrayString incDirs;

    if (!force && project == lastProject) // force == false means we can use the cached dirs
        return incDirs;
    else
    {
        incDirs.Clear();
        lastProject = project;
    }
    wxString prjPath;
    if (project)
        prjPath = project->GetCommonTopLevelPath();
#if 0
    ParserBase* parser = m_NativeParser.GetParserByProject(project);
    if (!parser)
        return incDirs;

    incDirs = parser->GetIncludeDirs();
    // we try to remove the dirs which belong to the project
    for (size_t i = 0; i < incDirs.GetCount();)
    {
        if (incDirs[i].Last() != wxFILE_SEP_PATH)
            incDirs[i].Append(wxFILE_SEP_PATH);
        // the dirs which have prjPath prefix are local dirs, so they should be removed
        if (project && incDirs[i].StartsWith(prjPath))
            incDirs.RemoveAt(i);
        else
            ++i;
    }
#endif

    return incDirs;
}

void ClangCodeCompletion::GetAbsolutePath(const wxString& basePath, const wxArrayString& targets, wxArrayString& dirs)
{
    for (size_t i = 0; i < targets.GetCount(); ++i)
    {
        wxString includePath = targets[i];
        Manager::Get()->GetMacrosManager()->ReplaceMacros(includePath);
        wxFileName fn(includePath, wxEmptyString);
        if (fn.IsRelative())
        {
            const wxArrayString oldDirs = fn.GetDirs();
            fn.SetPath(basePath);
            for (size_t j = 0; j < oldDirs.GetCount(); ++j)
                fn.AppendDir(oldDirs[j]);
        }

        const wxString path = fn.GetFullPath();
        if (dirs.Index(path) == wxNOT_FOUND)
            dirs.Add(path);
    }
}
