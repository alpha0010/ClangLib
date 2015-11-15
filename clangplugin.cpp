/*
 * A clang based plugin
 */

#include <sdk.h>
#include <stdio.h>
#include "clangplugin.h"

#include <cbcolourmanager.h>
#include <cbstyledtextctrl.h>
#include <compilercommandgenerator.h>
#include <editor_hooks.h>

#include <wx/tokenzr.h>

#ifndef CB_PRECOMP
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
#endif // CB_PRECOMP

#define CLANGPLUGIN_TRACE_FUNCTIONS

// this auto-registers the plugin
namespace
{
PluginRegistrant<ClangPlugin> reg(wxT("ClangLib"));
}

static const wxString g_InvalidStr(wxT("invalid"));
//const int idEdOpenTimer     = wxNewId();
const int idReparseTimer    = wxNewId();
const int idDiagnosticTimer = wxNewId();
const int idHightlightTimer = wxNewId();
const int idGotoDeclaration = wxNewId();
const int idReparse = wxNewId();
const int idDiagnoseEd = wxNewId();

// milliseconds
//#define ED_OPEN_DELAY 1000
//#define ED_ACTIVATE_DELAY 150
#define REPARSE_DELAY 900
#define DIAGNOSTIC_DELAY 3000
#define HIGHTLIGHT_DELAY 1700

DEFINE_EVENT_TYPE(cbEVT_COMMAND_REPARSE)
DEFINE_EVENT_TYPE(cbEVT_COMMAND_DIAGNOSEED)

// Asynchronous events received
DEFINE_EVENT_TYPE(cbEVT_CLANG_CREATETU_FINISHED)
DEFINE_EVENT_TYPE(cbEVT_CLANG_REPARSE_FINISHED)
DEFINE_EVENT_TYPE(cbEVT_CLANG_GETDIAGNOSTICS_FINISHED)
DEFINE_EVENT_TYPE(cbEVT_CLANG_SYNCTASK_FINISHED)
DEFINE_EVENT_TYPE(cbEVT_CLANG_REMOVETU_FINISHED)

const int idClangCreateTU = wxNewId();
const int idClangRemoveTU = wxNewId();
const int idClangReparse = wxNewId();
const int idClangGetDiagnostics = wxNewId();
const int idClangSyncTask = wxNewId();
const int idClangCodeCompleteTask = wxNewId();

ClangPlugin::ClangPlugin() :
    m_Proxy(this, m_Database, m_CppKeywords),
    m_ImageList(16, 16),
    //m_EdOpenTimer(this, idEdOpenTimer),
    m_ReparseTimer(this, idReparseTimer),
    m_DiagnosticTimer(this, idDiagnosticTimer),
    m_HightlightTimer(this, idHightlightTimer),
    m_pLastEditor(nullptr),
    m_TranslUnitId(wxNOT_FOUND),
    m_CCOutstanding(0),
    m_CCOutstandingPos(0),
    m_ReparseNeeded(0),
    m_ToolBar(nullptr),
    m_Function(nullptr),
    m_Scope(nullptr),
    m_UpdateCompileCommand(0)
{
    if (!Manager::LoadResource(_T("clanglib.zip")))
        NotifyMissingFile(_T("clanglib.zip"));
}

ClangPlugin::~ClangPlugin()
{
}

