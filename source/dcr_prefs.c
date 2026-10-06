/* Android SharedPreferences XML store, adapted from aks796/a8retry_nx (MIT).
 * TASM2 progress uses native save files as well as these Java preferences.
 * A retained backup protects FAT rename replacement and is recovered on load.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dcr_prefs.h"
#include "error.h"
#include "util.h"
#include <errno.h>

const char *dcr_game_root(void);

typedef struct {
  char type; /* 's' string, 'i' int, 'l' long, 'f' float, 'b' boolean */
  char *key;
  char *val; /* textual value */
} Entry;

struct DcrPrefs {
  char name[128];
  struct DcrPrefs *next;
  Entry *e;
  int n, cap;
  Mutex lock;
};

static DcrPrefs *g_stores;
static Mutex g_stores_lock;

/* ------------------------------------------------------------ helpers */
static char *xml_unescape(const char *s, size_t n) {
  char *out = malloc(n + 1), *o = out;
  if (!out)
    fatal_error("Out of memory reading preferences.");
  for (size_t i = 0; i < n;) {
    if (s[i] == '&') {
      static const struct {
        const char *ent;
        char c;
      } ents[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
      int done = 0;
      for (unsigned k = 0; k < sizeof ents / sizeof ents[0]; k++) {
        size_t l = strlen(ents[k].ent);
        if (i + l <= n && !strncmp(s + i, ents[k].ent, l)) {
          *o++ = ents[k].c;
          i += l;
          done = 1;
          break;
        }
      }
      if (!done && i + 3 < n && s[i + 1] == '#') {
        char *end;
        long v = s[i + 2] == 'x' ? strtol(s + i + 3, &end, 16) : strtol(s + i + 2, &end, 10);
        if (*end == ';' && v > 0 && v < 0x80) {
          *o++ = (char)v;
          i = (size_t)(end - s) + 1;
          done = 1;
        }
      }
      if (!done)
        *o++ = s[i++];
    } else {
      *o++ = s[i++];
    }
  }
  *o = 0;
  return out;
}

static void xml_escape(FILE *f, const char *s) {
  for (; *s; s++) {
    switch (*s) {
    case '&':
      fputs("&amp;", f);
      break;
    case '<':
      fputs("&lt;", f);
      break;
    case '>':
      fputs("&gt;", f);
      break;
    case '"':
      fputs("&quot;", f);
      break;
    default:
      fputc(*s, f);
      break;
    }
  }
}

static void path_for(const DcrPrefs *p, char *out, size_t cap, const char *suffix) {
  snprintf(out, cap, "%s/data/shared_prefs/%s.xml%s", dcr_game_root(), p->name, suffix);
}

static Entry *find(DcrPrefs *p, const char *key) {
  for (int i = 0; i < p->n; i++)
    if (!strcmp(p->e[i].key, key))
      return &p->e[i];
  return NULL;
}

static void set_locked(DcrPrefs *p, char type, const char *key, const char *val) {
  Entry *e = find(p, key);
  if (!e) {
    if (p->n == p->cap) {
      int cap = p->cap ? p->cap * 2 : 64;
      Entry *ne = realloc(p->e, (size_t)cap * sizeof *ne);
      if (!ne)
        fatal_error("Out of memory editing preferences.");
      p->e = ne;
      p->cap = cap;
    }
    e = &p->e[p->n++];
    e->key = strdup(key);
    e->val = NULL;
  }
  if (!e->key)
    fatal_error("Out of memory editing preferences.");
  free(e->val);
  e->type = type;
  e->val = strdup(val ? val : "");
  if (!e->val)
    fatal_error("Out of memory editing preferences.");
}

static void remove_locked(DcrPrefs *p, const char *key) {
  Entry *e = find(p, key);
  if (!e)
    return;
  free(e->key);
  free(e->val);
  *e = p->e[--p->n];
}

/* Attribute value of name="..." inside one tag. */
static char *attr(const char *tag, size_t tlen, const char *name) {
  char pat[32];
  snprintf(pat, sizeof pat, "%s=\"", name);
  const char *a = tag;
  while ((a = strstr(a, pat)) && a < tag + tlen) {
    if (a == tag || a[-1] == ' ' || a[-1] == '\t' || a[-1] == '\n') {
      a += strlen(pat);
      const char *e = strchr(a, '"');
      if (!e || e > tag + tlen)
        return NULL;
      return xml_unescape(a, (size_t)(e - a));
    }
    a++;
  }
  return NULL;
}

static void load(DcrPrefs *p) {
  char path[512];
  path_for(p, path, sizeof path, "");
  char backup[512];
  path_for(p, backup, sizeof backup, ".bak");
  if (access(backup, F_OK) == 0) {
    /* A surviving backup means the previous commit was interrupted. */
    if (unlink(path) != 0 && errno != ENOENT)
      fatal_error("Cannot recover preferences: %s", path);
    if (rename(backup, path) != 0)
      fatal_error("Cannot recover preferences backup: %s", backup);
    fsdevCommitDevice("sdmc");
    debugPrintf("[prefs] recovered %s from interrupted commit\n", p->name);
  }
  FILE *f = fopen(path, "rb");
  if (!f)
    return;
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  if (len < 0 || len > 8 * 1024 * 1024) {
    fclose(f);
    fatal_error("Invalid preferences file: %s", path);
  }
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)len + 1);
  if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
    free(buf);
    fclose(f);
    debugPrintf("[prefs] %s: read failed\n", path);
    return;
  }
  fclose(f);
  buf[len] = 0;

  static const struct {
    const char *tag;
    char type;
  } kinds[] = {
      {"<string ", 's'}, {"<int ", 'i'}, {"<long ", 'l'}, {"<float ", 'f'}, {"<boolean ", 'b'}};
  for (char *c = buf; (c = strchr(c, '<'));) {
    int k = -1;
    for (unsigned j = 0; j < sizeof kinds / sizeof kinds[0]; j++)
      if (!strncmp(c, kinds[j].tag, strlen(kinds[j].tag)))
        k = (int)j;
    char *gt = strchr(c, '>');
    if (!gt)
      break;
    if (k < 0) {
      c = gt + 1;
      continue;
    }
    size_t tlen = (size_t)(gt - c);
    char *key = attr(c, tlen, "name");
    if (key) {
      if (kinds[k].type == 's') {
        char *val = NULL;
        if (gt[-1] == '/') {
          val = strdup("");
        } else {
          char *close = strstr(gt + 1, "</string>");
          if (close) {
            val = xml_unescape(gt + 1, (size_t)(close - gt - 1));
            gt = close + 8;
          }
        }
        if (val)
          set_locked(p, 's', key, val);
        free(val);
      } else {
        char *val = attr(c, tlen, "value");
        if (val)
          set_locked(p, kinds[k].type, key, val);
        free(val);
      }
      free(key);
    }
    c = gt + 1;
  }
  free(buf);
  debugPrintf("[prefs] %s: %d entries loaded\n", p->name, p->n);
}

