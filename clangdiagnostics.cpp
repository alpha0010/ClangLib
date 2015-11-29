#include "clangdiagnostics.h"

#include <cbeditor.h>
#include <cbproject.h>
#include <configmanager.h>
#include <editormanager.h>
#include <logmanager.h>
#include <macrosmanager.h>
#include <projectmanager.h>
#include <cbstyledtextctrl.h>

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
    if( (diagLv == dlFull)&&(event.GetLocation().line != 0)&&(event.GetLocation().column != 0) )
    {
        update = true;
    }
    const std::vector<ClDiagnostic>& diagnostics = event.GetDiagnosticResults();
    cbStyledTextCtrl* stc = ed->GetControl();
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
    for ( std::vector<ClDiagnostic>::const_iterator dgItr = diagnostics.begin();
            dgItr != diagnostics.end(); ++dgItr )
    {
        //Manager::Get()->GetLogManager()->Log(dgItr->file + wxT(" ") + dgItr->message + F(wxT(" %d, %d"), dgItr->range.first, dgItr->range.second));
        if (dgItr->file != filename)
            continue;
        if (diagLv == dlFull)
        {
            if( (!update) || ((int)dgItr->line == (int)event.GetLocation().line) )
            {
                wxString str = stc->AnnotationGetText(dgItr->line - 1);
                if (!str.IsEmpty())
                    str += wxT('\n');
                stc->AnnotationSetText(dgItr->line - 1, str + dgItr->message);
                stc->AnnotationSetStyle(dgItr->line - 1, 50);
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
    }
    if ( diagLv == dlFull )
    {
        stc->AnnotationSetVisible(wxSCI_ANNOTATION_BOXED);
        stc->ScrollToLine(firstVisibleLine);
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