void ClangPlugin::OnAttach()
{
    wxBitmap bmp;
    wxString prefix = ConfigManager::GetDataFolder() + wxT("/images/codecompletion/");
    // bitmaps must be added by order of PARSER_IMG_* consts (which are also TokenCategory enums)
    const char* imgs[] =
    {
        "class_folder.png",        // PARSER_IMG_CLASS_FOLDER
        "class.png",               // PARSER_IMG_CLASS
        "class_private.png",       // PARSER_IMG_CLASS_PRIVATE
        "class_protected.png",     // PARSER_IMG_CLASS_PROTECTED
        "class_public.png",        // PARSER_IMG_CLASS_PUBLIC
        "ctor_private.png",        // PARSER_IMG_CTOR_PRIVATE
        "ctor_protected.png",      // PARSER_IMG_CTOR_PROTECTED
        "ctor_public.png",         // PARSER_IMG_CTOR_PUBLIC
        "dtor_private.png",        // PARSER_IMG_DTOR_PRIVATE
        "dtor_protected.png",      // PARSER_IMG_DTOR_PROTECTED
        "dtor_public.png",         // PARSER_IMG_DTOR_PUBLIC
        "method_private.png",      // PARSER_IMG_FUNC_PRIVATE
        "method_protected.png",    // PARSER_IMG_FUNC_PRIVATE
        "method_public.png",       // PARSER_IMG_FUNC_PUBLIC
        "var_private.png",         // PARSER_IMG_VAR_PRIVATE
        "var_protected.png",       // PARSER_IMG_VAR_PROTECTED
        "var_public.png",          // PARSER_IMG_VAR_PUBLIC
        "macro_def.png",           // PARSER_IMG_MACRO_DEF
        "enum.png",                // PARSER_IMG_ENUM
        "enum_private.png",        // PARSER_IMG_ENUM_PRIVATE
        "enum_protected.png",      // PARSER_IMG_ENUM_PROTECTED
        "enum_public.png",         // PARSER_IMG_ENUM_PUBLIC
        "enumerator.png",          // PARSER_IMG_ENUMERATOR
        "namespace.png",           // PARSER_IMG_NAMESPACE
        "typedef.png",             // PARSER_IMG_TYPEDEF
        "typedef_private.png",     // PARSER_IMG_TYPEDEF_PRIVATE
        "typedef_protected.png",   // PARSER_IMG_TYPEDEF_PROTECTED
        "typedef_public.png",      // PARSER_IMG_TYPEDEF_PUBLIC
        "symbols_folder.png",      // PARSER_IMG_SYMBOLS_FOLDER
        "vars_folder.png",         // PARSER_IMG_VARS_FOLDER
        "funcs_folder.png",        // PARSER_IMG_FUNCS_FOLDER
        "enums_folder.png",        // PARSER_IMG_ENUMS_FOLDER
        "macro_def_folder.png",    // PARSER_IMG_MACRO_DEF_FOLDER
        "others_folder.png",       // PARSER_IMG_OTHERS_FOLDER
        "typedefs_folder.png",     // PARSER_IMG_TYPEDEF_FOLDER
        "macro_use.png",           // PARSER_IMG_MACRO_USE
        "macro_use_private.png",   // PARSER_IMG_MACRO_USE_PRIVATE
        "macro_use_protected.png", // PARSER_IMG_MACRO_USE_PROTECTED
        "macro_use_public.png",    // PARSER_IMG_MACRO_USE_PUBLIC
        "macro_use_folder.png",    // PARSER_IMG_MACRO_USE_FOLDER
        "cpp_lang.png",            // tcLangKeyword
        nullptr
    };
    for (const char** itr = imgs; *itr; ++itr)
        m_ImageList.Add(cbLoadBitmap(prefix + wxString::FromUTF8(*itr), wxBITMAP_TYPE_PNG));

    EditorColourSet* theme = Manager::Get()->GetEditorManager()->GetColourSet();
    wxStringTokenizer tokenizer(theme->GetKeywords(theme->GetHighlightLanguage(wxT("C/C++")), 0));
    while (tokenizer.HasMoreTokens())
        m_CppKeywords.push_back(tokenizer.GetNextToken());
    std::sort(m_CppKeywords.begin(), m_CppKeywords.end());
    wxStringVec(m_CppKeywords).swap(m_CppKeywords);

    typedef cbEventFunctor<ClangPlugin, CodeBlocksEvent> ClEvent;
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_OPEN,      new ClEvent(this, &ClangPlugin::OnEditorOpen));
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_ACTIVATED, new ClEvent(this, &ClangPlugin::OnEditorActivate));
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_SAVE,      new ClEvent(this, &ClangPlugin::OnEditorSave));
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_CLOSE,     new ClEvent(this, &ClangPlugin::OnEditorClose));
    Manager::Get()->RegisterEventSink(cbEVT_PROJECT_ACTIVATE, new ClEvent(this, &ClangPlugin::OnProjectActivate));
    Manager::Get()->RegisterEventSink(cbEVT_PROJECT_FILE_CHANGED, new ClEvent(this, &ClangPlugin::OnProjectFileChanged));
    Manager::Get()->RegisterEventSink(cbEVT_PROJECT_OPTIONS_CHANGED, new ClEvent(this, &ClangPlugin::OnProjectOptionsChanged));
    Manager::Get()->RegisterEventSink(cbEVT_PROJECT_CLOSE,    new ClEvent(this, &ClangPlugin::OnProjectClose));
    //Connect(idEdOpenTimer,        wxEVT_TIMER, wxTimerEventHandler(ClangPlugin::OnTimer));
    Connect(idReparseTimer,       wxEVT_TIMER, wxTimerEventHandler(ClangPlugin::OnTimer));
    Connect(idDiagnosticTimer,    wxEVT_TIMER, wxTimerEventHandler(ClangPlugin::OnTimer));
    Connect(idHightlightTimer,      wxEVT_TIMER, wxTimerEventHandler(ClangPlugin::OnTimer));
    Connect(idGotoDeclaration,      wxEVT_COMMAND_MENU_SELECTED, /*wxMenuEventHandler*/wxCommandEventHandler(ClangPlugin::OnGotoDeclaration), nullptr, this);
    Connect(idReparse,              cbEVT_COMMAND_REPARSE, wxCommandEventHandler(ClangPlugin::OnReparse), nullptr, this);
    Connect(idDiagnoseEd,           cbEVT_COMMAND_DIAGNOSEED, wxCommandEventHandler(ClangPlugin::OnDiagnoseEd), nullptr, this);
    Connect(idClangCreateTU,        cbEVT_CLANG_CREATETU_FINISHED, wxEventHandler(ClangPlugin::OnClangCreateTUFinished), nullptr, this);
    Connect(idClangReparse,         cbEVT_CLANG_REPARSE_FINISHED, wxEventHandler(ClangPlugin::OnClangReparseFinished), nullptr, this);
    Connect(idClangGetDiagnostics,  cbEVT_CLANG_GETDIAGNOSTICS_FINISHED, wxEventHandler(ClangPlugin::OnClangGetDiagnosticsFinished), nullptr, this);
    Connect(idClangSyncTask, cbEVT_CLANG_SYNCTASK_FINISHED, wxEventHandler(ClangPlugin::OnClangSyncTaskFinished), nullptr, this);
    Connect(idClangCodeCompleteTask, cbEVT_CLANG_SYNCTASK_FINISHED, wxEventHandler(ClangPlugin::OnClangSyncTaskFinished), nullptr, this);

    m_EditorHookId = EditorHooks::RegisterHook(new EditorHooks::HookFunctor<ClangPlugin>(this, &ClangPlugin::OnEditorHook));
}

void ClangPlugin::OnRelease(bool WXUNUSED(appShutDown))
{
    EditorHooks::UnregisterHook(m_EditorHookId);
    Disconnect(idGotoDeclaration);
    Disconnect(idHightlightTimer);
    Disconnect(idReparseTimer);
    Disconnect(idDiagnosticTimer);
    Manager::Get()->RemoveAllEventSinksFor(this);
    m_ImageList.RemoveAll();
}

ClangPlugin::CCProviderStatus ClangPlugin::GetProviderStatusFor(cbEditor* ed)
{
    if (ed->GetLanguage() == ed->GetColourSet()->GetHighlightLanguage(wxT("C/C++")))
        return ccpsActive;
    return ccpsInactive;
}