static int save_locked(DcrPrefs *p) {
  char path[512], tmp[512], backup[512];
  path_for(p, path, sizeof path, "");
  path_for(p, tmp, sizeof tmp, ".tmp");
  path_for(p, backup, sizeof backup, ".bak");
  FILE *f = fopen(tmp, "wb");
  if (!f) {
    debugPrintf("[prefs] cannot write %s\n", tmp);
    return -1;
  }
  fputs("<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\n<map>\n", f);
  for (int i = 0; i < p->n; i++) {
    const Entry *e = &p->e[i];
    static const char *const names[] = {"int", "long", "float", "boolean"};
    const char *tn = e->type == 'i'   ? names[0]
                     : e->type == 'l' ? names[1]
                     : e->type == 'f' ? names[2]
                     : e->type == 'b' ? names[3]
                                      : NULL;
    fputs("    <", f);
    fputs(tn ? tn : "string", f);
    fputs(" name=\"", f);
    xml_escape(f, e->key);
    if (tn) {
      fputs("\" value=\"", f);
      xml_escape(f, e->val);
      fputs("\" />\n", f);
    } else {
      fputs("\">", f);
      xml_escape(f, e->val);
      fputs("</string>\n", f);
    }
  }
  fputs("</map>\n", f);
  int ok = fflush(f) == 0 && !ferror(f);
  if (ok && fsync(fileno(f)) != 0)
    ok = 0;
  if (fclose(f) != 0)
    ok = 0;
  if (!ok) {
    unlink(tmp);
    debugPrintf("[prefs] write of %s failed; previous save kept\n", path);
    return -1;
  }
  fsdevCommitDevice("sdmc");
  int had_old = access(path, F_OK) == 0;
  if (had_old && rename(path, backup) != 0) {
    unlink(tmp);
    debugPrintf("[prefs] could not retain previous %s\n", path);
    return -1;
  }
  fsdevCommitDevice("sdmc");
  if (rename(tmp, path) != 0) {
    if (had_old && rename(backup, path) != 0)
      debugPrintf("[prefs] recovery deferred to next launch: %s\n", backup);
    unlink(tmp);
    debugPrintf("[prefs] publish %s failed; previous save retained\n", path);
    fsdevCommitDevice("sdmc");
    return -1;
  }
  fsdevCommitDevice("sdmc");
  if (had_old)
    unlink(backup);
  fsdevCommitDevice("sdmc");
  debugPrintf("[save] preferences %s: %d entries committed\n", p->name, p->n);
  return 0;
}

/* ------------------------------------------------------------- public */
DcrPrefs *dcr_prefs_open(const char *name) {
  name = name && *name ? name : "default";
  if (strlen(name) >= 128 || strpbrk(name, "/\\:") || !strcmp(name, ".") || !strcmp(name, ".."))
    fatal_error("Invalid preference store name.");
  mutexLock(&g_stores_lock);
  for (DcrPrefs *p = g_stores; p; p = p->next) {
    if (!strcmp(p->name, name)) {
      mutexUnlock(&g_stores_lock);
      return p;
    }
  }
  DcrPrefs *p = calloc(1, sizeof *p);
  if (!p)
    fatal_error("Out of memory opening preferences.");
  strcpy(p->name, name);
  mutexInit(&p->lock);
  load(p);
  p->next = g_stores;
  g_stores = p;
  mutexUnlock(&g_stores_lock);
  return p;
}

