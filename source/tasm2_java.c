/* TASM2 Java service bridge. Generic MD5, identity, Bundle and preferences
 * adapted from aks796/a8retry_nx (MIT); TASM2 bindings verified from 1.2.8d. */
#include "config.h"
#include "dcr_manifest.h"
#include "dcr_path.h"
#include "dcr_prefs.h"
#include "dcr_setup.h"
#include "error.h"
#include "rt_applet.h"
#include "rt_cfg.h"
#include "tasm2.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#define PKG PORT_PACKAGE
#define A_FILES "/data/data/" PKG "/files"
#define A_CACHE "/data/data/" PKG "/cache"
#define A_APK "/data/app/" PKG "-1/base.apk"
#define S "Ljava/lang/String;"
#define H(fn) static jvalue fn(JObj *self, const jvalue *a, const JMethod *m)
static const char *arg_str(const jvalue *a, int i) { return jni_utf(a[i].l); }
JObj *g_activity;
int tasm2_in_action, tasm2_hid_listening;
char tasm2_device_id[40];
void *tasm2_class(const char *name) { return jni_class(name)->obj; }
H(h_void) { return jv_none(); }
H(h_false) { return jv_z(0); }
H(h_true) { return jv_z(1); }
H(h_zero) { return jv_i(0); }
H(h_empty) { return jv_l(jni_str("")); }
uint8_t *tasm2_apk_asset(const char *, size_t *);
int tasm2_signature_hash(void);
#include <time.h>
void tasm2_set_view(int, int, int, int, int);
static void tasm2_file_finalize(JObj *o) { free(o->p); }
typedef struct {
  uint32_t s[4];
  uint64_t n;
  uint8_t buf[64];
} Md5;

/* Scalar on purpose: GCC's NEON form of the rotates (vsri) is one Ryujinx's
 * A32 decoder lacks, and this runs once. */
__attribute__((optimize("no-tree-vectorize", "no-tree-slp-vectorize"))) static void
md5_block(Md5 *c, const uint8_t *p) {
  static const uint32_t K[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613,
      0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193,
      0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d,
      0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
      0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122,
      0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
      0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244,
      0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
      0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb,
      0xeb86d391};
  static const uint8_t R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
  uint32_t w[16];
  for (int i = 0; i < 16; i++)
    w[i] = (uint32_t)p[i * 4] | (uint32_t)p[i * 4 + 1] << 8 | (uint32_t)p[i * 4 + 2] << 16 |
           (uint32_t)p[i * 4 + 3] << 24;
  uint32_t A = c->s[0], B = c->s[1], C = c->s[2], D = c->s[3];
  for (int i = 0; i < 64; i++) {
    uint32_t f;
    int g;
    if (i < 16)
      f = (B & C) | (~B & D), g = i;
    else if (i < 32)
      f = (D & B) | (~D & C), g = (5 * i + 1) & 15;
    else if (i < 48)
      f = B ^ C ^ D, g = (3 * i + 5) & 15;
    else
      f = C ^ (B | ~D), g = (7 * i) & 15;
    uint32_t t = D;
    D = C;
    C = B;
    uint32_t x = A + f + K[i] + w[g];
    B = B + ((x << R[i]) | (x >> (32 - R[i])));
    A = t;
  }
  c->s[0] += A, c->s[1] += B, c->s[2] += C, c->s[3] += D;
}

static void md5(const void *data, size_t len, uint8_t out[16]) {
  Md5 c = {{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476}, 0, {0}};
  const uint8_t *p = data;
  size_t full = len & ~(size_t)63;
  for (size_t i = 0; i < full; i += 64)
    md5_block(&c, p + i);
  uint8_t tail[128] = {0};
  size_t rest = len - full;
  memcpy(tail, p + full, rest);
  tail[rest] = 0x80;
  size_t tl = rest < 56 ? 64 : 128;
  uint64_t bits = (uint64_t)len * 8;
  for (int i = 0; i < 8; i++)
    tail[tl - 8 + i] = (uint8_t)(bits >> (8 * i));
  md5_block(&c, tail);
  if (tl == 128)
    md5_block(&c, tail + 64);
  for (int i = 0; i < 4; i++)
    for (int k = 0; k < 4; k++)
      out[i * 4 + k] = (uint8_t)(c.s[i] >> (8 * k));
}

/* SUtils.getInt: big-endian */
static int32_t be32(const uint8_t *b) {
  return (int32_t)((uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3]);
}

static JObj *md5_ints(const char *text) {
  uint8_t d[16];
  md5(text, strlen(text), d);
  JObj *arr = jni_array('I', 4);
  for (int i = 0; i < 4; i++)
    ((jint *)arr->a.data)[i] = be32(d + 4 * i);
  return arr;
}

/* ============================================================= identity */
/* A device id made once and kept, as a phone's would be (HDIDFV is
 * Gameloft's own id: a UUID shared between its games). */
static char g_uuid[40], g_android_id[20];

static void identity_init(void) {
  DcrPrefs *p = dcr_prefs_open("tasm2_device");
  char t;
  char *u = dcr_prefs_get(p, "hdidfv", &t), *id = dcr_prefs_get(p, "android_id", &t);
  if (u && id && strlen(u) == 36 && strlen(id) == 16) {
    snprintf(g_uuid, sizeof g_uuid, "%s", u);
    snprintf(g_android_id, sizeof g_android_id, "%s", id);
  } else {
    uint8_t r[24];
    randomGet(r, sizeof r);
    r[6] = (uint8_t)((r[6] & 0x0f) | 0x40); /* version 4 */
    r[8] = (uint8_t)((r[8] & 0x3f) | 0x80);
    snprintf(g_uuid, sizeof g_uuid,
             "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", r[0], r[1],
             r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13], r[14],
             r[15]);
    snprintf(g_android_id, sizeof g_android_id, "%02x%02x%02x%02x%02x%02x%02x%02x", r[16], r[17],
             r[18], r[19], r[20], r[21], r[22], r[23]);
    DcrPrefsEdit *e = dcr_prefs_edit(p);
    dcr_prefs_put(e, 's', "hdidfv", g_uuid);
    dcr_prefs_put(e, 's', "android_id", g_android_id);
    if (dcr_prefs_commit(e))
      fatal_error("Cannot persist the local device identity. Check SD space and write access.");
    dcr_prefs_edit_free(e);
    debugPrintf("[java] new device identity %s\n", g_uuid);
  }
  free(u);
  free(id);
}