struct PrioritySorter
{
    bool operator()(const ClangPlugin::CCToken& a, const ClangPlugin::CCToken& b)
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

std::vector<ClangPlugin::CCToken> ClangPlugin::GetAutocompList(bool isAuto, cbEditor* ed,
        int& tknStart, int& tknEnd)
{
#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    fprintf(stdout,"%s isAuto=%d\n", __PRETTY_FUNCTION__,(int)isAuto);
#endif
    std::vector<CCToken> tokens;

    int CCOutstanding = m_CCOutstanding;
    if ((CCOutstanding > 0)&&(m_CCOutstandingPos != ed->GetControl()->GetCurrentPos()))
    {
        CCOutstanding = 0;
    }

    m_CCOutstanding = 0;
    if (ed  != m_pLastEditor)
    {
        // Switching files
        return tokens;
    }
    if (m_TranslUnitId == wxNOT_FOUND)
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
    std::map<wxString, wxString> unsavedFiles;
    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    for (int i = 0; i < edMgr->GetEditorsCount(); ++i)
    {
        cbEditor* editor = edMgr->GetBuiltinEditor(i);
        if (editor && editor->GetModified())
            unsavedFiles.insert(std::make_pair(editor->GetFilename(), editor->GetControl()->GetText()));
    }
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
        ClangProxy::CodeCompleteAtJob job( cbEVT_CLANG_SYNCTASK_FINISHED, idClangCodeCompleteTask, isAuto, ed->GetFilename(), line+1, column+1, m_TranslUnitId, unsavedFiles);
        m_Proxy.AppendPendingJob(job);
        unsigned long timeout = 200;

        if (wxCOND_TIMEOUT == job.WaitCompletion(timeout))
        {
            if (wxGetLocalTime() - m_CCOutstandingLastMessageTime > 10)
            {
                InfoWindow::Display(_("Code completion"), _("Busy parsing the document"), 1000);
                m_CCOutstandingLastMessageTime = wxGetLocalTime();
            }
            //std::cout<<"Timeout waiting for code completion"<<std::endl;
            m_CCOutstanding++;
            m_CCOutstandingPos = ed->GetControl()->GetCurrentPos();
            m_CCOutstandingResults.clear();
            return tokens;
        }
        tknResults = job.GetResults();
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
                tokens.push_back(CCToken(tknIt->id, tknIt->name, tknIt->name, tknIt->weight, tknIt->category));
        }
    }
    else if (prefix.IsEmpty())
    {
        for (std::vector<ClToken>::const_iterator tknIt = tknResults.begin();
                tknIt != tknResults.end(); ++tknIt)
        {
            // it is rather unlikely for an operator to be the desired completion
            if (!tknIt->name.StartsWith(wxT("operator")) && (includeCtors || tknIt->category != tcCtorPublic))
                tokens.push_back(CCToken(tknIt->id, tknIt->name, tknIt->name, tknIt->weight, tknIt->category));
        }
    }
    else // smaller context, only allow matches of the prefix at the beginning of the token
    {
        for (std::vector<ClToken>::const_iterator tknIt = tknResults.begin();
                tknIt != tknResults.end(); ++tknIt)
        {
            if (tknIt->name.Lower().StartsWith(prefix) && (includeCtors || tknIt->category != tcCtorPublic))
                tokens.push_back(CCToken(tknIt->id, tknIt->name, tknIt->name, tknIt->weight, tknIt->category));
        }
    }

    if (!tokens.empty())
    {
        if (prefix.IsEmpty() && tokens.size() > 1500) // reduce to give only top matches
        {
            std::partial_sort(tokens.begin(), tokens.begin() + 1000, tokens.end(), PrioritySorter());
            tokens.erase(tokens.begin() + 1000, tokens.end());
        }
        const int imgCount = m_ImageList.GetImageCount();
        for (int i = 0; i < imgCount; ++i)
            stc->RegisterImage(i, m_ImageList.GetBitmap(i));
        bool isPP = stc->GetLine(line).Strip(wxString::leading).StartsWith(wxT("#"));
        std::set<int> usedWeights;
        for (std::vector<CCToken>::iterator tknIt = tokens.begin();
                tknIt != tokens.end(); ++tknIt)
        {
            usedWeights.insert(tknIt->weight);
            switch (tknIt->category)
            {
            case tcNone:
                if (isPP)
                    tknIt->category = tcMacroDef;
                else if (std::binary_search(m_CppKeywords.begin(), m_CppKeywords.end(), GetActualName(tknIt->name)))
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
            for (std::vector<CCToken>::iterator tknIt = tokens.begin();
                    tknIt != tokens.end(); ++tknIt)
            {
                tknIt->weight = weightCompr[tknIt->weight];
            }
        }
    }

    std::cout<<"CodeCompletion finished"<<std::endl;
    return tokens;
}

wxString ClangPlugin::GetDocumentation(const CCToken& token)
{
#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
    if (token.id >= 0)
        return m_Proxy.DocumentCCToken(m_TranslUnitId, token.id);
    return wxEmptyString;
}

std::vector<ClangPlugin::CCCallTip> ClangPlugin::GetCallTips(int pos, int /*style*/, cbEditor* ed, int& argsPos)
{
#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
    std::vector<CCCallTip> tips;

    if (ed != m_pLastEditor)
    {
        return tips;
    }
    if (m_TranslUnitId == wxNOT_FOUND)
    {
        std::cout<<"GetCallTips: No translUnitId yet"<<std::endl;
        return tips;
    }

    cbStyledTextCtrl* stc = ed->GetControl();

    int nest = 0;
    int commas = 0;
    while (--pos > 0)
    {
        const int curStyle = stc->GetStyleAt(pos);
        if (stc->IsString(curStyle)
                || stc->IsCharacter(curStyle)
                || stc->IsComment(curStyle))
        {
            continue;
        }

        const wxChar ch = stc->GetCharAt(pos);
        if (ch == wxT(';'))
        {
            std::cout<<"GetCalltips: Error?"<<std::endl;
            return tips; // error?
        }
        else if (ch == wxT(','))
        {
            if (nest == 0)
                ++commas;
        }
        else if (ch == wxT(')'))
            --nest;
        else if (ch == wxT('('))
        {
            ++nest;
            if (nest > 0)
                break;
        }
    }
    while (--pos > 0)
    {
        if (stc->GetCharAt(pos) <= wxT(' ')
                || stc->IsComment(stc->GetStyleAt(pos)))
        {
            continue;
        }
        break;
    }
    argsPos = stc->WordEndPosition(pos, true);
    if (argsPos != m_LastCallTipPos)
    {
        m_LastCallTips.clear();
        const int line = stc->LineFromPosition(pos);
        const int column = pos - stc->PositionFromLine(line);
        const wxString& tknText = stc->GetTextRange(stc->WordStartPosition(pos, true), argsPos);
        if (!tknText.IsEmpty())
        {
            ClangProxy::GetCallTipsAtJob job( cbEVT_CLANG_SYNCTASK_FINISHED, idClangSyncTask, ed->GetFilename(), line + 1, column + 1, m_TranslUnitId, tknText);
            m_Proxy.AppendPendingJob(job);
            if (job.WaitCompletion(200) == wxCOND_TIMEOUT)
            {
                fprintf(stdout,"GetCallTips: Timeout\n");
                return tips;
            }
            m_LastCallTips = job.GetResults();
            // m_Proxy.GetCallTipsAt(ed->GetFilename(), line + 1, column + 1,
            //           m_TranslUnitId, tknText, m_LastCallTips);
        }
    }
    m_LastCallTipPos = argsPos;
    for (std::vector<wxStringVec>::const_iterator strVecItr = m_LastCallTips.begin();
            strVecItr != m_LastCallTips.end(); ++strVecItr)
    {
        int strVecSz = strVecItr->size();
        if (commas != 0 && strVecSz < commas + 3)
            continue;
        wxString tip;
        int hlStart = wxSCI_INVALID_POSITION;
        int hlEnd = wxSCI_INVALID_POSITION;
        for (int i = 0; i < strVecSz; ++i)
        {
            if (i == commas + 1 && strVecSz > 2)
            {
                hlStart = tip.Length();
                hlEnd = hlStart + (*strVecItr)[i].Length();
            }
            tip += (*strVecItr)[i];
            if (i > 0 && i < (strVecSz - 2))
                tip += wxT(", ");
        }
        tips.push_back(CCCallTip(tip, hlStart, hlEnd));
    }

    return tips;
}

