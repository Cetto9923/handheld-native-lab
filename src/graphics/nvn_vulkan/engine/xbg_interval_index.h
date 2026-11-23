#pragma once
#include <cstdint>
#include <vector>
#include "xbg_writeback_policy.h"

// Exact interval index over the pending writeback and pending report lists.
//
// Why it exists: the engine answers "does a pending range overlap this range"
// with a full linear scan of the list. While a frame is recorded the lists
// hold about 391 writebacks and 94 reports, and the engine asks about 1400
// (indexed readiness) plus 2400 (pending views) times per frame, so those
// scans cost about 2.05 ms of submission CPU per frame.
//
// Structure: a randomized treap keyed by (range start, id). Every node keeps
// max_end over its subtree, so a query prunes every subtree whose entries all
// end before the query starts. A query visits O(log n + k) nodes, k being the
// number of overlapping entries (normally zero), against n steps for a scan.
//
// The answers are the ones the scans gave:
//   - overlap is the half-open test of xbg_wb_overlaps, and a range that is
//     not valid never overlaps. Entries with an invalid range are counted and
//     the caller falls back to the scan while any exist, so a zero-length or
//     overflowing entry can never be answered differently by the index.
//   - the writeback queries keep the "newest overlapping entry wins" rule, so
//     the entry with the greatest id decides. Ids are handed out in push
//     order, which is also the order of the list, so the id doubles as the
//     version and as the newest-wins order.
//   - the report query only needs to know whether any report overlaps.
//
// Maintenance is incremental: one insert or erase per list mutation, each
// O(log n) and independent of how many bytes the entry covers.
//
// Reference: the classic augmented balanced search tree, an interval tree
// whose nodes carry the maximum end of their subtree. See OSS-NOTES.md.
struct XbgIntervalIndex {
    struct Node {
        uint64_t start=0,end=0,id=0,max_end=0;
        uint32_t left=0,right=0,priority=0;
    };
    std::vector<Node> nodes;              // nodes[0] is the null node
    std::vector<uint32_t> freelist;
    uint32_t root=0;
    uint64_t rng=0x9E3779B97F4A7C15ull;
    // Observation counters. Diagnostics only: the engine never reads them to
    // decide anything.
    uint64_t queries=0,hits=0,node_visits=0,inserts=0,erases=0,invalid_ranges=0;

    XbgIntervalIndex(){nodes.push_back(Node{});}

    // True while every live entry has a valid range, so the index answers
    // exactly what the scans answer.
    bool usable()const{return invalid_ranges==0;}
    size_t size()const{return nodes.size()-1-freelist.size();}

    bool insert(uint64_t id,uint64_t start,uint64_t bytes){
        if(!xbg_wb_range_valid(start,bytes)){++invalid_ranges;return false;}
        uint32_t i=alloc();
        Node &x=nodes[i];
        x.start=start;x.end=start+bytes;x.id=id;x.max_end=start+bytes;
        x.left=0;x.right=0;x.priority=next_priority();
        root=insert_rec(root,i);++inserts;return true;
    }
    void erase(uint64_t id,uint64_t start,uint64_t bytes){
        if(!xbg_wb_range_valid(start,bytes)){if(invalid_ranges)--invalid_ranges;}
        else root=erase_rec(root,start,id);
        ++erases;
    }
    void clear(){root=0;nodes.clear();nodes.push_back(Node{});freelist.clear();invalid_ranges=0;}

    // Newest overlapping entry (the greatest id). False when nothing overlaps.
    bool newest(uint64_t address,uint64_t bytes,uint64_t &out_id){
        ++queries;
        if(!xbg_wb_range_valid(address,bytes))return false;
        uint64_t a=address,b=address+bytes,best=0;
        bool found=false;
        collect(root,a,b,found,best);
        if(found){out_id=best;++hits;}
        return found;
    }
    // True when any entry overlaps.
    bool any(uint64_t address,uint64_t bytes){
        ++queries;
        if(!xbg_wb_range_valid(address,bytes))return false;
        uint64_t a=address,b=address+bytes;
        bool found=collect_any(root,a,b);
        if(found)++hits;
        return found;
    }