#define DEVICE_MANUFACTURER "Nintendo"
#define DEVICE_MODEL "Switch"
#define USER_AGENT                                                                                 \
  "Mozilla/5.0 (Linux; Android 9; " DEVICE_MODEL                                                   \
  ") AppleWebKit/537.36 (KHTML, like Gecko) Mobile Safari/537.36"

/* ======================================================= SharedPreferences */
static const char *pref_file(const char *name) { return name && *name ? name : "GamePrefs"; }

static char *pref_get(const char *file, const char *key, char *type) {
  return dcr_prefs_get(dcr_prefs_open(pref_file(file)), key, type);
}

static void pref_put(const char *file, char type, const char *key, const char *val) {
  DcrPrefsEdit *e = dcr_prefs_edit(dcr_prefs_open(pref_file(file)));
  dcr_prefs_put(e, type, key, val);
  dcr_prefs_commit(e);
  dcr_prefs_edit_free(e);
}

/* ================================================================ Bundle */
typedef struct BEntry {
  struct BEntry *next;
  char key[64];
  char type; /* s i l z b */
  jlong num;
  char *str;
  JObj *bytes;
} BEntry;

static void bundle_finalize(JObj *o) {
  for (BEntry *e = o->p, *n; e; e = n) {
    n = e->next;
    free(e->str);
    jni_release(e->bytes);
    free(e);
  }
  o->p = NULL;
}

static BEntry *bundle_find(JObj *o, const char *key, int create) {
  if (!o)
    return NULL;
  for (BEntry *e = o->p; e; e = e->next)
    if (!strcmp(e->key, key))
      return e;
  if (!create)
    return NULL;
  BEntry *e = calloc(1, sizeof *e);
  if (!e)
    return NULL;
  snprintf(e->key, sizeof e->key, "%s", key);
  e->next = o->p;
  o->p = e;
  o->finalize = bundle_finalize;
  return e;
}

static void bundle_clear_value(BEntry *e) {
  free(e->str);
  e->str = NULL;
  jni_release(e->bytes);
  e->bytes = NULL;
  e->num = 0;
}

H(h_bundle_init) {
  self->finalize = bundle_finalize;
  return jv_none();
}
H(h_bundle_putString) {
  BEntry *e = bundle_find(self, arg_str(a, 0), 1);
  if (e) {
    bundle_clear_value(e);
    e->type = 's';
    e->str = a[1].l ? strdup(jni_utf(a[1].l)) : NULL;
  }
  return jv_none();
}
H(h_bundle_getString) {
  BEntry *e = bundle_find(self, arg_str(a, 0), 0);
  return jv_l(e && e->type == 's' && e->str ? jni_str(e->str) : NULL);
}
static void bundle_put_num(JObj *self, const char *key, char type, jlong v) {
  BEntry *e = bundle_find(self, key, 1);
  if (e) {
    bundle_clear_value(e);
    e->type = type;
    e->num = v;
  }
}
static jlong bundle_get_num(JObj *self, const char *key) {
  BEntry *e = bundle_find(self, key, 0);
  return e && e->type != 's' && e->type != 'b' ? e->num : 0;
}
H(h_bundle_putInt) {
  bundle_put_num(self, arg_str(a, 0), 'i', a[1].i);
  return jv_none();
}
H(h_bundle_getInt) { return jv_i((jint)bundle_get_num(self, arg_str(a, 0))); }
H(h_bundle_putLong) {
  bundle_put_num(self, arg_str(a, 0), 'l', a[1].j);
  return jv_none();
}
H(h_bundle_getLong) { return jv_j(bundle_get_num(self, arg_str(a, 0))); }
H(h_bundle_putBoolean) {
  bundle_put_num(self, arg_str(a, 0), 'z', a[1].z != 0);
  return jv_none();
}
H(h_bundle_getBoolean) { return jv_z(bundle_get_num(self, arg_str(a, 0)) != 0); }
H(h_bundle_containsKey) { return jv_z(bundle_find(self, arg_str(a, 0), 0) != NULL); }
H(h_bundle_clear) {
  bundle_finalize(self);
  return jv_none();
}
H(h_bundle_putByteArray) {
  BEntry *e = bundle_find(self, arg_str(a, 0), 1);
  if (e) {
    bundle_clear_value(e);
    e->type = 'b';
    e->bytes = jni_retain(a[1].l);
  }
  return jv_none();
}
H(h_bundle_getByteArray) {
  BEntry *e = bundle_find(self, arg_str(a, 0), 0);
  return jv_l(e && e->type == 'b' ? jni_retain(e->bytes) : NULL);
}

/* SUtils.nativeGetPreference / nativeSetPreference: npDataType 0 int, 1 long,
 * 2 boolean, 3 String; npKey, npPrefName; npData / npDefaultValue -> npResult. */
