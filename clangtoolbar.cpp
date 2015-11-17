#include "clangtoolbar.h"
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


const int idToolbarUpdateSelection = wxNewId();
const int idToolbarUpdateContents = wxNewId();


DEFINE_EVENT_TYPE(cbEVT_COMMAND_UPDATETOOLBARSELECTION)
DEFINE_EVENT_TYPE(cbEVT_COMMAND_UPDATETOOLBARCONTENTS)


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

    Connect(idToolbarUpdateSelection,cbEVT_COMMAND_UPDATETOOLBARSELECTION, wxCommandEventHandler(ClangToolbar::OnToolbarUpdateSelection), nullptr, this );
    Connect(idToolbarUpdateContents, cbEVT_COMMAND_UPDATETOOLBARCONTENTS, wxCommandEventHandler(ClangToolbar::OnToolbarUpdateContents), nullptr, this );

    m_EditorHookId = EditorHooks::RegisterHook(new EditorHooks::HookFunctor<ClangToolbar>(this, &ClangToolbar::OnEditorHook));
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
        if( m_TranslUnitId != id )
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
        //fprintf(stdout,"wxEVT_SCI_UPDATEUI\n");
        if (event.GetUpdated() & wxSCI_UPDATE_SELECTION)
        {
            cbStyledTextCtrl* stc = ed->GetControl();
            const int line = stc->GetCurrentLine();
            if( line != m_CurrentEditorLine )
            {
                m_CurrentEditorLine = line;
                wxCommandEvent evt(cbEVT_COMMAND_UPDATETOOLBARCONTENTS, idToolbarUpdateContents);
                AddPendingEvent(evt);
                wxCommandEvent evt2(cbEVT_COMMAND_UPDATETOOLBARSELECTION, idToolbarUpdateSelection);
                AddPendingEvent(evt2);
                evt.SetInt(line);
            }
        }
    }
}


void ClangToolbar::OnToolbarUpdateSelection( wxCommandEvent& event )
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (ed)
    {
        cbStyledTextCtrl* stc = ed->GetControl();
        int line = stc->GetCurrentLine();
        int pos = stc->GetCurrentPos();
        const int column = stc->GetColumn(pos);
        wxString str = m_pClangPlugin->GetFunctionScope(GetCurrentTranslationUnitId(), ed->GetFilename(), line, column );
        if( str.CompareTo(wxT("::")) == 0 )
        {
            return;
        }
        pos = str.Find(':',true);
        line = m_Scope->FindString( str.Left(pos-1));
        if( line < 0 )
        {
            m_Scope->Append(str.Left(pos-1));
            line = m_Scope->FindString( str.Left(pos-1));
        }
        m_Scope->SetSelection(line);
        pos = str.Len() - pos + 1;
        line = m_Function->FindString( str.Right(pos) );
        if( line < 0 )
        {
            m_Function->Append(str.Right(pos));
            line = m_Function->FindString( str.Right(pos));
        }
        m_Function->SetSelection(line);
        fprintf(stdout,"Function scope: %s\n", (const char*)str.mb_str());
        EnableToolbarTools(true);
    }
    else
    {
        EnableToolbarTools(false);
    }
}

void ClangToolbar::OnToolbarUpdateContents( wxCommandEvent& event )
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (ed)
    {
        m_Scope->Clear();
        m_Function->Clear();
        wxStringVec list = m_pClangPlugin->GetFunctionScopes( GetCurrentTranslationUnitId(), ed->GetFilename() );
        if( list.size() == 0)
            return;
        for( wxStringVec::iterator it = list.begin(); it != list.end(); ++it )
        {
            wxString val = *it;
            wxString scope;
            wxString func;
            int pos = val.Find(':', true);
            if( pos < 0 )
            {
                scope = wxT("<global>");
                func = val;
            }else{
                scope = val.Left(pos);
                func = val.Right( it->Length() - pos + 1);
            }
            if( m_Scope->FindString(scope) < 0 )
            {
                m_Scope->Append(scope);
            }
            if( m_Function->FindString(func) < 0)
            {
                m_Function->Append(func);
            }
        }
    }
}

bool ClangToolbar::BuildToolBar(wxToolBar* toolBar)
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

void ClangToolbar::UpdateToolBar()
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

void ClangToolbar::EnableToolbarTools(bool enable)
{
    if (m_Scope)
        m_Scope->Enable(enable);
    if (m_Function)
        m_Function->Enable(enable);
}

ClTranslUnitId ClangToolbar::GetCurrentTranslationUnitId()
{
    if( m_TranslUnitId == -1 )
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