char *dcr_prefs_get(DcrPrefs *p, const char *key, char *type_out) {
  mutexLock(&p->lock);
  Entry *e = find(p, key);
  char *v = e ? strdup(e->val) : NULL;
  if (type_out)
    *type_out = e ? e->type : 0;
  mutexUnlock(&p->lock);
  return v;
}

int dcr_prefs_count(DcrPrefs *p) { return p->n; }

int dcr_prefs_entry(DcrPrefs *p, int i, const char **key, const char **val, char *type) {
  mutexLock(&p->lock);
  int ok = i >= 0 && i < p->n;
  if (ok) {
    if (key)
      *key = p->e[i].key;
    if (val)
      *val = p->e[i].val;
    if (type)
      *type = p->e[i].type;
  }
  mutexUnlock(&p->lock);
  return ok;
}

/* ----- editor: staged changes, applied together ----- */
struct DcrPrefsEdit {
  DcrPrefs *p;
  int clear;
  int n, cap;
  struct {
    char op;
    char type;
    char *key;
    char *val;
  } *ops; /* op: 's'et / 'r'emove */
};

DcrPrefsEdit *dcr_prefs_edit(DcrPrefs *p) {
  DcrPrefsEdit *e = calloc(1, sizeof *e);
  if (!e)
    fatal_error("Out of memory staging preferences.");
  e->p = p;
  return e;
}

static void edit_push(DcrPrefsEdit *e, char op, char type, const char *key, const char *val) {
  if (e->n == e->cap) {
    int cap = e->cap ? e->cap * 2 : 16;
    void *n = realloc(e->ops, (size_t)cap * sizeof *e->ops);
    if (!n)
      fatal_error("Out of memory staging preferences.");
    e->ops = n;
    e->cap = cap;
  }
  e->ops[e->n].op = op;
  e->ops[e->n].type = type;
  e->ops[e->n].key = strdup(key ? key : "");
  e->ops[e->n].val = val ? strdup(val) : NULL;
  if (!e->ops[e->n].key || (val && !e->ops[e->n].val))
    fatal_error("Out of memory staging preferences.");
  e->n++;
}

void dcr_prefs_put(DcrPrefsEdit *e, char type, const char *key, const char *val) {
  edit_push(e, 's', type, key, val);
}
void dcr_prefs_remove(DcrPrefsEdit *e, const char *key) { edit_push(e, 'r', 0, key, NULL); }
void dcr_prefs_clear(DcrPrefsEdit *e) {
  e->clear = 1;
  for (int i = 0; i < e->n; i++) {
    free(e->ops[i].key);
    free(e->ops[i].val);
  }
  e->n = 0;
}

static void entries_free(Entry *e, int n) {
  for (int i = 0; i < n; i++) {
    free(e[i].key);
    free(e[i].val);
  }
  free(e);
}
int dcr_prefs_commit(DcrPrefsEdit *e) {
  DcrPrefs *p = e->p;
  mutexLock(&p->lock);
  int oldn = p->n, changed = e->clear && oldn;
  Entry *old = calloc(oldn ? oldn : 1, sizeof *old);
  if (!old)
    fatal_error("Out of memory preserving preferences.");
  for (int i = 0; i < oldn; i++) {
    old[i] = (Entry){p->e[i].type, strdup(p->e[i].key), strdup(p->e[i].val)};
    if (!old[i].key || !old[i].val)
      fatal_error("Out of memory preserving preferences.");
  }
  if (e->clear) {
    entries_free(p->e, p->n);
    p->e = NULL;
    p->n = p->cap = 0;
  }
  for (int i = 0; i < e->n; i++) {
    Entry *found = find(p, e->ops[i].key);
    if (e->ops[i].op == 's') {
      if (!found || found->type != e->ops[i].type || strcmp(found->val, e->ops[i].val)) {
        set_locked(p, e->ops[i].type, e->ops[i].key, e->ops[i].val);
        changed = 1;
      }
    } else if (found) {
      remove_locked(p, e->ops[i].key);
      changed = 1;
    }
    free(e->ops[i].key);
    free(e->ops[i].val);
  }
  e->n = e->clear = 0;
  int r = changed ? save_locked(p) : 0;
  if (r) {
    entries_free(p->e, p->n);
    p->e = old;
    p->n = p->cap = oldn;
  } else
    entries_free(old, oldn);
  mutexUnlock(&p->lock);
  return r;
}

void dcr_prefs_edit_free(DcrPrefsEdit *e) {
  if (!e)
    return;
  for (int i = 0; i < e->n; i++) {
    free(e->ops[i].key);
    free(e->ops[i].val);
  }
  free(e->ops);
  free(e);
}
