#include "clangdiagnostics.h"

#include <sdk.h>

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

#include <cbstyledtextctrl.h>
#include <cbcolourmanager.h>
#include "cclogger.h"

const int idGotoNextDiagnostic = wxNewId();
const int idGotoPrevDiagnostic = wxNewId();
const wxString ClangDiagnostics::SettingName = _T("/diagnostics");

ClangDiagnostics::ClangDiagnostics() :
    m_TranslUnitId(-1),
    m_Diagnostics(),
    m_bShowInline(false),
    m_bShowWarning(false),
    m_bShowError(false)
{

}

ClangDiagnostics::~ClangDiagnostics()
{
}

void ClangDiagnostics::OnAttach( IClangPlugin* pClangPlugin )
{
    ClangPluginComponent::OnAttach(pClangPlugin);

    Manager::Get()->GetColourManager()->RegisterColour( wxT("Diagnostics"), wxT("Annotation info background"), wxT("diagnostics_popup_infobg"), wxColour(255, 255, 255));
    Manager::Get()->GetColourManager()->RegisterColour( wxT("Diagnostics"), wxT("Annotation info text"), wxT("diagnostics_popup_infotext"), wxColour(128,128,128));
    Manager::Get()->GetColourManager()->RegisterColour( wxT("Diagnostics"), wxT("Annotation warning background"), wxT("diagnostics_popup_warnbg"), wxColour(255, 255, 255));
    Manager::Get()->GetColourManager()->RegisterColour( wxT("Diagnostics"), wxT("Annotation warning text"), wxT("diagnostics_popup_warntext"), wxColour(0,0,255));
    Manager::Get()->GetColourManager()->RegisterColour( wxT("Diagnostics"), wxT("Annotation error background"), wxT("diagnostics_popup_errbg"), wxColour(255, 255, 255));
    Manager::Get()->GetColourManager()->RegisterColour( wxT("Diagnostics"), wxT("Annotation error text"), wxT("diagnostics_popup_errtext"), wxColour(255,0,0));

    typedef cbEventFunctor<ClangDiagnostics, CodeBlocksEvent> CBCCEvent;
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_ACTIVATED, new CBCCEvent(this, &ClangDiagnostics::OnEditorActivate));
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_CLOSE,     new CBCCEvent(this, &ClangDiagnostics::OnEditorClose));

    Connect(idGotoNextDiagnostic, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(ClangDiagnostics::OnGotoNextDiagnostic), nullptr, this);
    Connect(idGotoPrevDiagnostic, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(ClangDiagnostics::OnGotoPrevDiagnostic), nullptr, this);

    //Connect(wxEVT_IDLE, wxIdleEventHandler(ClangDiagnostics::OnIdle));

    typedef cbEventFunctor<ClangDiagnostics, ClangEvent> ClDiagEvent;
    pClangPlugin->RegisterEventSink(clEVT_DIAGNOSTICS_UPDATED, new ClDiagEvent(this, &ClangDiagnostics::OnDiagnosticsUpdated) );
}

void ClangDiagnostics::OnRelease( IClangPlugin* pClangPlugin )
{
    pClangPlugin->RemoveAllEventSinksFor( this );
    Disconnect( wxEVT_IDLE );
    Disconnect( idGotoPrevDiagnostic );
    Disconnect( idGotoNextDiagnostic );
    Manager::Get()->RemoveAllEventSinksFor(this);
    ClangPluginComponent::OnRelease( pClangPlugin );
}

void ClangDiagnostics::BuildMenu( wxMenuBar* menuBar )
{
    int idx = menuBar->FindMenu(_("Sea&rch"));
    if (idx != wxNOT_FOUND)
    {
        menuBar->GetMenu(idx)->AppendSeparator();
        menuBar->GetMenu(idx)->Append(idGotoPrevDiagnostic, _("Goto previous error/warning (clang)\tCtrl+Shift+UP"));
        menuBar->GetMenu(idx)->Append(idGotoNextDiagnostic, _("Goto next error/warning (clang)\tCtrl+Shift+DOWN"));
    }
}

// Command handlers

