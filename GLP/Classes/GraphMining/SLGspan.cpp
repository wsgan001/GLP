//
//  SLGspan.cpp
//  GLP
//
//  Created by Zheng Shao on 12/28/12.
//  Copyright (c) 2012 Saigo Laboratoire. All rights reserved.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
//  02111-1307, USA
//

#include <fstream>
#include <utility>
#include "SLGspan.h"
#include "../SLUtility.h"

using namespace std;

// Public Methods
bool SLGspan::setParameters(SLGspanParameters& parameters)
{
    ASSERT(parameters.gspFilename.empty() == false, "Parameter gspFilename is required for Gspan.");
    
    minsup = parameters.minsup;
    maxpat = parameters.maxpat;
    topk   = parameters.topk;
    
    gspFilename = parameters.gspFilename;
    read(gspFilename);
    
    doesUseMemoryBoost = parameters.doesUseMemoryBoost;
    
    init();

    return true;
}

SLGraphMiningInnerValues SLGspan::getInnerValues(SLGRAPHMININGINNERVALUE type) const
{
    ASSERT( type != SLGraphMiningInnerValueNotDefined, "inner values must be specified");
    
    SLGraphMiningInnerValues results;
    if ( type & SLGraphMiningInnerValueY )
    {
        MatrixXd y(transaction.size(), 1);
        for (int i = 0; i < y.size(); ++i)   y.col(0)[i] = transaction[i].value;
        
        results[SLGraphMiningInnerValueY] = y;
    }
    
    return results;
}

SLGraphMiningResult SLGspan::search(VectorXd residual, SLGRAPHMININGTASKTYPE taskType, SLGRAPHMININGRESULTYPE resultType)
{
    ASSERT(taskType != SLGraphMiningTasktypeNotDefined &&
           SLUtility::isIncludedOnlyOneType(taskType),
           "taskType must be specified either train or classify");
    
    this->taskType = taskType;
    
    rule_cache.clear();
    
    tau     = 0.0f;
    wbias   = 0.0f;

    size_t l = transaction.size();
    y.resize(l);
    
    if (!residual.size())
    {
        topk = 0;
        residual.resize(l);
        residual.setOnes();
    }
    
    w = residual.array().abs();
    if ( taskType & SLGraphMiningTasktypeTrain )
    {
        for (size_t i = 0; i < l; ++i)
        {
            if (residual[i] < 0)
            {
                y[i] = -1;
            }
            else
            {
                y[i] = 1;
            }
        }
    }

    if ( doesUseMemoryBoost )
    {
        vector<tree<TNODE>::pre_order_iterator> mount;

        tree<TNODE>::pre_order_iterator pre = memoryCache.begin();
        while ( pre != memoryCache.end() )
        {
            DFS_CODE.assign(pre->dfscode.begin(), pre->dfscode.end());
            
            bool p = can_prune(pre->projected);
            if ( pre->id == TNODEYETEXPLORE && !p){
                mount.push_back(pre);
            }
            ++pre;
        }
        
        vector<tree<TNODE>::pre_order_iterator>::iterator it = mount.begin();
        while ( it != mount.end() )
        {
            DFS_CODE.assign((*it)->dfscode.begin(), (*it)->dfscode.end());
            project((*it)->projected, (*it));
            ++it;
        }
    }
    else
    {
        for (Projected_iterator3 fromlabel = root.begin(); fromlabel != root.end(); ++fromlabel)
        {
            for (Projected_iterator2 elabel = fromlabel->second.begin(); elabel != fromlabel->second.end(); ++elabel)
            {
                for (Projected_iterator1 tolabel = elabel->second.begin();tolabel != elabel->second.end(); ++tolabel) {
                    DFS_CODE.push(0, 1, fromlabel->first, elabel->first, tolabel->first);
                    project(tolabel->second);
                    DFS_CODE.pop();
                }
            }
        }
    }
    
    if ( taskType == SLGraphMiningTasktypeTrain )
    {
        entireRules.insert(entireRules.end(), rule_cache.begin(), rule_cache.end());
    }
    else if ( taskType == SLGraphMiningTasktypeClassify )
    {
        // TODO:
    }
    
    SLGraphMiningResult result;
    vector<string> vDFS;
    if ( resultType & SLGraphMiningResultTypeX )
    {
        MatrixXd X(residual.rows(), rule_cache.size());
        X.setConstant(0.0f);
        
        size_t j = 0;
        for(set<Rule>::iterator it = rule_cache.begin(); it != rule_cache.end(); ++it)
        {
            for(size_t i = 0; i < it->loc.size(); ++i)
            {
                X(it->loc[i], j) = 1.0f;
            }
            ++j;
        }

        result[SLGraphMiningResultTypeX] = X;
    }
    
    if ( resultType & SLGraphMiningResultTypeDFS )
    {
        ostringstream osdfs;
    
        for(vector<Rule>::iterator it = entireRules.begin(); it != entireRules.end(); ++it)
        {
            vDFS.push_back((*it).dfs);
//            osdfs << (*it).dfs << endl;
        }
        result[SLGraphMiningResultTypeDFS] = vDFS;
    }
    
    return result;
}