H(h_nativeGetPreference) {
  JObj *b = a[0].l;
  if (!b)
    return jv_l(NULL);
  int type = (int)bundle_get_num(b, "npDataType");
  BEntry *ke = bundle_find(b, "npKey", 0), *fe = bundle_find(b, "npPrefName", 0);
  const char *key = ke && ke->str ? ke->str : "", *file = fe && fe->str ? fe->str : "";
  char t = 0;
  char *v = pref_get(file, key, &t);
  switch (type) {
  case 0:
    bundle_put_num(b, "npResult", 'i',
                   v ? (jint)strtol(v, NULL, 10) : (jint)bundle_get_num(b, "npDefaultValue"));
    break;
  case 1:
    bundle_put_num(b, "npResult", 'l',
                   v ? (jlong)strtoll(v, NULL, 10) : bundle_get_num(b, "npDefaultValue"));
    break;
  case 2:
    bundle_put_num(b, "npResult", 'z',
                   v ? !strcmp(v, "true") : bundle_get_num(b, "npDefaultValue") != 0);
    break;
  case 3: {
    BEntry *d = bundle_find(b, "npDefaultValue", 0);
    BEntry *r = bundle_find(b, "npResult", 1);
    if (r) {
      bundle_clear_value(r);
      r->type = 's';
      r->str = strdup(v ? v : d && d->str ? d->str : "");
    }
    break;
  }
  }
  free(v);
  return jv_l(jni_retain(b));
}

H(h_nativeSetPreference) {
  JObj *b = a[0].l;
  if (!b)
    return jv_none();
  int type = (int)bundle_get_num(b, "npDataType");
  BEntry *ke = bundle_find(b, "npKey", 0), *fe = bundle_find(b, "npPrefName", 0);
  const char *key = ke && ke->str ? ke->str : "", *file = fe && fe->str ? fe->str : "";
  char v[64];
  switch (type) {
  case 0:
    snprintf(v, sizeof v, "%d", (int)bundle_get_num(b, "npData"));
    pref_put(file, 'i', key, v);
    break;
  case 1:
    snprintf(v, sizeof v, "%lld", (long long)bundle_get_num(b, "npData"));
    pref_put(file, 'l', key, v);
    break;
  case 2:
    pref_put(file, 'b', key, bundle_get_num(b, "npData") ? "true" : "false");
    break;
  case 3: {
    BEntry *d = bundle_find(b, "npData", 0);
    pref_put(file, 's', key, d && d->str ? d->str : "");
    break;
  }
  }
  return jv_none();
}

H(h_getMetaDataValue) {
  const DcrMeta *md = dcr_manifest_meta(arg_str(a, 0));
  if (!md)
    return jv_l(jni_str(""));
  if (md->type == DCR_META_STRING)
    return jv_l(jni_str(md->s));
  if (md->type == DCR_META_FLOAT)
    return jv_l(jni_str_fmt("%g", (double)md->f));
  if (md->type == DCR_META_BOOL)
    return jv_l(jni_str(md->i ? "true" : "false"));
  return jv_l(jni_str_fmt("%d", (int)md->i));
}
H(h_getAssetAsString) {
  const char *name = arg_str(a, 0);
  if (name[0] == '.')
    name++;
  size_t len = 0;
  uint8_t *d = tasm2_apk_asset(name, &len);
  if (!d) {
    debugPrintf("[java] getAssetAsString(%s): not in the APK\n", arg_str(a, 0));
    return jv_l(NULL);
  }
  JObj *arr = jni_array('B', (jsize)len);
  memcpy(arr->a.data, d, len);
  free(d);
  return jv_l(arr);
}
/* getGLUID(String): MD5 of the string + the game's package, as 4 ints */
H(h_getGLUID_str) {
  char text[512];
  snprintf(text, sizeof text, "%s%s", arg_str(a, 0), PKG);
  return jv_l(md5_ints(text));
}
/* SendInfo.getGLUID(): the same over its GLDID and SendInfo's package */
H(h_sendinfo_getGLUID) {
  char text[256];
  snprintf(text, sizeof text, "%s%s", g_android_id, PKG);
  return jv_l(md5_ints(text));
}
/* retrieveBarrels: the hashCode()s of the APK's signatures (one) */
H(h_retrieveBarrels) {
  JObj *arr = jni_array('I', 1);
  ((jint *)arr->a.data)[0] = tasm2_signature_hash();
  return jv_l(arr);
}

H(h_androidId) { return jv_l(jni_str(g_android_id)); }
H(h_hdidfv) { return jv_l(jni_str(g_uuid)); }
H(h_hdidfvVersion) { return jv_l(jni_str("1")); }
H(h_gldid) {
  return jv_l(jni_str_fmt("hdidfv=%s imei= mac=02:00:00:00:00:00 aid=%s serialNo=%s ", g_uuid,
                          g_android_id, g_android_id));
}
H(h_serial) { return jv_l(jni_str(g_android_id)); }
H(h_mac) { return jv_l(jni_str("02:00:00:00:00:00")); }
H(h_manufacturer) { return jv_l(jni_str(DEVICE_MANUFACTURER)); }
H(h_model) { return jv_l(jni_str(DEVICE_MODEL)); }
H(h_deviceName) { return jv_l(jni_str(DEVICE_MANUFACTURER " " DEVICE_MODEL)); }
H(h_manufacturerModel) { return jv_l(jni_str(DEVICE_MANUFACTURER "_" DEVICE_MODEL)); }
H(h_country) { return jv_l(jni_str("US")); }
H(h_country_lower) { return jv_l(jni_str("us")); }
H(h_language) { return jv_l(jni_str("en")); }
H(h_locale) { return jv_l(jni_str("en_US")); }
H(h_product) { return jv_l(jni_str("nx")); }

/* ========================================================= DataSharing */
/* Values Gameloft games share on a device (a content provider): kept in the
 * "gameloft_sharing" preferences here. */
H(h_ds_set) {
  pref_put("gameloft_sharing", 's', arg_str(a, 0), arg_str(a, 1));
  return jv_none();
}
H(h_ds_get) {
  char t;
  char *v = pref_get("gameloft_sharing", arg_str(a, 0), &t);
  JObj *r = jni_str(v ? v : "");
  free(v);
  return jv_l(r);
}
H(h_ds_delete) {
  DcrPrefsEdit *e = dcr_prefs_edit(dcr_prefs_open("gameloft_sharing"));
  dcr_prefs_remove(e, arg_str(a, 0));
  dcr_prefs_commit(e);
  dcr_prefs_edit_free(e);
  return jv_none();
}
H(h_ds_is) {
  char t;
  char *v = pref_get("gameloft_sharing", arg_str(a, 0), &t);
  int r = v != NULL;
  free(v);
  return jv_z(r);
}