std::vector<ClangPlugin::CCToken> ClangPlugin::GetTokenAt(int pos, cbEditor* ed, bool& /*allowCallTip*/)
{
#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
    std::vector<CCToken> tokens;
    if (ed != m_pLastEditor)
    {
        return tokens;
    }
    if (m_TranslUnitId == wxNOT_FOUND)
    {
        return tokens;
    }

    cbStyledTextCtrl* stc = ed->GetControl();
    if (stc->GetTextRange(pos - 1, pos + 1).Strip().IsEmpty())
        return tokens;
    const int line = stc->LineFromPosition(pos);
    const int column = pos - stc->PositionFromLine(line);

    ClangProxy::GetTokensAtJob job( cbEVT_CLANG_SYNCTASK_FINISHED, idClangSyncTask, ed->GetFilename(), line + 1, column + 1, m_TranslUnitId);
    job.WaitCompletion(200);
    wxStringVec names = job.GetResults();
    for (wxStringVec::const_iterator nmIt = names.begin(); nmIt != names.end(); ++nmIt)
        tokens.push_back(CCToken(-1, *nmIt));
    return tokens;
}

wxString ClangPlugin::OnDocumentationLink(wxHtmlLinkEvent& /*event*/, bool& /*dismissPopup*/)
{
#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
    return wxEmptyString;
}

void ClangPlugin::DoAutocomplete(const CCToken& token, cbEditor* ed)
{
#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
    wxString tknText = token.name;
    int idx = tknText.Find(wxT(':'));
    if (idx != wxNOT_FOUND)
        tknText.Truncate(idx);
    std::pair<int, int> offsets = std::make_pair(0, 0);
    cbStyledTextCtrl* stc = ed->GetControl();
    wxString suffix = m_Proxy.GetCCInsertSuffix(m_TranslUnitId, token.id,
            GetEOLStr(stc->GetEOLMode())
            + ed->GetLineIndentString(stc->GetCurrentLine()),
            offsets);

    int pos = stc->GetCurrentPos();
    int startPos = std::min(stc->WordStartPosition(pos, true), std::min(stc->GetSelectionStart(),
            stc->GetSelectionEnd()));
    int moveToPos = startPos + tknText.Length();
    stc->SetTargetStart(startPos);
    int endPos = stc->WordEndPosition(pos, true);
    if (tknText.EndsWith(stc->GetTextRange(pos, endPos)))
    {
        if (!suffix.IsEmpty())
        {
            if (stc->GetCharAt(endPos) == (int)suffix[0])
            {
                if (suffix.Length() != 2 || stc->GetCharAt(endPos + 1) != (int)suffix[1])
                    offsets = std::make_pair(1, 1);
            }
            else
                tknText += suffix;
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
    stc->SetSelectionVoid(moveToPos + offsets.first, moveToPos + offsets.second);
    stc->ChooseCaretX();
    if (token.category != tcLangKeyword
            && (offsets.first != offsets.second || offsets.first == 1))
    {
        int tooltipMode = Manager::Get()->GetConfigManager(wxT("ccmanager"))->ReadInt(wxT("/tooltip_mode"), 1);
        if (tooltipMode != 3) // keybound only
        {
            CodeBlocksEvent evt(cbEVT_SHOW_CALL_TIP);
            Manager::Get()->ProcessEvent(evt);
        }
    }
}

void ClangPlugin::BuildMenu(wxMenuBar* menuBar)
{
    int idx = menuBar->FindMenu(_("Sea&rch"));
    if (idx != wxNOT_FOUND)
    {
        menuBar->GetMenu(idx)->Append(idGotoDeclaration, _("Find declaration (clang)"));
    }
}

void ClangPlugin::BuildModuleMenu(const ModuleType type, wxMenu* menu,
        const FileTreeData* WXUNUSED(data))
{
    if (type != mtEditorManager)
        return;
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
        return;
    if (ed != m_pLastEditor)
    {
        //m_TranslUnitId = m_Proxy.GetTranslationUnitId(ed->GetFilename());
        m_TranslUnitId = wxNOT_FOUND;
        m_pLastEditor = ed;
        m_ReparseNeeded = 0;
    }
    if (m_TranslUnitId == wxNOT_FOUND)
        return;
    cbStyledTextCtrl* stc = ed->GetControl();
    const int pos = stc->GetCurrentPos();
    if (stc->GetTextRange(pos - 1, pos + 1).Strip().IsEmpty())
        return;
    menu->Insert(0, idGotoDeclaration, _("Find declaration (clang)"));
}

bool ClangPlugin::BuildToolBar(wxToolBar* toolBar)
{
    // load the toolbar resource
    Manager::Get()->AddonToolBar(toolBar,_T("codecompletion_toolbar"));
    // get the wxChoice control pointers
    m_Function = XRCCTRL(*toolBar, "chcCodeCompletionFunction", wxChoice);
    m_Scope    = XRCCTRL(*toolBar, "chcCodeCompletionScope",    wxChoice);

    m_ToolBar = toolBar;

    // set the wxChoice and best toolbar size
    UpdateToolBar();

    // disable the wxChoices
    EnableToolbarTools(false);

    return true;
}


void ClangPlugin::OnEditorOpen(CodeBlocksEvent& event)
{
    event.Skip();
#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif

    return;
}

void ClangPlugin::OnEditorActivate(CodeBlocksEvent& event)
{
#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
    event.Skip();
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinEditor(event.GetEditor());
    if (ed)
    {
        if (ed != m_pLastEditor)
        {
            m_pLastEditor = ed;
            m_TranslUnitId = wxNOT_FOUND;
            m_ReparseNeeded = 0;
        }
        int reparseNeeded = UpdateCompileCommand(ed);
        if ((m_TranslUnitId == wxNOT_FOUND)||(reparseNeeded))
        {
            ClangProxy::CreateTranslationUnitJob job( cbEVT_CLANG_CREATETU_FINISHED, idClangCreateTU, ed->GetFilename(), m_CompileCommand );
            m_Proxy.AppendPendingJob(job);
            return;
        }
    }
}

void ClangPlugin::OnEditorSave(CodeBlocksEvent& event)
{
    event.Skip();
#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
    wxCommandEvent evt(cbEVT_COMMAND_DIAGNOSEED, idDiagnoseEd);
    AddPendingEvent(evt);
}

void ClangPlugin::OnEditorClose(CodeBlocksEvent& event)
{
    event.Skip();
#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
    int translId = m_TranslUnitId;
    EditorManager* edm = Manager::Get()->GetEditorManager();
    if (!edm)
    {
        event.Skip();
        return;
    }
    cbEditor* ed = edm->GetBuiltinEditor(event.GetEditor());
    if (ed)
    {
        if (ed != m_pLastEditor)
        {
            translId = m_Proxy.GetTranslationUnitId(m_TranslUnitId, event.GetEditor()->GetFilename() );
        }
    }
    ClangProxy::RemoveTranslationUnitJob job( cbEVT_CLANG_REMOVETU_FINISHED, idClangRemoveTU, translId);
    m_Proxy.AppendPendingJob(job);
    if (translId == m_TranslUnitId)
    {
        m_TranslUnitId = wxNOT_FOUND;
        m_ReparseNeeded = 0;
    }

    // we need to clear CC toolbar only when we are closing last editor
    // in other situations OnEditorActivated does this job
    // If no editors were opened, or a non-buildin-editor was active, disable the CC toolbar
    if (edm->GetEditorsCount() == 0 || !edm->GetActiveEditor() || !edm->GetActiveEditor()->IsBuiltinEditor())
    {
        EnableToolbarTools(false);

        // clear toolbar when closing last editor
        if (m_Scope)
            m_Scope->Clear();
        if (m_Function)
            m_Function->Clear();
    }
}

void ClangPlugin::OnProjectActivate(CodeBlocksEvent& event)
{
    event.Skip();
#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
    return;
}

void ClangPlugin::OnProjectOptionsChanged(CodeBlocksEvent& event)
{
    event.Skip();
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinEditor(event.GetEditor());
    if (ed)
    {
        int compileCommandChanged = UpdateCompileCommand(ed);
        if (compileCommandChanged)
        {
            std::cout<<"OnProjectOptionsChanged: Calling reparse (compile command changed)"<<std::endl;
            RequestReparse();
        }
    }
}
void ClangPlugin::OnProjectClose(CodeBlocksEvent& event)
{
    fprintf(stdout, "%s\n",__PRETTY_FUNCTION__);
}
void ClangPlugin::OnProjectFileChanged(CodeBlocksEvent& event)
{
#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
    event.Skip();
    if (IsAttached())
    {
        RequestReparse();
    }
}

void ClangPlugin::OnGotoDeclaration(wxCommandEvent& WXUNUSED(event))
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed || m_TranslUnitId == wxNOT_FOUND)
        return;
    cbStyledTextCtrl* stc = ed->GetControl();
    const int pos = stc->GetCurrentPos();
    wxString filename = ed->GetFilename();
    int line = stc->LineFromPosition(pos);
    int column = pos - stc->PositionFromLine(line) + 1;
    if (stc->GetLine(line).StartsWith(wxT("#include")))
        column = 2;
    ++line;
    m_Proxy.ResolveDeclTokenAt(filename, line, column, m_TranslUnitId);
    ed = Manager::Get()->GetEditorManager()->Open(filename);
    if (ed)
        ed->GotoTokenPosition(line - 1, stc->GetTextRange(stc->WordStartPosition(pos, true),
                stc->WordEndPosition(pos, true)));
}

