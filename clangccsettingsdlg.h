/*
 * This file is part of the Code::Blocks IDE and licensed under the GNU General Public License, version 3
 * http://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef ClangCodeCompletionSettingsDlg_H
#define ClangCodeCompletionSettingsDlg_H

#include <wx/intl.h>
#include "configurationpanel.h"
#include <settings.h>
#include "clangcc.h"

#define CLANG_CONFIGMANAGER _T("ClangLib")

class ClangPlugin;

class ClangSettingsDlg : public cbConfigurationPanel
{
public:
    ClangSettingsDlg(wxWindow* parent, ClangPlugin* pPlugin  );
    virtual ~ClangSettingsDlg();

    virtual wxString GetTitle() const          { return _("Code assistance"); }
    virtual wxString GetBitmapBaseName() const { return _T("codecompletion"); }
    virtual void OnApply();
    virtual void OnCancel()                    { ; }

protected:
    void OnChooseColour(wxCommandEvent& event);
    void OnCCDelayScroll(wxScrollEvent& event);

    void OnUpdateUI(wxUpdateUIEvent& event);

private:
    void UpdateCCDelayLabel();
    bool ValidateReplacementToken(wxString& from, wxString& to);

    DECLARE_EVENT_TABLE()
private:
    ClangPlugin* m_pPlugin;
};

#endif // ClangCodeCompletionSettingsDlg_H
