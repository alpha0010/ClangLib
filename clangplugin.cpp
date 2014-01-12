/*
 * A clang based plugin
 */

#include "clangplugin.h"

#include <sdk.h>
#include <compilercommandgenerator.h>
#include <cbstyledtextctrl.h>

#include <wx/tokenzr.h>

#ifndef CB_PRECOMP
    #include <cbeditor.h>
    #include <cbproject.h>
    #include <compilerfactory.h>
    #include <editorcolourset.h>
    #include <editormanager.h>
    #include <logmanager.h>
    #include <macrosmanager.h>
    #include <projectfile.h>
    #include <projectmanager.h>

    #include <wx/dir.h>
#endif // CB_PRECOMP

// this auto-registers the plugin
namespace
{
    PluginRegistrant<ClangPlugin> reg(wxT("ClangLib"));
}

static const wxString g_InvalidStr(wxT("invalid"));
const int idEdOpenTimer = wxNewId();

// milliseconds
#define ED_OPEN_DELAY 1000
#define ED_ACTIVATE_DELAY 150

ClangPlugin::ClangPlugin() :
    m_ImageList(16, 16),
    m_EdOpenTimer(this, idEdOpenTimer),
    m_pLastEditor(nullptr),
    m_TranslUnitId(wxNOT_FOUND)
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
    bmp = cbLoadBitmap(prefix + wxT("class_folder.png"),      wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_CLASS_FOLDER
    bmp = cbLoadBitmap(prefix + wxT("class.png"),             wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_CLASS
    bmp = cbLoadBitmap(prefix + wxT("class_private.png"),     wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_CLASS_PRIVATE
    bmp = cbLoadBitmap(prefix + wxT("class_protected.png"),   wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_CLASS_PROTECTED
    bmp = cbLoadBitmap(prefix + wxT("class_public.png"),      wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_CLASS_PUBLIC
    bmp = cbLoadBitmap(prefix + wxT("ctor_private.png"),      wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_CTOR_PRIVATE
    bmp = cbLoadBitmap(prefix + wxT("ctor_protected.png"),    wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_CTOR_PROTECTED
    bmp = cbLoadBitmap(prefix + wxT("ctor_public.png"),       wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_CTOR_PUBLIC
    bmp = cbLoadBitmap(prefix + wxT("dtor_private.png"),      wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_DTOR_PRIVATE
    bmp = cbLoadBitmap(prefix + wxT("dtor_protected.png"),    wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_DTOR_PROTECTED
    bmp = cbLoadBitmap(prefix + wxT("dtor_public.png"),       wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_DTOR_PUBLIC
    bmp = cbLoadBitmap(prefix + wxT("method_private.png"),    wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_FUNC_PRIVATE
    bmp = cbLoadBitmap(prefix + wxT("method_protected.png"),  wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_FUNC_PRIVATE
    bmp = cbLoadBitmap(prefix + wxT("method_public.png"),     wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_FUNC_PUBLIC
    bmp = cbLoadBitmap(prefix + wxT("var_private.png"),       wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_VAR_PRIVATE
    bmp = cbLoadBitmap(prefix + wxT("var_protected.png"),     wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_VAR_PROTECTED
    bmp = cbLoadBitmap(prefix + wxT("var_public.png"),        wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_VAR_PUBLIC
    bmp = cbLoadBitmap(prefix + wxT("preproc.png"),           wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_PREPROCESSOR
    bmp = cbLoadBitmap(prefix + wxT("enum.png"),              wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_ENUM
    bmp = cbLoadBitmap(prefix + wxT("enum_private.png"),      wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_ENUM_PRIVATE
    bmp = cbLoadBitmap(prefix + wxT("enum_protected.png"),    wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_ENUM_PROTECTED
    bmp = cbLoadBitmap(prefix + wxT("enum_public.png"),       wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_ENUM_PUBLIC
    bmp = cbLoadBitmap(prefix + wxT("enumerator.png"),        wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_ENUMERATOR
    bmp = cbLoadBitmap(prefix + wxT("namespace.png"),         wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_NAMESPACE
    bmp = cbLoadBitmap(prefix + wxT("typedef.png"),           wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_TYPEDEF
    bmp = cbLoadBitmap(prefix + wxT("typedef_private.png"),   wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_TYPEDEF_PRIVATE
    bmp = cbLoadBitmap(prefix + wxT("typedef_protected.png"), wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_TYPEDEF_PROTECTED
    bmp = cbLoadBitmap(prefix + wxT("typedef_public.png"),    wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_TYPEDEF_PUBLIC
    bmp = cbLoadBitmap(prefix + wxT("symbols_folder.png"),    wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_SYMBOLS_FOLDER
    bmp = cbLoadBitmap(prefix + wxT("vars_folder.png"),       wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_VARS_FOLDER
    bmp = cbLoadBitmap(prefix + wxT("funcs_folder.png"),      wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_FUNCS_FOLDER
    bmp = cbLoadBitmap(prefix + wxT("enums_folder.png"),      wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_ENUMS_FOLDER
    bmp = cbLoadBitmap(prefix + wxT("preproc_folder.png"),    wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_PREPROC_FOLDER
    bmp = cbLoadBitmap(prefix + wxT("others_folder.png"),     wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_OTHERS_FOLDER
    bmp = cbLoadBitmap(prefix + wxT("typedefs_folder.png"),   wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_TYPEDEF_FOLDER
    bmp = cbLoadBitmap(prefix + wxT("macro.png"),             wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_MACRO
    bmp = cbLoadBitmap(prefix + wxT("macro_private.png"),     wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_MACRO_PRIVATE
    bmp = cbLoadBitmap(prefix + wxT("macro_protected.png"),   wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_MACRO_PROTECTED
    bmp = cbLoadBitmap(prefix + wxT("macro_public.png"),      wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_MACRO_PUBLIC
    bmp = cbLoadBitmap(prefix + wxT("macro_folder.png"),      wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_MACRO_FOLDER
    bmp = cbLoadBitmap(prefix + wxT("cpp_lang.png"),          wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // tcLangKeyword

    typedef cbEventFunctor<ClangPlugin, CodeBlocksEvent> ClEvent;
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_OPEN,      new ClEvent(this, &ClangPlugin::OnEditorOpen));
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_ACTIVATED, new ClEvent(this, &ClangPlugin::OnEditorActivate));
    Connect(idEdOpenTimer, wxEVT_TIMER, wxTimerEventHandler(ClangPlugin::OnTimer));
}

void ClangPlugin::OnRelease(bool appShutDown)
{
    Disconnect(idEdOpenTimer);
    Manager::Get()->RemoveAllEventSinksFor(this);
    m_ImageList.RemoveAll();
}

ClangPlugin::CCProviderStatus ClangPlugin::GetProviderStatusFor(cbEditor* ed)
{
    if (ed->GetLanguage() == ed->GetColourSet()->GetHighlightLanguage(wxT("C/C++")))
        return ccpsActive;
    return ccpsInactive;
}

std::vector<ClangPlugin::CCToken> ClangPlugin::GetAutocompList(bool isAuto, cbEditor* ed, int& tknStart, int& tknEnd)
{
    std::vector<CCToken> tokens;
    if (ed != m_pLastEditor)
    {
        m_TranslUnitId = m_Proxy.GetTranslationUnitId(ed->GetFilename());
        m_pLastEditor = ed;
    }
    if (m_TranslUnitId == wxNOT_FOUND)
    {
        Manager::Get()->GetLogManager()->LogWarning(wxT("ClangLib: m_TranslUnitId == wxNOT_FOUND, cannot complete in file ") + ed->GetFilename());
        return tokens;
    }

    cbStyledTextCtrl* stc = ed->GetControl();
    const int style = stc->GetStyleAt(tknEnd);
    const wxChar curChar = stc->GetCharAt(tknEnd - 1);
    if (isAuto) // filter illogical cases of auto-launch
    {
        if (   (   curChar == wxT(':') // scope operator
                && stc->GetCharAt(tknEnd - 2) != wxT(':') )
            || (   curChar == wxT('>') // '->'
                && stc->GetCharAt(tknEnd - 2) != wxT('-') )
            || (   wxString(wxT("<\"/")).Find(curChar) != wxNOT_FOUND // #include directive
                && !stc->IsPreprocessor(style) ) )
        {
            return tokens;
        }
    }

    std::vector<ClToken> tknResults;
    const int line = stc->LineFromPosition(tknStart);
    std::map<wxString, wxString> unsavedFiles;
    if (ed->GetModified())
    {
        unsavedFiles.insert(std::make_pair(ed->GetFilename(), stc->GetText())); // current file
        // todo: add other editors (do we want to?)
    }
    m_Proxy.CodeCompleteAt(ed->GetFilename(), line + 1, tknStart - stc->PositionFromLine(line) + 1, m_TranslUnitId, unsavedFiles, tknResults);
    const wxString& prefix = stc->GetTextRange(tknStart, tknEnd).Lower();
    if (prefix.Length() > 3)
    {
        for (std::vector<ClToken>::const_iterator tknIt = tknResults.begin();
             tknIt != tknResults.end(); ++tknIt)
        {
            if (tknIt->name.Lower().Find(prefix) != wxNOT_FOUND)
                tokens.push_back(CCToken(tknIt->id, tknIt->name, tknIt->name, tknIt->weight, tknIt->category));
        }
    }
    else
    {
        for (std::vector<ClToken>::const_iterator tknIt = tknResults.begin();
             tknIt != tknResults.end(); ++tknIt)
        {
            if (tknIt->name.Lower().StartsWith(prefix))
                tokens.push_back(CCToken(tknIt->id, tknIt->name, tknIt->name, tknIt->weight, tknIt->category));
        }
    }

    if (!tokens.empty())
    {
        const int imgCount = m_ImageList.GetImageCount();
        for (int i = 0; i < imgCount; ++i)
            stc->RegisterImage(i, m_ImageList.GetBitmap(i));
        bool isPP = stc->GetLine(line).Strip(wxString::leading).StartsWith(wxT("#"));
        wxStringVec keywords;
        if (!isPP)
        {
            EditorColourSet* theme = ed->GetColourSet();
            wxStringTokenizer tokenizer(theme->GetKeywords(theme->GetHighlightLanguage(wxT("C/C++")), 0));
            while (tokenizer.HasMoreTokens())
                keywords.push_back(tokenizer.GetNextToken());
            std::sort(keywords.begin(), keywords.end());
        }
        for (std::vector<CCToken>::iterator tknIt = tokens.begin();
             tknIt != tokens.end(); ++tknIt)
        {
            if (tknIt->category != -1)
                continue;
            if (isPP)
                tknIt->category = tcPreprocessor;
            else if (std::binary_search(keywords.begin(), keywords.end(), tknIt->name))
                tknIt->category = tcLangKeyword;
        }
    }

    return tokens;
}

wxString ClangPlugin::GetDocumentation(const CCToken& token)
{
    return wxEmptyString;
}

wxStringVec ClangPlugin::GetCallTips(int pos, int style, cbEditor* ed, int& hlStart, int& hlEnd, int& argsPos)
{
    return wxStringVec();
}

std::vector<ClangPlugin::CCToken> ClangPlugin::GetTokenAt(int pos, cbEditor* ed)
{
    return std::vector<CCToken>();
}

wxString ClangPlugin::OnDocumentationLink(wxHtmlLinkEvent& event, bool& dismissPopup)
{
    return wxEmptyString;
}

void ClangPlugin::OnEditorOpen(CodeBlocksEvent& event)
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinEditor(event.GetEditor());
    if (ed)
        m_EdOpenTimer.Start(ED_OPEN_DELAY, wxTIMER_ONE_SHOT);
    event.Skip();
}

void ClangPlugin::OnEditorActivate(CodeBlocksEvent& event)
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinEditor(event.GetEditor());
    if (ed && !m_EdOpenTimer.IsRunning())
        m_EdOpenTimer.Start(ED_ACTIVATE_DELAY, wxTIMER_ONE_SHOT);
    event.Skip();
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
    wxDir::GetAllFiles(theFile.GetPath(wxPATH_GET_VOLUME), &fileArray, theFile.GetName() + wxT(".*"), wxDIR_FILES | wxDIR_HIDDEN);
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
        for (FilesList::const_iterator it = project->GetFilesList().begin(); it != project->GetFilesList().end(); ++it)
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

wxFileName ClangPlugin::FindSourceIn(const wxArrayString& candidateFilesArray, const wxFileName& activeFile, bool& isCandidate)
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

bool ClangPlugin::IsSourceOf(const wxFileName& candidateFile, const wxFileName& activeFile, bool& isCandidate)
{
    if (candidateFile.GetName().CmpNoCase(activeFile.GetName()) == 0)
    {
        isCandidate = (candidateFile.GetName() != activeFile.GetName());
        if (FileTypeOf(candidateFile.GetFullName()) == ftSource)
        {
            if (candidateFile.GetPath() != activeFile.GetPath())
            {
                wxArrayString fileArray;
                wxDir::GetAllFiles(candidateFile.GetPath(wxPATH_GET_VOLUME), &fileArray, candidateFile.GetName() + wxT(".*"), wxDIR_FILES | wxDIR_HIDDEN);
                for (size_t i = 0; i < fileArray.GetCount(); ++i)
                    if (wxFileName(fileArray[i]).GetFullName() == activeFile.GetFullName())
                        return false;
            }
            return candidateFile.FileExists();
        }
    }
    return false;
}

void ClangPlugin::OnTimer(wxTimerEvent& event)
{
    if (event.GetId() == idEdOpenTimer)
    {
        EditorManager* edMgr = Manager::Get()->GetEditorManager();
        cbEditor* ed = edMgr->GetBuiltinActiveEditor();
        if (  !ed || GetProviderStatusFor(ed) == ccpsInactive
            || m_Proxy.GetTranslationUnitId(ed->GetFilename()) != wxNOT_FOUND )
        {
            return;
        }
        wxString compileCommand;
        ProjectFile* pf = ed->GetProjectFile();
        ProjectBuildTarget* target = nullptr;
        Compiler* comp = nullptr;
        if (pf)
        {
            target = pf->GetParentProject()->GetBuildTarget(pf->GetbuildTargets()[0]);
            comp = CompilerFactory::GetCompiler(target->GetCompilerID());
            if (pf->GetUseCustomBuildCommand(target->GetCompilerID()))
            {
                compileCommand = pf->GetCustomBuildCommand(target->GetCompilerID()).AfterFirst(wxT(' '));
            }
        }
        if (compileCommand.IsEmpty())
            compileCommand = wxT("$options $includes");
        cbProject* proj = (pf ? pf->GetParentProject() : nullptr);
        if (!comp && proj)
            comp = CompilerFactory::GetCompiler(proj->GetCompilerID());
        if (!comp)
        {
            cbProject* tmpPrj = Manager::Get()->GetProjectManager()->GetActiveProject();
            if (tmpPrj)
                comp = CompilerFactory::GetCompiler(tmpPrj->GetCompilerID());
        }
        if (!comp)
            comp = CompilerFactory::GetDefaultCompiler();
        comp->GetCommandGenerator(proj)->GenerateCommandLine(compileCommand, target, pf, ed->GetFilename(),
                                                             g_InvalidStr, g_InvalidStr, g_InvalidStr );
        compileCommand += GetCompilerInclDirs(comp->GetID());
        if (FileTypeOf(ed->GetFilename()) == ftHeader) // try to find the associated source
        {
            const wxString& source = GetSourceOf(ed);
            if (!source.IsEmpty())
            {
                m_Proxy.CreateTranslationUnit(source, compileCommand);
                if (m_Proxy.GetTranslationUnitId(ed->GetFilename()) != wxNOT_FOUND)
                    return; // got it
            }
        }
        m_Proxy.CreateTranslationUnit(ed->GetFilename(), compileCommand);
    }
    else
        event.Skip();
}