wxString ClangPlugin::GetCompilerInclDirs(const wxString& compId)
{
    std::map<wxString, wxString>::const_iterator idItr = m_compInclDirs.find(compId);
    if (idItr != m_compInclDirs.end())
        return idItr->second;

    Compiler* comp = CompilerFactory::GetCompiler(compId);
    wxFileName fn(wxEmptyString, comp->GetPrograms().CPP);
    wxString masterPath = comp->GetMasterPath();
    Manager::Get()->GetMacrosManager()->ReplaceMacros(masterPath);
    fn.SetPath(masterPath);
    if (!fn.FileExists())
        fn.AppendDir(wxT("bin"));
#ifdef __WXMSW__
    wxString command = fn.GetFullPath() + wxT(" -v -E -x c++ nul");
#else
    wxString command = fn.GetFullPath() + wxT(" -v -E -x c++ /dev/null");
#endif // __WXMSW__
    wxArrayString output, errors;
    wxExecute(command, output, errors, wxEXEC_NODISABLE);

    wxArrayString::const_iterator errItr = errors.begin();
    for (; errItr != errors.end(); ++errItr)
    {
        if (errItr->IsSameAs(wxT("#include <...> search starts here:")))
        {
            ++errItr;
            break;
        }
    }
    wxString includeDirs;
    for (; errItr != errors.end(); ++errItr)
    {
        if (errItr->IsSameAs(wxT("End of search list.")))
            break;
        includeDirs += wxT(" -I") + errItr->Strip(wxString::both);
    }
    return m_compInclDirs.insert(std::pair<wxString, wxString>(compId, includeDirs)).first->second;
}