H(h_setupPaths) {
  tasm2_setup_paths();
  return jv_none();
}
H(h_context) { return jv_z(tasm2_context(a[0].i)); }
H(h_view) {
  tasm2_set_view(a[0].i, a[1].i, a[2].i, a[3].i, a[4].i);
  return jv_none();
}
H(h_width) { return jv_i(1280); }
H(h_height) { return jv_i(720); }
H(h_action) {
  tasm2_in_action = a[0].z;
  return jv_none();
}
H(h_listener) {
  tasm2_hid_listening = !strcmp(m->name, "NativeListenerRegistered");
  return jv_none();
}
H(h_files) { return jv_l(jni_str(A_FILES)); }
H(h_sd) { return jv_l(jni_str(A_RESOURCE)); }
H(h_obb) { return jv_l(jni_str(A_OBB)); }
H(h_apk) { return jv_l(jni_str(A_APK)); }
H(h_pkg) { return jv_l(jni_str(PKG)); }
H(h_name) { return jv_l(jni_str("ANMP.GloftASHM")); }
H(h_activity) { return jv_l(g_activity); }
H(h_cpu) { return jv_f(1.02f); }
H(h_ram) { return jv_f(3072.0f); }
float tasm2_free_disk_kb(void);
/* SUtils DEX returns KiB. The native JGetFreeDiskSpace consumer at 0x3c6744
 * multiplies its float result by 1/1024 to obtain MiB, so it also needs KiB. */
H(h_space_kb) { return jv_f(tasm2_free_disk_kb()); }
H(h_uptime) { return jv_i((jint)(armTicksToNs(armGetSystemTick()) / 1000000000ull)); }
H(h_exit) {
  rt_request_exit();
  return jv_none();
}
H(h_prefString) {
  char t;
  int three = strstr(m->sig, S S S) != NULL;
  char *v = pref_get(arg_str(a, three ? 2 : 1), arg_str(a, 0), &t);
  JObj *s = jni_str(v ? v : three ? arg_str(a, 1) : "");
  free(v);
  return jv_l(s);
}
H(h_resource) { return h_getAssetAsString(self, a, m); }
H(h_file) {
  JObj *o = jni_new("java/io/File");
  o->p = strdup(!strcmp(m->name, "getObbDir")     ? A_OBB
                : !strcmp(m->name, "getCacheDir") ? A_CACHE
                                                  : A_FILES);
  o->finalize = tasm2_file_finalize;
  return jv_l(o);
}
H(h_filePath) { return jv_l(jni_str(self->p ? self->p : "")); }
H(h_clock) {
  struct timespec now;
  if (clock_gettime(CLOCK_REALTIME, &now))
    return jv_j((jlong)time(NULL) * 1000);
  return jv_j((jlong)now.tv_sec * 1000 + now.tv_nsec / 1000000);
}
H(h_injection) {
  char key[80], store[112], type;
  snprintf(key, sizeof key, "%s",
           !strcmp(m->name, "getInjectedIGP") ? "injectedIGP" : "injectedSerialKey");
  snprintf(store, sizeof store, "injectedIGP%s", "ANMP.GloftASHM");
  char *v = pref_get(store, key, &type);
  JObj *r = jni_str(v ? v : "");
  free(v);
  return jv_l(r);
}
H(h_firmware) { return jv_l(jni_str("4.4.4")); }
H(h_keyboard) { return jv_l(jni_array('B', 0)); }

