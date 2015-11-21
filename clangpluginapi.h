#ifndef __CLANGPLUGINAPI_H
#define __CLANGPLUGINAPI_H

#include <cbplugin.h>


typedef int ClTranslUnitId;

struct ClTokenPosition{
    ClTokenPosition(unsigned int ln, unsigned int col){line = ln; column = col;}
    unsigned int line;
    unsigned int column;
};

/* interface */
class IClangPlugin
{
public:
    virtual ClTranslUnitId GetTranslationUnitId( const wxString& filename ) = 0;
    virtual std::pair<wxString,wxString> GetFunctionScopeAt( ClTranslUnitId id, const wxString& filename, const ClTokenPosition& location ) = 0;
    virtual ClTokenPosition GetFunctionScopeLocation( ClTranslUnitId id, const wxString& filename, const wxString& scope, const wxString& functioname) = 0;
    virtual std::vector<std::pair<wxString, wxString> > GetFunctionScopes( ClTranslUnitId, const wxString& filename ) = 0;
};

/* abstract */
class ClangPluginComponent : public wxEvtHandler
{
public:
    ClangPluginComponent(){}
    virtual void OnAttach( IClangPlugin *pClangPlugin ){ m_pClangPlugin = pClangPlugin; }
    virtual void OnRelease( IClangPlugin */*pClangPlugin*/ ){ m_pClangPlugin = NULL; }
    virtual bool BuildToolBar(wxToolBar* toolBar){ return false; }
protected:
    IClangPlugin* m_pClangPlugin;
};

#endif // __CLANGPLUGINAPI_H