wxString ClangPlugin::GetSourceOf(cbEditor* ed)
{
    cbProject* project = nullptr;
    ProjectFile* opf = ed->GetProjectFile();
    if (opf)
        project = opf->GetParentProject();
    if (!project)
        project = Manager::Get()->GetProjectManager()->GetActiveProject();

    wxFileName theFile(ed->GetFilename());
    wxFileName candidateFile;
    bool isCandidate;
    wxArrayString fileArray;
    wxDir::GetAllFiles(theFile.GetPath(wxPATH_GET_VOLUME), &fileArray,
            theFile.GetName() + wxT(".*"), wxDIR_FILES | wxDIR_HIDDEN);
    wxFileName currentCandidateFile = FindSourceIn(fileArray, theFile, isCandidate);
    if (isCandidate)
        candidateFile = currentCandidateFile;
    else if (currentCandidateFile.IsOk())
        return currentCandidateFile.GetFullPath();

    fileArray.Clear();
    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    for (int i = 0; i < edMgr->GetEditorsCount(); ++i)
    {
        cbEditor* edit = edMgr->GetBuiltinEditor(i);
        if (!edit)
            continue;

        ProjectFile* pf = edit->GetProjectFile();
        if (!pf)
            continue;

        fileArray.Add(pf->file.GetFullPath());
    }
    currentCandidateFile = FindSourceIn(fileArray, theFile, isCandidate);
    if (!isCandidate && currentCandidateFile.IsOk())
        return currentCandidateFile.GetFullPath();

    if (project)
    {
        fileArray.Clear();
        for (FilesList::const_iterator it = project->GetFilesList().begin();
                it != project->GetFilesList().end(); ++it)
        {
            ProjectFile* pf = *it;
            if (!pf)
                continue;

            fileArray.Add(pf->file.GetFullPath());
        }
        currentCandidateFile = FindSourceIn(fileArray, theFile, isCandidate);
        if (isCandidate && !candidateFile.IsOk())
            candidateFile = currentCandidateFile;
        else if (currentCandidateFile.IsOk())
            return currentCandidateFile.GetFullPath();

        wxArrayString dirs = project->GetIncludeDirs();
        for (int i = 0; i < project->GetBuildTargetsCount(); ++i)
        {
            ProjectBuildTarget* target = project->GetBuildTarget(i);
            if (target)
            {
                for (size_t ti = 0; ti < target->GetIncludeDirs().GetCount(); ++ti)
                {
                    wxString dir = target->GetIncludeDirs()[ti];
                    if (dirs.Index(dir) == wxNOT_FOUND)
                        dirs.Add(dir);
                }
            }
        }
        for (size_t i = 0; i < dirs.GetCount(); ++i)
        {
            wxString dir = dirs[i];
            Manager::Get()->GetMacrosManager()->ReplaceMacros(dir);
            wxFileName dname(dir);
            if (!dname.IsAbsolute())
                dname.Normalize(wxPATH_NORM_ALL & ~wxPATH_NORM_CASE, project->GetBasePath());
            fileArray.Clear();
            wxDir::GetAllFiles(dname.GetPath(), &fileArray, theFile.GetName() + wxT(".*"), wxDIR_FILES | wxDIR_HIDDEN);
            currentCandidateFile = FindSourceIn(fileArray, theFile, isCandidate);
            if (isCandidate)
                candidateFile = currentCandidateFile;
            else if (currentCandidateFile.IsOk())
                return currentCandidateFile.GetFullPath();
        }
    }
    if (candidateFile.IsOk())
        return candidateFile.GetFullPath();
    return wxEmptyString;
}

wxFileName ClangPlugin::FindSourceIn(const wxArrayString& candidateFilesArray,
        const wxFileName& activeFile, bool& isCandidate)
{
    bool extStartsWithCapital = wxIsupper(activeFile.GetExt()[0]);
    wxFileName candidateFile;
    for (size_t i = 0; i < candidateFilesArray.GetCount(); ++i)
    {
        wxFileName currentCandidateFile(candidateFilesArray[i]);
        if (IsSourceOf(currentCandidateFile, activeFile, isCandidate))
        {
            bool isUpper = wxIsupper(currentCandidateFile.GetExt()[0]);
            if (isUpper == extStartsWithCapital && !isCandidate)
                return currentCandidateFile;
            else
                candidateFile = currentCandidateFile;
        }
    }
    isCandidate = true;
    return candidateFile;
}

bool ClangPlugin::IsSourceOf(const wxFileName& candidateFile,
        const wxFileName& activeFile, bool& isCandidate)
{
    if (candidateFile.GetName().CmpNoCase(activeFile.GetName()) == 0)
    {
        isCandidate = (candidateFile.GetName() != activeFile.GetName());
        if (FileTypeOf(candidateFile.GetFullName()) == ftSource)
        {
            if (candidateFile.GetPath() != activeFile.GetPath())
            {
                wxArrayString fileArray;
                wxDir::GetAllFiles(candidateFile.GetPath(wxPATH_GET_VOLUME), &fileArray,
                        candidateFile.GetName() + wxT(".*"), wxDIR_FILES | wxDIR_HIDDEN);
                for (size_t i = 0; i < fileArray.GetCount(); ++i)
                    if (wxFileName(fileArray[i]).GetFullName() == activeFile.GetFullName())
                        return false;
            }
            return candidateFile.FileExists();
        }
    }
    return false;
}

// Don't call this function from within the scope of:
//      ClangPlugin::OnEditorHook
//      ClangPlugin::OnTimer
int ClangPlugin::UpdateCompileCommand(cbEditor* ed)
{
    wxString compileCommand;
    ProjectFile* pf = ed->GetProjectFile();

    m_UpdateCompileCommand++;
    if( m_UpdateCompileCommand > 1 )
    {
        // Re-entry is not allowed
        m_UpdateCompileCommand--;
        return 0;
    }

    ProjectBuildTarget* target = nullptr;
    Compiler* comp = nullptr;
    if (pf && pf->GetParentProject() && !pf->GetBuildTargets().IsEmpty())
    {
        target = pf->GetParentProject()->GetBuildTarget(pf->GetBuildTargets()[0]);
        comp = CompilerFactory::GetCompiler(target->GetCompilerID());
    }
    cbProject* proj = (pf ? pf->GetParentProject() : nullptr);
    if ( (!comp) && proj)
        comp = CompilerFactory::GetCompiler(proj->GetCompilerID());
    if (!comp)
    {
        cbProject* tmpPrj = Manager::Get()->GetProjectManager()->GetActiveProject();
        if (tmpPrj)
            comp = CompilerFactory::GetCompiler(tmpPrj->GetCompilerID());
    }
    if (!comp)
        comp = CompilerFactory::GetDefaultCompiler();

    if (pf && (!pf->GetBuildTargets().IsEmpty()) )
    {
        target = pf->GetParentProject()->GetBuildTarget(pf->GetBuildTargets()[0]);

        if (pf->GetUseCustomBuildCommand(target->GetCompilerID() ))
            compileCommand = pf->GetCustomBuildCommand(target->GetCompilerID()).AfterFirst(wxT(' '));

    }

    if (compileCommand.IsEmpty())
        compileCommand = wxT("$options $includes");
    CompilerCommandGenerator* gen = comp->GetCommandGenerator(proj);
    if (gen)
        gen->GenerateCommandLine(compileCommand, target, pf, ed->GetFilename(),
                g_InvalidStr, g_InvalidStr, g_InvalidStr );
    delete gen;

    wxStringTokenizer tokenizer(compileCommand);
    compileCommand.Empty();
    wxString pathStr;
    while (tokenizer.HasMoreTokens())
    {
        wxString flag = tokenizer.GetNextToken();
        // make all include paths absolute, so clang does not choke if Code::Blocks switches directories
        if (flag.StartsWith(wxT("-I"), &pathStr))
        {
            wxFileName path(pathStr);
            if (path.Normalize(wxPATH_NORM_ALL & ~wxPATH_NORM_CASE))
                flag = wxT("-I") + path.GetFullPath();
        }
        compileCommand += flag + wxT(" ");
    }
    compileCommand += GetCompilerInclDirs(comp->GetID());

    m_UpdateCompileCommand--;

    if (compileCommand != m_CompileCommand)
    {
        m_CompileCommand = compileCommand;
        return 1;
    }
    return 0;
}