// Private Methods
bool SLGspan::is_min()
{
    if (DFS_CODE.size() == 1) return true;
    
    DFS_CODE.toGraph(GRAPH_IS_MIN);
    DFS_CODE_IS_MIN.clear();
    
    Projected_map3 root;
    EdgeList       edges;
    
    for (size_t from = 0; from < GRAPH_IS_MIN.size() ; ++from)
        if (Utility::get_forward_root(GRAPH_IS_MIN, GRAPH_IS_MIN[from], edges))
            for (EdgeList::iterator it = edges.begin(); it != edges.end();  ++it)
                root[GRAPH_IS_MIN[from].label][(*it)->elabel][GRAPH_IS_MIN[(*it)->to].label].push(0, *it, 0);
    
    Projected_iterator3 fromlabel = root.begin();
    Projected_iterator2 elabel    = fromlabel->second.begin();
    Projected_iterator1 tolabel   = elabel->second.begin();
    
    DFS_CODE_IS_MIN.push(0, 1, fromlabel->first, elabel->first, tolabel->first);
    
    return project_is_min(tolabel->second);
}

bool SLGspan::project_is_min(Projected &projected)
{
    const RMPath& rmpath = DFS_CODE_IS_MIN.buildRMPath();
    int minlabel         = DFS_CODE_IS_MIN[0].fromlabel;
    int maxtoc           = DFS_CODE_IS_MIN[rmpath[0]].to;
    
    {
        Projected_map1 root;
        bool flg = false;
        int newto = 0;
        
        for (int i = (int)rmpath.size()-1; !flg  && i >= 1; --i) {
            for (size_t n = 0; n < projected.size(); ++n) {
                PDFS *cur = &projected[n];
                History history (GRAPH_IS_MIN, cur);
                Edge *e = Utility::get_backward (GRAPH_IS_MIN, history[rmpath[i]], history[rmpath[0]], history);
                if (e) {
                    root[e->elabel].push (0, e, cur);
                    newto = DFS_CODE_IS_MIN[rmpath[i]].from;
                    flg = true;
                }
            }
        }
        
        if (flg) {
            Projected_iterator1 elabel = root.begin();
            DFS_CODE_IS_MIN.push (maxtoc, newto, -1, elabel->first, -1);
            if (DFS_CODE[DFS_CODE_IS_MIN.size()-1] != DFS_CODE_IS_MIN [DFS_CODE_IS_MIN.size()-1]) return false;
            return project_is_min (elabel->second);
        }
    }
    
    {
        bool flg = false;
        int newfrom = 0;
        Projected_map2 root;
        EdgeList edges;
        
        for (size_t n = 0; n < projected.size(); ++n) {
            PDFS *cur = &projected[n];
            History history (GRAPH_IS_MIN, cur);
            if (Utility::get_forward_pure (GRAPH_IS_MIN, history[rmpath[0]], minlabel, history, edges)) {
                flg = true;
                newfrom = maxtoc;
                for (EdgeList::iterator it = edges.begin(); it != edges.end();  ++it)
                    root[(*it)->elabel][GRAPH_IS_MIN[(*it)->to].label].push (0, *it, cur);
            }
        }
        
        for (int i = 0; ! flg && i < (int)rmpath.size(); ++i) {
            for (size_t n = 0; n < projected.size(); ++n) {
                PDFS *cur = &projected[n];
                History history (GRAPH_IS_MIN, cur);
                if (Utility::get_forward_rmpath (GRAPH_IS_MIN, history[rmpath[i]], minlabel, history, edges)) {
                    flg = true;
                    newfrom = DFS_CODE_IS_MIN[rmpath[i]].from;
                    for (EdgeList::iterator it = edges.begin(); it != edges.end();  ++it)
                        root[(*it)->elabel][GRAPH_IS_MIN[(*it)->to].label].push (0, *it, cur);
                }
            }
        }
        
        if (flg) {
            Projected_iterator2 elabel  = root.begin();
            Projected_iterator1 tolabel = elabel->second.begin();
            DFS_CODE_IS_MIN.push (newfrom, maxtoc + 1, -1, elabel->first, tolabel->first);
            if (DFS_CODE[DFS_CODE_IS_MIN.size()-1] != DFS_CODE_IS_MIN [DFS_CODE_IS_MIN.size()-1]) return false;
            return project_is_min (tolabel->second);
        }
    }
    
    return true;
}

