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
    m_pFilenames(new TreeMap<wxString>()),
    m_Mutex(wxMUTEX_RECURSIVE)
{
}

TokenDatabase::~TokenDatabase()
{
    delete m_pFilenames;
    delete m_pTokens;
}

FileId TokenDatabase::GetFilenameId(const wxString& filename)
{
    wxMutexLocker lock(m_Mutex);
    assert(m_pFilenames);
    assert(m_pT);
    wxFileName fln(filename.c_str());
    fln.Normalize(wxPATH_NORM_ALL & ~wxPATH_NORM_CASE);
    const wxString& normFile = fln.GetFullPath(wxPATH_UNIX);
    std::vector<int> id = m_pFilenames->GetIdSet(normFile);
    if (id.empty())
    {
        wxString f = wxString(normFile.c_str());
        FileId id = m_pFilenames->Insert( f, f );
        //fprintf(stdout,"%s this=%p Storing %s(%p) as %d\n", __PRETTY_FUNCTION__, (void*)this, (const char*)f.mb_str(), (void*)f.c_str(), (int)id );
        return id;
    }
    return id.front();
}

wxString TokenDatabase::GetFilename(FileId fId)
{
    wxMutexLocker lock( m_Mutex);

    assert(m_pFilenames->HasValue(fId));
    //if (!m_pFilenames->HasValue(fId))
    //    return wxString();

    const wxChar* val = m_pFilenames->GetValue(fId).c_str();
    if (val == NULL)
        return wxString();

    //fprintf(stdout, "%s this=%p id=%d Returning %p", __PRETTY_FUNCTION__, (void*)this, (int)fId, (void*)val );
    return wxString(val);
}

TokenId TokenDatabase::InsertToken(const wxString& identifier, const AbstractToken& token)
{
    wxMutexLocker lock(m_Mutex);

    TokenId tId = GetTokenId(identifier, token.tokenHash);
    if (tId == wxNOT_FOUND)
        return m_pTokens->Insert(wxString(identifier.c_str()), token);
    return tId;
}

TokenId TokenDatabase::GetTokenId(const wxString& identifier, unsigned tokenHash)
{
    wxMutexLocker lock( m_Mutex);
    std::vector<int> ids = m_pTokens->GetIdSet(identifier);
    for (std::vector<int>::const_iterator itr = ids.begin();
            itr != ids.end(); ++itr)
    {
        if (m_pTokens->HasValue(*itr))
            if (m_pTokens->GetValue(*itr).tokenHash == tokenHash)
                return *itr;
    }
    return wxNOT_FOUND;
}

AbstractToken TokenDatabase::GetToken(TokenId tId)
{
    wxMutexLocker lock( m_Mutex);
    assert( m_pTokens->HasValue(tId) );
    return m_pTokens->GetValue(tId);
}

std::vector<TokenId> TokenDatabase::GetTokenMatches(const wxString& identifier)
{
    wxMutexLocker lock( m_Mutex);
    return m_pTokens->GetIdSet(identifier);
}

void TokenDatabase::Shrink()
{
    wxMutexLocker lock(m_Mutex);
    m_pFilenames->Shrink();
    m_pTokens->Shrink();
}
