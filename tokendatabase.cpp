/*
 * Database responsible for resolving tokens between translation units
 */

#include "tokendatabase.h"

#include <wx/filename.h>
#include <wx/string.h>

#include "treemap.h"

TokenDatabase::TokenDatabase() :
    m_pTokens(new TreeMap<AbstractToken>()),
    m_pFilenames(new TreeMap<wxString>())
{
}

TokenDatabase::~TokenDatabase()
{
    delete m_pFilenames;
    delete m_pTokens;
}

FileId TokenDatabase::GetFilenameId(const wxString& filename)
{
    wxFileName fln(filename);
    fln.Normalize(wxPATH_NORM_ALL & ~wxPATH_NORM_CASE);
    const wxString& normFile = fln.GetFullPath(wxPATH_UNIX);
    std::vector<int> id = m_pFilenames->GetIdSet(normFile);
    if (id.empty())
        return m_pFilenames->Insert(normFile, normFile);
    return id.front();
}

wxString TokenDatabase::GetFilename(FileId fId) const
{
    return m_pFilenames->GetValue(fId);
}

TokenId TokenDatabase::InsertToken(const wxString& identifier, const AbstractToken& token)
{
    TokenId tId = GetTokenId(identifier, token.tokenHash);
    if (tId == wxNOT_FOUND)
        return m_pTokens->Insert(identifier, token);
    return tId;
}

TokenId TokenDatabase::GetTokenId(const wxString& identifier, unsigned tokenHash) const
{
    std::vector<int> ids = m_pTokens->GetIdSet(identifier);
    for (std::vector<int>::const_iterator itr = ids.begin();
         itr != ids.end(); ++itr)
    {
        if (m_pTokens->GetValue(*itr).tokenHash == tokenHash)
            return *itr;
    }
    return wxNOT_FOUND;
}

AbstractToken& TokenDatabase::GetToken(TokenId tId) const
{
    return m_pTokens->GetValue(tId);
}
