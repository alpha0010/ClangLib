#ifndef __CLANGPLUGINAPI_H
#define __CLANGPLUGINAPI_H

#include <cbplugin.h>


typedef int ClTranslUnitId;

/* interface */
class IClangPlugin
{
public:
    virtual ClTranslUnitId GetTranslationUnitId( const wxString& filename ) = 0;
    virtual wxString GetFunctionScope( ClTranslUnitId id, const wxString& filename, int line, int column ) = 0;
    virtual wxStringVec GetFunctionScopes( ClTranslUnitId, const wxString& filename ) = 0;
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