void ClangPlugin::UpdateToolBar()
{
    bool showScope = Manager::Get()->GetConfigManager(_T("code_completion"))->ReadBool(_T("/scope_filter"), true);

    if (showScope && !m_Scope)
    {
        m_Scope = new wxChoice(m_ToolBar, wxNewId(), wxPoint(0, 0), wxSize(280, -1), 0, 0);
        m_ToolBar->InsertControl(0, m_Scope);
    }
    else if (!showScope && m_Scope)
    {
        m_ToolBar->DeleteTool(m_Scope->GetId());
        m_Scope = NULL;
    }
    else
        return;

    m_ToolBar->Realize();
    m_ToolBar->SetInitialSize();
}

void ClangPlugin::EnableToolbarTools(bool enable)
{
    if (m_Scope)
        m_Scope->Enable(enable);
    if (m_Function)
        m_Function->Enable(enable);
}

void ClangPlugin::OnTimer(wxTimerEvent& event)
{
    if (!IsAttached())
    {
        return;
    }
    const int evId = event.GetId();

    if (evId == idReparseTimer) // m_ReparseTimer
    {
        wxCommandEvent evt(cbEVT_COMMAND_REPARSE, idReparse);
        AddPendingEvent(evt);
    }
    else if (evId == idHightlightTimer)
    {
        cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
        if (!ed)
        {
            return;
        }
        if (ed != m_pLastEditor)
        {
            return;
        }
        //    m_TranslUnitId = m_Proxy.GetTranslationUnitId(ed->GetFilename());
        //    m_pLastEditor = ed;
        //}
        if (m_TranslUnitId == wxNOT_FOUND)
        {
            return;
        }
        HighlightOccurrences(ed);
    }
    else if (evId == idDiagnosticTimer)
    {
        wxCommandEvent evt(cbEVT_COMMAND_DIAGNOSEED, idDiagnoseEd);
        AddPendingEvent(evt);
    }
    else
    {
        event.Skip();
    }
}

void ClangPlugin::OnClangCreateTUFinished( wxEvent& event )
{
#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif

    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
    {
        fprintf(stdout,"%s No editor ???\n", __PRETTY_FUNCTION__);
        return;
    }

    ClangProxy::CreateTranslationUnitJob* obj = static_cast<ClangProxy::CreateTranslationUnitJob*>(event.GetEventObject());
    if (!obj)
    {
        fprintf(stdout,"%s No passed job ???\n", __PRETTY_FUNCTION__);
        return;
    }
    if (obj->m_Filename != ed->GetFilename())
    {
        fprintf(stdout,"%s: Filename mismatch, probably another file was loaded while we were busy parsing. TU filename: '%s', current filename: '%s'\n", __PRETTY_FUNCTION__, (const char*)obj->m_Filename.mb_str(), (const char*)ed->GetFilename().mb_str());
        return;
    }
    m_TranslUnitId = obj->m_TranslationUnitId;
    m_pLastEditor = ed;
    std::cout<<"New translation unit: "<<m_TranslUnitId<<std::endl;
}

void ClangPlugin::OnReparse( wxCommandEvent& /*event*/ )
{
#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif

    m_DiagnosticTimer.Stop();

    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    cbEditor* ed = edMgr->GetBuiltinActiveEditor();
    if (!ed)
    {
        std::cout<<"No editor..."<<std::endl;
        return;
    }

    m_ReparseNeeded = 0;

    if (ed != m_pLastEditor)
    {
        //m_TranslUnitId = m_Proxy.GetTranslationUnitId(ed->GetFilename());
        m_TranslUnitId = wxNOT_FOUND;
        m_pLastEditor = ed;
        std::cout<<"Reparse: TranslUnitId set to "<<m_TranslUnitId<<std::endl;
    }
    if (m_TranslUnitId == wxNOT_FOUND)
    {
        std::cout<<"Translation unit not found: "<<m_TranslUnitId<<" file="<<(const char*)ed->GetFilename().c_str()<<std::endl;
        return;
    }

    //std::cout<<"Reparsing with translUnitId "<<m_TranslUnitId<<std::endl;
    std::map<wxString, wxString> unsavedFiles;
    for (int i = 0; i < edMgr->GetEditorsCount(); ++i)
    {
        ed = edMgr->GetBuiltinEditor(i);
        if (ed && ed->GetModified())
            unsavedFiles.insert(std::make_pair(ed->GetFilename(), ed->GetControl()->GetText()));
    }
    ClangProxy::ReparseJob job( cbEVT_CLANG_REPARSE_FINISHED, idClangReparse, m_TranslUnitId, m_CompileCommand, ed->GetFilename(), unsavedFiles);
    m_Proxy.AppendPendingJob(job);
    m_ReparseBusy++;
}

void ClangPlugin::OnClangReparseFinished( wxEvent& event )
{
#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
    m_ReparseBusy--;
    ClangProxy::ReparseJob* pJob = static_cast<ClangProxy::ReparseJob*>(event.GetEventObject());
    if (pJob->m_TranslId != this->m_TranslUnitId)
    {
        std::cout<<" Reparse finished but another file was loaded"<<std::endl;
        return;
    }
    if (m_ReparseNeeded)
    {
        fprintf(stdout,"%s: Reparsing again, was modified while we were busy...\n", __PRETTY_FUNCTION__);
        wxCommandEvent evt(cbEVT_COMMAND_REPARSE, idReparse);
        AddPendingEvent(evt);
    }
    else
    {
        m_DiagnosticTimer.Stop();
        m_DiagnosticTimer.Start(DIAGNOSTIC_DELAY, wxTIMER_ONE_SHOT);
    }

}

