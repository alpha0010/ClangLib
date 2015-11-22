#ifndef TREEMAP_H
#define TREEMAP_H

#include <vector>

struct TreeNode;
class wxString;
template<typename _TpVal> class ClTreeMap;

template<>
class ClTreeMap<int>
{
    public:
        ClTreeMap();
        ~ClTreeMap();
        int Insert(const wxString& key, int value); // returns value
        void Shrink();
        std::vector<int> GetIdSet(const wxString& key) const;
        int GetValue(int id) const; // returns id
    private:
        TreeNode* m_Root;
};

template<typename _TpVal>
class ClTreeMap
{
    public:
        // returns the id of the value inserted
        int Insert(const wxString& key, const _TpVal& value)
        {
            m_Data.push_back(value);
            return m_Tree.Insert(key, m_Data.size() - 1);
        }

        void Shrink()
        {
            m_Tree.Shrink();
#if __cplusplus >= 201103L
            m_Data.shrink_to_fit();
#else
            std::vector<_TpVal>(m_Data).swap(m_Data);
#endif
        }

        std::vector<int> GetIdSet(const wxString& key) const
        {
            return m_Tree.GetIdSet(key);
        }

        bool HasValue(int id)
        {
            if (id < 0)
                return false;
            if (id >= (int)m_Data.size())
                return false;
            return true;
        }

        _TpVal& GetValue(int id)
        {
            return m_Data[id];
        }

    private:
        ClTreeMap<int> m_Tree;
        std::vector<_TpVal> m_Data;
};

#endif // TREEMAP_H
