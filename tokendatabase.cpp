/*
 * Database responsible for resolving tokens between translation units
 */

#include "tokendatabase.h"

#include <wx/filename.h>
#include <wx/string.h>
#include <iostream>

#include "treemap.h"

TokenDatabase::TokenDatabase() :
    m_pTokens(new TreeMap<AbstractToken>()),
    m_pFilenames(new TreeMap<wxString>())
{
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
}

TokenDatabase::~TokenDatabase()
{
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
    delete m_pFilenames;
    delete m_pTokens;
}

FileId TokenDatabase::GetFilenameId(const wxString& filename)
{
    //std::cout<<__PRETTY_FUNCTION__<<std::endl;
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
    //std::cout<<__PRETTY_FUNCTION__<<std::endl;
    return m_pFilenames->GetValue(fId);
}

TokenId TokenDatabase::InsertToken(const wxString& identifier, const AbstractToken& token)
{
    //std::cout<<__PRETTY_FUNCTION__<<std::endl;
    TokenId tId = GetTokenId(identifier, token.tokenHash);
    //wxMutexLocker locker(m_Mutex);
    if (tId == wxNOT_FOUND)
        return m_pTokens->Insert(identifier, token);
    return tId;
}

TokenId TokenDatabase::GetTokenId(const wxString& identifier, unsigned tokenHash) const
{
    //std::cout<<__PRETTY_FUNCTION__<<std::endl;
    //wxMutexLocker locker(m_Mutex);
    //std::cout<<__PRETTY_FUNCTION__<<" acquired lock"<<std::endl;
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
    //std::cout<<__PRETTY_FUNCTION__<<std::endl;
    //wxMutexLocker locker(m_Mutex);
    return m_pTokens->GetValue(tId);
}

std::vector<TokenId> TokenDatabase::GetTokenMatches(const wxString& identifier) const
{
    //std::cout<<__PRETTY_FUNCTION__<<std::endl;
    //wxMutexLocker locker(m_Mutex);
    return m_pTokens->GetIdSet(identifier);
}

void TokenDatabase::Shrink()
{
    //std::cout<<__PRETTY_FUNCTION__<<std::endl;
    m_pFilenames->Shrink();
    //wxMutexLocker locker(m_Mutex);
    m_pTokens->Shrink();
}