void SLGspan::read(string filename)
{
    ifstream ifs(filename.c_str(), ios::in);
    ASSERT(ifs.fail() == false, "Cannot open gsp file.");
    
    Graph g;
    while(true) {
        g.read(ifs);
        if (g.empty()) break;
        transaction.push_back(g);
    }
    ifs.close();
}

bool SLGspan::can_prune(Projected& projected)
{
    double gain = 0;
    double upos = 0;
    double uneg = 0;
    
    if( taskType & SLGraphMiningTasktypeClassify )
    {
        gain = -wbias;
        upos = -wbias;
        uneg = wbias;
    }
    
    size_t support = 0;
    int oid = UINT_MAX;
    int multi = (taskType & SLGraphMiningTasktypeTrain) ? 1 : 2;
    for (Projected::iterator it = projected.begin(); it != projected.end(); ++it)
    {
        if (oid != it->id)
        {
            gain += multi * y[it->id] * w[it->id];
            
            if (y[it->id] > 0)  upos += multi * w[it->id];
            else                uneg += multi * w[it->id];
            
            ++support;
        }
        oid = it->id;
    }
    
    double g = fabs(gain);
    if (support < minsup || max (upos, uneg) <= tau)
    {
        return true;
    }
    
    if (g > tau || (g == tau && DFS_CODE.size() < rule.size))
    {
        rule.gain = gain;
        rule.size = DFS_CODE.size();
        
        ostringstream ostrs;
        DFS_CODE.write (ostrs);
        rule.dfs = ostrs.str();
        
        rule.loc.clear ();
        int oid = UINT_MAX;
        for (Projected::iterator it = projected.begin(); it != projected.end(); ++it)
        {
            if (oid != it->id) { // remember used location using mask "oid"
                rule.loc.push_back(it->id);
            }
            oid = it->id;
        }
        
        if (find(rule_cache.begin(), rule_cache.end(), rule) == rule_cache.end() &&
            find(entireRules.begin(), entireRules.end(), rule) == entireRules.end())
        {
            rule_cache.insert(rule);
            if( topk > 0 && rule_cache.size() > topk)
            {
                // delete minimum gain rule
                set<Rule>::iterator it = rule_cache.begin();
                rule_cache.erase(it);
                ++it;
                tau = fabs(it->gain);
            }
        }
    }
    
    return false;
}