#include "bionic.h"
#include <dirent.h>
#include <unistd.h>
static char keyboard_text[256];
H(h_showKeyboard) {
  SwkbdConfig k;
  Result r = swkbdCreate(&k, 0);
  if (R_FAILED(r))
    return jv_none();
  swkbdConfigMakePresetDefault(&k);
  swkbdConfigSetInitialText(&k, arg_str(a, 0));
  swkbdConfigSetStringLenMax(&k, 255);
  dcr_applet_busy(1);
  r = swkbdShow(&k, keyboard_text, sizeof keyboard_text);
  dcr_applet_busy(0);
  swkbdClose(&k);
  if (R_SUCCEEDED(r)) {
    void (*callback)(void *, void *, void *) = tasm2_native(
        "Java_com_gameloft_android_ANMP_GloftASHM_GLUtils_SUtils_nativeSendKeyboardData");
    if (callback) {
      JObj *t = jni_str(keyboard_text);
      callback(g_jni_env, tasm2_class(C_SU), t);
      jni_release(t);
    }
  }
  return jv_none();
}
H(h_keyboardString) { return jv_l(jni_str(keyboard_text)); }
static int local_path(const char *android, char *out, size_t cap) {
  const char *r = dcr_translate_path(android, out, cap);
  if (r != out)
    snprintf(out, cap, "%s", r);
  size_t n = strlen(dcr_game_root());
  return !strncmp(out, dcr_game_root(), n) && out[n] == '/' && !strstr(out, "..") &&
         strlen(out) > n + 1;
}
H(h_delete) {
  char path[768];
  if (local_path(arg_str(a, 0), path, sizeof path))
    unlink(path);
  return jv_none();
}
static int remove_tree(const char *path) {
  DIR *d = opendir(path);
  if (!d)
    return unlink(path) == 0;
  struct dirent *e;
  int ok = 1;
  while ((e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
      continue;
    char child[1024];
    if (snprintf(child, sizeof child, "%s/%s", path, e->d_name) >= (int)sizeof child) {
      ok = 0;
      continue;
    }
    if (!remove_tree(child))
      ok = 0;
  }
  closedir(d);
  return rmdir(path) == 0 && ok;
}
H(h_remove) {
  char path[768];
  return jv_z(local_path(arg_str(a, 0), path, sizeof path) && remove_tree(path));
}
H(h_unzip) {
  char src[768], dst[768];
  if (!local_path(arg_str(a, 0), src, sizeof src) || !local_path(arg_str(a, 1), dst, sizeof dst))
    return jv_z(0);
  mz_zip_archive z = {0};
  if (!mz_zip_reader_init_file(&z, src, 0))
    return jv_z(0);
  int ok = 1;
  for (unsigned i = 0; i < mz_zip_reader_get_num_files(&z) && ok; i++) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&z, i, &st)) {
      ok = 0;
      break;
    }
    if (strstr(st.m_filename, "..") || *st.m_filename == '/' || strchr(st.m_filename, ':')) {
      ok = 0;
      break;
    }
    char out[1024];
    if (snprintf(out, sizeof out, "%s/%s", dst, st.m_filename) >= (int)sizeof out) {
      ok = 0;
      break;
    }
    if (st.m_is_directory)
      rt_mkdirs(out);
    else {
      rt_mkdirs_for(out);
      ok = mz_zip_reader_extract_to_file(&z, i, out, 0);
    }
  }
  mz_zip_reader_end(&z);
  return jv_z(ok);
}
H(h_localeObj) { return jv_l(jni_singleton("java/util/Locale")); }
H(h_service) {
  const char *name = arg_str(a, 0);
  if (!strcmp(name, "window"))
    return jv_l(jni_singleton("android/view/WindowManager"));
  if (!strcmp(name, "connectivity"))
    return jv_l(jni_singleton("android/net/ConnectivityManager"));
  return jv_l(NULL);
}
H(h_displayManager) { return jv_l(jni_singleton("android/view/WindowManager")); }
H(h_display) { return jv_l(jni_singleton("android/view/Display")); }
H(h_metrics) {
  JObj *o = a[0].l;
  int point = strstr(m->sig, "graphics/Point") != NULL;
  jni_set_field(o, point ? "x" : "widthPixels", jv_i(1280), 0);
  jni_set_field(o, point ? "y" : "heightPixels", jv_i(720), 0);
  if (!point) {
    jni_set_field(o, "density", jv_f(1.0f), 0);
    jni_set_field(o, "scaledDensity", jv_f(1.0f), 0);
    jni_set_field(o, "densityDpi", jv_i(160), 0);
  }
  return jv_none();
}

#define C_IAB "com/gameloft/android/ANMP/GloftASHM/iab/InAppBilling"
const char *tasm2_catalog_json(void);
static volatile unsigned pending_catalog;
static int services_ready;
static void bundle_bytes(JObj *b, const char *key, const char *text) {
  BEntry *e = bundle_find(b, key, 1);
  if (!e)
    return;
  bundle_clear_value(e);
  e->type = 'b';
  e->bytes = jni_array('B', strlen(text));
  memcpy(e->bytes->a.data, text, strlen(text));
}
H(h_iab_getData) {
  JObj *b = a[0].l;
  if (!b)
    return jv_l(NULL);
  int op = bundle_get_num(b, "O");
  static unsigned reported;
  if (op >= 0 && op < 32 && !(reported & (1u << op))) {
    reported |= 1u << op;
    debugPrintf("[online] billing request %d (offline)\n", op);
  }
  if (op == 0 || op == 6)
    __atomic_store_n(&pending_catalog, 1, __ATOMIC_RELEASE);
  else if (op == 7)
    bundle_bytes(b, "R", tasm2_catalog_json());
  else if (op == 8)
    bundle_bytes(b, "R", "en_US@currency=USD");
  else if (op == 1 || op == 2 || op == 9)
    bundle_put_num(b, "R", 'i', -1); /* no purchase/store/update service */
  return jv_l(jni_retain(b));
}
void tasm2_services_poll(void) {
  if (!services_ready) return; /* Native consumers are constructed by first step. */
  if (!__atomic_exchange_n(&pending_catalog, 0, __ATOMIC_ACQ_REL))
    return;
  /* DEX uses O/R; the 1.2.8d native operation 17 consumes a byte-array catalog. */
  void *(*send)(void *, void *, void *) =
      tasm2_native("Java_com_gameloft_android_ANMP_GloftASHM_iab_InAppBilling_nativeSendData");
  if (!send)
    return;
  JObj *b = jni_new("android/os/Bundle");
  bundle_put_num(b, "O", 'i', 17);
  /* native operation 17 parses Google SKU metadata (store_items), whereas the
   * native OfflineStore reads its own original CRM configuration separately. */
  bundle_bytes(b, "R", tasm2_catalog_json());
  JObj *reply = send(g_jni_env, tasm2_class(C_IAB), b);
  if (reply != b)
    jni_release(reply);
  jni_release(b);
}

void tasm2_services_init(void) {
  void (*connection)(void *, void *, int) = tasm2_native(
      "Java_com_gameloft_android_ANMP_GloftASHM_GLUtils_NetworkStateReceiver_setConnectionType");
  if (connection)
    connection(g_jni_env,
               tasm2_class("com/gameloft/android/ANMP/GloftASHM/GLUtils/NetworkStateReceiver"), 0);
  services_ready = 1;
  /* Original local catalog is data, never an entitlement or purchase response. */
  __atomic_store_n(&pending_catalog, 1, __ATOMIC_RELEASE);
}