    // Structural comparison for the verification mode: both indexes must hold
    // the same entries. The treap is ordered by (start,id), so an in-order walk
    // compares them without sorting.
    bool same_contents(const XbgIntervalIndex &other)const{
        std::vector<const Node *> a,b;
        inorder(nodes,root,a);
        inorder(other.nodes,other.root,b);
        if(a.size()!=b.size())return false;
        for(size_t i=0;i<a.size();i++)
            if(a[i]->start!=b[i]->start||a[i]->id!=b[i]->id||a[i]->end!=b[i]->end)return false;
        return true;
    }

private:
    uint32_t alloc(){
        if(!freelist.empty()){uint32_t i=freelist.back();freelist.pop_back();return i;}
        nodes.push_back(Node{});
        return (uint32_t)nodes.size()-1;
    }
    void release(uint32_t i){nodes[i]=Node{};freelist.push_back(i);}
    uint32_t next_priority(){rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return (uint32_t)(rng>>32);}
    static bool key_less(uint64_t s1,uint64_t i1,uint64_t s2,uint64_t i2){return s1!=s2?s1<s2:i1<i2;}
    void pull(uint32_t i){
        Node &x=nodes[i];
        uint64_t m=x.end;
        if(x.left&&nodes[x.left].max_end>m)m=nodes[x.left].max_end;
        if(x.right&&nodes[x.right].max_end>m)m=nodes[x.right].max_end;
        x.max_end=m;
    }
    uint32_t rotate_right(uint32_t y){
        uint32_t x=nodes[y].left;
        nodes[y].left=nodes[x].right;nodes[x].right=y;
        pull(y);pull(x);return x;
    }
    uint32_t rotate_left(uint32_t x){
        uint32_t y=nodes[x].right;
        nodes[x].right=nodes[y].left;nodes[y].left=x;
        pull(x);pull(y);return y;
    }
    uint32_t insert_rec(uint32_t t,uint32_t i){
        if(!t)return i;
        if(key_less(nodes[i].start,nodes[i].id,nodes[t].start,nodes[t].id)){
            nodes[t].left=insert_rec(nodes[t].left,i);
            if(nodes[nodes[t].left].priority>nodes[t].priority)t=rotate_right(t);
        }else{
            nodes[t].right=insert_rec(nodes[t].right,i);
            if(nodes[nodes[t].right].priority>nodes[t].priority)t=rotate_left(t);
        }
        pull(t);return t;
    }
    uint32_t merge(uint32_t a,uint32_t b){
        if(!a)return b;
        if(!b)return a;
        if(nodes[a].priority>nodes[b].priority){nodes[a].right=merge(nodes[a].right,b);pull(a);return a;}
        nodes[b].left=merge(a,nodes[b].left);pull(b);return b;
    }
    uint32_t erase_rec(uint32_t t,uint64_t start,uint64_t id){
        if(!t)return 0;
        if(key_less(start,id,nodes[t].start,nodes[t].id))nodes[t].left=erase_rec(nodes[t].left,start,id);
        else if(key_less(nodes[t].start,nodes[t].id,start,id))nodes[t].right=erase_rec(nodes[t].right,start,id);
        else{
            uint32_t m=merge(nodes[t].left,nodes[t].right);
            release(t);
            return m;
        }
        pull(t);return t;
    }
    void collect(uint32_t t,uint64_t a,uint64_t b,bool &found,uint64_t &best){
        if(!t)return;
        ++node_visits;
        uint32_t l=nodes[t].left;
        // The left subtree holds smaller starts; it can only overlap when one of
        // its entries ends after the query starts.
        if(l&&nodes[l].max_end>a)collect(l,a,b,found,best);
        if(nodes[t].start<b&&nodes[t].end>a){
            if(!found||nodes[t].id>best){found=true;best=nodes[t].id;}
        }
        // The right subtree holds larger starts; it can only overlap while the
        // start of this node is still inside the query.
        if(nodes[t].start<b&&nodes[t].right)collect(nodes[t].right,a,b,found,best);
    }
    bool collect_any(uint32_t t,uint64_t a,uint64_t b){
        if(!t)return false;
        ++node_visits;
        uint32_t l=nodes[t].left;
        if(l&&nodes[l].max_end>a&&collect_any(l,a,b))return true;
        if(nodes[t].start<b&&nodes[t].end>a)return true;
        if(nodes[t].start<b&&nodes[t].right&&collect_any(nodes[t].right,a,b))return true;
        return false;
    }
    static void inorder(const std::vector<Node> &n,uint32_t t,std::vector<const Node *> &out){
        if(!t)return;
        inorder(n,n[t].left,out);
        out.push_back(&n[t]);
        inorder(n,n[t].right,out);
    }
};