void ClangDiagnostics::OnGotoNextDiagnostic( wxCommandEvent& WXUNUSED(event) )
{
    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    cbEditor* ed = edMgr->GetBuiltinActiveEditor();
    if (!ed)
    {
        return;
    }

    cbStyledTextCtrl* stc = ed->GetControl();
    for (std::vector<ClDiagnostic>::const_iterator it = m_Diagnostics.begin(); it != m_Diagnostics.end(); ++it)
    {
        if ((it->line - 1) > stc->GetCurrentLine() )
        {
            stc->GotoLine( it->line - 1 );
            stc->MakeNearbyLinesVisible( it->line - 1 );
            break;
        }
    }
}

void ClangDiagnostics::OnGotoPrevDiagnostic( wxCommandEvent& WXUNUSED(event) )
{
    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    cbEditor* ed = edMgr->GetBuiltinActiveEditor();
    if (!ed)
    {
        return;
    }

    cbStyledTextCtrl* stc = ed->GetControl();
    int prevLine = -1;
    for (std::vector<ClDiagnostic>::const_iterator it = m_Diagnostics.begin(); it != m_Diagnostics.end(); ++it)
    {
        if ((it->line - 1) < stc->GetCurrentLine() )
        {
            prevLine = it->line - 1;
        }
        else break;
    }
    if ( prevLine >= 0 )
    {
        if ( prevLine < stc->GetFirstVisibleLine() )
        {
            stc->GotoLine( prevLine );
            stc->ScrollLines( - stc->LinesOnScreen()/2 );
        }
        else
        {
            stc->GotoLine( prevLine );
            stc->MakeNearbyLinesVisible(prevLine);
        }
    }
}

// Code::Blocks events
void ClangDiagnostics::OnEditorActivate(CodeBlocksEvent& event)
{
    event.Skip();
    if ( !IsAttached() )
        return;

    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinEditor(event.GetEditor());
    if (ed)
    {
        wxString fn = ed->GetFilename();

        ConfigManager* cfg = Manager::Get()->GetConfigManager(_T("ClangLib"));
        m_bShowInline = cfg->ReadBool( _T("/diagnostics_show_inline"), true);
        m_bShowWarning = cfg->ReadBool( _T("/diagnostics_show_warnings"), true);
        m_bShowError = cfg->ReadBool( _T("/diagnostics_show_errors"), true);

        m_TranslUnitId = m_pClangPlugin->GetTranslationUnitId(fn);
        cbStyledTextCtrl* stc = ed->GetControl();
        stc->StyleSetBackground( 51, Manager::Get()->GetColourManager()->GetColour(wxT("diagnostics_popup_warnbg") ));
        stc->StyleSetForeground( 51, Manager::Get()->GetColourManager()->GetColour(wxT("diagnostics_popup_warntext") ));
        stc->StyleSetBackground( 52, Manager::Get()->GetColourManager()->GetColour(wxT("diagnostics_popup_errbg") ));
        stc->StyleSetForeground( 52, Manager::Get()->GetColourManager()->GetColour(wxT("diagnostics_popup_errtext") ));
    }
    m_Diagnostics.clear();
}

void ClangDiagnostics::OnEditorClose(CodeBlocksEvent& event)
{
    event.Skip();
    if ( !IsAttached() )
        return;
    m_Diagnostics.clear();
    m_TranslUnitId = -1;
}