void SLGspan::project(Projected& projected)
{
    if ( taskType == SLGraphMiningTasktypeTrain )
    {
        if (!is_min ()                  ||
            maxpat == DFS_CODE.size()   ||
            can_prune(projected))
        {
            return;
        }
    }
    else if ( taskType == SLGraphMiningTasktypeClassify )
    {
        ostringstream ostrs;
        DFS_CODE.write (ostrs);
        
        vector<Rule>::iterator it;
        it = find(entireRules.begin(), entireRules.end(), ostrs.str());
        if ( it != entireRules.end() )
        {
            size_t index = it - entireRules.begin();
            patternMatchedResult.push_back(index);
        }
        
        if (maxpat == DFS_CODE.size()) return;
    }
    
    const RMPath &rmpath = DFS_CODE.buildRMPath ();
    int minlabel         = DFS_CODE[0].fromlabel;
    int maxtoc           = DFS_CODE[rmpath[0]].to;

    Projected_map3 new_fwd_root;
    Projected_map2 new_bck_root;
    EdgeList edges;
    
    for (size_t n = 0; n < projected.size(); ++n)
    {
        int id = projected[n].id;
        PDFS *cur = &projected[n];
        History history (transaction[id], cur);
        
        // backward
        for (int i = (int)rmpath.size()-1; i >= 1; --i) {
            Edge *e = Utility::get_backward (transaction[id], history[rmpath[i]], history[rmpath[0]], history);
            if (e) new_bck_root[DFS_CODE[rmpath[i]].from][e->elabel].push (id, e, cur);
        }
        
        // pure forward
        if (Utility::get_forward_pure (transaction[id], history[rmpath[0]], minlabel,
                                       history, edges)) {
            for (EdgeList::iterator it = edges.begin(); it != edges.end();  ++it) {
                new_fwd_root[maxtoc][(*it)->elabel][transaction[id][(*it)->to].label].push (id, *it, cur);
            }
        }
        
        // backtracked forward
        for (int i = 0; i < (int)rmpath.size(); ++i) {
            if (Utility::get_forward_rmpath (transaction[id], history[rmpath[i]], minlabel, history, edges)) {
                for (EdgeList::iterator it = edges.begin(); it != edges.end();  ++it)
                    new_fwd_root[DFS_CODE[rmpath[i]].from][(*it)->elabel][transaction[id][(*it)->to].label].push (id, *it, cur);
            }
        }
    }
    
    // backward (recursive)
    for (Projected_iterator2 to = new_bck_root.begin(); to != new_bck_root.end(); ++to) {
        for (Projected_iterator1 elabel = to->second.begin(); elabel != to->second.end(); ++elabel) {
            DFS_CODE.push (maxtoc, to->first, -1, elabel->first, -1);
            project(elabel->second);
            DFS_CODE.pop();
        }
    }
    
    // forward
    for (Projected_riterator3 from = new_fwd_root.rbegin(); from != new_fwd_root.rend(); ++from) {
        for (Projected_iterator2 elabel = from->second.begin(); elabel != from->second.end(); ++elabel) {
            for (Projected_iterator1 tolabel = elabel->second.begin();
                 tolabel != elabel->second.end(); ++tolabel) {
                DFS_CODE.push (from->first, maxtoc+1, -1, elabel->first, tolabel->first);
                project(tolabel->second);
                DFS_CODE.pop ();
            }
        }
    }
}

