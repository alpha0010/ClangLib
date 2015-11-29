#include "clangtoolbar.h"
#include <cbcolourmanager.h>
#include <cbstyledtextctrl.h>
#include <compilercommandgenerator.h>
#include <editor_hooks.h>

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
#include <wx/tokenzr.h>
#include <wx/choice.h>
#endif // CB_PRECOMP


const int idToolbarUpdateSelection = wxNewId();
const int idToolbarUpdateContents = wxNewId();


DEFINE_EVENT_TYPE(clEVT_COMMAND_UPDATETOOLBARSELECTION)
DEFINE_EVENT_TYPE(clEVT_COMMAND_UPDATETOOLBARCONTENTS)

ClangToolbar::ClangToolbar() :
    ClangPluginComponent(),
    m_TranslUnitId(-1),
    m_EditorHookId(-1),
    m_CurrentEditorLine(-1),
    m_ToolBar(nullptr),
    m_Function(nullptr),
    m_Scope(nullptr)
{

}

ClangToolbar::~ClangToolbar()
{

}

void ClangToolbar::OnAttach(IClangPlugin* pClangPlugin)
{
    ClangPluginComponent::OnAttach(pClangPlugin);
    typedef cbEventFunctor<ClangToolbar, CodeBlocksEvent> ClToolbarEvent;
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_ACTIVATED, new ClToolbarEvent(this, &ClangToolbar::OnEditorActivate));
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_CLOSE,     new ClToolbarEvent(this, &ClangToolbar::OnEditorClose));

    typedef cbEventFunctor<ClangToolbar, ClangEvent> ClangToolbarEvent;
    pClangPlugin->RegisterEventSink(clEVT_TRANSLATIONUNIT_CREATED, new ClangToolbarEvent(this, &ClangToolbar::OnTranslationUnitCreated));
    pClangPlugin->RegisterEventSink(clEVT_REPARSE_FINISHED, new ClangToolbarEvent(this, &ClangToolbar::OnReparseFinished) );

    Connect(idToolbarUpdateSelection,clEVT_COMMAND_UPDATETOOLBARSELECTION, wxCommandEventHandler(ClangToolbar::OnUpdateSelection), nullptr, this );
    Connect(idToolbarUpdateContents, clEVT_COMMAND_UPDATETOOLBARCONTENTS, wxCommandEventHandler(ClangToolbar::OnUpdateContents), nullptr, this );
    m_EditorHookId = EditorHooks::RegisterHook(new EditorHooks::HookFunctor<ClangToolbar>(this, &ClangToolbar::OnEditorHook));

    wxCommandEvent evt(clEVT_COMMAND_UPDATETOOLBARCONTENTS, idToolbarUpdateContents);
    AddPendingEvent(evt);
    wxCommandEvent evt2(clEVT_COMMAND_UPDATETOOLBARSELECTION, idToolbarUpdateSelection);
    AddPendingEvent(evt2);

}

void ClangToolbar::OnRelease(IClangPlugin* pClangPlugin)
{
    ClangPluginComponent::OnRelease(pClangPlugin);
    EditorHooks::UnregisterHook(m_EditorHookId);
    Manager::Get()->RemoveAllEventSinksFor(this);

}

void ClangToolbar::OnEditorActivate(CodeBlocksEvent& event)
{
    event.Skip();
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinEditor(event.GetEditor());
    if (ed)
    {
        wxString fn = ed->GetFilename();

        ClTranslUnitId id = m_pClangPlugin->GetTranslationUnitId(fn);
        if (m_TranslUnitId != id )
        {
            m_Scope->Clear();
            m_Function->Clear();
            EnableToolbarTools(false);
        }
        m_TranslUnitId = id;
    }
}