void ClangDiagnostics::OnDiagnosticsUpdated( ClangEvent& event )
{
    event.Skip();
    if ( !IsAttached() )
        return;

    ClDiagnosticLevel diagLv = dlFull; // TODO
    bool update = false;
    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    cbEditor* ed = edMgr->GetBuiltinActiveEditor();
    if (!ed)
    {
        return;
    }
    if ( event.GetTranslationUnitId() != GetCurrentTranslationUnitId() )
    {
        CCLogger::Get()->DebugLog( wxT("OnDiagnostics: tu ID mismatch") );
        // Switched translation unit before event delivered
        return;
    }

    const std::vector<ClDiagnostic>& diagnostics = event.GetDiagnosticResults();
    if ( (diagLv == dlFull)&&(event.GetLocation().line != 0)&&(event.GetLocation().column != 0) )
    {
        CCLogger::Get()->DebugLog( wxT("OnDiagnostics: Doing partial update") );
        update = true;
    }
    else
    {
        CCLogger::Get()->DebugLog( wxT("OnDiagnostics: Doing full update") );
        m_Diagnostics = diagnostics;
    }

    cbStyledTextCtrl* stc = ed->GetControl();

    int firstVisibleLine = stc->GetFirstVisibleLine();

    const int warningIndicator = 0; // predefined
    const int errorIndicator = 15; // hopefully we do not clash with someone else...
    stc->SetIndicatorCurrent(warningIndicator);
    if ( !update )
        stc->IndicatorClearRange(0, stc->GetLength());
    stc->IndicatorSetStyle(errorIndicator, wxSCI_INDIC_SQUIGGLE);
    stc->IndicatorSetForeground(errorIndicator, *wxRED);
    stc->SetIndicatorCurrent(errorIndicator);
    if ( !update )
        stc->IndicatorClearRange(0, stc->GetLength());

    const wxString& filename = ed->GetFilename();
    if (!m_bShowInline)
    {
        stc->AnnotationClearAll();
    }
    else if ( (diagLv == dlFull)&&(update) )
    {
        int line = event.GetLocation().line-1;
        stc->AnnotationClearLine(line);
    }
    else
    {
        stc->AnnotationClearAll();
    }

    int lastLine = 0;
    for ( std::vector<ClDiagnostic>::const_iterator dgItr = diagnostics.begin();
            dgItr != diagnostics.end(); ++dgItr )
    {
        Manager::Get()->GetLogManager()->Log(dgItr->file + wxT(" ") + dgItr->message + F(wxT(" %d, %d"), dgItr->range.first, dgItr->range.second));
        if (dgItr->file != filename)
        {
            CCLogger::Get()->Log(wxT("WARNING: Filename mismatch in diagnostics !!"));
            continue;
        }
        if (update)
        {
            m_Diagnostics.push_back( *dgItr );
        }
        if (diagLv == dlFull)
        {
            if (update && (lastLine != (dgItr->line -1) ) )
            {
                stc->AnnotationClearLine(dgItr->line - 1);
            }
            if (m_bShowInline)
            {
                wxString str = stc->AnnotationGetText(dgItr->line - 1);
                if (!str.IsEmpty())
                    str += wxT('\n');
                if (!str.Contains(dgItr->message))
                {
                    switch (dgItr->severity)
                    {
                    case sWarning:
                        if (m_bShowWarning)
                        {
                            stc->AnnotationSetText(dgItr->line - 1, str + dgItr->message);
                            stc->AnnotationSetStyle(dgItr->line - 1, 51);
                        }
                        break;
                    case sError:
                        if (m_bShowError)
                        {
                            stc->AnnotationSetText(dgItr->line - 1, str + dgItr->message);
                            stc->AnnotationSetStyle(dgItr->line - 1, 52);
                        }
                        break;
                    case sNote:
                        break;
                    }
                }
            }
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
        else if (  dgItr != diagnostics.begin()
                   && dgItr->line == (dgItr - 1)->line
                   && dgItr->range.first <= (dgItr - 1)->range.second )
        {
            continue; // do not overwrite the last indicator
        }
        else
            stc->SetIndicatorCurrent(warningIndicator);
        stc->IndicatorFillRange(pos, range);
        lastLine = dgItr->line - 1;
    }
    if ( diagLv == dlFull )
    {
        stc->AnnotationSetVisible(wxSCI_ANNOTATION_BOXED);
        stc->ScrollLines( firstVisibleLine - stc->GetFirstVisibleLine() );
    }
}

ClTranslUnitId ClangDiagnostics::GetCurrentTranslationUnitId()
{
    if ( m_TranslUnitId == -1 )
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

void ClangDiagnostics::OnIdle( wxIdleEvent& /*event*/ )
{
    //fprintf(stdout,"ClangDiagnostics::OnIdle\n");
}