void SLGspan::project(Projected& projected, tree<TNODE>::iterator& tnode)
{
    bool p = false;
    if ( taskType == SLGraphMiningTasktypeTrain )
    {
        if (!is_min() || maxpat == DFS_CODE.size())
        {
            return;
        }
                
        p = can_prune(projected);
        if ( p && tnode->id == TNODEYETEXPLORE )
        {
            return;
        }
    }
    else if ( taskType == SLGraphMiningTasktypeClassify )
    {
        ostringstream ostrs;
        DFS_CODE.write (ostrs);
        
        vector<Rule>::iterator it;
        it = find(entireRules.begin(), entireRules.end(), ostrs.str());
        if ( it != entireRules.end() )
        {
            size_t index = it - entireRules.begin();
            patternMatchedResult.push_back(index);
        }
        
        if (maxpat == DFS_CODE.size()) return;
        
        p = can_prune(projected);
    }

    tree<TNODE>::iterator child;
        
    if (!p && (tnode->id == TNODEYETEXPLORE)){
//        printf("*");
        tnode->id = 0;
        child = tnode;  // not making a child
    }
    else{
//        printf(".");
        TNODE tn;
        
        tn.id = 0;
        tn.dfscode.clear();
        for (vector<DFS>::iterator it = DFS_CODE.begin() ; it != DFS_CODE.end() ; ++it){
            tn.dfscode.push_back(*it);
        }
        
        tn.projected.clear();
        for (Projected::iterator it=projected.begin() ; it != projected.end() ; ++it){
            tn.projected.push(it->id, it->edge, it->prev);
        }
        
        if (tnode != memoryCache.begin())
            child = memoryCache.append_child(tnode, tn);
        else
            child = memoryCache.insert(tnode, tn);
        
        if (p){
            child->id = TNODEYETEXPLORE;
            return; // Make mounting node and quit
        }
    }
    
    const RMPath &rmpath = DFS_CODE.buildRMPath();
    int minlabel         = DFS_CODE[0].fromlabel;
    int maxtoc           = DFS_CODE[rmpath[0]].to;

    Projected_map3 new_fwd_root;
    Projected_map2 new_bck_root;
    EdgeList edges;
    
    //printf("%d %d %d\n",projected.size(), child->projected.size(), child->id);
    
    for (size_t n = 0; n < child->projected.size(); ++n)
    {
        int id = child->projected[n].id;
        PDFS *cur = &(child->projected[n]);
        History history (transaction[id], cur);
        
        // backward
        for (int i = (int)rmpath.size()-1; i >= 1; --i) {
            Edge *e = Utility::get_backward (transaction[id], history[rmpath[i]], history[rmpath[0]], history);
            if (e) new_bck_root[DFS_CODE[rmpath[i]].from][e->elabel].push (id, e, cur);
        }
        
        // pure forward
        if (Utility::get_forward_pure (transaction[id], history[rmpath[0]], minlabel, history, edges)) {
            for (EdgeList::iterator it = edges.begin(); it != edges.end(); ++it) {
                new_fwd_root[maxtoc][(*it)->elabel][transaction[id][(*it)->to].label].push (id, *it, cur);
            }
        }
        
        // backtracked forward
        for (int i = 0; i < (int)rmpath.size(); ++i) {
            if (Utility::get_forward_rmpath (transaction[id], history[rmpath[i]], minlabel, history, edges)) {
                for (EdgeList::iterator it = edges.begin(); it != edges.end(); ++it)
                    new_fwd_root[DFS_CODE[rmpath[i]].from][(*it)->elabel][transaction[id][(*it)->to].label].push (id, *it, cur);
            }
        }
    }
    
    // backward (recursive)
    for (Projected_iterator2 to = new_bck_root.begin(); to != new_bck_root.end(); ++to) {
        for (Projected_iterator1 elabel = to->second.begin(); elabel != to->second.end(); ++elabel) {
            DFS_CODE.push(maxtoc, to->first, -1, elabel->first, -1);
            project(elabel->second, child);
            DFS_CODE.pop();
        }
    }
    
    // forward
    for (Projected_riterator3 from = new_fwd_root.rbegin(); from != new_fwd_root.rend(); ++from) {
        for (Projected_iterator2 elabel = from->second.begin(); elabel != from->second.end(); ++elabel) {
            for (Projected_iterator1 tolabel = elabel->second.begin(); tolabel != elabel->second.end(); ++tolabel) {
                DFS_CODE.push(from->first, maxtoc+1, -1, elabel->first, tolabel->first);
                project(tolabel->second, child);
                DFS_CODE.pop();
            }
        }
    }
    //printf("---\n");
}

void SLGspan::init()
{
    initDFSTree(root);
    
    if ( doesUseMemoryBoost )
    {
       initMemoryCache(root); 
    }
}

void SLGspan::initDFSTree(Projected_map3 &root)
{    
    EdgeList edges;
    
    for (size_t id = 0; id < transaction.size(); ++id)
    {
        Graph& g = transaction[id];
        for (size_t from = 0; from < g.size(); ++from)
        {
            if (Utility::get_forward_root(g, g[from], edges))
            {
                for (EdgeList::iterator it = edges.begin(); it != edges.end(); ++it)
                {
                    root[g[from].label][(*it)->elabel][g[(*it)->to].label].push(id, *it, 0);
                }
            }
        }
    }
}

void SLGspan::initMemoryCache(Projected_map3 &root)
{
    tree<TNODE>::iterator top = memoryCache.begin();
    for (Projected_iterator3 fromlabel = root.begin(); fromlabel != root.end(); ++fromlabel)
    {
        for (Projected_iterator2 elabel = fromlabel->second.begin(); elabel != fromlabel->second.end(); ++elabel)
        {
            for (Projected_iterator1 tolabel = elabel->second.begin(); tolabel != elabel->second.end(); ++tolabel)
            {
                DFS_CODE.push(0, 1, fromlabel->first, elabel->first, tolabel->first);
                
                TNODE tn;
                tn.id = TNODEYETEXPLORE;
                tn.dfscode.assign(DFS_CODE.begin(), DFS_CODE.end());
                
                for (Projected::iterator it = tolabel->second.begin() ; it != tolabel->second.end() ; ++it)
                {
                    tn.projected.push(it->id, it->edge, it->prev);
                }
                
                memoryCache.insert(top, tn);
                DFS_CODE.pop();
            }
        }
    }
}