void ClangToolbar::OnEditorClose(CodeBlocksEvent& event)
{
    EditorManager* edm = Manager::Get()->GetEditorManager();
    if (!edm)
    {
        event.Skip();
        return;
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
    event.Skip();
}

void ClangToolbar::OnEditorHook(cbEditor* ed, wxScintillaEvent& event)
{
    event.Skip();
    //cbStyledTextCtrl* stc = ed->GetControl();
    if (event.GetEventType() == wxEVT_SCI_UPDATEUI)
    {
        if (event.GetUpdated() & wxSCI_UPDATE_SELECTION)
        {
            cbStyledTextCtrl* stc = ed->GetControl();
            const int line = stc->GetCurrentLine();
            if (line != m_CurrentEditorLine )
            {
                m_CurrentEditorLine = line;
                wxCommandEvent evt(clEVT_COMMAND_UPDATETOOLBARCONTENTS, idToolbarUpdateContents);
                AddPendingEvent(evt);
                wxCommandEvent evt2(clEVT_COMMAND_UPDATETOOLBARSELECTION, idToolbarUpdateSelection);
                AddPendingEvent(evt2);
            }
        }
    }
}

void ClangToolbar::OnTranslationUnitCreated( ClangEvent& /*event*/ )
{
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
    wxCommandEvent evt(clEVT_COMMAND_UPDATETOOLBARCONTENTS, idToolbarUpdateContents);
    AddPendingEvent(evt);
    wxCommandEvent evt2(clEVT_COMMAND_UPDATETOOLBARSELECTION, idToolbarUpdateSelection);
    AddPendingEvent(evt2);
}

void ClangToolbar::OnReparseFinished( ClangEvent& /*event*/)
{
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
    wxCommandEvent evt(clEVT_COMMAND_UPDATETOOLBARCONTENTS, idToolbarUpdateContents);
    AddPendingEvent(evt);
    wxCommandEvent evt2(clEVT_COMMAND_UPDATETOOLBARSELECTION, idToolbarUpdateSelection);
    AddPendingEvent(evt2);
}

void ClangToolbar::OnUpdateSelection( wxCommandEvent& event )
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (ed)
    {
        if (m_Scope->GetCount() == 0 )
            OnUpdateContents(event);
        cbStyledTextCtrl* stc = ed->GetControl();
        int pos = stc->GetCurrentPos();
        int line = stc->LineFromPosition(pos);
        ClTokenPosition loc(line + 1, pos - stc->PositionFromLine(line) + 1);
        std::pair<wxString,wxString> scopePair = m_pClangPlugin->GetFunctionScopeAt(GetCurrentTranslationUnitId(), ed->GetFilename(), loc );
        if (scopePair.first.Length() == 0 )
        {
            return;
        }
        line = m_Scope->FindString(scopePair.first);
        if (line < 0 )
        {
            m_Scope->Append(scopePair.first);
            line = m_Scope->FindString(scopePair.first);
        }
        m_Scope->SetSelection(line);
        line = m_Function->FindString( scopePair.second );
        if (line < 0 )
        {
            m_Function->Append(scopePair.second);
            line = m_Function->FindString( scopePair.second);
        }
        m_Function->SetSelection(line);
        EnableToolbarTools(true);
    }
    else
    {
        EnableToolbarTools(false);
    }
}

static int SortByScopeName( const std::pair<wxString, wxString>& first, const std::pair<wxString, wxString>& second )
{
    return first.first < second.first;
}

static int SortByFunctionName( const std::pair<wxString, wxString>& first, const std::pair<wxString, wxString>& second )
{
    return first.second < second.second;
}

void ClangToolbar::OnUpdateContents( wxCommandEvent& event )
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
    {
        return;
    }
    int sel = m_Scope->GetSelection();
    wxString selScope = m_Scope->GetString(sel);
    std::vector<std::pair<wxString, wxString> > scopes;
    m_pClangPlugin->GetFunctionScopes( GetCurrentTranslationUnitId(), ed->GetFilename(), scopes );
    if (scopes.size() == 0)
        return;
    std::sort(scopes.begin(), scopes.end() , SortByScopeName );
    m_Scope->Freeze();
    m_Scope->Clear();
    for( std::vector<std::pair<wxString, wxString> >::iterator it = scopes.begin(); it != scopes.end(); ++it )
    {
        wxString scope = it->first;
        if( scope.Length() == 0 )
        {
            scope = wxT("<global>");
        }
        if (m_Scope->FindString(scope) < 0 )
        {
            m_Scope->Append(scope);
        }
    }
    UpdateFunctions(selScope);
    m_Scope->Thaw();
}