const JMethodDef jni_method_defs[] = {
    {C_IAB, "getData", "(Landroid/os/Bundle;)Landroid/os/Bundle;", h_iab_getData},
    {C_IAB, "IsInternetAvaliable", "()Z", h_false},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "d1", "()Ljava/lang/String;", h_empty},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "disableUserLocation", "()V", h_void},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "enableUserLocation", "()V", h_void},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "getDeviceFirmware",
     "()Ljava/lang/String;", h_firmware},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "getDeviceIMEI", "()Ljava/lang/String;",
     h_empty},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "getDeviceName", "()Ljava/lang/String;",
     h_deviceName},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "getGoogleAdId", "()Ljava/lang/String;",
     h_empty},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "getGoogleAdIdStatus", "()I", h_zero},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "getHDIDFVVersion",
     "()Ljava/lang/String;", h_hdidfvVersion},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "getPhoneDevice", "()Ljava/lang/String;",
     h_product},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "getPhoneManufacturer",
     "()Ljava/lang/String;", h_manufacturer},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "getPhoneModel", "()Ljava/lang/String;",
     h_model},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "getPhoneProduct",
     "()Ljava/lang/String;", h_product},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "getSerialNo", "()Ljava/lang/String;",
     h_serial},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "retrieveCPUSerial",
     "()Ljava/lang/String;", h_serial},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "retrieveDeviceCarrier",
     "()Ljava/lang/String;", h_empty},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "retrieveDeviceCountry",
     "()Ljava/lang/String;", h_country},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "retrieveDeviceLanguage",
     "()Ljava/lang/String;", h_language},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "retrieveDeviceRegion",
     "()Ljava/lang/String;", h_locale},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "GetVirtualKeyboardText",
     "()Ljava/lang/String;", h_keyboardString},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "HideKeyboard", "()V", h_void},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "IsDialogShown", "()Z", h_false},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "IsFirstTimeAskingForContactsPermission",
     "()Z", h_true},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "IsKeyboardVisible", "()Z", h_false},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils",
     "IsNeverAskAgainTickedForContactsPermission", "()Z", h_false},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "ShowKeyboard", "(Ljava/lang/String;)V",
     h_showKeyboard},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "deleteFile", "(Ljava/lang/String;)V",
     h_delete},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "genericUnzipArchive",
     "(Ljava/lang/String;Ljava/lang/String;)Z", h_unzip},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "isCheckVersionSDKs", "()I", h_zero},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "isContactsPermissionEnabled", "()Z",
     h_false},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "playVideo", "(Ljava/lang/String;Z)Z",
     h_false},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "removeDirectoryRecursively",
     "(Ljava/lang/String;)Z", h_remove},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "requestContactsPermission", "(Z)I",
     jni_h_minus1},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "showCantGoBackPopup", "(I)V", h_void},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "stopVideo", "()V", h_void},
    {"android/content/Context", "getApplicationContext", "()Landroid/content/Context;", h_activity},
    {"android/content/Context", "getSystemService", "(" S ")Ljava/lang/Object;", h_service},
    {"android/app/Activity", "getWindowManager", "()Landroid/view/WindowManager;",
     h_displayManager},
    {"android/view/WindowManager", "getDefaultDisplay", "()Landroid/view/Display;", h_display},
    {"android/view/Display", "getWidth", "()I", h_width},
    {"android/view/Display", "getHeight", "()I", h_height},
    {"android/view/Display", "getRotation", "()I", h_zero},
    {"android/view/Display", "getSize", "(Landroid/graphics/Point;)V", h_metrics},
    {"android/view/Display", "getRealSize", "(Landroid/graphics/Point;)V", h_metrics},
    {"android/view/Display", "getMetrics", "(Landroid/util/DisplayMetrics;)V", h_metrics},
    {"android/net/ConnectivityManager", "getActiveNetworkInfo", "()Landroid/net/NetworkInfo;",
     jni_h_null},
    {"java/util/Locale", "getDefault", "()Ljava/util/Locale;", h_localeObj},
    {"java/util/Locale", "getCountry", "()" S, h_country},
    {"java/util/Locale", "getLanguage", "()" S, h_language},
    {"java/util/Locale", "toString", "()" S, h_locale},

    {"com/gameloft/android/ANMP/GloftASHM/DataSharing", "deleteSharedValue",
     "(Ljava/lang/String;)V", h_ds_delete},
    {"com/gameloft/android/ANMP/GloftASHM/DataSharing", "getContext", "()Landroid/content/Context;",
     h_activity},
    {"com/gameloft/android/ANMP/GloftASHM/DataSharing", "getSharedValue",
     "(Ljava/lang/String;)Ljava/lang/String;", h_ds_get},
    {"com/gameloft/android/ANMP/GloftASHM/DataSharing", "isSharedValue", "(Ljava/lang/String;)Z",
     h_ds_is},
    {"com/gameloft/android/ANMP/GloftASHM/DataSharing", "setSharedValue",
     "(Ljava/lang/String;Ljava/lang/String;)V", h_ds_set},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "getAndroidId", "()Ljava/lang/String;",
     h_androidId},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "getGLDID", "()Ljava/lang/String;",
     h_gldid},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "getHDIDFV", "()Ljava/lang/String;",
     h_hdidfv},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "getMacAddress", "()Ljava/lang/String;",
     h_mac},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/Device", "getSerial", "()Ljava/lang/String;",
     h_serial},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "CheckConnectionType", "()I", h_zero},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "GetApkPath", "()Ljava/lang/String;",
     h_apk},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "getAssetAsString",
     "(Ljava/lang/String;)[B", h_getAssetAsString},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "getContext",
     "()Landroid/content/Context;", h_activity},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "getGLUID", "(Ljava/lang/String;)[I",
     h_getGLUID_str},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "getGameName", "()Ljava/lang/String;",
     h_name},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "getInjectedIGP", "()Ljava/lang/String;",
     h_injection},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "getInjectedSerialKey",
     "()Ljava/lang/String;", h_injection},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "getMetaDataValue",
     "(Ljava/lang/String;)Ljava/lang/String;", h_getMetaDataValue},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "getPackage", "()Ljava/lang/String;",
     h_pkg},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "getPreferenceString",
     "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", h_prefString},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "getPreferenceString",
     "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", h_prefString},
    {C_SU, "getFreeSpace", "(Ljava/lang/String;)F", h_space_kb},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "getSDFolder", "()Ljava/lang/String;",
     h_sd},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "getSaveFolder", "()Ljava/lang/String;",
     h_files},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "hasConnectivity", "()I", h_zero},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "inGameVideoOnPause", "()V", h_void},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "inGameVideoOnResume", "()V", h_void},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "inGameVideoSetSkipEnabled", "(Z)V",
     h_void},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "initCheckConnectionType", "()I",
     h_zero},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "nativeGetPreference",
     "(Landroid/os/Bundle;)Landroid/os/Bundle;", h_nativeGetPreference},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "nativeSetPreference",
     "(Landroid/os/Bundle;)V", h_nativeSetPreference},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "retrieveBarrels", "()[I",
     h_retrieveBarrels},
    {"com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils", "shareInfo",
     "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", h_void},
    {"com/gameloft/glf/GL2JNIActivity", "GetUptimeSystem", "()I", h_uptime},
    {"com/gameloft/glf/GL2JNIActivity", "RestartGame", "()V", h_exit},
    {"com/gameloft/glf/GL2JNIActivity", "RetrieveObbPath", "()Ljava/lang/String;", h_obb},
    {"com/gameloft/glf/GL2JNIActivity", "SetIsInActionPhase", "(Z)V", h_action},
    {"com/gameloft/glf/GL2JNIActivity", "sBrowserLaunch", "(Ljava/lang/String;)V", h_void},
    {"com/gameloft/glf/GL2JNIActivity", "sBrowserLaunchEncrypt", "(Ljava/lang/String;)V", h_void},
    {"com/gameloft/glf/GL2JNIActivity", "sGetKeyboardText", "()[B", h_keyboard},
    {"com/gameloft/glf/GL2JNIActivity", "sIGPLaunch", "(ILjava/lang/String;)V", h_void},
    {"com/gameloft/glf/GL2JNIActivity", "sIsKeyboardVisible", "()I", h_zero},
    {"com/gameloft/glf/GL2JNIActivity", "sLaunchVideoPlayer", "(Ljava/lang/String;)Z", h_false},
    {"com/gameloft/glf/GL2JNIActivity", "sOnlineTracker", "(II)V", h_void},
    {"com/gameloft/glf/GL2JNIActivity", "sShowKeyboard", "(ILjava/lang/String;I)V", h_void},
    {"com/gameloft/glf/GL2JNIActivity", "sWelcomeScreenLaunch", "(I)V", h_void},
    {"com/gameloft/glf/GL2JNIActivity", "sWelcomeScreenSetIsPau", "(Z)V", h_void},
    {"com/gameloft/glf/GL2JNIActivity", "setAnonymousID", "(ILjava/lang/String;)V", h_void},
    {"com/gameloft/glf/GL2JNIActivity", "trackAndroidHits", "(II)V", h_void},
    {"com/gameloft/android/ANMP/GloftASHM/SendInfo", "getAndroidID", "()Ljava/lang/String;",
     h_androidId},
    {"com/gameloft/android/ANMP/GloftASHM/SendInfo", "getGLDID", "()Ljava/lang/String;", h_gldid},
    {"com/gameloft/android/ANMP/GloftASHM/SendInfo", "getGLUID", "()[I", h_sendinfo_getGLUID},
    {"com/gameloft/android/ANMP/GloftASHM/SendInfo", "getLocaleCountry", "()Ljava/lang/String;",
     h_country_lower},
    {"com/gameloft/android/ANMP/GloftASHM/SendInfo", "getLocaleLanguage", "()Ljava/lang/String;",
     h_locale},
    {"com/gameloft/android/ANMP/GloftASHM/SendInfo", "getMacAddress", "()Ljava/lang/String;",
     h_mac},
    {"com/gameloft/android/ANMP/GloftASHM/SendInfo", "getManufacturerModel", "()Ljava/lang/String;",
     h_manufacturerModel},
    {"com/gameloft/android/ANMP/GloftASHM/SendInfo", "getPhoneCarrier", "()Ljava/lang/String;",
     h_empty},
    {"com/gameloft/android/ANMP/GloftASHM/SendInfo", "getPreferenceString",
     "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", h_prefString},
    {"com/gameloft/android/ANMP/GloftASHM/SendInfo", "getSDFolder", "()Ljava/lang/String;", h_sd},
    {"com/gameloft/android/ANMP/GloftASHM/SendInfo", "getSaveFolder", "()Ljava/lang/String;",
     h_files},
    {"com/gameloft/android/ANMP/GloftASHM/SendInfo", "getSerial", "()Ljava/lang/String;", h_serial},
    {"com/gameloft/android/ANMP/GloftASHM/iab/utils/Device", "getGLDID", "()Ljava/lang/String;",
     h_gldid},
    {"com/gameloft/android/ANMP/GloftASHM/iab/utils/Device", "getHDIDFV", "()Ljava/lang/String;",
     h_hdidfv},
    {"com/gameloft/glf/GL2JNILib", "CheckHDMIState", "()I", h_false},
    {"com/gameloft/glf/GL2JNILib", "GetDeviceFirmware", "()Ljava/lang/String;", h_firmware},
    {"com/gameloft/glf/GL2JNILib", "GetDeviceHeight", "()I", h_height},
    {"com/gameloft/glf/GL2JNILib", "GetDeviceLanguage", "()Ljava/lang/String;", h_language},
    {"com/gameloft/glf/GL2JNILib", "GetDeviceName", "()Ljava/lang/String;", h_deviceName},
    {"com/gameloft/glf/GL2JNILib", "GetDeviceWidth", "()I", h_width},
    {"com/gameloft/glf/GL2JNILib", "GetHDMIName", "()Ljava/lang/String;", h_empty},
    {"com/gameloft/glf/GL2JNILib", "GetManufacturer", "()Ljava/lang/String;", h_manufacturer},
    {"com/gameloft/glf/GL2JNILib", "GetUptimeSystem", "()I", h_uptime},
    {"com/gameloft/glf/GL2JNILib", "GetWindowHeight", "()I", h_height},
    {"com/gameloft/glf/GL2JNILib", "GetWindowWidth", "()I", h_width},
    {"com/gameloft/glf/GL2JNILib", "JGetCurrentCPUSpeed", "()F", h_cpu},
    {"com/gameloft/glf/GL2JNILib", "JGetFreeDiskSpace", "()F", h_space_kb},
    {"com/gameloft/glf/GL2JNILib", "JGetFreeRam", "()F", h_ram},
    {"com/gameloft/glf/GL2JNILib", "JGetMaxAvailableRam", "()F", h_ram},
    {"com/gameloft/glf/GL2JNILib", "JGetMaxCPUSpeed", "()F", h_cpu},
    {"com/gameloft/glf/GL2JNILib", "createView", "()V", h_void},
    {"com/gameloft/glf/GL2JNILib", "getAndroidId", "()Ljava/lang/String;", h_androidId},
    {"com/gameloft/glf/GL2JNILib", "getCountry", "()Ljava/lang/String;", h_country},
    {"com/gameloft/glf/GL2JNILib", "getHDIDFV", "()Ljava/lang/String;", h_hdidfv},
    {"com/gameloft/glf/GL2JNILib", "getResource", "(Ljava/lang/String;)[B", h_resource},
    {"com/gameloft/glf/GL2JNILib", "getUDID", "()Ljava/lang/String;", h_androidId},
    {"com/gameloft/glf/GL2JNILib", "sSetFixedSize", "(F)V", h_void},
    {"com/gameloft/glf/GL2JNILib", "sShowToast", "(Ljava/lang/String;)V", h_void},
    {"com/gameloft/glf/GL2JNILib", "setCurrentContext", "(I)Z", h_context},
    {"com/gameloft/glf/GL2JNILib", "setViewSettings", "(IIIII)V", h_view},
    {"com/gameloft/glf/GL2JNILib", "setupPaths", "()V", h_setupPaths},
    {C_HID, "NativeListenerRegistered", "(I)V", h_listener},
    {C_HID, "NativeListenerUnRegistered", "()V", h_listener},
    {"android/content/Context", "getFilesDir", "()Ljava/io/File;", h_file},
    {"android/content/Context", "getCacheDir", "()Ljava/io/File;", h_file},
    {"android/content/Context", "getObbDir", "()Ljava/io/File;", h_file},
    {"android/content/Context", "getPackageName", "()" S, h_pkg},
    {"java/io/File", "getAbsolutePath", "()" S, h_filePath},
    {"java/io/File", "toString", "()" S, h_filePath},
    {"java/lang/System", "exit", "(I)V", h_exit},
    {C_ACTIVITY, "ExitGame", "()V", h_exit},
    {"java/lang/System", "currentTimeMillis", "()J", h_clock},
    {C_ACTIVITY, "sGetMilliseconds", "()J", h_clock},
    {"android/content/Intent", "<init>", "()V", h_void},
    {"android/os/Process", "setThreadPriority", "(I)V", h_void},
    /* ---- android/os/Bundle ---- */
    {"android/os/Bundle", "<init>", "()V", h_bundle_init},
    {"android/os/Bundle", "putString", "(" S S ")V", h_bundle_putString},
    {"android/os/Bundle", "getString", "(" S ")" S, h_bundle_getString},
    {"android/os/Bundle", "getInt", "(" S ")I", h_bundle_getInt},
    {"android/os/Bundle", "putInt", "(" S "I)V", h_bundle_putInt},
    {"android/os/Bundle", "getLong", "(" S ")J", h_bundle_getLong},
    {"android/os/Bundle", "putLong", "(" S "J)V", h_bundle_putLong},
    {"android/os/Bundle", "getBoolean", "(" S ")Z", h_bundle_getBoolean},
    {"android/os/Bundle", "putBoolean", "(" S "Z)V", h_bundle_putBoolean},
    {"android/os/Bundle", "containsKey", "(" S ")Z", h_bundle_containsKey},
    {"android/os/Bundle", "clear", "()V", h_bundle_clear},
    {"android/os/Bundle", "getByteArray", "(" S ")[B", h_bundle_getByteArray},
    {"android/os/Bundle", "putByteArray", "(" S "[B)V", h_bundle_putByteArray},

    {NULL, NULL, NULL, NULL}};

static jvalue f_prefs(JObj *o, const JField *f) { return jv_l(jni_str("GamePrefs")); }
const JFieldDef jni_field_defs[] = {{"com/gameloft/android/ANMP/GloftASHM/installer/GameInstaller",
                                     "mPreferencesName", f_prefs, 0, NULL},
                                    {"android/os/Build", "MANUFACTURER", NULL, 0, "Nintendo"},
                                    {"android/os/Build", "MODEL", NULL, 0, "Switch"},
                                    {"android/os/Build$VERSION", "SDK_INT", NULL, 19, NULL},
                                    {NULL, NULL, NULL, 0, NULL}};
const char *const jni_class_supers[][2] = {{C_GAME, C_ACTIVITY},
                                           {C_ACTIVITY, "android/app/Activity"},
                                           {"android/app/Activity", "android/content/Context"},
                                           {"java/nio/DirectByteBuffer", "java/nio/ByteBuffer"},
                                           {NULL, NULL}};
const char *const jni_missing_classes[] = {NULL};
void tasm2_jni_prepare(void) {
  jni_init();
  g_activity = jni_singleton(C_GAME);
  identity_init();
  snprintf(tasm2_device_id, sizeof tasm2_device_id, "%s", g_uuid);
}
