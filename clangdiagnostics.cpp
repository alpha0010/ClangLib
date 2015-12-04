#include "clangdiagnostics.h"

#include <cbeditor.h>
#include <cbproject.h>
#include <configmanager.h>
#include <editormanager.h>
#include <logmanager.h>
#include <macrosmanager.h>
#include <projectmanager.h>
#include <cbstyledtextctrl.h>
#include <cbcolourmanager.h>

const int idDiagnosticTimer = wxNewId();

ClangDiagnostics::ClangDiagnostics() :
    m_TranslUnitId(-1)
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

    Connect(idDiagnosticTimer, wxEVT_TIMER, wxTimerEventHandler(ClangDiagnostics::OnTimer));

    typedef cbEventFunctor<ClangDiagnostics, ClangEvent> ClCCEvent;
    pClangPlugin->RegisterEventSink(clEVT_DIAGNOSTICS_UPDATED, new ClCCEvent(this, &ClangDiagnostics::OnDiagnostics) );
}

void ClangDiagnostics::OnRelease( IClangPlugin* pClangPlugin )
{

}

// Code::Blocks events
void ClangDiagnostics::OnEditorActivate(CodeBlocksEvent& event)
{

}
void ClangDiagnostics::OnEditorClose(CodeBlocksEvent& event)
{

}
void ClangDiagnostics::OnTimer(wxTimerEvent& event)
{

}

void ClangDiagnostics::OnDiagnostics( ClangEvent& event )
{
    ClDiagnosticLevel diagLv = dlFull; // TODO
    bool update = false;
    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    cbEditor* ed = edMgr->GetBuiltinActiveEditor();
    if (!ed)
    {
        std::cout<<"No editor..."<<std::endl;
        return;
    }
    if( event.GetTranslationUnitId() != GetCurrentTranslationUnitId() )
    {
        // Switched translation unit before event delivered
        return;
    }
    ConfigManager* cfg = Manager::Get()->GetConfigManager(_T("ClangLib"));
    bool show_inline = cfg->ReadBool( _T("/diagnostics_show_inline"), true);
    bool show_warning = cfg->ReadBool( _T("/diagnostics_show_warnings"), true);
    bool show_error = cfg->ReadBool( _T("/diagnostics_show_errors"), true);
    bool show_note = cfg->ReadBool( _T("/diagnostics_show_notes"), false);

    if( (diagLv == dlFull)&&(event.GetLocation().line != 0)&&(event.GetLocation().column != 0) )
    {
        update = true;
    }
    const std::vector<ClDiagnostic>& diagnostics = event.GetDiagnosticResults();
    cbStyledTextCtrl* stc = ed->GetControl();
    stc->StyleSetBackground( 51, Manager::Get()->GetColourManager()->GetColour(wxT("diagnostics_popup_warnbg") ));
    stc->StyleSetForeground( 51, Manager::Get()->GetColourManager()->GetColour(wxT("diagnostics_popup_warntext") ));
    stc->StyleSetBackground( 52, Manager::Get()->GetColourManager()->GetColour(wxT("diagnostics_popup_errbg") ));
    stc->StyleSetForeground( 52, Manager::Get()->GetColourManager()->GetColour(wxT("diagnostics_popup_errtext") ));
    int firstVisibleLine = stc->GetFirstVisibleLine();
    if ((diagLv == dlFull)&&(!update) )
        stc->AnnotationClearAll();
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
    if ( (diagLv == dlFull)&&(update) )
    {
        int line = event.GetLocation().line-1;
        stc->AnnotationClearLine(line);
    }
    if (!show_inline)
    {
        stc->AnnotationClearAll();
    }
    int lastLine = 0;
    for ( std::vector<ClDiagnostic>::const_iterator dgItr = diagnostics.begin();
            dgItr != diagnostics.end(); ++dgItr )
    {
        //Manager::Get()->GetLogManager()->Log(dgItr->file + wxT(" ") + dgItr->message + F(wxT(" %d, %d"), dgItr->range.first, dgItr->range.second));
        if (dgItr->file != filename)
        {
            fprintf(stdout, "Filename mismatch: '%s' != '%s'\n", (const char*)dgItr->file.mb_str(), (const char*)filename.mb_str());
            continue;
        }
        if (diagLv == dlFull)
        {
            if (update && (lastLine != (dgItr->line -1) ) )
            {
                stc->AnnotationClearLine(dgItr->line - 1);
            }
            if (show_inline)
            {
                wxString str = stc->AnnotationGetText(dgItr->line - 1);
                if (!str.IsEmpty())
                    str += wxT('\n');
                if (!str.Contains(dgItr->message))
                {
                    switch(dgItr->severity){
                    case sWarning:
                        if (show_warning)
                        {
                            stc->AnnotationSetText(dgItr->line - 1, str + dgItr->message);
                            stc->AnnotationSetStyle(dgItr->line - 1, 51);
                        }
                        break;
                    case sError:
                        if (show_error)
                        {
                            stc->AnnotationSetText(dgItr->line - 1, str + dgItr->message);
                            stc->AnnotationSetStyle(dgItr->line - 1, 52);
                        }
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
        //stc->ScrollToLine(firstVisibleLine);
        stc->ScrollLines( firstVisibleLine - stc->GetFirstVisibleLine() );
    }
}

ClTranslUnitId ClangDiagnostics::GetCurrentTranslationUnitId()
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
