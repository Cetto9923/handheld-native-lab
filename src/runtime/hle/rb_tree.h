#ifndef FFRS_RUNTIME_HLE_RB_TREE_H
#define FFRS_RUNTIME_HLE_RB_TREE_H

#include <stddef.h>

enum { HLE_RB_BLACK = 0, HLE_RB_RED = 1 };

struct hle_rb_node {
    struct hle_rb_node *parent;
    struct hle_rb_node *child[2];
    int color;
};

#define HLE_RB_ENTRY(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

static inline int hle_rb_is_red(const struct hle_rb_node *n) {
    return n && n->color == HLE_RB_RED;
}

static inline struct hle_rb_node *hle_rb_first(struct hle_rb_node *n) {
    while (n && n->child[0]) n = n->child[0];
    return n;
}

static inline struct hle_rb_node *hle_rb_next(struct hle_rb_node *n) {
    if (!n) return NULL;
    if (n->child[1]) return hle_rb_first(n->child[1]);
    while (n->parent && n == n->parent->child[1]) n = n->parent;
    return n->parent;
}

static inline void hle_rb_rotate(struct hle_rb_node **root, struct hle_rb_node *x, int dir) {
    struct hle_rb_node *y = x->child[1 - dir];
    x->child[1 - dir] = y->child[dir];
    if (y->child[dir]) y->child[dir]->parent = x;
    y->parent = x->parent;
    if (!x->parent) *root = y;
    else x->parent->child[x == x->parent->child[1]] = y;
    y->child[dir] = x;
    x->parent = y;
}

static inline void hle_rb_link_and_insert(struct hle_rb_node **root,
                                          struct hle_rb_node *parent,
                                          struct hle_rb_node **link,
                                          struct hle_rb_node *z) {
    z->parent = parent;
    z->child[0] = z->child[1] = NULL;
    z->color = HLE_RB_RED;
    *link = z;
    while (z->parent && z->parent->color == HLE_RB_RED) {
        struct hle_rb_node *p = z->parent, *g = p->parent;
        int dir = (p == g->child[1]);
        struct hle_rb_node *u = g->child[1 - dir];
        if (hle_rb_is_red(u)) {
            p->color = HLE_RB_BLACK;
            u->color = HLE_RB_BLACK;
            g->color = HLE_RB_RED;
            z = g;
        } else {
            if (z == p->child[1 - dir]) {
                z = p;
                hle_rb_rotate(root, z, dir);
                p = z->parent;
                g = p->parent;
            }
            p->color = HLE_RB_BLACK;
            g->color = HLE_RB_RED;
            hle_rb_rotate(root, g, 1 - dir);
        }
    }
    (*root)->color = HLE_RB_BLACK;
}

static inline void hle_rb_transplant(struct hle_rb_node **root,
                                     struct hle_rb_node *u,
                                     struct hle_rb_node *v) {
    if (!u->parent) *root = v;
    else u->parent->child[u == u->parent->child[1]] = v;
    if (v) v->parent = u->parent;
}

static inline void hle_rb_erase(struct hle_rb_node **root, struct hle_rb_node *z) {
    struct hle_rb_node *y = z, *x, *x_parent;
    int y_orig_color = y->color;
    if (!z->child[0]) {
        x = z->child[1];
        x_parent = z->parent;
        hle_rb_transplant(root, z, z->child[1]);
    } else if (!z->child[1]) {
        x = z->child[0];
        x_parent = z->parent;
        hle_rb_transplant(root, z, z->child[0]);
    } else {
        y = hle_rb_first(z->child[1]);
        y_orig_color = y->color;
        x = y->child[1];
        if (y->parent == z) {
            x_parent = y;
        } else {
            x_parent = y->parent;
            hle_rb_transplant(root, y, y->child[1]);
            y->child[1] = z->child[1];
            y->child[1]->parent = y;
        }
        hle_rb_transplant(root, z, y);
        y->child[0] = z->child[0];
        y->child[0]->parent = y;
        y->color = z->color;
    }
    if (y_orig_color != HLE_RB_BLACK) return;
    while (x != *root && !hle_rb_is_red(x)) {
        int dir = (x == x_parent->child[1]);
        struct hle_rb_node *w = x_parent->child[1 - dir];
        if (hle_rb_is_red(w)) {
            w->color = HLE_RB_BLACK;
            x_parent->color = HLE_RB_RED;
            hle_rb_rotate(root, x_parent, dir);
            w = x_parent->child[1 - dir];
        }
        if (!hle_rb_is_red(w->child[0]) && !hle_rb_is_red(w->child[1])) {
            w->color = HLE_RB_RED;
            x = x_parent;
            x_parent = x->parent;
        } else {
            if (!hle_rb_is_red(w->child[1 - dir])) {
                if (w->child[dir]) w->child[dir]->color = HLE_RB_BLACK;
                w->color = HLE_RB_RED;
                hle_rb_rotate(root, w, 1 - dir);
                w = x_parent->child[1 - dir];
            }
            w->color = x_parent->color;
            x_parent->color = HLE_RB_BLACK;
            if (w->child[1 - dir]) w->child[1 - dir]->color = HLE_RB_BLACK;
            hle_rb_rotate(root, x_parent, dir);
            x = *root;
            break;
        }
    }
    if (x) x->color = HLE_RB_BLACK;
}

static inline struct hle_rb_node *hle_rb_pop_vine(struct hle_rb_node **root) {
    while (*root) {
        struct hle_rb_node *n = *root;
        if (n->child[0]) {
            hle_rb_rotate(root, n, 1);
            continue;
        }
        *root = n->child[1];
        if (*root) (*root)->parent = NULL;
        return n;
    }
    return NULL;
}

#endif