void ClangPlugin::OnEditorHook(cbEditor* ed, wxScintillaEvent& event)
{
    event.Skip();
    bool clearIndicator = false;
    bool reparse = false;
    if (!IsProviderFor(ed))
        return;
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
        if (event.GetUpdated() & wxSCI_UPDATE_SELECTION)
        {
            m_HightlightTimer.Stop();
            m_HightlightTimer.Start(HIGHTLIGHT_DELAY, wxTIMER_ONE_SHOT);
        }
        clearIndicator = true;
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

void ClangPlugin::OnDiagnoseEd( wxCommandEvent& /*event*/ )
{
#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    //fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif

    if (m_ReparseNeeded > 0)
    {
        // Diagnostics will be requested again after reparse
        fprintf(stdout,"%s: Reparse needed... (%d)\n", __PRETTY_FUNCTION__, m_ReparseNeeded);
        m_ReparseTimer.Stop();
        wxCommandEvent evt(cbEVT_COMMAND_REPARSE, idReparse);
        AddPendingEvent(evt);
        return;
    }
    if (m_ReparseBusy > 0)
    {
        fprintf(stdout,"%s: Reparse busy (%d)...\n", __PRETTY_FUNCTION__, m_ReparseBusy);
        return;
    }
    ClangProxy::GetDiagnosticsJob job( cbEVT_CLANG_GETDIAGNOSTICS_FINISHED, idClangGetDiagnostics, m_TranslUnitId );
    m_Proxy.AppendPendingJob(job);
}


void ClangPlugin::OnClangGetDiagnosticsFinished( wxEvent& event )
{
//#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
//#endif
    DiagnosticLevel diagLv = dlFull; // TODO
    ClangProxy::GetDiagnosticsJob* pJob = static_cast<ClangProxy::GetDiagnosticsJob*>(event.GetEventObject());

    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    cbEditor* ed = edMgr->GetBuiltinActiveEditor();
    if (!ed)
    {
        std::cout<<"No editor..."<<std::endl;
        return;
    }
    if ((pJob->m_TranslId != m_TranslUnitId)||(ed != m_pLastEditor))
    {
        // No longer the current editor
        std::cout<<"Diagnostics requested but another file was loaded..."<<std::endl;
        return;
    }
    cbStyledTextCtrl* stc = ed->GetControl();
    if (diagLv == dlFull)
        stc->AnnotationClearAll();
    const int warningIndicator = 0; // predefined
    const int errorIndicator = 15; // hopefully we do not clash with someone else...
    stc->SetIndicatorCurrent(warningIndicator);
    stc->IndicatorClearRange(0, stc->GetLength());
    stc->IndicatorSetStyle(errorIndicator, wxSCI_INDIC_SQUIGGLE);
    stc->IndicatorSetForeground(errorIndicator, *wxRED);
    stc->SetIndicatorCurrent(errorIndicator);
    stc->IndicatorClearRange(0, stc->GetLength());
    const wxString& fileNm = ed->GetFilename();
    for ( std::vector<ClDiagnostic>::const_iterator dgItr = pJob->m_Diagnostics.begin();
            dgItr != pJob->m_Diagnostics.end(); ++dgItr )
    {
        //Manager::Get()->GetLogManager()->Log(dgItr->file + wxT(" ") + dgItr->message + F(wxT(" %d, %d"), dgItr->range.first, dgItr->range.second));
        if (dgItr->file != fileNm)
            continue;
        if (diagLv == dlFull)
        {
            wxString str = stc->AnnotationGetText(dgItr->line - 1);
            if (!str.IsEmpty())
                str += wxT('\n');
            stc->AnnotationSetText(dgItr->line - 1, str + dgItr->message);
            stc->AnnotationSetStyle(dgItr->line - 1, 50);
        }
        int pos = stc->PositionFromLine(dgItr->line - 1) + dgItr->range.first - 1;
        int range = dgItr->range.second - dgItr->range.first;
        if (range == 0)
        {
            range = stc->WordEndPosition(pos, true) - pos;
            if (range == 0)
            {
                pos = stc->WordStartPosition(pos, true);
                range = stc->WordEndPosition(pos, true) - pos;
            }
        }
        if (dgItr->severity == sError)
            stc->SetIndicatorCurrent(errorIndicator);
        else if (  dgItr != pJob->m_Diagnostics.begin()
                && dgItr->line == (dgItr - 1)->line
                && dgItr->range.first <= (dgItr - 1)->range.second )
        {
            continue; // do not overwrite the last indicator
        }
        else
            stc->SetIndicatorCurrent(warningIndicator);
        stc->IndicatorFillRange(pos, range);
    }
    if (diagLv == dlFull)
        stc->AnnotationSetVisible(wxSCI_ANNOTATION_BOXED);
}

void ClangPlugin::OnClangSyncTaskFinished( wxEvent& event )
{
#ifdef CLANGPLUGIN_TRACE_FUNCTIONS
    fprintf(stdout,"%s (m_CCOutstanding=%d)\n", __PRETTY_FUNCTION__, (int)m_CCOutstanding);
#endif
    ClangProxy::SyncJob* pJob = static_cast<ClangProxy::SyncJob*>(event.GetEventObject());

    if (event.GetId() == idClangCodeCompleteTask)
    {
        ClangProxy::CodeCompleteAtJob* pCCJob = dynamic_cast<ClangProxy::CodeCompleteAtJob*>(pJob);
        if (m_CCOutstanding > 0)
        {
            EditorManager* edMgr = Manager::Get()->GetEditorManager();
            cbEditor* ed = edMgr->GetBuiltinActiveEditor();
            if (ed)
            {
                if (ed->GetControl()->GetCurrentPos() == m_CCOutstandingPos)
                {
                    m_CCOutstandingResults = pCCJob->GetResults();
                    if( m_CCOutstandingResults.size() > 0 )
                    {
                        CodeBlocksEvent evt(cbEVT_COMPLETE_CODE);
                        Manager::Get()->ProcessEvent(evt);
                    }
                }
            }
        }
    }

    pJob->Finalize();

    m_DiagnosticTimer.Stop();
    m_DiagnosticTimer.Start(DIAGNOSTIC_DELAY, wxTIMER_ONE_SHOT);
}

void ClangPlugin::HighlightOccurrences(cbEditor* ed)
{
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
    const int column = pos - stc->PositionFromLine(line);
    ClangProxy::GetOccurrencesOfJob job(cbEVT_CLANG_SYNCTASK_FINISHED, idClangSyncTask, ed->GetFilename(), line + 1, column + 1, m_TranslUnitId);
    m_Proxy.AppendPendingJob(job);
    if (job.WaitCompletion(200) == wxCOND_TIMEOUT)
    {
        return;
    }
    std::vector< std::pair<int, int> > occurrences = job.GetResults();

    //std::vector< std::pair<int, int> > occurrences;
    //m_Proxy.GetOccurrencesOf(ed->GetFilename(), line + 1, column + 1, m_TranslUnitId, occurrences);
    for (std::vector< std::pair<int, int> >::const_iterator tkn = occurrences.begin();
            tkn != occurrences.end(); ++tkn)
    {
        stc->IndicatorFillRange(tkn->first, tkn->second);
    }
}

void ClangPlugin::RequestReparse()
{
    m_ReparseNeeded++;
    m_ReparseTimer.Start(REPARSE_DELAY, wxTIMER_ONE_SHOT);
    m_DiagnosticTimer.Stop();
    m_DiagnosticTimer.Start(DIAGNOSTIC_DELAY, wxTIMER_ONE_SHOT);
}