void ClangToolbar::OnScope( wxCommandEvent& evt )
{
    int sel = m_Scope->GetSelection();
    if (sel == -1)
        return;
    wxString selStr = m_Scope->GetString(sel);
    UpdateFunctions(selStr);
}

void ClangToolbar::OnFunction( wxCommandEvent& evt )
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
    {
        return;
    }

    wxString func = m_Function->GetString(m_Function->GetSelection());
    wxString scope = m_Scope->GetString( m_Scope->GetSelection() );
    ClTokenPosition tokenPos = m_pClangPlugin->GetFunctionScopeLocation(GetCurrentTranslationUnitId(), ed->GetFilename(), scope, func);
    ed->GetControl()->GotoLine(tokenPos.line-1);
}

bool ClangToolbar::BuildToolBar(wxToolBar* toolBar)
{
    // load the toolbar resource
    Manager::Get()->AddonToolBar(toolBar,_T("codecompletion_toolbar"));
    // get the wxChoice control pointers
    m_Function = XRCCTRL(*toolBar, "chcCodeCompletionFunction", wxChoice);
    m_Scope    = XRCCTRL(*toolBar, "chcCodeCompletionScope",    wxChoice);

    if (m_Function )
        Manager::Get()->GetAppWindow()->Connect(m_Function->GetId(), wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler(ClangToolbar::OnFunction), nullptr, this);
    if (m_Scope )
        Manager::Get()->GetAppWindow()->Connect(m_Scope->GetId(), wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler(ClangToolbar::OnScope), nullptr, this);
    m_ToolBar = toolBar;

    // set the wxChoice and best toolbar size
    UpdateToolBar();

    // disable the wxChoices
    EnableToolbarTools(false);


    return true;
}

void ClangToolbar::UpdateToolBar()
{
    bool showScope = Manager::Get()->GetConfigManager(_T("code_completion"))->ReadBool(_T("/scope_filter"), true);

    if (showScope && !m_Scope)
    {
        m_Scope = new wxChoice(m_ToolBar, wxNewId(), wxPoint(0, 0), wxSize(280, -1), 0, 0);
        m_ToolBar->InsertControl(0, m_Scope);
        m_Scope->Connect(m_Scope->GetId(), wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler(ClangToolbar::OnScope), nullptr, this);
    }
    else if ((!showScope) && m_Scope)
    {
        m_ToolBar->DeleteTool(m_Scope->GetId());
        m_Scope = NULL;
    }
    else
        return;

    m_ToolBar->Realize();
    m_ToolBar->SetInitialSize();
}

void ClangToolbar::UpdateFunctions( const wxString& scopeItem )
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
    {
        return;
    }
    std::vector<std::pair<wxString, wxString> > funcList;
    m_pClangPlugin->GetFunctionScopes(GetCurrentTranslationUnitId(), ed->GetFilename(), funcList);
    std::sort(funcList.begin(), funcList.end() , SortByFunctionName);
    m_Function->Freeze();
    m_Function->Clear();
    for( std::vector<std::pair<wxString, wxString> >::const_iterator it = funcList.begin(); it != funcList.end(); ++it)
    {
        if( it->first == scopeItem )
        {
            m_Function->Append(it->second);
        }
    }
    m_Function->Thaw();
}

void ClangToolbar::EnableToolbarTools(bool enable)
{
    if (m_Scope)
        m_Scope->Enable(enable);
    if (m_Function)
        m_Function->Enable(enable);
}

ClTranslUnitId ClangToolbar::GetCurrentTranslationUnitId()
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
